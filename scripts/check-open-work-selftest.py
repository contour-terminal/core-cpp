#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Proves that check-open-work.py refuses every entry it claims to, and accepts a sound one.

    python scripts/check-open-work-selftest.py       # quiet unless something fails
    python scripts/check-open-work-selftest.py -v    # one line per case

Fixtures are written rather than the real tree read, so the self-test does not go red when a lane
adds an entry. The online half is reached through `--states`, never the network.
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
CHECKER = REPOSITORY_ROOT / "scripts" / "check-open-work.py"


def load_checker():
    """Loads check-open-work.py as a module, by path, since its name has hyphens.

    :return: the imported module.
    """
    spec = importlib.util.spec_from_file_location("check_open_work", CHECKER)
    module = importlib.util.module_from_spec(spec)
    # Registered before it runs: its dataclass looks its own module up by name.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


CHECK = load_checker()

GOOD = "- **[core-cpp#12](https://github.com/contour-terminal/core-cpp/issues/12)** — what is left.\n"


class OpenWorkTest(unittest.TestCase):
    """One fixture tree per case."""

    def run_check(self, rules: str, states: dict[int, str] | None = None) -> tuple[int, str]:
        """Writes @p rules as a rule file, runs the checker over it.

        :param rules: the content of `.agent/rules/x.md`.
        :param states: issue states for the online half, or None for the offline half alone.
        :return: the exit status and what the checker printed.
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / ".agent" / "rules").mkdir(parents=True)
            (root / ".agent" / "rules" / "x.md").write_text(rules, encoding="utf-8")
            arguments = ["--root", str(root)]
            if states is not None:
                (root / "states.json").write_text(json.dumps(states), encoding="utf-8")
                arguments += ["--states", str(root / "states.json")]
            captured = io.StringIO()
            with contextlib.redirect_stdout(captured):
                status = CHECK.main(arguments)
            return status, captured.getvalue()

    def test_a_sound_entry_passes_and_is_counted(self):
        status, said = self.run_check(f"# X\n\n## Open work\n\n{GOOD}  more prose\n", {12: "open"})
        self.assertEqual(status, 0, said)
        self.assertIn("1 Open work entries hold", said)

    def test_an_entry_not_leading_with_an_issue_is_refused(self):
        status, said = self.run_check("## Open work\n\n- remember to do the thing\n")
        self.assertEqual(status, 1, said)
        self.assertIn("does not lead with", said)

    def test_an_issue_of_another_repository_is_refused(self):
        entry = "- **[fastcached#12](https://github.com/LASTRADA-Software/fastcached/issues/12)** — x\n"
        status, said = self.run_check(f"## Open work\n\n{entry}")
        self.assertEqual(status, 1, said)
        self.assertIn("does not lead with", said)

    def test_text_and_link_that_disagree_are_refused(self):
        entry = "- **[core-cpp#12](https://github.com/contour-terminal/core-cpp/issues/13)** — x\n"
        status, said = self.run_check(f"## Open work\n\n{entry}")
        self.assertEqual(status, 1, said)
        self.assertIn("says core-cpp#12 and links issue 13", said)

    def test_an_empty_section_is_refused(self):
        status, said = self.run_check("## Open work\n\nNothing.\n\n## Next\n")
        self.assertEqual(status, 1, said)
        self.assertIn("no entries", said)

    def test_a_closed_issue_is_refused(self):
        status, said = self.run_check(f"## Open work\n\n{GOOD}", {12: "closed"})
        self.assertEqual(status, 1, said)
        self.assertIn("core-cpp#12 is closed", said)

    def test_an_unanswered_state_is_skipped_neither_passed_nor_refused(self):
        status, said = self.run_check(f"## Open work\n\n{GOOD}", {99: "open"})
        self.assertEqual(status, 77, said)
        self.assertIn("SKIPPED", said)
        self.assertIn("could not be read", said)

    def test_a_refusal_still_fails_beside_an_unanswered_state(self):
        other = GOOD.replace("#12", "#13").replace("/12)", "/13)")
        status, said = self.run_check(f"## Open work\n\n{GOOD}{other}", {12: "closed"})
        self.assertEqual(status, 1, said)
        self.assertIn("core-cpp#12 is closed", said)

    def test_other_sections_and_code_blocks_are_not_entries(self):
        rules = f"## Rules\n\n- a rule, not an entry\n\n```\n## Open work\n- not an entry either\n```\n\n## Open work\n\n{GOOD}"
        status, said = self.run_check(rules, {12: "open"})
        self.assertEqual(status, 0, said)
        self.assertIn("1 Open work entries hold", said)


if __name__ == "__main__":
    unittest.main()
