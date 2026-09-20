# SPDX-License-Identifier: Apache-2.0
"""The drift gate's own proof: that it accepts this tree, and refuses each way the table can rot.

A rename table that silently drifts from the delivered API turns six consumer migrations into six
debugging sessions of the same table (Ruling R68).
"""

from __future__ import annotations

import importlib.util
import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import renames

HERE = Path(__file__).resolve().parent
REPOSITORY_ROOT = HERE.parent.parent
TABLE = HERE / "renames.json"


def loadChecker():
    """Loads check-renames.py, whose name is not an identifier, the way a ctest invokes it: by path."""
    spec = importlib.util.spec_from_file_location("check_renames", HERE / "check-renames.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module  # a dataclass in the module needs to find its own module
    spec.loader.exec_module(module)
    return module


check = loadChecker()


class TheDeliveredTableValidates(unittest.TestCase):
    def test_this_tree_passes_the_gate(self) -> None:
        failures = check.validate(REPOSITORY_ROOT, TABLE)
        self.assertEqual(failures, [], "\n".join(failures))

    def test_the_gate_checked_something(self) -> None:
        # A gate that validated no row at all reports the same green as one that validated them all.
        summary = check.summarise(REPOSITORY_ROOT, TABLE)
        self.assertGreater(summary.validated, 40, f"only {summary.validated} rows carry a core-cpp target")
        self.assertGreater(summary.rows, 100, f"only {summary.rows} rows in the table")


class ASandbox:
    """A minimal core-cpp tree: one public header in a FILE_SET, and a table to point at it."""

    def __init__(self, directory: str) -> None:
        self.root = Path(directory)
        module = self.root / "src" / "core" / "net"
        module.mkdir(parents=True)
        (module / "EventLoop.hpp").write_text(
            "#pragma once\nnamespace core::net\n{\nclass EventLoop\n{\n};\n}\n", encoding="utf-8"
        )
        (module / "CMakeLists.txt").write_text(
            "core_cpp_add_module(net KIND STATIC\n    HEADERS\n        EventLoop.hpp\n    SOURCES\n"
            "        EventLoop.cpp\n    PUBLIC_LIBS core::async)\n",
            encoding="utf-8",
        )
        self.table = self.root / "renames.json"

    def write(self, rows: list[dict]) -> Path:
        self.table.write_text(
            json.dumps({"version": 1, "profiles": {"contour": "contour"}, "rows": rows}, indent=2),
            encoding="utf-8",
        )
        return self.table


class TheGateRefusesDrift(unittest.TestCase):
    GOOD = {
        "kind": "namespace",
        "from": "net",
        "to": "core::net",
        "profiles": ["contour"],
        "target": {"header": "core/net/EventLoop.hpp", "symbol": "core::net::EventLoop"},
    }

    def refusalFor(self, row: dict) -> str:
        with TemporaryDirectory() as directory:
            sandbox = ASandbox(directory)
            failures = check.validate(sandbox.root, sandbox.write([row]))
            return "\n".join(failures)

    def test_it_accepts_a_row_whose_target_is_delivered(self) -> None:
        self.assertEqual(self.refusalFor(self.GOOD), "")

    def test_it_refuses_a_target_header_that_does_not_exist(self) -> None:
        row = self.GOOD | {"target": {"header": "core/net/Gone.hpp", "symbol": "core::net::EventLoop"}}
        self.assertIn("core/net/Gone.hpp", self.refusalFor(row))
        self.assertIn("no such header", self.refusalFor(row))

    def test_it_refuses_a_symbol_the_header_does_not_declare(self) -> None:
        row = self.GOOD | {"target": {"header": "core/net/EventLoop.hpp", "symbol": "core::net::IoBackend"}}
        self.assertIn("IoBackend", self.refusalFor(row))
        self.assertIn("declares no", self.refusalFor(row))

    def test_it_refuses_a_symbol_in_a_namespace_the_header_does_not_open(self) -> None:
        row = self.GOOD | {"target": {"header": "core/net/EventLoop.hpp", "symbol": "core::tui::EventLoop"}}
        self.assertIn("core::tui", self.refusalFor(row))

    def test_it_refuses_a_public_target_that_is_in_no_file_set(self) -> None:
        with TemporaryDirectory() as directory:
            sandbox = ASandbox(directory)
            private = sandbox.root / "src" / "core" / "net" / "detail"
            private.mkdir()
            (private / "Guard.hpp").write_text(
                "#pragma once\nnamespace core::net\n{\nclass Guard\n{\n};\n}\n", encoding="utf-8"
            )
            guard = {"header": "core/net/detail/Guard.hpp", "symbol": "core::net::Guard"}
            row = self.GOOD | {"target": guard}
            failures = check.validate(sandbox.root, sandbox.write([row]))
            self.assertIn("is in no FILE_SET HEADERS", "\n".join(failures))

    def test_a_target_declared_private_is_accepted(self) -> None:
        with TemporaryDirectory() as directory:
            sandbox = ASandbox(directory)
            private = sandbox.root / "src" / "core" / "net" / "detail"
            private.mkdir()
            (private / "Guard.hpp").write_text(
                "#pragma once\nnamespace core::net\n{\nclass Guard\n{\n};\n}\n", encoding="utf-8"
            )
            row = self.GOOD | {
                "target": {
                    "header": "core/net/detail/Guard.hpp",
                    "symbol": "core::net::Guard",
                    "public": False,
                }
            }
            self.assertEqual(check.validate(sandbox.root, sandbox.write([row])), [])


class APendingRowIsCheckedTheOtherWayRound(unittest.TestCase):
    """Phase B has not landed `IoBackend` yet. A row waiting for it must be refused once it lands."""

    PENDING = {
        "kind": "symbol",
        "from": "net::EventSource",
        "to": "core::net::IoBackend",
        "profiles": ["contour"],
        "status": "pending",
        "task": "B3",
        "target": {"header": "core/net/IoBackend.hpp", "symbol": "core::net::IoBackend"},
    }

    def test_a_pending_row_whose_symbol_is_absent_is_accepted(self) -> None:
        with TemporaryDirectory() as directory:
            sandbox = ASandbox(directory)
            self.assertEqual(check.validate(sandbox.root, sandbox.write([self.PENDING])), [])

    def test_a_pending_row_whose_symbol_has_landed_is_refused(self) -> None:
        with TemporaryDirectory() as directory:
            sandbox = ASandbox(directory)
            landed = sandbox.root / "src" / "core" / "net" / "IoBackend.hpp"
            landed.write_text(
                "#pragma once\nnamespace core::net\n{\nclass IoBackend\n{\n};\n}\n", encoding="utf-8"
            )
            failures = "\n".join(check.validate(sandbox.root, sandbox.write([self.PENDING])))
            self.assertIn("IoBackend", failures)
            self.assertIn("mark the row delivered", failures)


class TheSchemaIsChecked(unittest.TestCase):
    def loadOne(self, row: dict) -> None:
        with TemporaryDirectory() as directory:
            path = Path(directory) / "renames.json"
            path.write_text(
                json.dumps({"version": 1, "profiles": {"contour": "c"}, "rows": [row]}), encoding="utf-8"
            )
            renames.load(path)

    def test_an_unknown_kind_is_refused(self) -> None:
        with self.assertRaisesRegex(renames.TableError, "kind"):
            self.loadOne({"kind": "sideways", "from": "a", "to": "b", "profiles": ["contour"]})

    def test_an_unknown_profile_is_refused(self) -> None:
        with self.assertRaisesRegex(renames.TableError, "profile"):
            self.loadOne({"kind": "namespace", "from": "a", "to": "b", "profiles": ["nosuch"]})

    def test_a_pending_row_without_a_task_is_refused(self) -> None:
        with self.assertRaisesRegex(renames.TableError, "task"):
            self.loadOne(
                {"kind": "namespace", "from": "a", "to": "b", "profiles": ["contour"], "status": "pending"}
            )

    def test_a_semantic_row_without_a_scope_is_refused(self) -> None:
        with self.assertRaisesRegex(renames.TableError, "scope"):
            self.loadOne(
                {"kind": "member", "from": "Read", "to": "read", "profiles": ["contour"], "apply": "semantic"}
            )

    def test_a_duplicate_source_within_one_profile_is_refused(self) -> None:
        with TemporaryDirectory() as directory:
            path = Path(directory) / "renames.json"
            row = {"kind": "namespace", "from": "net", "to": "core::net", "profiles": ["contour"]}
            path.write_text(
                json.dumps({"version": 1, "profiles": {"contour": "c"}, "rows": [row, dict(row)]}),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(renames.TableError, "twice"):
                renames.load(path)

    def test_the_real_table_loads(self) -> None:
        table = renames.load(TABLE)
        self.assertGreater(len(table.rows), 100)
        self.assertEqual(set(table.profiles), {"contour", "endo", "tuidu", "fastcached"})


if __name__ == "__main__":
    unittest.main()
