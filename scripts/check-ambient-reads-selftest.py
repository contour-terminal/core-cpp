#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Proves that check-ambient-reads.py refuses a direct read and excuses nothing it should not.

    python scripts/check-ambient-reads-selftest.py       # quiet unless something fails
    python scripts/check-ambient-reads-selftest.py -v    # one line per case

A case per spelling it refuses, a case per thing that is NOT a read -- a comment about the rule, a
longer identifier that ends in `getenv`, a test file -- because a scan that fires on its own
documentation is a gate that fails on a true negative, and one case per way the allow list can go
wrong.
"""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

CHECKER = Path(__file__).resolve().parent / "check-ambient-reads.py"


def load_checker():
    """:return: check-ambient-reads.py, loaded by path because its name has hyphens."""
    spec = importlib.util.spec_from_file_location("check_ambient_reads", CHECKER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CHECK = load_checker()


class AmbientReadsTest(unittest.TestCase):
    def run_on(self, files: dict[str, str], allowed: dict[str, str] | None = None) -> tuple[list[str], int]:
        """Writes @p files under a scratch root and scans it."""
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for path, text in files.items():
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                (root / path).write_text(text, encoding="utf-8")
            return CHECK.scan(root, allowed or {})

    def test_a_steady_clock_read_is_refused_by_line(self):
        problems, _ = self.run_on(
            {"src/core/net/A.cpp": "int a;\nauto t = std::chrono::steady_clock::now();\n"}
        )
        self.assertEqual(len(problems), 1)
        self.assertIn("src/core/net/A.cpp:2: [clock]", problems[0])

    def test_a_wall_clock_read_is_refused(self):
        problems, _ = self.run_on({"src/core/B.hpp": "auto t = std::chrono::system_clock::now();\n"})
        self.assertEqual(len(problems), 1)

    def test_an_environment_read_is_refused(self):
        problems, _ = self.run_on({"src/core/cli/C.cpp": 'auto* home = std::getenv("HOME");\n'})
        self.assertEqual(len(problems), 1)
        self.assertIn("[environment]", problems[0])

    def test_what_is_not_a_read_is_not_refused(self):
        files = {
            "src/core/D.hpp": "/// Take an IClock rather than calling steady_clock::now() yourself.\n"
            '// never getenv() here\n/* nor std::getenv("X") */\nint mygetenv(int);\n',
            "src/core/E_test.cpp": "auto t = std::chrono::steady_clock::now();\n",
        }
        problems, read = self.run_on(files)
        self.assertEqual(problems, [])
        self.assertEqual(read, 1)

    def test_an_allowed_seam_is_excused_and_a_stale_row_is_not(self):
        files = {
            "src/core/platform/Clock.hpp": "auto now() { return std::chrono::steady_clock::now(); }\n",
            "src/core/F.cpp": "int f;\n",
        }
        problems, _ = self.run_on(
            files, {"src/core/platform/Clock.hpp": "the seam", "src/core/F.cpp": "nothing"}
        )
        self.assertEqual(len(problems), 1)
        self.assertIn("src/core/F.cpp: [stale-allow]", problems[0])

    def test_the_real_tree_is_read(self):
        _, read = CHECK.scan(CHECKER.parent.parent, CHECK.ALLOWED)
        self.assertGreater(read, 100)


if __name__ == "__main__":
    verbosity = 2 if "-v" in sys.argv else 1
    result = unittest.TextTestRunner(verbosity=verbosity).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(AmbientReadsTest)
    )
    sys.exit(0 if result.wasSuccessful() else 1)
