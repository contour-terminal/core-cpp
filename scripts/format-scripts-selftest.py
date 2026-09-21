#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Proves that clang-format.py and python-style.py refuse a path that is not theirs to format.

Both scripts filtered by extension only inside `sources()`, which serves `--all`, so a path a
caller NAMED went to the tool unfiltered. clang-format parses its input as C++ whatever the name
is, so `clang-format.py x.cmake` rewrote a CMake file as C++ and printed "1 file(s) formatted" --
and `--check` was not blind but INVERTED: it failed the pristine file and passed the mangled one,
driving a caller toward the damage and then certifying it.

Every gate in this tree has a self-test that proves it refuses. These two did not, which is why the
hole survived until a lane named a `.cmake` and lost it. That is what this file is.

Two properties, both necessary:

  * a path with the wrong extension is REFUSED BY NAME, in the rewriting mode as well as `--check`.
    Refused, not skipped: a silent skip leaves a caller believing a file they named was formatted
    when nothing touched it.
  * a path with the RIGHT extension is not refused, which is what stops this being a guard that
    refuses everything.

The second property is asserted on the refusal MESSAGE rather than the exit status, because a
machine with no clang-format installed exits 2 from the version check -- the same status as a
refusal, for an entirely different reason. Asserting the status would make this pass on a machine
where the guard had been deleted.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
REFUSAL = {
    "clang-format.py": "not C++ sources",
    "python-style.py": "not Python sources",
}


def run(script: str, arguments: list[str]) -> tuple[int, str]:
    """Runs scripts/<script> with @p arguments, returning its exit status and combined output."""
    completed = subprocess.run(
        [sys.executable, str(REPOSITORY_ROOT / "scripts" / script), *arguments],
        capture_output=True,
        text=True,
        check=False,
        cwd=REPOSITORY_ROOT,
    )
    return completed.returncode, completed.stdout + completed.stderr


def main() -> int:
    failures: list[str] = []
    checks = 0

    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        # One file per shape a caller plausibly names. `Makefile` has no suffix at all, which is the
        # case a naive `suffix not in EXTENSIONS` test gets right only by accident.
        names = [
            "a.cpp",
            "a.hpp",
            "a.h",
            "a.ipp",
            "a.inl",
            "a.py",
            "a.cmake",
            "a.md",
            "a.txt",
            "CMakeLists.txt",
            "Makefile",
        ]
        for name in names:
            (work / name).write_text("# placeholder\n", encoding="utf-8")

        expectations = [
            # script, extensions it must accept, everything else it must refuse
            ("clang-format.py", {"a.cpp", "a.hpp", "a.h", "a.ipp", "a.inl"}),
            ("python-style.py", {"a.py"}),
        ]

        for script, accepted in expectations:
            marker = REFUSAL[script]
            for name in names:
                path = str(work / name)
                # The rewriting mode for what must be refused -- the guard has to fire before
                # anything is written, and that is the mode that did the damage. `--check` for what
                # must be accepted, so this file never rewrites anything anywhere.
                status, output = run(script, [path] if name not in accepted else ["--check", path])
                checks += 1
                if name in accepted:
                    if marker in output:
                        failures.append(f"{script}: refused {name}, which it must accept")
                    continue
                if status != 2:
                    failures.append(f"{script}: {name} exited {status}, expected 2 (refusal)")
                elif marker not in output:
                    failures.append(f"{script}: refused {name} without saying why: {output!r}")
                elif name not in output:
                    failures.append(f"{script}: refused {name} without naming it: {output!r}")

            # The case that found this: one file the tool owns, one it does not, in one invocation.
            # The whole invocation must be refused and only the offender named -- a caller who is
            # told "1 file formatted" here concludes both were.
            good = sorted(accepted)[0]
            status, output = run(script, [str(work / good), str(work / "a.md")])
            checks += 1
            if status != 2 or "a.md" not in output:
                failures.append(f"{script}: a mixed batch was not refused: {status}, {output!r}")
            elif good in output:
                failures.append(f"{script}: a mixed batch named {good}, which is not the offender")

            # --check carries the identical hole and is the mode CI runs.
            status, output = run(script, ["--check", str(work / "a.md")])
            checks += 1
            if status != 2 or marker not in output:
                failures.append(f"{script}: --check did not refuse a.md: {status}, {output!r}")

    # A self-test that silently stopped exercising cases would still print a pass, so the count is
    # held against the table that produces it: per script, one check per shape plus a mixed batch
    # and a --check case. Derived from `names`, not written as a literal, so adding a shape moves
    # both sides together and neither drifts.
    expected = len(expectations) * (len(names) + 2)
    if checks != expected:
        print(
            f"format-scripts-selftest: ran {checks} checks, expected {expected} -- the case table "
            "and this count have parted company; fix whichever is wrong rather than the number.",
            file=sys.stderr,
        )
        return 1

    if failures:
        print("format-scripts-selftest:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print(f"format-scripts-selftest: {checks} checks; both scripts refuse what is not theirs, by name")
    return 0


if __name__ == "__main__":
    sys.exit(main())
