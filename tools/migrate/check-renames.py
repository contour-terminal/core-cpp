# SPDX-License-Identifier: Apache-2.0
"""The drift gate: every core-cpp target named in `renames.json` exists in this tree (Ruling R68).

    python tools/migrate/check-renames.py            # over this repository
    ctest -L hygiene -R core-cpp.migrate-renames     # the same, in every local and CI build

A rename table that silently drifts from the delivered API turns six consumer migrations into six
debugging sessions of the same table, so the table is checked against the headers on every build,
and every Phase B task that renames a public symbol updates it in the same commit.

The gate reads a row's `target` and asserts, for a **delivered** row, that:

1. `src/<target.header>` exists;
2. it is listed in a `FILE_SET HEADERS` of its module, unless the row says `"public": false`;
3. the header opens the namespace `target.symbol` names, and declares its remaining components;
4. an include row's `to` is that same header.

A **pending** row -- one whose target a Phase B task still owes -- is checked the other way round:
its symbol must be absent from `src/core/`. So the row cannot rot in either direction; the day the
task lands `core::net::IoBackend`, this gate fails and says to mark the row delivered.

A **removed** row is that inversion made permanent: its `from` names a symbol core-cpp no longer
has, and the gate asserts it stays gone, so a re-introduction is refused (Ruling R75).

Every arm reads a qualified symbol through `qualified_failure()`, which walks the namespace prefixes
instead of testing the whole path as a namespace. That is what lets it see a symbol scoped to an
**enum or a class** -- `core::net::NetErrorCode::SystemError` -- which a whole-path test would call
absent forever, and a pending row that can never resolve is worse than no row at all (Ruling R74).

What it cannot check: the source side (fastcached's and contour's symbols are not in this tree),
a signature, an overload set, or a declaration behind an `#if`. It is a text scan over the headers,
not a compile. Everything it cannot read it *says* it could not read -- a header list held in a
variable this scan cannot resolve is reported, never answered as an empty list, because "this
module publishes nothing" and "I did not look" are the same silence (Ruling R95).
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

# `renames` is the loader beside this script. Python puts a script's own directory first on
# sys.path, so this resolves however the script is reached -- by relative path, by the absolute
# path a ctest gives it, or exec'd by the self-test -- with no sys.path surgery, and therefore no
# import that has to sit below other statements and be excused from the linter.
import renames

HERE = Path(__file__).resolve().parent
DEFAULT_ROOT = HERE.parent.parent
DEFAULT_TABLE = HERE / "renames.json"

COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)
STRING = re.compile(
    r"""R"([^()\\\s]{0,16})\(.*?\)\1" | "(?:[^"\\\n]|\\.)*" | '(?:[^'"\\\n]|\\.)*'""", re.VERBOSE | re.DOTALL
)
NAMESPACE_OPEN = re.compile(r"\bnamespace\s+([A-Za-z_][\w:]*)\s*\{")
MODULE_CALL = re.compile(r"\bcore_cpp_add_module\s*\(")
CMAKE_KEYWORD = re.compile(r"^[A-Z][A-Z0-9_]*$")


@dataclass(frozen=True)
class Summary:
    rows: int
    validated: int
    pending: int
    removed: int
    headers: int


def _stripped(text: str) -> str:
    """The header with its comments and literals blanked, so a mention in prose is not a declaration."""
    return STRING.sub('""', COMMENT.sub(" ", text))


def open_namespaces(text: str) -> set[str]:
    """Every namespace path the header opens, `namespace core::net` and the nested form alike."""
    body = _stripped(text)
    opens = {match.end() - 1: match.group(1) for match in NAMESPACE_OPEN.finditer(body)}
    found: set[str] = set()
    stack: list[tuple[str, int]] = []
    depth = 0
    for index, character in enumerate(body):
        if character == "{":
            depth += 1
            name = opens.get(index)
            if name is not None:
                stack.append((name, depth))
                found.add("::".join(entry for entry, _ in stack))
        elif character == "}":
            while stack and stack[-1][1] == depth:
                stack.pop()
            depth -= 1
    return found


def declares(text: str, name: str) -> bool:
    """Whether the header declares @p name: a type, alias, concept, enumerator, function or macro."""
    body = _stripped(text)
    escaped = re.escape(name)
    patterns = (
        rf"\b(?:class|struct|union)\s+(?:\[\[[^\]]*\]\]\s*)?(?:[A-Z_][A-Z0-9_]*\s+)?{escaped}\b",
        rf"\benum\s+(?:class\s+|struct\s+)?{escaped}\b",
        rf"\busing\s+{escaped}\s*=",
        rf"\btypedef\b[^;]*\b{escaped}\s*;",
        rf"\bconcept\s+{escaped}\b",
        rf"^[ \t]*#[ \t]*define[ \t]+{escaped}\b",
        # A function, template or macro. The lookbehind keeps a *call* through another scope
        # (`std::sort(`, `core::digitValue<16>(`) from reading as a declaration of that name here.
        rf"(?<![\w.>:]){escaped}\s*[(<]",
        rf"(?<![\w.>:]){escaped}\s*(?:=|,|;|\}})",
    )
    return any(re.search(pattern, body, re.MULTILINE) for pattern in patterns)


def qualified_failure(text: str, components: list[str]) -> str | None:
    """Why @p text does not declare the qualified symbol @p components, or None if it does.

    The prefix walk is what makes a symbol scoped to an **enum or a class** findable:
    `core::net::NetErrorCode::SystemError` has no namespace called `core::net::NetErrorCode`, so a
    test for the whole path as a namespace answers "absent" forever (controller ruling R74).

    This is the one place the walk exists. It returns a *reason* rather than a bool so that the
    delivered arm, which has to say what is wrong, can use it too: that arm previously carried its
    own copy of the walk under a docstring claiming every arm shared this one, which is how a fix
    made here would have reached two arms of three and read as reaching all of them (R93).
    """
    if len(components) == 1:
        return None if defines_macro(text, components[0]) else f"declares no macro {components[0]}"
    symbol = "::".join(components)
    namespaces = open_namespaces(text)
    shortest: list[str] | None = None
    for cut in range(len(components) - 1, 0, -1):
        if "::".join(components[:cut]) not in namespaces:
            continue
        absent = [name for name in components[cut:] if not declares(text, name)]
        if not absent:
            return None
        if shortest is None or len(absent) < len(shortest):
            shortest = absent
    if shortest is None:
        opened = ", ".join(sorted(namespaces)) or "none"
        return f"opens no namespace of '{symbol}' (it opens {opened})"
    return f"declares no {', '.join(repr(name) for name in shortest)}, for {symbol}"


def declares_qualified(text: str, components: list[str]) -> bool:
    """Whether the header declares the qualified symbol @p components, e.g. `core::net::EventLoop`.

    The pending and removed arms want the yes/no; the delivered arm wants the reason. Both read
    `qualified_failure()`, which is the walk itself.
    """
    return qualified_failure(text, components) is None


def find_symbol(root: Path, symbol: str) -> Path | None:
    """The header under `src/core/` that declares @p symbol, or None if nothing does."""
    components = symbol.split("::")
    for header in sorted((root / "src" / "core").rglob("*.hpp")):
        if declares_qualified(header.read_text(encoding="utf-8"), components):
            return header
    return None


def defines_macro(text: str, name: str) -> bool:
    """Whether the header defines @p name -- `#cmakedefine` included, for a configured header."""
    pattern = rf"^[ \t]*#[ \t]*(?:define|cmakedefine01|cmakedefine)[ \t]+{re.escape(name)}\b"
    return re.search(pattern, _stripped(text), re.MULTILINE) is not None


def public_headers(root: Path) -> tuple[set[str], list[str]]:
    """Every include path a module's `FILE_SET HEADERS` publishes, and every list it could not read.

    Returns (`core/<module>/<Header>.hpp` for each published header, problems). The second half is
    the point. This resolver answers "can a consumer include this?", and every way it fails answers
    *no* while looking exactly like a module that publishes nothing: an unknown `${X}` resolved to
    the empty list, and -- the quiet one -- a `${X}` a `list(APPEND)` extended resolved to whatever
    the `set()` beside it held, which is a confidently short answer that an "unresolvable reference"
    check passes, because the reference does resolve; it is just missing entries. An empty list is
    at least suspicious on inspection, while a list one entry short looks entirely normal, so both
    arms are reported rather than resolved (controller ruling R95).
    """
    source_root = root / "src"
    found: set[str] = set()
    problems: list[str] = []
    for listing in sorted((source_root / "core").rglob("CMakeLists.txt")):
        text = listing.read_text(encoding="utf-8")
        directory = listing.parent.relative_to(source_root).as_posix()
        variables, unfollowed = _cmake_variables(text)
        where = listing.relative_to(root).as_posix()
        for call in MODULE_CALL.finditer(text):
            body = _balanced(text, call.end() - 1)
            headers, unreadable = _header_tokens(body, variables, unfollowed)
            for header in headers:
                found.add(header if header.startswith("core/") else f"{directory}/{header}")
            problems += [f"{where}: {problem}" for problem in unreadable]
    return found, problems


#: The `list()` sub-commands this text scan models: each one only adds entries, so reading every
#: occurrence of a variable gives the full set whatever the surrounding `if()` decides. Any other
#: sub-command -- TRANSFORM, REMOVE_ITEM, FILTER, POP_BACK, SORT -- changes a list in a way a scan
#: cannot follow, and a HEADERS list that reaches one is reported instead of answered short.
LIST_ADDITIONS = ("APPEND", "PREPEND", "INSERT")

SET_CALL = re.compile(r"\bset\s*\(\s*([A-Za-z_]\w*)([^)]*)\)")
LIST_CALL = re.compile(r"\blist\s*\(\s*([A-Z][A-Z0-9_]*)\s+([A-Za-z_]\w*)([^)]*)\)")


def _cmake_variables(text: str) -> tuple[dict[str, list[str]], dict[str, str]]:
    """Every variable of a CMakeLists this scan can resolve, and every one it knows it cannot.

    A header a module publishes only under an option (`CORE_CPP_WITH_IMAGES`) is public, so every
    assignment contributes, not the last one -- the scan has no idea which branch a build takes, and
    the union is the right answer for "could a consumer include it".
    """
    variables: dict[str, list[str]] = {}
    unfollowed: dict[str, str] = {}

    def words(tail: str) -> list[str]:
        return [token.strip('"') for token in re.findall(r'"[^"]*"|\S+', tail)]

    for match in SET_CALL.finditer(text):
        variables.setdefault(match.group(1), []).extend(words(match.group(2)))
    for match in LIST_CALL.finditer(text):
        command, name, tail = match.group(1), match.group(2), match.group(3)
        if command not in LIST_ADDITIONS:
            unfollowed[name] = f"list({command})"
            continue
        # `list(INSERT <var> <index> <element>...)`: the index is not an element.
        entries = words(tail)[1:] if command == "INSERT" else words(tail)
        variables.setdefault(name, []).extend(entries)
    return variables, unfollowed


def _balanced(text: str, open_index: int) -> str:
    """The text between the parenthesis at @p open_index and the one that closes it."""
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return text[open_index + 1 : index]
    return text[open_index + 1 :]


def _header_tokens(
    body: str, variables: dict[str, list[str]], unfollowed: dict[str, str]
) -> tuple[list[str], list[str]]:
    """The tokens of the call's HEADERS section, up to the next all-caps keyword, and what it could
    not read. A `${X}` this scan cannot resolve in full is a problem, never an empty answer (R95)."""
    headers: list[str] = []
    problems: list[str] = []
    collecting = False
    for token in re.findall(r'"[^"]*"|\S+', body):
        bare = token.strip('"')
        if CMAKE_KEYWORD.match(bare):
            collecting = bare == "HEADERS"
            continue
        if not collecting:
            continue
        reference = re.fullmatch(r"\$\{([A-Za-z_]\w*)\}", bare)
        if reference:  # a HEADERS list held in a variable of the same CMakeLists
            name = reference.group(1)
            if name in unfollowed:
                problems.append(
                    f"HEADERS names ${{{name}}}, which {unfollowed[name]} changes in a way this "
                    f"scan cannot follow; the list it would read is short, and every header missing "
                    f"from it reads as private"
                )
            elif name not in variables:
                problems.append(
                    f"HEADERS names ${{{name}}}, which this CMakeLists never sets; the module would "
                    f"read as publishing nothing, which is indistinguishable from a private module"
                )
            else:
                headers += [entry for entry in variables[name] if entry]
            continue
        if "}" in bare:  # a generated header, named through a ${...} path prefix
            bare = bare.rsplit("}", 1)[1].lstrip("/")
        if bare:
            headers.append(bare)
    return headers, problems


def _header_text(root: Path, header: str) -> str | None:
    """The header's text. A configured header (`core/Config.hpp`) is read from its `.in` template,
    which is where it is delivered from: CMake writes the header itself into the build tree."""
    for path in (root / "src" / header, root / "src" / f"{header}.in"):
        if path.is_file():
            return path.read_text(encoding="utf-8")
    return None


def _check_delivered(root: Path, row: renames.Row, public: set[str], where: str) -> list[str]:
    target = row.delivers
    failures: list[str] = []
    text = _header_text(root, target.header)
    if text is None:
        return [f"{where}: no such header src/{target.header} for {row.label}"]
    if target.public and target.header not in public:
        failures.append(
            f"{where}: src/{target.header} is in no FILE_SET HEADERS, so a consumer cannot include it "
            f'(say "public": false if that is the point)'
        )
    if row.kind == "include" and row.target != target.header:
        failures.append(
            f"{where}: the row rewrites to <{row.target}> but names src/{target.header} as its target"
        )
    if not target.symbol:
        return failures

    # The same walk the pending and removed arms use, not a copy of it (controller ruling R93).
    reason = qualified_failure(text, target.symbol.split("::"))
    if reason is not None:
        failures.append(f"{where}: src/{target.header} {reason}")
    return failures


def _check_pending(root: Path, row: renames.Row, where: str) -> list[str]:
    target = row.delivers
    if not target or not target.symbol:
        return []
    header = find_symbol(root, target.symbol)
    if header is None:
        return []
    return [
        f"{where}: {target.symbol} now exists in {header.relative_to(root).as_posix()}, "
        f"so mark the row delivered (it waits on task {row.task})"
    ]


def _check_removed(root: Path, row: renames.Row, where: str) -> list[str]:
    """The gate run backwards: a symbol core-cpp removed must stay removed (controller ruling R75)."""
    header = find_symbol(root, row.source)
    if header is None:
        return []
    return [
        f"{where}: {row.source} is a removed symbol, but {header.relative_to(root).as_posix()} "
        f"declares it again. Either the removal was reverted, or the row is stale and should go."
    ]


def validate(root: Path, table: Path) -> list[str]:
    """Returns every way @p table disagrees with the tree at @p root, or an empty list."""
    # The tree is read first, and what it could not read is reported whatever the table says. A
    # header list this scan cannot resolve is a fact about the tree, and a table that fails to load
    # must not swallow it -- that is the same silence R95 is about, one level up.
    public, failures = public_headers(root)
    try:
        loaded = renames.load(table)
    except renames.TableError as error:
        return failures + [str(error)]
    for index, row in enumerate(loaded.rows):
        where = f"rows[{index}]"
        if row.kind == "removed":
            failures += _check_removed(root, row, where)
        elif row.status == "pending":
            failures += _check_pending(root, row, where)
        elif row.delivers is not None:
            failures += _check_delivered(root, row, public, where)
    return failures


def summarise(root: Path, table: Path) -> Summary:
    loaded = renames.load(table)
    return Summary(
        rows=len(loaded.rows),
        validated=len([row for row in loaded.rows if row.status == "delivered" and row.delivers]),
        pending=len([row for row in loaded.rows if row.status == "pending"]),
        removed=len([row for row in loaded.rows if row.kind == "removed"]),
        headers=len(public_headers(root)[0]),
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--root", type=Path, default=DEFAULT_ROOT, help="the core-cpp checkout to check against"
    )
    parser.add_argument("--table", type=Path, default=DEFAULT_TABLE, help="the rename table")
    arguments = parser.parse_args(argv)

    failures = validate(arguments.root, arguments.table)
    for failure in failures:
        print(f"check-renames: {failure}")
    try:
        summary = summarise(arguments.root, arguments.table)
    except renames.TableError:
        # validate() has already reported it; a table that does not load has no rows to count.
        return 1
    print(
        f"check-renames: {summary.rows} rows, {summary.validated} with a delivered core-cpp target, "
        f"{summary.pending} pending, {summary.removed} removed, over {summary.headers} public headers: "
        f"{len(failures)} failure(s)"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
