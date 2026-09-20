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

Every arm reads a qualified symbol through `declaresQualified()`, which walks the namespace prefixes
instead of testing the whole path as a namespace. That is what lets it see a symbol scoped to an
**enum or a class** -- `core::net::NetErrorCode::SystemError` -- which a whole-path test would call
absent forever, and a pending row that can never resolve is worse than no row at all (Ruling R74).

What it cannot check: the source side (fastcached's and contour's symbols are not in this tree),
a signature, an overload set, or a declaration behind an `#if`. It is a text scan over the headers,
not a compile.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import renames  # noqa: E402  (the loader lives beside this script, which a ctest invokes by path)

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


def openNamespaces(text: str) -> set[str]:
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


def declaresQualified(text: str, components: list[str]) -> bool:
    """Whether the header declares the qualified symbol @p components, e.g. `core::net::EventLoop`.

    The prefix walk is what makes a symbol scoped to an **enum or a class** findable:
    `core::net::NetErrorCode::SystemError` has no namespace called `core::net::NetErrorCode`, so a
    test for the whole path as a namespace answers "absent" forever. Every arm of the gate --
    delivered, pending and removed -- reads a symbol through this one function, so none of them can
    drift into that hole on its own (controller ruling R74).
    """
    if len(components) == 1:
        return definesMacro(text, components[0])
    namespaces = openNamespaces(text)
    return any(
        "::".join(components[:cut]) in namespaces and all(declares(text, name) for name in components[cut:])
        for cut in range(len(components) - 1, 0, -1)
    )


def findSymbol(root: Path, symbol: str) -> Path | None:
    """The header under `src/core/` that declares @p symbol, or None if nothing does."""
    components = symbol.split("::")
    for header in sorted((root / "src" / "core").rglob("*.hpp")):
        if declaresQualified(header.read_text(encoding="utf-8"), components):
            return header
    return None


def definesMacro(text: str, name: str) -> bool:
    """Whether the header defines @p name -- `#cmakedefine` included, for a configured header."""
    pattern = rf"^[ \t]*#[ \t]*(?:define|cmakedefine01|cmakedefine)[ \t]+{re.escape(name)}\b"
    return re.search(pattern, _stripped(text), re.MULTILINE) is not None


def publicHeaders(root: Path) -> set[str]:
    """Every include path a module's `FILE_SET HEADERS` publishes, as `core/<module>/<Header>.hpp`."""
    sourceRoot = root / "src"
    found: set[str] = set()
    for listing in sorted((sourceRoot / "core").rglob("CMakeLists.txt")):
        text = listing.read_text(encoding="utf-8")
        directory = listing.parent.relative_to(sourceRoot).as_posix()
        variables = _setVariables(text)
        for call in MODULE_CALL.finditer(text):
            body = _balanced(text, call.end() - 1)
            for header in _headerTokens(body, variables):
                found.add(header if header.startswith("core/") else f"{directory}/{header}")
    return found


def _setVariables(text: str) -> dict[str, list[str]]:
    """Every `set(<var> ...)` of a CMakeLists, so a HEADERS list held in a variable is still read.

    A header a module publishes only under an option (`CORE_CPP_WITH_IMAGES`) is public, so every
    assignment of a variable contributes, not the last one.
    """
    variables: dict[str, list[str]] = {}
    for match in re.finditer(r"\bset\s*\(\s*([A-Za-z_]\w*)([^)]*)\)", text):
        variables.setdefault(match.group(1), []).extend(
            token.strip('"') for token in re.findall(r'"[^"]*"|\S+', match.group(2))
        )
    return variables


def _balanced(text: str, openIndex: int) -> str:
    """The text between the parenthesis at @p openIndex and the one that closes it."""
    depth = 0
    for index in range(openIndex, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return text[openIndex + 1 : index]
    return text[openIndex + 1 :]


def _headerTokens(body: str, variables: dict[str, list[str]]) -> list[str]:
    """The tokens of the call's HEADERS section, up to the next all-caps keyword."""
    headers: list[str] = []
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
            headers += [name for name in variables.get(reference.group(1), []) if name]
            continue
        if "}" in bare:  # a generated header, named through a ${...} path prefix
            bare = bare.rsplit("}", 1)[1].lstrip("/")
        if bare:
            headers.append(bare)
    return headers


def _headerText(root: Path, header: str) -> str | None:
    """The header's text. A configured header (`core/Config.hpp`) is read from its `.in` template,
    which is where it is delivered from: CMake writes the header itself into the build tree."""
    for path in (root / "src" / header, root / "src" / f"{header}.in"):
        if path.is_file():
            return path.read_text(encoding="utf-8")
    return None


def _checkDelivered(root: Path, row: renames.Row, public: set[str], where: str) -> list[str]:
    target = row.delivers
    failures: list[str] = []
    text = _headerText(root, target.header)
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

    components = target.symbol.split("::")
    if len(components) == 1:
        if not definesMacro(text, target.symbol):
            failures.append(f"{where}: src/{target.header} declares no macro {target.symbol}")
        return failures

    namespaces = openNamespaces(text)
    for cut in range(len(components) - 1, 0, -1):
        if "::".join(components[:cut]) in namespaces:
            break
    else:
        failures.append(
            f"{where}: src/{target.header} opens no namespace of '{target.symbol}' "
            f"(it opens {', '.join(sorted(namespaces)) or 'none'})"
        )
        return failures
    for name in components[cut:]:
        if not declares(text, name):
            failures.append(f"{where}: src/{target.header} declares no '{name}', for {target.symbol}")
    return failures


def _checkPending(root: Path, row: renames.Row, where: str) -> list[str]:
    target = row.delivers
    if not target or not target.symbol:
        return []
    header = findSymbol(root, target.symbol)
    if header is None:
        return []
    return [
        f"{where}: {target.symbol} now exists in {header.relative_to(root).as_posix()}, "
        f"so mark the row delivered (it waits on task {row.task})"
    ]


def _checkRemoved(root: Path, row: renames.Row, where: str) -> list[str]:
    """The gate run backwards: a symbol core-cpp removed must stay removed (controller ruling R75)."""
    header = findSymbol(root, row.source)
    if header is None:
        return []
    return [
        f"{where}: {row.source} is a removed symbol, but {header.relative_to(root).as_posix()} "
        f"declares it again. Either the removal was reverted, or the row is stale and should go."
    ]


def validate(root: Path, table: Path) -> list[str]:
    """Returns every way @p table disagrees with the tree at @p root, or an empty list."""
    try:
        loaded = renames.load(table)
    except renames.TableError as error:
        return [str(error)]
    public = publicHeaders(root)
    failures: list[str] = []
    for index, row in enumerate(loaded.rows):
        where = f"rows[{index}]"
        if row.kind == "removed":
            failures += _checkRemoved(root, row, where)
        elif row.status == "pending":
            failures += _checkPending(root, row, where)
        elif row.delivers is not None:
            failures += _checkDelivered(root, row, public, where)
    return failures


def summarise(root: Path, table: Path) -> Summary:
    loaded = renames.load(table)
    return Summary(
        rows=len(loaded.rows),
        validated=len([row for row in loaded.rows if row.status == "delivered" and row.delivers]),
        pending=len([row for row in loaded.rows if row.status == "pending"]),
        removed=len([row for row in loaded.rows if row.kind == "removed"]),
        headers=len(publicHeaders(root)),
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
