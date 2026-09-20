# SPDX-License-Identifier: Apache-2.0
"""The semantic half of a consumer migration: members renamed by their declaration, not their name.

    python tools/migrate/semantic_rename.py --profile fastcached \\
        --compile-db build/linux/compile_commands.json build/windows/compile_commands.json \\
        --decl-paths src/FastCache/Net src/FastCache/Async

`Read`, `Write`, `Run` and `Stop` are far too common to rewrite by text: `sock.Read(` must become
`sock.read(` only where `sock` is a `FastCache::ISocket`. So the rows marked `"apply": "semantic"`
in `renames.json` are applied through libclang: a cursor is rewritten only when the **declaration**
it refers to lives under `--decl-paths` and its enclosing class is the row's `scope`.

Four details that are the whole reason this is a tool and not a `sed` line:

- **Several compile databases are unioned.** fastcached's Windows and Linux builds compile
  different files, so one database alone silently misses the other platform's call sites.
- **Edits are applied back to front within a file**, so a replacement of a different length cannot
  invalidate the offsets of the edits before it.
- **Overrides are followed.** Renaming an interface's method without its implementations leaves an
  override of a virtual that no longer exists, so the migrated tree does not compile.
- **A translation unit that did not parse stops the run**, because a partial AST yields a partial
  rename reported as a confident total. `--allow-parse-errors` takes what can be resolved.

**Run each compile database on the host that produced it.** A compile database carries no target
triple, so libclang parses with the host default: parsing fastcached's Linux database on Windows
activates the `_WIN32` branches for *both* databases and misses every POSIX call site, silently.
Unioning databases across hosts works only if each is parsed where it was generated, or a
`--target=` is added to its command lines. Task C4 should run this pass once per platform and union
the resulting trees, not run one host over both databases. For the same reason a member call on a
dependent type inside a template is invisible to any AST tool; the count of undecided call sites is
printed so the operator knows how many a human still owes.

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
from renames import Row, TableError

TABLE = Path(__file__).resolve().parent / "renames.json"

#: Compiler arguments that name the build's output rather than how to parse the input.
DROPPED_FLAGS = frozenset({"-c", "-o", "-MD", "-MMD", "-MP", "-MF", "-MT", "-MQ", "--"})
DROPPED_WITH_VALUE = frozenset({"-o", "-MF", "-MT", "-MQ"})


class ParseError(Exception):
    """A translation unit libclang could not parse cleanly, so its call sites cannot be trusted."""


@dataclass(frozen=True, order=True)
class Edit:
    """One token to replace, as a byte range in a file."""

    offset: int
    length: int
    replacement: bytes
    label: str


@lru_cache(maxsize=1)
def bindings_available() -> bool:
    """Whether `clang.cindex` imports *and* finds its native library."""
    try:
        from clang import cindex

        cindex.Index.create()
        return True
    # Any failure here -- a missing module, a missing native library, a broken install -- means
    # the bindings are not usable, which is the one thing the caller needs to know.
    except Exception:
        return False


def _cursor_kinds():
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


def overridden_cursors(cursor) -> list:
    """The declarations @p cursor directly overrides.

    `clang_getOverriddenCursors` is in the shared library but **not** wrapped by the `libclang` PyPI
    bindings (checked against 18.1.1: `Cursor` has no `get_overriden_cursors`), so it is bound here
    by hand. Each result is copied out of libclang's array before that array is disposed of, and
    carries the translation unit its properties need.
    """
    import ctypes

    from clang.cindex import Cursor, conf

    entry = getattr(conf.lib, "clang_getOverriddenCursors", None)
    if entry is None:  # pragma: no cover -- guarded by _require_override_support()
        return []
    if not getattr(entry, "_coreCppBound", False):
        entry.argtypes = [Cursor, ctypes.POINTER(ctypes.POINTER(Cursor)), ctypes.POINTER(ctypes.c_uint)]
        entry.restype = None
        conf.lib.clang_disposeOverriddenCursors.argtypes = [ctypes.POINTER(Cursor)]
        conf.lib.clang_disposeOverriddenCursors.restype = None
        entry._coreCppBound = True

    array = ctypes.POINTER(Cursor)()
    count = ctypes.c_uint()
    entry(cursor, ctypes.byref(array), ctypes.byref(count))
    try:
        found = []
        for index in range(count.value):
            copied = Cursor.from_buffer_copy(array[index])
            copied._tu = cursor._tu
            found.append(copied)
        return found
    finally:
        conf.lib.clang_disposeOverriddenCursors(array)


def _require_override_support() -> None:
    """Refuses to run where overrides cannot be followed, rather than renaming half a hierarchy."""
    from clang.cindex import conf

    if getattr(conf.lib, "clang_getOverriddenCursors", None) is None:
        raise TableError(
            "this libclang exposes no clang_getOverriddenCursors, so an interface's overrides "
            "cannot be found and a rename would leave a tree that does not compile. Install the "
            "pinned bindings: python -m pip install libclang"
        )


def declaration_chain(cursor) -> list:
    """@p cursor and every declaration it transitively overrides, nearest first.

    An interface's method is renamed across the hierarchy that implements it, or not at all:
    renaming `ISocket::Read` while `TcpSocket::Read` **override** keeps its name leaves an override
    of a virtual that no longer exists -- a hard compile error -- plus every call site typed to the
    concrete class. The walk runs to a fixed point, so a grandchild override is reached through its
    parent (controller ruling R91).
    """
    found: dict[tuple[str, int], object] = {}
    pending = [cursor]
    while pending:
        current = pending.pop()
        location = current.location
        key = (location.file.name if location.file else "", location.offset)
        if key in found:
            continue
        found[key] = current
        pending.extend(overridden_cursors(current))
    return list(found.values())


def qualified_name(cursor) -> str:
    """`FastCache::ISocket::Read` for a method cursor: every named semantic parent, outermost first."""
    from clang.cindex import CursorKind

    parts: list[str] = []
    node = cursor
    while node is not None and node.kind != CursorKind.TRANSLATION_UNIT:
        if node.spelling:
            parts.append(node.spelling)
        node = node.semantic_parent
    return "::".join(reversed(parts))


def compile_commands(database: Path) -> list[tuple[Path, Path, list[str]]]:
    """Reads a compile database into (working directory, source file, parse arguments) triples."""
    entries = json.loads(database.read_text(encoding="utf-8"))
    commands: list[tuple[Path, Path, list[str]]] = []
    for entry in entries:
        directory = Path(entry["directory"])
        source = Path(entry["file"])
        if not source.is_absolute():
            source = directory / source
        words = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"], posix=False)
        commands.append((directory, source.resolve(), _parse_arguments(words[1:], source)))
    return commands


def _parse_arguments(words: list[str], source: Path) -> list[str]:
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


def _is_under(path: Path, roots: list[Path]) -> bool:
    try:
        resolved = path.resolve()
    except OSError:
        return False
    return any(resolved == root or resolved.is_relative_to(root) for root in roots)


def collect_edits(
    databases: list[Path],
    decl_paths: list[Path],
    rows: list[Row],
    allow_parse_errors: bool = False,
) -> dict[Path, list[Edit]]:
    """Unions the edits @p rows imply over every translation unit of every @p databases entry.

    Raises ParseError when libclang reported an error for any translation unit, unless
    @p allow_parse_errors: a stale compile database parses to a partial AST, where `cursor.referenced`
    is null at the call sites whose class failed to resolve, and the run then reports a confident
    count of the edits it *did* make. That is the difference between "53 call sites moved" and
    "53 of 80 moved, and you find the rest on the platform you did not build" (finding I6).
    """
    if not bindings_available():
        raise TableError("libclang's Python bindings are missing: python -m pip install libclang")
    _require_override_support()
    from clang.cindex import Diagnostic, Index

    by_scope = {(row.scope, row.source): row for row in rows}
    sources = {row.source for row in rows}
    roots = [path.resolve() for path in decl_paths]
    kinds = _cursor_kinds()
    index = Index.create()
    found: dict[Path, set[Edit]] = {}
    contents: dict[Path, bytes] = {}
    problems: list[str] = []
    undecided = 0

    for database in databases:
        for directory, source, arguments in compile_commands(database):
            unit = index.parse(str(source), args=[f"-working-directory={directory}", *arguments])
            problems += [
                f"{source}: {diagnostic.spelling}"
                for diagnostic in unit.diagnostics
                if diagnostic.severity >= Diagnostic.Error
            ]
            for cursor in _walk(unit.cursor):
                if cursor.kind not in kinds:
                    continue
                declaration = cursor.referenced
                if declaration is None:
                    # A dependent or unresolved expression: libclang cannot say what it refers to,
                    # so neither can this tool. Counted rather than ignored.
                    if cursor.spelling in sources:
                        undecided += 1
                    continue
                if declaration.spelling not in sources:
                    continue
                # The declaration, plus everything it overrides: an override lives in a different
                # class, so its own scope never matches an interface's row.
                chain = declaration_chain(declaration)
                if not any(
                    _is_under(Path(entry.location.file.name), roots)
                    for entry in chain
                    if entry.location.file is not None
                ):
                    continue
                row = next(
                    (
                        match
                        for match in (
                            by_scope.get((qualified_name(entry.semantic_parent), entry.spelling))
                            for entry in chain
                        )
                        if match is not None
                    ),
                    None,
                )
                if row is None:
                    continue
                edit = _edit_at(cursor, row, contents)
                if edit is not None:
                    found.setdefault(Path(cursor.location.file.name).resolve(), set()).add(edit)

    if problems and not allow_parse_errors:
        listed = "\n  ".join(sorted(set(problems))[:20])
        raise ParseError(
            f"libclang reported {len(problems)} error(s); the call sites in those translation units "
            f"cannot be trusted, so nothing was rewritten. Fix the compile database, or pass "
            f"--allow-parse-errors to take what can be resolved:\n  {listed}"
        )

    if undecided:
        print(
            f"semantic_rename: {undecided} call site(s) name a renamed member but could not be "
            f"resolved -- a dependent type in a template, or a branch this host does not compile. "
            f"A human owes those."
        )

    return {path: sorted(edits) for path, edits in found.items() if edits}


def _walk(cursor):
    yield cursor
    for child in cursor.get_children():
        yield from _walk(child)


def _edit_at(cursor, row: Row, contents: dict[Path, bytes]) -> Edit | None:
    """The edit at @p cursor, or None where the bytes there are not the token the row renames."""
    path = Path(cursor.location.file.name).resolve()
    if path not in contents:
        contents[path] = path.read_bytes()
    offset = cursor.location.offset
    expected = row.source.encode("utf-8")
    if contents[path][offset : offset + len(expected)] != expected:
        return None
    return Edit(offset=offset, length=len(expected), replacement=row.target.encode("utf-8"), label=row.label)


def apply_edits(edits: dict[Path, list[Edit]], dry_run: bool = False) -> int:
    """Applies every edit, back to front within each file so earlier offsets stay valid."""
    total = 0
    for path, file_edits in sorted(edits.items()):
        data = path.read_bytes()
        for edit in sorted(file_edits, key=lambda item: item.offset, reverse=True):
            data = data[: edit.offset] + edit.replacement + data[edit.offset + edit.length :]
        total += len(file_edits)
        print(f"{path}: {len(file_edits)} edit(s)")
        for label in sorted({edit.label for edit in file_edits}):
            print(f"    {len([e for e in file_edits if e.label == label]):5d}  {label}")
        if not dry_run:
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
    parser.add_argument(
        "--allow-parse-errors",
        action="store_true",
        help="rewrite what could be resolved even where a translation unit failed to parse",
    )
    arguments = parser.parse_args(argv)

    if not bindings_available():
        print(
            "semantic_rename: libclang's Python bindings are missing, so nothing was checked and "
            f"nothing was rewritten. Install them with: {sys.executable} -m pip install libclang"
        )
        return 1

    try:
        rows = renames.load(arguments.table).semantic_rows(arguments.profile)
    except TableError as error:
        print(f"semantic_rename: {error}")
        return 1
    if not rows:
        print(f"semantic_rename: profile '{arguments.profile}' has no semantic rows")
        return 0

    try:
        edits = collect_edits(
            arguments.compile_db,
            arguments.decl_paths,
            rows,
            allow_parse_errors=arguments.allow_parse_errors,
        )
    except ParseError as error:
        print(f"semantic_rename: {error}")
        return 1
    total = apply_edits(edits, arguments.dry_run)
    verb = "would apply" if arguments.dry_run else "applied"
    print(
        f"semantic_rename --profile {arguments.profile}: {verb} {total} edit(s) in {len(edits)} file(s), "
        f"from {len(rows)} row(s) over {len(arguments.compile_db)} compile database(s)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
