#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Proves that tidy-record.py refuses every result it cannot vouch for, and accepts one it can.

    python scripts/tidy-record-selftest.py       # quiet unless something fails
    python scripts/tidy-record-selftest.py -v    # one line per case

Each field of the record gets a case that breaks exactly that field -- the three silent failures
of 2026-09-21 among them: an analyser that was never invoked, a log that stopped part-way, and an
incremental tree whose zero meant nothing needed recompiling -- plus the one record that is sound.
The parsers get cases of their own, including the one that zeroed a finding count before: an
`error:` from `-warnings-as-errors` where a pattern looked for `warning:`.
"""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent / "tidy-record.py"


def load_script():
    """:return: tidy-record.py, loaded by path because its name has a hyphen."""
    spec = importlib.util.spec_from_file_location("tidy_record", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    # Registered before it runs: a dataclass looks its own module up in sys.modules.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


TR = load_script()


def sound() -> TR.Record:
    """:return: a record every field of which vouches for a clean result."""
    return TR.Record(
        exit_code=0,
        tidy="/opt/clang-tidy",
        version="22.1.8",
        pin="22.1.8",
        statements=431,
        steps_done=606,
        steps_total=606,
        log_lines=573,
        canary=True,
    )


class RecordTest(unittest.TestCase):
    def assert_refused(self, record: TR.Record, field: str, incremental: bool = False) -> None:
        refusals = record.refusals(incremental)
        self.assertTrue(any(why.startswith(field + ":") for why in refusals), refusals)

    def test_a_sound_record_is_accepted(self):
        self.assertEqual(sound().refusals(incremental=False), [])

    def test_a_failed_build_is_refused(self):
        record = sound()
        record.exit_code = 1
        self.assert_refused(record, "exit")

    def test_a_missing_exit_code_is_refused(self):
        record = sound()
        record.exit_code = None
        self.assert_refused(record, "exit")

    def test_an_analyser_the_build_never_named_is_refused(self):
        record = sound()
        record.tidy = None
        self.assert_refused(record, "tidy")

    def test_an_analyser_that_does_not_run_is_refused(self):
        record = sound()
        record.version = None
        self.assert_refused(record, "tidy")

    def test_an_unpinned_analyser_is_refused(self):
        record = sound()
        record.version = "21.1.0"
        self.assert_refused(record, "tidy")

    def test_no_analysed_statement_is_refused(self):
        record = sound()
        record.statements = 0
        self.assert_refused(record, "statements")

    def test_a_log_that_stopped_part_way_is_refused(self):
        record = sound()
        record.steps_done = 179
        self.assert_refused(record, "steps")

    def test_a_log_with_no_step_is_refused(self):
        record = sound()
        record.steps_done = record.steps_total = 0
        self.assert_refused(record, "steps")

    def test_an_incremental_tree_is_refused_unless_asked_for(self):
        record = sound()
        record.steps_done = record.steps_total = 25
        self.assert_refused(record, "steps")
        self.assertEqual(record.refusals(incremental=True), [])

    def test_an_idle_analyser_is_refused(self):
        record = sound()
        record.canary = False
        self.assert_refused(record, "canary")

    def test_an_unanalysed_canary_is_refused(self):
        record = sound()
        record.canary = None
        self.assert_refused(record, "canary")


class ParserTest(unittest.TestCase):
    def test_the_build_resolved_analyser_and_its_statements_are_read_from_build_ninja(self):
        ninja = (
            'build a.o: CXX a.cpp\n  LAUNCHER = cmake -E __run_co_compile --tidy="/x/clang-tidy;--extra-arg=-a" --\n'
            'build b.o: CXX b.cpp\n  LAUNCHER = cmake -E __run_co_compile --tidy="/x/clang-tidy;--extra-arg=-a" --\n'
            "build c.o: CXX c.cpp\n"
        )
        self.assertEqual(TR.read_ninja(ninja), ("/x/clang-tidy", 2))
        self.assertEqual(TR.read_ninja("build c.o: CXX c.cpp\n"), (None, 0))

    def test_the_last_step_and_both_finding_spellings_are_read_from_the_log(self):
        log = (
            "[1/3] Building CXX object a.o\n"
            "/src/a.cpp:3:5: warning: something [readability-foo]\n"
            "[2/3] Building CXX object b.o\n"
            "/src/b.cpp:9:1: error: something else [misc-bar,-warnings-as-errors]\n"
            "[3/3] Linking\n"
        )
        record = TR.Record()
        TR.read_log(log, record)
        self.assertEqual((record.steps_done, record.steps_total), (3, 3))
        self.assertEqual(len(record.findings), 2)
        self.assertEqual(record.log_lines, 5)

    def test_the_pin_is_read_from_its_file(self):
        pin = TR.read_pin(SCRIPT.parent.parent)
        self.assertIsNotNone(pin)
        self.assertRegex(pin, r"^\d+\.\d+\.\d+$")


if __name__ == "__main__":
    verbosity = 2 if "-v" in sys.argv else 1
    suite = unittest.TestSuite()
    for case in (RecordTest, ParserTest):
        suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(case))
    result = unittest.TextTestRunner(verbosity=verbosity).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
