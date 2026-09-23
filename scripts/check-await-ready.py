#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Refuses an `await_ready` whose body makes a call.

    python scripts/check-await-ready.py

MSVC 19.44's ARM64 code generator drops the enclosing `try` of a `co_await` on a TEMPORARY awaiter
whose `await_ready` makes a call ([fastcached#1546]). The resume function's exception table then
declares no try block for the handler the source wrote, and an `OperationCancelled` thrown by
`await_resume` passes a typed `catch` and `catch (...)` alike. It was measured with a virtual call
-- `DelayAwaiter::await_ready` reading `IClock::now()` -- and it does not happen when
`await_ready` makes no call, when the value is read in the constructor, or when the awaiter is a
named local. The rule that follows, in `.agent/rules/async-and-net.md`: **an `await_ready` stays
trivial -- a member read, a comparison of members, or a constant -- and a decision that needs a
call moves into `await_suspend`, which may decline to park.**

The defect is observable on one CI leg, `windows (cl-release-arm64)`, and only for a shape a test
happens to exercise. So the shape is refused here, on every push, by reading the source. Every
`await_ready` DEFINITION under `src/` and `tests/` -- tests included, since they run on that leg
too -- has its body read, and two spellings in it are refused:

    call          an identifier followed by `(`: `clock().now()`, `hasBufferedInput()`,
                  `handle.done()`, `_task->await_ready()`, `empty()`
    construction  an identifier followed by `{`: `std::scoped_lock { mutex }` runs a constructor,
                  and taking a lock is a call

A use of `await_ready` that is not a definition (`awaiter.await_ready()`, a `static_assert`, a
concept's requirement) has no body and is not read. Keywords and casts are not calls.

**What it cannot see is an overloaded operator.** `!_child` on a handle wrapper, `_gate->_busy`
through a `shared_ptr` and `a == b` on a class type are calls the text does not show. That is why
the awaiters fixed for #1546 answer a constant and carry a `static_assert` that says so: a constant
cannot hide an operator. This scan is the net under the rest.

An `await_ready` that must make a call anyway is listed in ALLOWED under its path and the called
name, with the reason; a row that no longer matches is refused as stale, so the list cannot outlive
what it excuses. A scan that read no file, or found no definition, fails: that is a broken scan,
not a clean tree.

Exit status: 0 when every `await_ready` is trivial or excused; 1 otherwise, naming each by file
and line.

[fastcached#1546]: https://github.com/LASTRADA-Software/fastcached/issues/1546
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent

SUFFIXES = (".hpp", ".cpp", ".h")

# `await_ready()`, its qualifiers, and the `{` that opens a body. Anything else after the `()` --
# `)`, `;`, `}` -- is a use or a declaration, which has no body to read.
DEFINITION = re.compile(
    r"\bawait_ready\s*\(\s*\)\s*"
    r"(?:const\s*)?"
    r"(?:noexcept(?:\s*\([^()]*\))?\s*)?"
    r"(?:(?:override|final)\s*)*"
    r"(?:->\s*[\w:<>,\s]+?\s*)?"
    r"\{"
)

# A name -- qualified, or reached through `.` or `->` -- with optional template arguments, then the
# `(` of a call or the `{` of a construction.
CALL = re.compile(r"([A-Za-z_]\w*(?:\s*::\s*[A-Za-z_]\w*)*)\s*(?:<[^;{}()]*>)?\s*([({])")

# Names followed by `(` or `{` that run no code of their own.
NOT_CALLS = {
    "return",
    "if",
    "while",
    "for",
    "switch",
    "else",
    "do",
    "try",
    "catch",
    "sizeof",
    "alignof",
    "decltype",
    "noexcept",
    "requires",
    "static_cast",
    "const_cast",
    "reinterpret_cast",
    "bool",
    "char",
    "int",
    "unsigned",
    "long",
    "short",
    "float",
    "double",
}

# (path, called name) -> reason. Empty, and that is the state to keep: every `await_ready` in the
# tree was made trivial for #1546, and a new decision that needs a call belongs in `await_suspend`.
# A row here is for the case that genuinely cannot move, and its reason must say why not.
ALLOWED: dict[tuple[str, str], str] = {}


def blank_comments_and_strings(text: str) -> str:
    """:return: @p text with comments and string and character literals blanked, lines kept."""
    pattern = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", re.S)
    return pattern.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), text)


def body_at(code: str, open_brace: int) -> str | None:
    """:return: the text between the `{` at @p open_brace and its matching `}`, or None."""
    depth = 0
    for index in range(open_brace, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[open_brace + 1 : index]
    return None


def calls_in(body: str) -> list[tuple[str, str, int]]:
    """:return: (name, kind, offset) for every call or construction in @p body."""
    found = []
    for match in CALL.finditer(body):
        name = re.sub(r"\s+", "", match.group(1))
        if name.rsplit("::", 1)[-1] in NOT_CALLS:
            continue
        kind = "call" if match.group(2) == "(" else "construction"
        found.append((name, kind, match.start()))
    return found


def scan(root: Path, allowed: dict[tuple[str, str], str]) -> tuple[list[str], int, int]:
    """Scans @p root/src and @p root/tests.

    :return: (problems, files read, `await_ready` definitions read).
    """
    problems = []
    read = 0
    definitions = 0
    matched = set()
    paths = [path for top in ("src", "tests") for path in sorted((root / top).rglob("*"))]
    for path in paths:
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        read += 1
        relative = path.relative_to(root).as_posix()
        code = blank_comments_and_strings(path.read_text(encoding="utf-8"))
        for definition in DEFINITION.finditer(code):
            body = body_at(code, definition.end() - 1)
            if body is None:
                line = code.count("\n", 0, definition.start()) + 1
                problems.append(f"{relative}:{line}: [unreadable] an await_ready body with no closing brace")
                continue
            definitions += 1
            for name, kind, offset in calls_in(body):
                if (relative, name) in allowed:
                    matched.add((relative, name))
                    continue
                line = code.count("\n", 0, definition.end() + offset) + 1
                verb = "calls" if kind == "call" else "constructs"
                problems.append(
                    f"{relative}:{line}: [{kind}] await_ready {verb} `{name}`; answer a member or a "
                    f"constant and decide in await_suspend (fastcached#1546, .agent/rules/async-and-net.md)"
                )
    for relative, name in sorted(set(allowed) - matched):
        problems.append(
            f"{relative}: [stale-allow] `{name}` is allowed ({allowed[(relative, name)]}), "
            f"but no await_ready there makes that call; drop the row"
        )
    return problems, read, definitions


def main() -> int:
    problems, read, definitions = scan(REPOSITORY_ROOT, ALLOWED)
    if read == 0 or definitions == 0:
        print(
            f"check-await-ready: read {read} file(s) and {definitions} await_ready definition(s) "
            f"-- the scan is broken, not the tree clean"
        )
        return 1
    for problem in problems:
        print(problem)
    if problems:
        print(f"check-await-ready: {len(problems)} problem(s) in {definitions} await_ready definition(s)")
        return 1
    print(f"check-await-ready: {definitions} await_ready definition(s) in {read} file(s); none makes a call")
    return 0


if __name__ == "__main__":
    sys.exit(main())
