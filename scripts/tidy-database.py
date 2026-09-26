#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Runs the pinned clang-tidy over a clang-cl build's compile database, and vouches for the result.

    python scripts/tidy-database.py --build-dir out/build/clangcl-release --tidy <clang-tidy.exe>

**Why a compile database and not the `clang-tidy` preset's `CXX_CLANG_TIDY`.** That preset hard-codes
a Unix toolchain, so every `windows/` source and every `_WIN32` arm of a shared header went
unanalysed while the `clang-tidy` job was green (core-cpp#38, core-cpp#44). And `CXX_CLANG_TIDY`
does not work over clang-cl: measured with the pinned 22.1.8, CMake's co-compile hands clang-tidy
the clang-cl command in a form it reads with exceptions disabled, so 634 of the findings were
`cannot use 'throw' with exceptions disabled` and the real ones were buried under them. Read
through `-p <build dir>`, the same command is understood as the clang-cl command it is.

It analyses every `src/core/**.cpp` the database compiles, once each, and the verdict is the
conjunction of what `tidy-record.py` insists on for the Linux job, adapted:

    version     the analyser answers the pin in `.clang-tidy-version`
    canary      a planted violation, analysed with that binary, is REPORTED
    windows     exactly as many analysed sources are under a `windows/` directory as git tracks
                there: a leg that analysed none of them is the defect this leg exists for
    findings    every analysis exited 0 -- findings are fatal (`WarningsAsErrors: '*'`)

Exit status: 0 when every field holds; 1 otherwise, naming which.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import importlib.util
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent


def load_tidy_record():
    """Loads tidy-record.py by path, for the pin, the version question and the canary.

    :return: the imported module.
    """
    spec = importlib.util.spec_from_file_location(
        "tidy_record", REPOSITORY_ROOT / "scripts" / "tidy-record.py"
    )
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


TIDY_RECORD = load_tidy_record()


def select_sources(entries: list[dict], root: Path) -> list[PurePosixPath]:
    """The core-cpp sources a compile database compiles, once each, relative to @p root.

    :param entries: the database's entries.
    :param root: the repository root.
    :return: every `src/core/**.cpp`, sorted; a source two targets compile is listed once.
    """
    selected = set()
    for entry in entries:
        path = Path(entry["file"])
        if not path.is_absolute():
            path = Path(entry["directory"]) / path
        # relpath, which compares with the platform's case rules: a Windows database may spell
        # the drive or a directory in another case than the checkout does.
        try:
            relative = PurePosixPath(Path(os.path.relpath(path.resolve(), Path(root).resolve())).as_posix())
        except ValueError:  # another drive: not the checkout's
            continue
        if relative.parts[:2] == ("src", "core") and relative.suffix == ".cpp":
            selected.add(relative)
    return sorted(selected)


def windows_sources(sources: list[PurePosixPath]) -> int:
    """:return: how many of @p sources are under a `windows/` directory."""
    return sum(1 for source in sources if "windows" in source.parts[:-1])


def tracked_windows_sources(root: Path) -> int:
    """:return: how many `src/core/**/windows/*.cpp` files git tracks."""
    listing = subprocess.run(
        ["git", "ls-files", "src/core/**/windows/*.cpp"], cwd=root, capture_output=True, text=True, check=True
    )
    return len([line for line in listing.stdout.splitlines() if line])


@dataclass
class Verdict:
    """What one run proved, field by field."""

    version: str | None
    pin: str | None
    canary: bool
    windows: int
    expected_windows: int
    analysed: int
    failed: list[str]

    def problems(self) -> list[str]:
        """:return: one line per field that does not hold."""
        why = []
        if self.version is None or self.version != self.pin:
            why.append(f"version: the analyser answers {self.version}, the pin is {self.pin}")
        if not self.canary:
            why.append(f"canary: the planted violation was not reported as {TIDY_RECORD.CANARY_CHECK}")
        if self.expected_windows == 0 or self.windows != self.expected_windows:
            why.append(
                f"windows: {self.windows} analysed source(s) under windows/, where git tracks {self.expected_windows}"
            )
        if self.analysed == 0:
            why.append("analysed: the database compiles no src/core source")
        if self.failed:
            why.append(f"findings: {len(self.failed)} source(s) did not analyse clean")
        return why


def analyse(tidy: str, build_dir: Path, root: Path, source: PurePosixPath) -> tuple[PurePosixPath, int, str]:
    """Runs @p tidy over one source, through the compile database in @p build_dir."""
    answer = subprocess.run(
        [tidy, "-p", str(build_dir), "--quiet", str(root / source)],
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    return source, answer.returncode, answer.stdout + answer.stderr


def main(argv: list[str] | None = None) -> int:
    """Runs the analysis. :return: the exit status."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--build-dir", type=Path, required=True, help="a configured tree with compile_commands.json"
    )
    parser.add_argument("--tidy", required=True, help="the clang-tidy binary to run")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    arguments = parser.parse_args(argv)

    database = arguments.build_dir / "compile_commands.json"
    if not database.is_file():
        print(f"tidy-database: {database} does not exist; configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON")
        return 1
    sources = select_sources(json.loads(database.read_text(encoding="utf-8")), REPOSITORY_ROOT)

    verdict = Verdict(
        version=TIDY_RECORD.ask_version(arguments.tidy),
        pin=TIDY_RECORD.read_pin(REPOSITORY_ROOT),
        canary=TIDY_RECORD.run_canary(arguments.tidy, arguments.build_dir),
        windows=windows_sources(sources),
        expected_windows=tracked_windows_sources(REPOSITORY_ROOT),
        analysed=len(sources),
        failed=[],
    )
    with concurrent.futures.ThreadPoolExecutor(max_workers=arguments.jobs) as pool:
        jobs = [
            pool.submit(analyse, arguments.tidy, arguments.build_dir, REPOSITORY_ROOT, s) for s in sources
        ]
        for job in concurrent.futures.as_completed(jobs):
            source, status, output = job.result()
            if status != 0:
                verdict.failed.append(source.as_posix())
                print(output, end="" if output.endswith("\n") else "\n")

    print(
        f"tidy-database: analysed={verdict.analysed} windows={verdict.windows}/{verdict.expected_windows} "
        f"version={verdict.version} pin={verdict.pin} canary={'reported' if verdict.canary else 'SILENT'} "
        f"failed={len(verdict.failed)}"
    )
    problems = verdict.problems()
    for problem in problems:
        print(f"  refused: {problem}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
