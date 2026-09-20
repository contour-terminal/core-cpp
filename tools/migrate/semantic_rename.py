# SPDX-License-Identifier: Apache-2.0
"""The semantic half of a consumer migration: members renamed by their declaration, not their name.

    python tools/migrate/semantic_rename.py --profile fastcached \\
        --compile-db build/linux/compile_commands.json build/windows/compile_commands.json \\
        --decl-paths src/FastCache/Net src/FastCache/Async

`Read`, `Write`, `Run` and `Stop` are far too common to rewrite by text: `sock.Read(` must become
`sock.read(` only where `sock` is a `FastCache::ISocket`. So the rows marked `"apply": "semantic"`
in `renames.json` are applied through libclang: a cursor is rewritten only when the **declaration**
it refers to lives under `--decl-paths` and its enclosing class is the row's `scope`.

Two details that are the whole reason this is a tool and not a `sed` line:

- **Several compile databases are unioned.** fastcached's Windows and Linux builds compile
  different files, so one database alone silently misses the other platform's call sites.
- **Edits are applied back to front within a file**, so a replacement of a different length cannot
  invalidate the offsets of the edits before it.

libclang's Python bindings are optional (`python -m pip install libclang`). Where they are missing
this tool refuses to run and says so; it never falls back to a textual pass.
"""

from __future__ import annotations

import argparse
import json
import shlex
import sys
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

import renames
from renames import Row, TableError  # noqa: F401  (Row is this module's public vocabulary too)

TABLE = Path(__file__).resolve().parent / "renames.json"

#: Compiler arguments that name the build's output rather than how to parse the input.
DROPPED_FLAGS = frozenset({"-c", "-o", "-MD", "-MMD", "-MP", "-MF", "-MT", "-MQ", "--"})
DROPPED_WITH_VALUE = frozenset({"-o", "-MF", "-MT", "-MQ"})


@dataclass(frozen=True, order=True)
class Edit:
    """One token to replace, as a byte range in a file."""

    offset: int
    length: int
    replacement: bytes
    label: str


@lru_cache(maxsize=1)
def bindingsAvailable() -> bool:
    """Whether `clang.cindex` imports *and* finds its native library."""
    try:
        from clang import cindex

        cindex.Index.create()
        return True
    except Exception:  # noqa: BLE001 -- any failure here means "not usable", and the caller says so
        return False


def _cursorKinds():
    from clang.cindex import CursorKind

    return {
        CursorKind.MEMBER_REF_EXPR,
        CursorKind.DECL_REF_EXPR,
        CursorKind.MEMBER_REF,
        CursorKind.TYPE_REF,
        CursorKind.TEMPLATE_REF,
        CursorKind.OVERLOADED_DECL_REF,
        CursorKind.CXX_METHOD,
        CursorKind.FIELD_DECL,
        CursorKind.FUNCTION_DECL,
        CursorKind.VAR_DECL,
        CursorKind.CLASS_DECL,
        CursorKind.STRUCT_DECL,
        CursorKind.ENUM_DECL,
        CursorKind.ENUM_CONSTANT_DECL,
        CursorKind.TYPE_ALIAS_DECL,
        CursorKind.TYPEDEF_DECL,
    }


def qualifiedName(cursor) -> str:
    """`FastCache::ISocket::Read` for a method cursor: every named semantic parent, outermost first."""
    from clang.cindex import CursorKind

    parts: list[str] = []
    node = cursor
    while node is not None and node.kind != CursorKind.TRANSLATION_UNIT:
        if node.spelling:
            parts.append(node.spelling)
        node = node.semantic_parent
    return "::".join(reversed(parts))


def compileCommands(database: Path) -> list[tuple[Path, Path, list[str]]]:
    """Reads a compile database into (working directory, source file, parse arguments) triples."""
    entries = json.loads(database.read_text(encoding="utf-8"))
    commands: list[tuple[Path, Path, list[str]]] = []
    for entry in entries:
        directory = Path(entry["directory"])
        source = Path(entry["file"])
        if not source.is_absolute():
            source = directory / source
        words = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"], posix=False)
        commands.append((directory, source.resolve(), _parseArguments(words[1:], source)))
    return commands


def _parseArguments(words: list[str], source: Path) -> list[str]:
    arguments: list[str] = []
    skip = False
    for word in words:
        if skip:
            skip = False
            continue
        bare = word.strip('"')
        if bare in DROPPED_WITH_VALUE:
            skip = True
            continue
        if bare in DROPPED_FLAGS:
            continue
        if Path(bare).name == source.name:
            continue
        arguments.append(bare)
    return arguments


def _isUnder(path: Path, roots: list[Path]) -> bool:
    try:
        resolved = path.resolve()
    except OSError:
        return False
    return any(resolved == root or resolved.is_relative_to(root) for root in roots)


def collectEdits(databases: list[Path], declPaths: list[Path], rows: list[Row]) -> dict[Path, list[Edit]]:
    """Unions the edits @p rows imply over every translation unit of every @p databases entry."""
    if not bindingsAvailable():
        raise TableError("libclang's Python bindings are missing: python -m pip install libclang")
    from clang.cindex import Index

    byScope = {(row.scope, row.source): row for row in rows}
    roots = [path.resolve() for path in declPaths]
    kinds = _cursorKinds()
    index = Index.create()
    found: dict[Path, set[Edit]] = {}
    contents: dict[Path, bytes] = {}

    for database in databases:
        for directory, source, arguments in compileCommands(database):
            unit = index.parse(str(source), args=[f"-working-directory={directory}", *arguments])
            for cursor in _walk(unit.cursor):
                if cursor.kind not in kinds:
                    continue
                declaration = cursor.referenced or cursor
                if declaration.spelling not in {row.source for _, row in byScope.items()}:
                    continue
                location = declaration.location.file
                if location is None or not _isUnder(Path(location.name), roots):
                    continue
                row = byScope.get((qualifiedName(declaration.semantic_parent), declaration.spelling))
                if row is None:
                    continue
                edit = _editAt(cursor, row, contents)
                if edit is not None:
                    found.setdefault(Path(cursor.location.file.name).resolve(), set()).add(edit)

    return {path: sorted(edits) for path, edits in found.items() if edits}


def _walk(cursor):
    yield cursor
    for child in cursor.get_children():
        yield from _walk(child)


def _editAt(cursor, row: Row, contents: dict[Path, bytes]) -> Edit | None:
    """The edit at @p cursor, or None where the bytes there are not the token the row renames."""
    path = Path(cursor.location.file.name).resolve()
    if path not in contents:
        contents[path] = path.read_bytes()
    offset = cursor.location.offset
    expected = row.source.encode("utf-8")
    if contents[path][offset : offset + len(expected)] != expected:
        return None
    return Edit(offset=offset, length=len(expected), replacement=row.target.encode("utf-8"), label=row.label)


def applyEdits(edits: dict[Path, list[Edit]], dryRun: bool = False) -> int:
    """Applies every edit, back to front within each file so earlier offsets stay valid."""
    total = 0
    for path, fileEdits in sorted(edits.items()):
        data = path.read_bytes()
        for edit in sorted(fileEdits, key=lambda item: item.offset, reverse=True):
            data = data[: edit.offset] + edit.replacement + data[edit.offset + edit.length :]
        total += len(fileEdits)
        print(f"{path}: {len(fileEdits)} edit(s)")
        for label in sorted({edit.label for edit in fileEdits}):
            print(f"    {len([e for e in fileEdits if e.label == label]):5d}  {label}")
        if not dryRun:
            path.write_bytes(data)
    return total


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--profile", required=True, help="which consumer's semantic rows to apply")
    parser.add_argument("--table", type=Path, default=TABLE, help="the rename table")
    parser.add_argument(
        "--compile-db", type=Path, nargs="+", required=True, help="one or more compile databases"
    )
    parser.add_argument(
        "--decl-paths", type=Path, nargs="+", required=True, help="where the declarations live"
    )
    parser.add_argument("--dry-run", action="store_true", help="report what would change and write nothing")
    arguments = parser.parse_args(argv)

    if not bindingsAvailable():
        print(
            "semantic_rename: libclang's Python bindings are missing, so nothing was checked and "
            f"nothing was rewritten. Install them with: {sys.executable} -m pip install libclang"
        )
        return 1

    try:
        rows = renames.load(arguments.table).semanticRows(arguments.profile)
    except TableError as error:
        print(f"semantic_rename: {error}")
        return 1
    if not rows:
        print(f"semantic_rename: profile '{arguments.profile}' has no semantic rows")
        return 0

    edits = collectEdits(arguments.compile_db, arguments.decl_paths, rows)
    total = applyEdits(edits, arguments.dry_run)
    verb = "would apply" if arguments.dry_run else "applied"
    print(
        f"semantic_rename --profile {arguments.profile}: {verb} {total} edit(s) in {len(edits)} file(s), "
        f"from {len(rows)} row(s) over {len(arguments.compile_db)} compile database(s)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
