#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Proves that check-preset-coverage.py refuses every disagreement it claims to.

    python scripts/check-preset-coverage-selftest.py       # quiet unless something fails
    python scripts/check-preset-coverage-selftest.py -v    # one line per case

The checker's whole value is that it fails; a checker that accepts everything reports the same
"0 problems" as a tree with nothing wrong. So each rule it states gets a fixture that violates
exactly that rule and nothing else, and one that violates none.

**Fixtures are written rather than the real tree read.** A self-test that passes only while this
repository happens to be consistent proves nothing about the checker, and would go red for reasons
that have nothing to do with it -- the next lane to add a preset would be told its self-test broke.

Three of the cases are not about the comparison at all but about the INSTRUMENT:

    an empty preset list       a parse that yielded nothing reports no differences, which reads
    no preset referenced       identically to a tree with none. Both are refused outright.
    a preset named in a COMMENT is not coverage -- Ruling R81's hazard, and the dangerous
                               direction, because it INVENTS a leg that does not exist
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
CHECKER = REPOSITORY_ROOT / "scripts" / "check-preset-coverage.py"


def load_checker():
    """Loads check-preset-coverage.py as a module.

    By path, because its name has hyphens and cannot be imported: the same way
    scripts/clang-format.py loads scripts/tool-versions.py.

    :return: the imported module.
    """
    spec = importlib.util.spec_from_file_location("check_preset_coverage", CHECKER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CHECK = load_checker()


def write_tree(root: Path, presets: list[tuple[str, bool]], workflows: dict[str, str]) -> None:
    """Writes a fixture repository.

    :param root: where to write it.
    :param presets: (name, hidden) per configure preset, in order.
    :param workflows: file name to body, under .github/workflows/.
    """
    document = {
        "version": 6,
        "configurePresets": [
            {"name": name, **({"hidden": True} if hidden else {})} for name, hidden in presets
        ],
    }
    (root / "CMakePresets.json").write_text(json.dumps(document, indent=2), encoding="utf-8")
    directory = root / ".github" / "workflows"
    directory.mkdir(parents=True, exist_ok=True)
    for name, body in workflows.items():
        (directory / name).write_text(body, encoding="utf-8")


def job(*presets: str) -> str:
    """A workflow body whose matrix names @p presets, in the list spelling."""
    return "jobs:\n  build:\n    strategy:\n      matrix:\n        preset: [" + ", ".join(presets) + "]\n"


class PresetCoverage(unittest.TestCase):
    """One case per rule the checker states."""

    def setUp(self) -> None:
        self._directory = tempfile.TemporaryDirectory()
        self.root = Path(self._directory.name)
        self.addCleanup(self._directory.cleanup)
        self._allowlist = dict(CHECK.ALLOWLIST)
        self.addCleanup(lambda: CHECK.ALLOWLIST.update(self._allowlist))

    def allow(self, **entries: str) -> None:
        """Replaces the checker's allowlist for one case."""
        CHECK.ALLOWLIST.clear()
        CHECK.ALLOWLIST.update(entries)

    def check(self) -> list[str]:
        """Runs the checker over the fixture."""
        return CHECK.check(self.root)

    # -- the agreeing tree -------------------------------------------------------------------

    def test_every_visible_preset_referenced_is_accepted(self) -> None:
        write_tree(self.root, [("a", False), ("b", False)], {"build.yml": job("a", "b")})
        self.allow()
        self.assertEqual(self.check(), [])

    def test_a_hidden_preset_needs_no_leg(self) -> None:
        # A hidden preset cannot be configured by name, so there is nothing for a job to run.
        write_tree(self.root, [("base", True), ("a", False)], {"build.yml": job("a")})
        self.allow()
        self.assertEqual(self.check(), [])

    # -- direction 1: a preset that runs nowhere ---------------------------------------------

    def test_an_unreferenced_preset_is_refused_by_name(self) -> None:
        write_tree(self.root, [("a", False), ("lonely", False)], {"build.yml": job("a")})
        self.allow()
        failures = self.check()
        self.assertEqual(len(failures), 1)
        self.assertIn("lonely", failures[0])
        self.assertIn("NOWHERE", failures[0])

    def test_an_allowlisted_preset_needs_no_leg(self) -> None:
        write_tree(self.root, [("a", False), ("lonely", False)], {"build.yml": job("a")})
        self.allow(lonely="no runner offers this platform")
        self.assertEqual(self.check(), [])

    # -- direction 2: the allowlist itself rots ----------------------------------------------

    def test_a_stale_exemption_is_refused(self) -> None:
        # The leg was added and nobody removed the excuse.
        write_tree(self.root, [("a", False)], {"build.yml": job("a")})
        self.allow(a="was not run anywhere, once")
        failures = self.check()
        self.assertEqual(len(failures), 1)
        self.assertIn("stale exemption", failures[0])

    def test_an_allowlist_entry_for_an_absent_preset_is_refused(self) -> None:
        write_tree(self.root, [("a", False)], {"build.yml": job("a")})
        self.allow(renamed="this preset no longer exists")
        failures = self.check()
        self.assertEqual(len(failures), 1)
        self.assertIn("renamed", failures[0])

    def test_an_empty_reason_is_refused(self) -> None:
        write_tree(self.root, [("a", False), ("lonely", False)], {"build.yml": job("a")})
        self.allow(lonely="   ")
        failures = self.check()
        self.assertEqual(len(failures), 1)
        self.assertIn("empty reason", failures[0])

    # -- direction 3: a workflow naming a preset that is gone --------------------------------

    def test_a_workflow_naming_an_unknown_preset_is_refused(self) -> None:
        write_tree(self.root, [("a", False)], {"build.yml": job("a", "typo")})
        self.allow()
        failures = self.check()
        self.assertEqual(len(failures), 1)
        self.assertIn("typo", failures[0])
        self.assertIn("does not define", failures[0])

    # -- the spellings a workflow may use ----------------------------------------------------

    def test_the_command_line_spelling_counts_as_coverage(self) -> None:
        body = "jobs:\n  tidy:\n    steps:\n      - run: cmake --preset a\n"
        write_tree(self.root, [("a", False)], {"build.yml": body})
        self.allow()
        self.assertEqual(self.check(), [])

    def test_the_matrix_include_spelling_counts_as_coverage(self) -> None:
        body = (
            "jobs:\n  macos:\n    strategy:\n      matrix:\n        include:\n"
            "          - { name: x, os: macos-15, preset: a, extra: true }\n"
        )
        write_tree(self.root, [("a", False)], {"build.yml": body})
        self.allow()
        self.assertEqual(self.check(), [])

    # -- the instrument itself ---------------------------------------------------------------

    def test_a_preset_named_only_in_a_comment_is_not_coverage(self) -> None:
        # Ruling R81's hazard, and the direction that matters: a comment must never INVENT a leg.
        body = "jobs:\n  build:\n    steps:\n      # preset: a is what this used to run\n      - run: true\n"
        write_tree(self.root, [("a", False)], {"build.yml": body})
        self.allow()
        failures = self.check()
        self.assertEqual(len(failures), 1)
        self.assertIn("no workflow", failures[0])

    def test_no_visible_preset_is_refused_rather_than_passed(self) -> None:
        write_tree(self.root, [("base", True)], {"build.yml": job("base")})
        self.allow()
        failures = self.check()
        self.assertTrue(any("no visible configurePresets" in f for f in failures), failures)

    def test_no_preset_referenced_anywhere_is_refused_rather_than_passed(self) -> None:
        write_tree(self.root, [("a", False)], {"build.yml": "jobs:\n  build:\n    steps: []\n"})
        self.allow()
        failures = self.check()
        self.assertTrue(any("no workflow named any preset" in f for f in failures), failures)

    # -- the exit-status contract, through the real command line -----------------------------

    def test_exit_status_is_zero_when_the_tree_agrees(self) -> None:
        write_tree(self.root, [("a", False)], {"build.yml": job("a")})
        finished = subprocess.run(
            [sys.executable, str(CHECKER), "--root", str(self.root)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(finished.returncode, 0, finished.stderr)

    def test_exit_status_is_one_when_it_does_not(self) -> None:
        write_tree(self.root, [("a", False), ("lonely", False)], {"build.yml": job("a")})
        finished = subprocess.run(
            [sys.executable, str(CHECKER), "--root", str(self.root)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(finished.returncode, 1)
        self.assertIn("lonely", finished.stderr)


def main() -> int:
    """Entry point.

    :return: 0 when every case passed, 1 otherwise.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-v", "--verbose", action="store_true", help="one line per case")
    arguments = parser.parse_args()

    suite = unittest.TestLoader().loadTestsFromTestCase(PresetCoverage)
    result = unittest.TextTestRunner(verbosity=2 if arguments.verbose else 1).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
