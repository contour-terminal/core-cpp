# SPDX-License-Identifier: Apache-2.0
"""The mechanical half of a consumer migration: includes, namespaces, symbols, members and macros.

    python tools/migrate/rewrite.py --profile endo src
    python tools/migrate/rewrite.py --profile tuidu --dry-run src/tuidu

Every rule is a row of `renames.json`, selected by `--profile`, and every pattern is anchored so
that a longer qualified name is never a match: `(?<![\\w:])net::` leaves `std::net::`, `endo::net::`
and `mynet::` alone. The tool is idempotent -- a second run over a rewritten tree changes nothing,
and a half-converted tree converges -- so a reviewer can re-run it after a rebase.

Three deliberate boundaries:

- **String and character literals are not rewritten.** A codemod may change what the code says; it
  must never change what the program sends. That includes a raw string holding C++ -- test data for
  a parser or a highlighter -- whose line-initial `#include` stays exactly as written. Comments
  *are* rewritten, because a comment documents the code beside it. One ordered scan (`SPANS`)
  decides which is which; the order of its alternatives is the reason it holds.
- **A namespace *definition* (`namespace net { ... }`) is left to a human**, and so is a namespace
  **alias** (`namespace cli = crispy::cli;`). A consumer's own namespace and the one being moved are
  the same token, and only a person can tell them apart. Only qualified uses and `using namespace`
  directives are mechanical.
- **Bytes the rename does not reach are returned unchanged**, so a file's line endings and encoding
  survive it: a codemod that also converted CRLF to LF would rewrite every line of every file it
  touched, and the diff a reviewer needs would be the one thing it destroyed. A file that is not
  UTF-8 is reported and skipped, and the run then exits non-zero rather than leaving a tree that is
  half converted and says it succeeded.

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
#: Compared case-folded, because `Build/` is a build tree too, and CPM puts fetched third-party
#: sources under `_deps/` -- which a consumer pointing this at a repository root would rewrite.
SKIPPED_DIRECTORIES = frozenset({"build", "node_modules", "out", "third_party", "vendor", "_deps"})
SKIPPED_DIRECTORY_PREFIXES = ("cmake-build",)

# One scan classifies the whole file, and the ORDER of these alternatives is the substance of it:
#
#   directive  first, so that the quoted path of `#include "net/X.hpp"` is never read as a string;
#   comment    before the raw-string rule, because that rule is the only one not confined to a line
#              -- a comment mentioning R"( used to open a mask that ran to the next )" anywhere in
#              the file, silently swallowing the code between (finding I2);
#   rawstring  before code, so a line-initial #include *inside* embedded C++ test data is part of
#              the literal and is left alone (finding I1). A highlighter or parser suite is full of
#              these, and rewriting one changes what the program sends.
#   char       ONE character or one escape sequence between the quotes, and nothing longer. It used
#              to take any run of characters but a `"`, so `'"'` did not match it, the `"` opened a
#              string that ran to the next `"`, and the code in between -- contour's
#              `os << '"' << crispy::escape(s) << '"'` -- was masked as data. Allowing the `"`
#              without the length bound would let a literal span a digit separator's quotes
#              (`1'000'000`) and mask whatever lay between; a single character cannot.
#
# Whatever the scan does not match is code. Code and comments are rewritten; a directive takes the
# include rows only; a string, character or raw-string literal is never touched.
SPANS = re.compile(
    r"""(?P<directive>^[ \t]*\#[ \t]*include[ \t]*(?:<[^>\n]*>|"[^"\n]*"))
      | (?P<comment>//[^\n]*|/\*.*?\*/)
      | (?P<rawstring>R"(?P<delim>[^()\\\s]{0,16})\(.*?\)(?P=delim)")
      | (?P<string>"(?:[^"\\\n]|\\.)*")
      | (?P<char>'(?:[^'\\\n]|\\(?:[xX][0-9A-Fa-f]+|u[0-9A-Fa-f]{4}|U[0-9A-Fa-f]{8}|[0-7]{1,3}|.))')""",
    re.VERBOSE | re.DOTALL | re.MULTILINE,
)


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


def _apply(chunk: str, rows: list[Row], applied: Counter[Row]) -> str:
    """Applies @p rows to one span of text, counting each row's hits into @p applied."""
    for row in rows:
        for pattern, replacement in _patterns_for(row):
            chunk, count = pattern.subn(replacement, chunk)
            applied[row] += count
    return chunk


def rewrite_text(text: str, rows: list[Row]) -> tuple[str, Counter[Row]]:
    """Applies @p rows to @p text, returning the result and how often each row fired."""
    applied: Counter[Row] = Counter()
    includes = [row for row in rows if row.kind == "include"]
    others = [row for row in rows if row.kind != "include"]

    pieces: list[str] = []
    position = 0
    for match in SPANS.finditer(text):
        pieces.append(_apply(text[position : match.start()], others, applied))
        if match.group("directive") is not None:
            pieces.append(_apply(match.group(0), includes, applied))
        elif match.group("comment") is not None:
            # A comment documents the code beside it, so it follows the rename.
            pieces.append(_apply(match.group(0), others, applied))
        else:
            pieces.append(match.group(0))  # a string, character or raw-string literal: data
        position = match.end()
    pieces.append(_apply(text[position:], others, applied))

    return "".join(pieces), applied


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
        folded = [part.casefold() for part in parts]
        if any(
            part.startswith(".") or part in SKIPPED_DIRECTORIES or part.startswith(SKIPPED_DIRECTORY_PREFIXES)
            for part in folded
        ):
            continue
        if not path.resolve().is_relative_to(resolved):
            print(f"  skipped {path}: it resolves outside {resolved}")
            continue
        found.append(path)
    return found


def rewrite_tree(roots: list[Path], rows: list[Row], dry_run: bool) -> tuple[int, Counter[Row], list[Path]]:
    """Rewrites every source under @p roots, reporting each changed file.

    Returns (files changed, totals, files skipped). Bytes are read and written as bytes: decoding
    with Python's universal newlines and writing back `\\n` rewrote every line of every changed
    file, including the lines the codemod never touched, which makes the diff unreviewable on a
    checkout that holds CRLF -- and neither the tests nor the purity proof could see it, because
    both sides of that comparison go through this same tool (finding I4).
    """
    changed = 0
    totals: Counter[Row] = Counter()
    skipped: list[Path] = []
    for root in roots:
        for path in _sources(root):
            try:
                text = path.read_bytes().decode("utf-8")
            except UnicodeDecodeError as error:
                # One odd byte is not a stop order: report it, keep going, and fail at the end.
                # Aborting here left the tree half-converted behind a traceback (finding I3).
                print(f"{path}: skipped, not UTF-8 ({error.reason} at byte {error.start})")
                skipped.append(path)
                continue
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
                path.write_bytes(result.encode("utf-8"))
    return changed, totals, skipped


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

    changed, totals, skipped = rewrite_tree(arguments.paths, rows, arguments.dry_run)
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
    # A file this could not read is a file the migration has not converted, so the run failed even
    # though most of it succeeded. Exiting 0 here would let a CI-run migration half-succeed quietly.
    if skipped:
        print(f"rewrite: {len(skipped)} file(s) skipped, not UTF-8; the tree is not fully converted")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
