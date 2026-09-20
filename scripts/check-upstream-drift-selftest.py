#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Proves that check-upstream-drift.py reports what it exists to report.

    python scripts/check-upstream-drift-selftest.py          # quiet unless something fails
    python scripts/check-upstream-drift-selftest.py -v       # one line per case

Each case builds a throwaway git repository with real commits and a provenance table of its own,
runs the checker against them, and requires the answer AND the exit status. Real repositories
rather than a fake `git`, because the whole checker is a wrapper around `git log`, `git cat-file`
and `git merge-base`: a stub would prove only that the stub agrees with itself.

The cases are the four the checker must tell apart, which is the whole of its contract:

    a row whose upstream did not move      -> reported clean, exit 0
    a row whose upstream moved             -> reported as drift with the commit, exit 0
    a row whose upstream file was deleted  -> drift, marked DELETED, exit 0 (news, not a defect)
    a malformed row                        -> reported as malformed, exit 1

and the two the exit status turns on: drift alone never reddens, and a missing checkout skips (77)
rather than passing, so a nightly that cannot reach an upstream does not read as "no drift".

Python rather than a `-P` CMake script, though `check-cmake-hygiene-selftest.cmake` is the shape
for the checkers written IN CMake: this one is Python, `tools/migrate/*_test.py` is the precedent
for testing Python here, and building git history from CMake script would be several dozen
`execute_process` calls doing what `subprocess` does in one line each.
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
CHECKER = REPOSITORY_ROOT / "scripts" / "check-upstream-drift.py"

# ctest reads this as a skip (CORE_CPP_SKIP_EXIT_CODE in cmake/CoreCppTargets.cmake).
SKIP_EXIT_CODE = 77

TABLE_HEADER = (
    "| core-cpp path | upstream repo | upstream path | synced SHA | notes |\n|---|---|---|---|---|\n"
)


def run_git(repository: Path, *arguments: str) -> str:
    """Runs git in @p repository and returns its stdout, raising if it failed."""
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments], capture_output=True, text=True, check=True
    )
    return result.stdout.strip()


class UpstreamFixture:
    """A throwaway upstream repository, plus the `origin/master` the checker compares against.

    The checker only ever reads `origin/master`, so the fixture creates the remote-tracking ref
    directly rather than building a second clone: the shape git sees is identical and the fixture
    stays one directory.
    """

    def __init__(self, directory: Path) -> None:
        self.path = directory
        self.path.mkdir(parents=True, exist_ok=True)
        run_git(self.path, "init", "--quiet", "--initial-branch=master")
        run_git(self.path, "config", "user.email", "selftest@example.invalid")
        run_git(self.path, "config", "user.name", "Selftest")

    def commit(self, message: str, **files: str | None) -> str:
        """Writes or deletes files and commits them. A value of None deletes the file."""
        for name, content in files.items():
            target = self.path / name.replace("__", "/")
            if content is None:
                target.unlink()
                run_git(self.path, "rm", "--quiet", "--", str(target.relative_to(self.path)))
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")
            run_git(self.path, "add", "--", str(target.relative_to(self.path)))
        run_git(self.path, "commit", "--quiet", "-m", message)
        return run_git(self.path, "rev-parse", "HEAD")

    def publish(self) -> None:
        """Points origin/master at the current HEAD, as a fetch from a real origin would."""
        run_git(self.path, "update-ref", "refs/remotes/origin/master", "HEAD")


class CheckerCase(unittest.TestCase):
    """Base fixture: a scratch directory, an upstream, and a provenance table to point at it."""

    def setUp(self) -> None:
        self._scratch = tempfile.TemporaryDirectory(prefix="core-cpp-drift-selftest-")
        self.scratch = Path(self._scratch.name)
        self.upstream = UpstreamFixture(self.scratch / "upstream")
        self.addCleanup(self._scratch.cleanup)

    def write_table(self, rows: str) -> Path:
        """Writes a provenance table containing @p rows and returns its path."""
        table = self.scratch / "provenance.md"
        table.write_text(TABLE_HEADER + rows, encoding="utf-8")
        return table

    def run_checker(self, table: Path, *extra: str) -> subprocess.CompletedProcess[str]:
        """Runs the checker over @p table with the fixture upstream, never fetching."""
        return subprocess.run(
            [
                sys.executable,
                str(CHECKER),
                "--provenance",
                str(table),
                "--checkout",
                f"selftest/upstream={self.upstream.path}",
                "--no-fetch",
                *extra,
            ],
            capture_output=True,
            text=True,
            check=False,
            cwd=str(REPOSITORY_ROOT),
        )

    def row(self, core_path: str, upstream_path: str, sha: str, notes: str = "-") -> str:
        """One provenance row pointing at the fixture upstream."""
        return f"| `{core_path}` | selftest/upstream | `{upstream_path}` | `{sha}` | {notes} |\n"


class TestCleanRow(CheckerCase):
    def test_a_row_whose_upstream_did_not_move_is_clean(self) -> None:
        sha = self.upstream.commit("add", Widget_hpp="one\n")
        self.upstream.publish()
        # README.md stands in for the core-cpp file: the checker requires the core-cpp path to
        # exist, and any tracked file in this repository proves that branch without inventing one.
        table = self.write_table(self.row("README.md", "Widget_hpp", sha))

        result = self.run_checker(table)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("1 up to date, 0 drifted", result.stdout)
        self.assertNotIn("DRIFT", result.stdout)


class TestDriftedRow(CheckerCase):
    def test_a_row_whose_upstream_moved_is_drift_and_not_a_failure(self) -> None:
        synced = self.upstream.commit("add", Widget_hpp="one\n")
        self.upstream.commit("change the widget", Widget_hpp="two\n")
        self.upstream.publish()
        table = self.write_table(self.row("README.md", "Widget_hpp", synced))

        result = self.run_checker(table)

        # Exit 0 is the point of the case, not a detail of it: a nightly that reddens on drift is a
        # nightly somebody mutes, and then the next drift goes unseen.
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("DRIFT", result.stdout)
        self.assertIn("change the widget", result.stdout)
        self.assertIn("0 up to date, 1 drifted", result.stdout)

    def test_only_commits_touching_the_row_count_as_its_drift(self) -> None:
        synced = self.upstream.commit("add both", Widget_hpp="one\n", Other_hpp="other\n")
        self.upstream.commit("touch only the other file", Other_hpp="changed\n")
        self.upstream.publish()
        table = self.write_table(self.row("README.md", "Widget_hpp", synced))

        result = self.run_checker(table)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("1 up to date, 0 drifted", result.stdout)


class TestDeletedUpstream(CheckerCase):
    def test_a_file_deleted_upstream_is_drift_marked_deleted(self) -> None:
        synced = self.upstream.commit("add", Widget_hpp="one\n", Keep_hpp="keep\n")
        self.upstream.commit("delete the widget", Widget_hpp=None)
        self.upstream.publish()
        table = self.write_table(self.row("README.md", "Widget_hpp", synced))

        result = self.run_checker(table)

        # Drift, not malformedness: the row was true when it was written, and "it is gone now" is
        # the news this exists to carry.
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("DELETED upstream", result.stdout)
        self.assertIn("delete the widget", result.stdout)


class TestMalformedRows(CheckerCase):
    def test_a_path_absent_at_its_own_synced_sha_is_malformed(self) -> None:
        sha = self.upstream.commit("add", Widget_hpp="one\n")
        self.upstream.publish()
        table = self.write_table(self.row("README.md", "NeverExisted_hpp", sha))

        result = self.run_checker(table)

        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("MALFORMED", result.stdout)
        self.assertIn("does not exist at its own synced SHA", result.stdout)

    def test_a_sha_that_is_not_an_ancestor_is_malformed(self) -> None:
        self.upstream.commit("add", Widget_hpp="one\n")
        self.upstream.publish()
        # A commit made after origin/master was published is not an ancestor of it.
        orphan = self.upstream.commit("not published", Widget_hpp="two\n")
        table = self.write_table(self.row("README.md", "Widget_hpp", orphan))

        result = self.run_checker(table)

        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("is not an ancestor", result.stdout)

    def test_a_sha_the_checkout_does_not_have_is_malformed(self) -> None:
        self.upstream.commit("add", Widget_hpp="one\n")
        self.upstream.publish()
        table = self.write_table(self.row("README.md", "Widget_hpp", "0" * 40))

        result = self.run_checker(table)

        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("is not a commit in this checkout", result.stdout)

    def test_a_core_cpp_path_that_no_longer_exists_is_stale_not_a_failure(self) -> None:
        sha = self.upstream.commit("add", Widget_hpp="one\n")
        self.upstream.publish()
        table = self.write_table(self.row("src/core/GoneFromThisTree.hpp", "Widget_hpp", sha))

        result = self.run_checker(table)

        # check-cmake-hygiene's `provenance` rule already refuses this, in almost these words. Two
        # gates reddening for one defect gets both of them ignored, and it would put this one in
        # the red for any module mid-rewrite -- the state it is least useful in.
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("STALE ROW", result.stdout)
        self.assertIn("check-cmake-hygiene owns this", result.stdout)
        self.assertIn("0 row(s) checked", result.stdout)

    def test_a_brace_pattern_is_malformed_and_says_what_to_do(self) -> None:
        sha = self.upstream.commit("add", Widget_hpp="one\n")
        self.upstream.publish()
        table = self.write_table(self.row("README.md", "Widget.{hpp,cpp}", sha))

        result = self.run_checker(table)

        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("is a pattern, not a file", result.stdout)
        self.assertIn("primary upstream", result.stdout)

    def test_an_unparseable_line_is_malformed(self) -> None:
        table = self.write_table("| `README.md` | selftest/upstream |\n")

        result = self.run_checker(table)

        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("expected 5 columns", result.stdout)

    def test_a_short_sha_is_malformed(self) -> None:
        sha = self.upstream.commit("add", Widget_hpp="one\n")
        self.upstream.publish()
        table = self.write_table(self.row("README.md", "Widget_hpp", sha[:12]))

        result = self.run_checker(table)

        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("not a full 40-character hash", result.stdout)


class TestMissingCheckout(CheckerCase):
    def test_an_unreachable_upstream_skips_rather_than_passing(self) -> None:
        sha = self.upstream.commit("add", Widget_hpp="one\n")
        self.upstream.publish()
        table = self.write_table(self.row("README.md", "Widget_hpp", sha))

        result = subprocess.run(
            [
                sys.executable,
                str(CHECKER),
                "--provenance",
                str(table),
                "--checkout",
                f"selftest/upstream={self.scratch / 'nowhere'}",
                "--no-fetch",
            ],
            capture_output=True,
            text=True,
            check=False,
            cwd=str(REPOSITORY_ROOT),
        )

        # 77, not 0: "I could not look" must not be reported the same way as "I looked and found
        # nothing" (.agent/rules/testing.md -- a gate that does not report reads as passed).
        self.assertEqual(result.returncode, SKIP_EXIT_CODE, result.stdout + result.stderr)
        self.assertIn("NOT CHECKED", result.stdout)


class TestFilters(CheckerCase):
    def test_path_prefix_selects_a_subtree(self) -> None:
        sha = self.upstream.commit("add", Widget_hpp="one\n")
        self.upstream.publish()
        table = self.write_table(
            self.row("README.md", "Widget_hpp", sha) + self.row("NOTICE", "Widget_hpp", sha)
        )

        result = self.run_checker(table, "--path-prefix", "NOTICE")

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("1 row(s) checked", result.stdout)

    def test_repo_matches_the_whole_name_or_the_bare_name_but_not_the_owner(self) -> None:
        sha = self.upstream.commit("add", Widget_hpp="one\n")
        self.upstream.publish()
        table = self.write_table(self.row("README.md", "Widget_hpp", sha))

        # "selftest" is the OWNER here. Matching it would be the bug that made `--repo contour`
        # select every contour-terminal row, endo's included.
        self.assertIn("0 row(s) checked", self.run_checker(table, "--repo", "selftest").stdout)
        self.assertIn("1 row(s) checked", self.run_checker(table, "--repo", "upstream").stdout)
        self.assertIn("1 row(s) checked", self.run_checker(table, "--repo", "selftest/upstream").stdout)


if __name__ == "__main__":
    # git refuses to run without an identity, and a CI image may have none configured globally.
    os.environ.setdefault("GIT_CONFIG_GLOBAL", os.devnull)
    os.environ.setdefault("GIT_CONFIG_SYSTEM", os.devnull)
    unittest.main(verbosity=2 if "-v" in sys.argv else 1, argv=[a for a in sys.argv if a != "-v"])
