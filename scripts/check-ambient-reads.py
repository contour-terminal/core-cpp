#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Refuses a direct read of an ambient resource in core-cpp's code, outside the seam that owns it.

    python scripts/check-ambient-reads.py

`.agent/rules/design-principles.md` says it first: anything that touches time, the environment or
any other ambient resource is reached through an interface the object is GIVEN, never through a
free function with hidden state -- `IClock`, not `std::chrono::steady_clock::now()`;
`EnvironmentProvider`, not `std::getenv()`. That rule had no step behind it. It was a sentence a
reviewer agreed with, and the tree carries direct reads it never caught. This is the step: a scan
over `src/core/` for the spellings that bypass a seam.

    clock        `steady_clock::now()`, `system_clock::now()`, `high_resolution_clock::now()`
    environment  `getenv(`, `secure_getenv(`, `_wgetenv(`

A spelling inside a comment is not a read, and a test (`*_test.cpp`) may read what it likes. A read
that IS the seam -- the production clock, the process environment provider -- or that is debt
recorded on purpose is listed in ALLOWED with its reason, and a row that no longer matches a read is
refused as stale, so the list cannot outlive what it excuses. A scan that read no file fails.

Exit status: 0 when every read is a seam or excused; 1 otherwise, naming each by file and line.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent

READS = {
    "clock": re.compile(r"\b(?:steady_clock|system_clock|high_resolution_clock)::now\s*\("),
    "environment": re.compile(r"(?<![A-Za-z0-9_])(?:secure_getenv|_wgetenv|getenv)\s*\("),
}

# path -> reason. The seams themselves, and the reads that are debt on purpose.
ALLOWED: dict[str, str] = {
    "src/core/platform/Clock.hpp": "the seam: SteadyClock and SystemWallClock are the production IClock and IWallClock",
    "src/core/log/LogSink.cpp": "debt: a log line's timestamp is the wall clock; LogSink predates IWallClock (contour)",
    "src/core/net/windows/IocpBackend.cpp": "a real-time budget for the destructor's drain of the port, which must bound in wall time whatever clock the loop was given",
    "src/core/tui/HoverState.cpp": "debt imported from endo f774a210: tui widgets read the clock directly",
    "src/core/tui/InputField.cpp": "debt imported from endo f774a210: tui widgets read the clock directly",
    "src/core/tui/Screen.cpp": "debt imported from endo f774a210: tui widgets read the clock directly",
    "src/core/tui/SemanticBlockClient.cpp": "debt imported from endo f774a210: tui widgets read the clock directly",
    "src/core/tui/Spinner.cpp": "debt imported from endo f774a210: tui widgets read the clock directly",
    "src/core/tui/TimerUtils.hpp": "debt imported from endo f774a210: tui widgets read the clock directly",
    "src/core/tui/TreeTableView.cpp": "debt imported from endo f774a210: tui widgets read the clock directly",
}


def strip_comments(text: str) -> str:
    """:return: @p text with `//` and `/* */` comments blanked, line structure kept."""
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group()), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def scan(root: Path, allowed: dict[str, str]) -> tuple[list[str], int]:
    """Scans @p root/src/core.

    :return: (problems, how many files were read).
    """
    problems = []
    read = 0
    matched = set()
    for path in sorted((root / "src" / "core").rglob("*")):
        if path.suffix not in (".hpp", ".cpp", ".h") or path.name.endswith("_test.cpp"):
            continue
        read += 1
        relative = path.relative_to(root).as_posix()
        code = strip_comments(path.read_text(encoding="utf-8"))
        for number, line in enumerate(code.split("\n"), start=1):
            for kind, pattern in READS.items():
                if not pattern.search(line):
                    continue
                if relative in allowed:
                    matched.add(relative)
                    continue
                problems.append(
                    f"{relative}:{number}: [{kind}] a direct read of the {kind}; take it through the seam "
                    f"the object is given (.agent/rules/design-principles.md, Dependency injection)"
                )
    for relative in sorted(set(allowed) - matched):
        problems.append(
            f"{relative}: [stale-allow] allowed ({allowed[relative]}), but it reads nothing; drop the row"
        )
    return problems, read


def main() -> int:
    problems, read = scan(REPOSITORY_ROOT, ALLOWED)
    if read == 0:
        print("check-ambient-reads: read no source under src/core -- the scan is broken, not the tree clean")
        return 1
    for problem in problems:
        print(problem)
    if problems:
        print(f"check-ambient-reads: {len(problems)} problem(s) in {read} file(s)")
        return 1
    print(
        f"check-ambient-reads: {read} file(s); every clock and environment read is a seam or excused by name"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
