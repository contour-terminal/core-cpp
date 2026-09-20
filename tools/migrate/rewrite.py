# SPDX-License-Identifier: Apache-2.0
"""The mechanical half of a consumer migration: includes, namespaces, symbols, members and macros.

    python tools/migrate/rewrite.py --profile endo src
    python tools/migrate/rewrite.py --profile tuidu --dry-run src/tuidu

Every rule is a row of `renames.json`, selected by `--profile`, and every pattern is anchored so
that a longer qualified name is never a match: `(?<![\\w:])net::` leaves `std::net::`, `endo::net::`
and `mynet::` alone. The tool is idempotent -- a second run over a rewritten tree changes nothing,
and a half-converted tree converges -- so a reviewer can re-run it after a rebase.

Two deliberate boundaries:

- **String and character literals are not rewritten.** A codemod may change what the code says; it
  must never change what the program sends. Comments *are* rewritten, because a comment documents
  the code beside it.
- **A namespace *definition* (`namespace net { ... }`) is left to a human.** A consumer's own
  namespace and the one being moved are the same token, and only a person can tell them apart. Only
  qualified uses and `using namespace` directives are mechanical.

What the tool cannot decide is in the table as an `apply` of `semantic` (semantic_rename.py owns it)
or `manual` (a human does), and neither is touched here. Nor is a row of kind `removed`, which names
a symbol core-cpp deleted: there is nothing to rename it to, so rewriting it would produce code that
cannot compile. `renames.py` refuses to hand one to a rewrite tool and `_patterns_for` raises on one,
so it is structurally impossible rather than a convention somebody remembers.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

import renames
from renames import Row, TableError

TABLE = Path(__file__).resolve().parent / "renames.json"

#: The extensions a C++ codemod may open. A build tree is not walked at all (see `_sources`).
SOURCE_SUFFIXES = frozenset(
    {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".ixx", ".cppm"}
)

#: Directories that hold somebody else's code or a build's output, never a consumer's sources.
SKIPPED_DIRECTORIES = frozenset({"build", "node_modules", "out", "third_party", "vendor"})

# A raw string first (its body may contain quotes), then an ordinary string, then a character
# literal whose body holds no quote -- so that C++'s digit separator (1'000'000) cannot swallow one.
LITERAL = re.compile(
    r"""R"([^()\\\s]{0,16})\(.*?\)\1" | "(?:[^"\\\n]|\\.)*" | '(?:[^'"\\\n]|\\.)*'""",
    re.VERBOSE | re.DOTALL,
)
PLACEHOLDER = re.compile("\x00(\\d+)\x00")


def _patterns_for(row: Row) -> list[tuple[re.Pattern[str], str]]:
    """The anchored pattern(s) a row is applied with, and their replacements."""
    source = re.escape(row.source)
    if row.kind == "include":
        # Both spellings of the directive; core-cpp headers are always included with angle brackets.
        directive = re.compile(rf"""^([ \t]*#[ \t]*include[ \t]*)[<"]{source}[>"]""", re.MULTILINE)
        return [(directive, rf"\1<{row.target}>")]
    if row.kind == "symbol":
        # A trailing `::` is allowed through, so `cli::command::CommandList` renames its head.
        return [(re.compile(rf"(?<![\w:]){source}(?![\w])"), row.target)]
    if row.kind == "member":
        return [(re.compile(rf"(\.|->)([ \t]*){source}(?=[ \t]*\()"), rf"\1\g<2>{row.target}")]
    if row.kind == "macro":
        return [(re.compile(rf"(?<![\w]){source}(?![\w])"), row.target)]
    if row.kind == "namespace":
        return [
            (re.compile(rf"(?<![\w:]){source}::"), f"{row.target}::"),
            (re.compile(rf"(\busing[ \t]+namespace[ \t]+){source}[ \t]*;"), rf"\1{row.target};"),
        ]
    raise TableError(f"no pattern for kind '{row.kind}'")


def _mask_literals(text: str) -> tuple[str, list[str]]:
    """Replaces every string and character literal with a placeholder no rename pattern can match."""
    literals: list[str] = []

    def keep(match: re.Match[str]) -> str:
        literals.append(match.group(0))
        return f"\x00{len(literals) - 1}\x00"

    return LITERAL.sub(keep, text), literals


def _unmask_literals(text: str, literals: list[str]) -> str:
    return PLACEHOLDER.sub(lambda match: literals[int(match.group(1))], text)


def rewrite_text(text: str, rows: list[Row]) -> tuple[str, Counter[Row]]:
    """Applies @p rows to @p text, returning the result and how often each row fired."""
    applied: Counter[Row] = Counter()

    # Includes first, on the raw text: the quoted form of the directive is not a string literal.
    for row in (row for row in rows if row.kind == "include"):
        for pattern, replacement in _patterns_for(row):
            text, count = pattern.subn(replacement, text)
            applied[row] += count

    masked, literals = _mask_literals(text)
    for row in (row for row in rows if row.kind != "include"):
        for pattern, replacement in _patterns_for(row):
            masked, count = pattern.subn(replacement, masked)
            applied[row] += count

    return _unmask_literals(masked, literals), applied


def _sources(root: Path) -> list[Path]:
    """Every C++ source under @p root, and nothing outside it -- a symlink included."""
    resolved = root.resolve()
    if resolved.is_file():
        return [resolved] if resolved.suffix in SOURCE_SUFFIXES else []
    found: list[Path] = []
    for path in sorted(resolved.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        parts = path.relative_to(resolved).parts[:-1]
        if any(part.startswith(".") or part in SKIPPED_DIRECTORIES for part in parts):
            continue
        if not path.resolve().is_relative_to(resolved):
            print(f"  skipped {path}: it resolves outside {resolved}")
            continue
        found.append(path)
    return found


def rewrite_tree(roots: list[Path], rows: list[Row], dry_run: bool) -> tuple[int, Counter[Row]]:
    """Rewrites every source under @p roots, reporting each changed file. Returns (files, totals)."""
    changed = 0
    totals: Counter[Row] = Counter()
    for root in roots:
        for path in _sources(root):
            text = path.read_text(encoding="utf-8")
            result, applied = rewrite_text(text, rows)
            if result == text:
                continue
            changed += 1
            totals.update(applied)
            print(f"{path}: {sum(applied.values())} replacement(s)")
            for row, count in sorted(applied.items(), key=lambda item: item[1], reverse=True):
                if count:
                    print(f"    {count:5d}  {row.label}")
            if not dry_run:
                path.write_text(result, encoding="utf-8", newline="\n")
    return changed, totals


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--profile", required=True, help="which consumer's subset of the table to apply")
    parser.add_argument("--table", type=Path, default=TABLE, help="the rename table (default: renames.json)")
    parser.add_argument("--dry-run", action="store_true", help="report what would change and write nothing")
    parser.add_argument(
        "paths", type=Path, nargs="+", help="the files and directories to rewrite, and only those"
    )
    arguments = parser.parse_args(argv)

    try:
        rows = renames.load(arguments.table).text_rows(arguments.profile)
    except TableError as error:
        print(f"rewrite: {error}")
        return 1

    missing = [path for path in arguments.paths if not path.exists()]
    for path in missing:
        print(f"rewrite: no such path: {path}")
    if missing:
        return 1

    changed, totals = rewrite_tree(arguments.paths, rows, arguments.dry_run)
    verb = "would change" if arguments.dry_run else "changed"
    print(
        f"rewrite --profile {arguments.profile}: {changed} file{'' if changed == 1 else 's'} {verb}, "
        f"{sum(totals.values())} replacement(s) from {len([r for r in totals if totals[r]])} "
        f"of {len(rows)} rows"
    )
    # A row whose target a Phase B task still owes is applied like any other -- a consumer migrates
    # after v0.1.0, when nothing is pending -- but never in silence: run before that task lands and
    # the rewritten tree names a symbol core-cpp does not have yet.
    pending = (row for row in rows if row.status == "pending" and totals[row])
    for row in sorted(pending, key=lambda r: r.label):
        print(
            f"rewrite: warning: applied {totals[row]}x {row.label}, whose target task {row.task} still owes"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
