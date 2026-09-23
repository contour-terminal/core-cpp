#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Proves that check-await-ready.py refuses an `await_ready` that makes a call, and nothing else.

    python scripts/check-await-ready-selftest.py       # quiet unless something fails
    python scripts/check-await-ready-selftest.py -v    # one line per case

A case per shape fastcached#1546 was measured with or the tree held when it was written -- a
virtual clock read, a call on a member, a lock -- and a case per thing that is NOT a definition
with a call: a constant, a member read, a use of `await_ready` from outside, a comment about the
rule. A scan that fires on its own documentation is a gate that fails on a true negative, and one
that reads nothing is a gate that passes on everything, so both are cases here.
"""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

CHECKER = Path(__file__).resolve().parent / "check-await-ready.py"


def load_checker():
    """:return: check-await-ready.py, loaded by path because its name has hyphens."""
    spec = importlib.util.spec_from_file_location("check_await_ready", CHECKER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CHECK = load_checker()


class AwaitReadyTest(unittest.TestCase):
    def run_on(
        self, files: dict[str, str], allowed: dict[tuple[str, str], str] | None = None
    ) -> tuple[list[str], int, int]:
        """Writes @p files under a scratch root and scans it."""
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for path, text in files.items():
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                (root / path).write_bytes(text.encode("utf-8"))
            return CHECK.scan(root, allowed or {})

    def test_the_measured_shape_is_refused_by_line(self):
        # The one fastcached#1546 was found with: a virtual clock read.
        problems, _, definitions = self.run_on(
            {
                "src/core/net/A.hpp": "struct A {\n"
                "    bool await_ready() const noexcept\n"
                "    {\n"
                "        return _loop == nullptr || _deadline <= _loop->clock().now();\n"
                "    }\n"
                "};\n"
            }
        )
        self.assertEqual(definitions, 1)
        self.assertEqual(len(problems), 2, problems)
        self.assertIn("src/core/net/A.hpp:4: [call] await_ready calls `clock`", problems[0])
        self.assertIn("calls `now`", problems[1])

    def test_a_call_on_a_member_is_refused(self):
        problems, _, _ = self.run_on(
            {"src/core/B.hpp": "bool await_ready() const { return _runtime.hasBufferedInput(); }\n"}
        )
        self.assertEqual(len(problems), 1)
        self.assertIn("calls `hasBufferedInput`", problems[0])

    def test_a_lock_is_refused_as_a_construction(self):
        problems, _, _ = self.run_on(
            {
                "src/core/C.cpp": "bool await_ready() const noexcept\n{\n"
                "    auto const guard = std::scoped_lock { _mutex };\n    return _done;\n}\n"
            }
        )
        self.assertEqual(len(problems), 1)
        self.assertIn("[construction] await_ready constructs `std::scoped_lock`", problems[0])

    def test_a_test_file_is_read_too(self):
        # Tests run on the ARM64 leg as well; a test double with the shape is refused like any other.
        problems, _, _ = self.run_on(
            {
                "src/core/net/D_test.cpp": "bool await_ready() const noexcept { return loop->clock().now() >= d; }\n"
            }
        )
        self.assertEqual(len(problems), 2)

    def test_what_is_trivial_or_not_a_definition_is_not_refused(self):
        files = {
            "src/core/E.hpp": "struct E {\n"
            "    [[nodiscard]] static constexpr bool await_ready() noexcept { return false; }\n"
            "    [[nodiscard]] bool await_ready() const noexcept { return _settled; }\n"
            "    [[nodiscard]] bool await_ready() const noexcept { return _handle == platform::InvalidHandle; }\n"
            "    [[nodiscard]] std::coroutine_handle<> await_ready() const noexcept { return {}; }\n"
            "    [[nodiscard]] bool await_ready() const noexcept { return static_cast<bool>(_flag); }\n"
            "    /// Never `await_ready() { return clock().now(); }` -- see fastcached#1546.\n"
            "    bool await_ready() const noexcept; // declared, defined elsewhere\n"
            "};\n"
            "static_assert(!E::await_ready());\n"
            "void f(E& e) { std::ignore = e.await_ready(); }\n"
            "template <typename A> concept C = requires(A a) { { a.await_ready() } -> bool; };\n"
        }
        problems, _, definitions = self.run_on(files)
        self.assertEqual(problems, [])
        self.assertEqual(definitions, 5)

    def test_an_out_of_line_definition_is_read(self):
        problems, _, definitions = self.run_on(
            {"src/core/F.cpp": "bool F::await_ready() const noexcept\n{\n    return _queue.empty();\n}\n"}
        )
        self.assertEqual(definitions, 1)
        self.assertEqual(len(problems), 1)
        self.assertIn("src/core/F.cpp:3: [call]", problems[0])

    def test_an_allowed_call_is_excused_and_a_stale_row_is_not(self):
        files = {
            "src/core/G.hpp": "bool await_ready() const noexcept { return ready(); }\n",
            "src/core/H.hpp": "bool await_ready() const noexcept { return false; }\n",
        }
        problems, _, _ = self.run_on(
            files, {("src/core/G.hpp", "ready"): "a reason", ("src/core/H.hpp", "now"): "gone"}
        )
        self.assertEqual(len(problems), 1)
        self.assertIn("src/core/H.hpp: [stale-allow] `now`", problems[0])

    def test_a_tree_with_no_definition_is_a_broken_scan_not_a_clean_one(self):
        _, read, definitions = self.run_on({"src/core/I.hpp": "int i;\n"})
        self.assertEqual((read, definitions), (1, 0))

    def test_the_real_tree_is_read(self):
        _, read, definitions = CHECK.scan(CHECKER.parent.parent, CHECK.ALLOWED)
        self.assertGreater(read, 100)
        self.assertGreater(definitions, 20)


if __name__ == "__main__":
    verbosity = 2 if "-v" in sys.argv else 1
    result = unittest.TextTestRunner(verbosity=verbosity).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(AwaitReadyTest)
    )
    sys.exit(0 if result.wasSuccessful() else 1)
