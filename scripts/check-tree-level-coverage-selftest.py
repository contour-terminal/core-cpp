#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Proves that check-tree-level-coverage.py refuses both ways the pairing can break.

    python scripts/check-tree-level-coverage-selftest.py       # quiet unless something fails
    python scripts/check-tree-level-coverage-selftest.py -v    # one line per case

The checker exists because R96's arrangement is held together by two hand-maintained lists, and a
disagreement between them makes a check VANISH rather than fail. So the cases that matter are the
two disagreements, and they are not symmetric:

    a labelled test with no style step   -> the check runs nowhere; caught loudly
    a style step naming no labelled test -> the step covers nothing while looking like it does

The second is the one that rots quietly, because nothing about it looks wrong: the job still has a
step, the step still runs a script, and only the CLAIM it makes is stale. A checker that refused
only the first would let a rename silently uncover a check.

Fixtures are written rather than the real tree read: a self-test that passes only while the tree
happens to be consistent proves nothing about the checker, and would start failing for reasons that
have nothing to do with it.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
CHECKER = REPOSITORY_ROOT / "scripts" / "check-tree-level-coverage.py"


def tests_cmake(*labelled: str, unlabelled: tuple[str, ...] = ()) -> str:
    """A tests/CMakeLists.txt naming @p labelled as tree-level and @p unlabelled as not."""
    text = "# SPDX-License-Identifier: Apache-2.0\n"
    for name in labelled:
        text += (
            f"add_test(NAME {name} COMMAND true)\n"
            f'set_tests_properties({name} PROPERTIES LABELS "core-cpp;hygiene;tree-level"'
            f" TIMEOUT 60)\n"
        )
    for name in unlabelled:
        text += (
            f"add_test(NAME {name} COMMAND true)\n"
            f'set_tests_properties({name} PROPERTIES LABELS "core-cpp;hygiene" TIMEOUT 60)\n'
        )
    return text


def workflow(*covered: str) -> str:
    """A build.yml whose style job covers @p covered, followed by another job."""
    steps = ""
    for name in covered:
        steps += f"      # covers: {name}\n      - name: step for {name}\n        run: true\n\n"
    return (
        "jobs:\n  style:\n    runs-on: ubuntu-24.04\n    steps:\n"
        + steps
        + "  linux:\n    runs-on: ubuntu-24.04\n"
    )


class CoverageCase(unittest.TestCase):
    def setUp(self) -> None:
        self._scratch = tempfile.TemporaryDirectory(prefix="core-cpp-coverage-selftest-")
        self.scratch = Path(self._scratch.name)
        self.addCleanup(self._scratch.cleanup)

    def run_checker(self, tests: str, build_yml: str) -> subprocess.CompletedProcess[str]:
        tests_path = self.scratch / "CMakeLists.txt"
        workflow_path = self.scratch / "build.yml"
        tests_path.write_text(tests, encoding="utf-8")
        workflow_path.write_text(build_yml, encoding="utf-8")
        return subprocess.run(
            [sys.executable, str(CHECKER), "--tests", str(tests_path), "--workflow", str(workflow_path)],
            capture_output=True,
            text=True,
            check=False,
        )


class TestAgreement(CoverageCase):
    def test_every_labelled_check_covered_passes(self) -> None:
        result = self.run_checker(
            tests_cmake("core-cpp.alpha", "core-cpp.beta", unlabelled=("core-cpp.exit-codes",)),
            workflow("core-cpp.alpha", "core-cpp.beta"),
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("2 tree-level check(s)", result.stdout)

    def test_an_unlabelled_check_needs_no_step(self) -> None:
        """A per-leg check runs in all 24 jobs, so the style job must NOT have to cover it."""
        result = self.run_checker(
            tests_cmake("core-cpp.alpha", unlabelled=("core-cpp.exit-codes", "core-cpp.async-link-smoke")),
            workflow("core-cpp.alpha"),
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class TestMarkerSyntax(CoverageCase):
    def test_prose_mentioning_the_marker_is_not_a_marker(self) -> None:
        """A comment ABOUT the syntax is not an instance of it.

        This is not hypothetical: the step documenting these markers says "carries a `# covers:`
        marker", and an unanchored pattern read that as a marker covering a backtick -- the checker
        refused a tree that was entirely correct. A gate that fires on a true negative is worse
        than no gate (Ruling R81), so the marker must be the whole line.
        """
        prose = (
            "jobs:\n  style:\n    runs-on: ubuntu-24.04\n    steps:\n"
            "      # every step here carries a `# covers:` marker naming what it stands for\n"
            "      # covers: core-cpp.alpha\n"
            "      - name: step for alpha\n        run: true\n\n"
            "  linux:\n    runs-on: ubuntu-24.04\n"
        )

        result = self.run_checker(tests_cmake("core-cpp.alpha"), prose)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("1 tree-level check(s)", result.stdout)


class TestLabelledButUncovered(CoverageCase):
    def test_a_labelled_check_with_no_style_step_is_refused(self) -> None:
        result = self.run_checker(
            tests_cmake("core-cpp.alpha", "core-cpp.beta"),
            workflow("core-cpp.alpha"),
        )

        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("NO STYLE STEP", result.stdout)
        self.assertIn("core-cpp.beta", result.stdout)
        self.assertIn("runs NOWHERE in CI", result.stdout)


class TestCoveredButUnlabelled(CoverageCase):
    def test_a_step_covering_no_labelled_check_is_refused(self) -> None:
        """The rename case: the step still exists and still runs, and its claim is stale."""
        result = self.run_checker(
            tests_cmake("core-cpp.alpha"),
            workflow("core-cpp.alpha", "core-cpp.renamed-away"),
        )

        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("COVERS NOTHING", result.stdout)
        self.assertIn("core-cpp.renamed-away", result.stdout)

    def test_a_check_that_lost_the_label_is_refused(self) -> None:
        """Same shape, reached the other way: the name survives, the label does not."""
        result = self.run_checker(
            tests_cmake("core-cpp.alpha", unlabelled=("core-cpp.beta",)),
            workflow("core-cpp.alpha", "core-cpp.beta"),
        )

        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("COVERS NOTHING", result.stdout)
        self.assertIn("core-cpp.beta", result.stdout)


class TestParseControl(CoverageCase):
    def test_a_truncating_parse_fails_loudly_rather_than_dropping_a_name(self) -> None:
        """A parenthesis in a property value truncates the block and would drop every name after it.

        Silently, which is the failure this whole file exists to prevent -- so the checker counts
        the label sites the file writes and refuses to disagree with itself.
        """
        text = (
            "add_test(NAME core-cpp.alpha COMMAND true)\n"
            'set_tests_properties(core-cpp.alpha PROPERTIES SKIP_REGULAR_EXPRESSION "a(b)c"'
            ' LABELS "core-cpp;hygiene;tree-level")\n'
        )

        result = self.run_checker(text, workflow("core-cpp.alpha"))

        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("truncated", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=1)
