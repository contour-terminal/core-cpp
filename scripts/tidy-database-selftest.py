#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Proves that tidy-database.py refuses every result it cannot vouch for, and selects what it says.

    python scripts/tidy-database-selftest.py       # quiet unless something fails
    python scripts/tidy-database-selftest.py -v    # one line per case

The analysis itself needs a clang-cl build and is what CI's `windows (clang-tidy)` job runs; what
is proven here is the part that decides what a zero means: which sources a database contributes,
how the `windows/` ones are counted, and that each field of the verdict refuses on its own.
"""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path, PurePosixPath

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent


def load_script():
    """Loads tidy-database.py by path, since its name has hyphens.

    :return: the imported module.
    """
    spec = importlib.util.spec_from_file_location(
        "tidy_database", REPOSITORY_ROOT / "scripts" / "tidy-database.py"
    )
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


TD = load_script()


def sound() -> object:
    """:return: a verdict every field of which holds."""
    return TD.Verdict(
        version="22.1.8", pin="22.1.8", canary=True, windows=3, expected_windows=3, analysed=10, failed=[]
    )


class SelectionTest(unittest.TestCase):
    def test_only_core_sources_are_selected_once_each(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "out" / "build"
            entries = [
                {"directory": str(build), "file": str(root / "src" / "core" / "A.cpp")},
                # A runtime twin compiles the same source a second time.
                {"directory": str(build), "file": str(root / "src" / "core" / "A.cpp")},
                {"directory": str(build), "file": "../../src/core/net/windows/B.cpp"},
                {"directory": str(build), "file": str(root / ".cache" / "cpm" / "catch2" / "x.cpp")},
                {"directory": str(build), "file": str(build / "header-self-check" / "y.cpp")},
                {"directory": str(build), "file": str(root / "tests" / "Canary.cpp")},
            ]
            selected = TD.select_sources(entries, root)
        self.assertEqual(
            selected, [PurePosixPath("src/core/A.cpp"), PurePosixPath("src/core/net/windows/B.cpp")]
        )
        self.assertEqual(TD.windows_sources(selected), 1)

    def test_a_file_named_windows_is_not_a_windows_directory(self):
        self.assertEqual(TD.windows_sources([PurePosixPath("src/core/tui/windows.cpp")]), 0)


class VerdictTest(unittest.TestCase):
    def assert_refused(self, verdict, field):
        problems = verdict.problems()
        self.assertTrue(any(problem.startswith(f"{field}:") for problem in problems), problems)

    def test_a_sound_verdict_holds(self):
        self.assertEqual(sound().problems(), [])

    def test_another_version_is_refused(self):
        verdict = sound()
        verdict.version = "21.1.0"
        self.assert_refused(verdict, "version")

    def test_a_silent_canary_is_refused(self):
        verdict = sound()
        verdict.canary = False
        self.assert_refused(verdict, "canary")

    def test_fewer_windows_sources_than_git_tracks_are_refused(self):
        verdict = sound()
        verdict.windows = 0
        self.assert_refused(verdict, "windows")

    def test_no_tracked_windows_source_is_refused_not_matched(self):
        verdict = sound()
        verdict.windows = 0
        verdict.expected_windows = 0
        self.assert_refused(verdict, "windows")

    def test_nothing_analysed_is_refused(self):
        verdict = sound()
        verdict.analysed = 0
        self.assert_refused(verdict, "analysed")

    def test_a_finding_is_refused(self):
        verdict = sound()
        verdict.failed = ["src/core/A.cpp"]
        self.assert_refused(verdict, "findings")


if __name__ == "__main__":
    unittest.main()
