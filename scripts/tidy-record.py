#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Runs the clang-tidy preset and refuses to report a result it cannot vouch for.

    python scripts/tidy-record.py                    # delete the tree, configure, build, record
    python scripts/tidy-record.py --preset clang-tidy --build-dir out/build/clang-tidy
    python scripts/tidy-record.py --log build.log --exit-code 0   # record a build CI already ran

`.agent/rules/build-and-toolchain.md` states what a clang-tidy result has to carry before a zero
means anything, and it was written after one gate went quiet three different ways in an hour: a
log read at 179 of 526 files, a gate never started, and an analyser absent from PATH printing
`warnings=0` from a tool that never ran. The rule was correct and prevented none of them, because
a principle with no executable step is a sentence people agree with. This is the step.

A RECORD is printed on every run, and the verdict is the conjunction of its fields -- none implies
another, and each fails a different way:

    exit        the build's exit code. Findings are fatal, so this is the verdict proper, and it is
                the one field that passes through no pattern anyone wrote.
    tidy        the analyser the BUILD resolved (read from build.ninja's `--tidy=`, not from PATH),
                and the version it answers, which must equal the pin in `.clang-tidy-version`.
    statements  how many build statements carry `--tidy=`: zero is an analyser wired to nothing.
    steps       the `[k/N]` the build reached. A fresh tree must reach N of N; an incremental one
                is refused unless asked for, because an up-to-date object skips its analysis and a
                zero then means nothing needed recompiling.
    canary      a planted violation, analysed with the same binary and the tree's configuration,
                must be REPORTED: a present, wired, idle tool reports nothing either.

Findings are PRINTED, never counted into the verdict: a count passes through a regex, and a regex
(`warning:` against `-warnings-as-errors`' `error:`) is exactly where this silently zeroed before.

Exit status: 0 when every field vouches for the result and the build passed; 1 otherwise, naming
which field failed.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent

TIDY_STATEMENT = re.compile(r'--tidy="([^";]+)')
STEP = re.compile(r"^\[(\d+)/(\d+)\]", re.M)
FINDING = re.compile(r"^.*: (?:warning|error): .*\[[a-z0-9.,-]+\]$", re.M)
VERSION = re.compile(r"version (\d+\.\d+\.\d+)")
CANARY_CHECK = "readability-identifier-naming"
CANARY_SOURCE = "// SPDX-License-Identifier: Apache-2.0\nint Badly_Named_Global = 0;\n"


@dataclass
class Record:
    """What one tidy build proved, field by field."""

    exit_code: int | None = None
    tidy: str | None = None
    version: str | None = None
    pin: str | None = None
    statements: int = 0
    steps_done: int = 0
    steps_total: int = 0
    log_lines: int = 0
    canary: bool | None = None
    findings: list[str] = field(default_factory=list)

    def refusals(self, incremental: bool) -> list[str]:
        """:return: why this record does not vouch for a clean result; empty when it does."""
        why = []
        if self.exit_code is None:
            why.append("exit: no build exit code was recorded")
        elif self.exit_code != 0:
            why.append(f"exit: the build failed ({self.exit_code})")
        if self.tidy is None:
            why.append(
                "tidy: build.ninja names no analyser -- CORE_CPP_CLANG_TIDY is off, or nothing was configured"
            )
        elif self.version is None:
            why.append(f"tidy: {self.tidy} did not answer --version")
        elif self.pin is not None and self.version != self.pin:
            why.append(f"tidy: {self.tidy} is {self.version}, but .clang-tidy-version pins {self.pin}")
        if self.pin is None:
            why.append("tidy: .clang-tidy-version has no `version:` line to compare against")
        if self.statements == 0:
            why.append("statements: no build statement carries --tidy=, so nothing was analysed")
        if self.steps_total == 0:
            why.append(
                "steps: the log shows no [k/N] step at all -- it is not a build log, or the build never started"
            )
        elif self.steps_done != self.steps_total:
            why.append(f"steps: the build stopped at [{self.steps_done}/{self.steps_total}]")
        elif not incremental and self.steps_total < self.statements:
            why.append(
                f"steps: {self.steps_total} step(s) ran against {self.statements} analysed statement(s) -- an incremental "
                f"build inherits the rest's verdict; delete the tree, or pass --incremental to say you mean it"
            )
        if self.canary is None:
            why.append("canary: the planted violation was not analysed")
        elif not self.canary:
            why.append(
                f"canary: the planted violation was not reported as {CANARY_CHECK} -- the analyser is idle"
            )
        return why

    def render(self) -> str:
        """:return: the record, one field per `key=value`."""
        return (
            f"tidy-record: exit={self.exit_code} tidy={self.tidy} version={self.version} pin={self.pin} "
            f"statements={self.statements} steps={self.steps_done}/{self.steps_total} log-lines={self.log_lines} "
            f"canary={'reported' if self.canary else self.canary} findings-printed={len(self.findings)}"
        )


def read_pin(root: Path) -> str | None:
    """:return: the `version:` of .clang-tidy-version, or None."""
    path = root / ".clang-tidy-version"
    if not path.is_file():
        return None
    match = re.search(r"^version:\s*(\S+)", path.read_text(encoding="utf-8"), re.M)
    return match.group(1) if match else None


def read_ninja(build_ninja: str) -> tuple[str | None, int]:
    """:return: (the analyser the build resolved, how many statements carry it)."""
    tidies = TIDY_STATEMENT.findall(build_ninja)
    return (tidies[0] if tidies else None), len(tidies)


def read_log(log: str, record: Record) -> None:
    """Fills the steps, the line count and the printed findings from a build log."""
    steps = [(int(done), int(total)) for done, total in STEP.findall(log)]
    if steps:
        record.steps_done, record.steps_total = steps[-1]
    record.log_lines = log.count("\n")
    record.findings = FINDING.findall(log)


def ask_version(tidy: str) -> str | None:
    """:return: the version the analyser answers, or None if it does not run."""
    try:
        answer = subprocess.run([tidy, "--version"], capture_output=True, text=True, check=False, timeout=60)
    except OSError:
        return None
    match = VERSION.search(answer.stdout + answer.stderr)
    return match.group(1) if match else None


def run_canary(tidy: str, build_dir: Path) -> bool:
    """Analyses a planted violation with the tree's configuration.

    Written inside the build directory, which is inside the repository, so clang-tidy finds the
    repository's `.clang-tidy` above it -- and nowhere in `src/`, so an interrupted run leaves no
    stray file in the source tree.
    """
    canary = build_dir / "tidy-record-canary.cpp"
    canary.write_text(CANARY_SOURCE, encoding="utf-8")
    try:
        answer = subprocess.run(
            [tidy, str(canary), "--", "-std=c++23"], capture_output=True, text=True, check=False, timeout=120
        )
    finally:
        canary.unlink(missing_ok=True)
    return CANARY_CHECK in answer.stdout + answer.stderr


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--preset", default="clang-tidy")
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--log", type=Path, help="record a build that already ran, from its log")
    parser.add_argument("--exit-code", type=int, help="with --log: that build's exit code")
    parser.add_argument(
        "--incremental", action="store_true", help="accept a build that ran fewer steps than it analyses"
    )
    arguments = parser.parse_args()

    build_dir = arguments.build_dir or REPOSITORY_ROOT / "out" / "build" / arguments.preset
    record = Record(pin=read_pin(REPOSITORY_ROOT))

    if arguments.log is None:
        # DELETE THE TREE: an up-to-date object skips its analysis, so a zero from an old tree means
        # nothing needed recompiling.
        if not arguments.incremental and build_dir.exists():
            shutil.rmtree(build_dir)
        configure = subprocess.run(["cmake", "--preset", arguments.preset], cwd=REPOSITORY_ROOT, check=False)
        if configure.returncode != 0:
            print(f"tidy-record: configure failed ({configure.returncode}); refusing to report a result")
            return 1
        build = subprocess.run(
            ["cmake", "--build", "--preset", arguments.preset, "--", "-k", "0"],
            cwd=REPOSITORY_ROOT,
            capture_output=True,
            text=True,
            errors="replace",
            check=False,
        )
        log = build.stdout + build.stderr
        record.exit_code = build.returncode
    else:
        log = arguments.log.read_text(encoding="utf-8", errors="replace")
        record.exit_code = arguments.exit_code

    ninja = build_dir / "build.ninja"
    if not ninja.is_file():
        print(f"tidy-record: {ninja} does not exist -- no build tree, so no result to report")
        return 1
    record.tidy, record.statements = read_ninja(ninja.read_text(encoding="utf-8", errors="replace"))
    read_log(log, record)
    if record.tidy is not None:
        record.version = ask_version(record.tidy)
        record.canary = run_canary(record.tidy, build_dir)

    for finding in record.findings:
        print(finding)
    print(record.render())
    refusals = record.refusals(arguments.incremental)
    for why in refusals:
        print(f"tidy-record: REFUSED -- {why}")
    return 1 if refusals else 0


if __name__ == "__main__":
    sys.exit(main())
