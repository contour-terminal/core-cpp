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
    """What the table itself must be true of, without reading the working tree.

    There is deliberately no case here asserting that *this tree* passes the gate. A unit test must
    not take the mutable working tree as its fixture: in a checkout three other lanes are writing
    into, the verdict would depend on what they have half-written, and one true fact would be
    reported twice -- by this case and by the ctest -- which makes triage harder, not easier, and
    teaches people to ignore reds (controller ruling R76). The tree-level assertion is
    `core-cpp.migrate-renames`, which runs the identical check under `ctest -L hygiene` in every
    local and CI build. Everything below is hermetic: a sandbox tree, or the table alone.
    """

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


class APendingRowScopedToAnEnumIsSeenWhenItLands(unittest.TestCase):
    """The shape that had no case: a symbol scoped to an enum, not to a namespace.

    `core::net::NetErrorCode::SystemError` has no namespace called `core::net::NetErrorCode`, so a
    pending arm that asked for one would answer "not landed" forever -- a row that can never
    resolve, which is worse than no row, because it teaches the next reader to ignore the pending
    list (controller ruling R74). A symbol scoped to a class or a struct has the same shape.
    """

    PENDING = {
        "kind": "symbol",
        "from": "net::NetErrorCode::Other",
        "to": "core::net::NetErrorCode::SystemError",
        "profiles": ["contour"],
        "status": "pending",
        "task": "B2",
        "target": {"header": "core/net/NetError.hpp", "symbol": "core::net::NetErrorCode::SystemError"},
    }

    ENUM_HEADER = (
        "#pragma once\nnamespace core::net\n{\nenum class NetErrorCode : int\n"
        "{\n    BadHandle,\n    SystemError,\n};\n}\n"
    )
    CLASS_HEADER = (
        "#pragma once\nnamespace core::net\n{\nclass IListener\n{\n"
        "  public:\n    int boundPort() const;\n};\n}\n"
    )

    def test_a_pending_enum_scoped_row_that_has_landed_is_refused(self) -> None:
        with TemporaryDirectory() as directory:
            sandbox = ASandbox(directory)
            landed = sandbox.root / "src" / "core" / "net" / "NetError.hpp"
            landed.write_text(self.ENUM_HEADER, encoding="utf-8")
            failures = "\n".join(check.validate(sandbox.root, sandbox.write([self.PENDING])))
            self.assertIn("core::net::NetErrorCode::SystemError", failures)
            self.assertIn("mark the row delivered", failures)

    def test_a_pending_enum_scoped_row_that_has_not_landed_is_accepted(self) -> None:
        with TemporaryDirectory() as directory:
            sandbox = ASandbox(directory)
            absent = sandbox.root / "src" / "core" / "net" / "NetError.hpp"
            absent.write_text(self.ENUM_HEADER.replace("SystemError", "Other"), encoding="utf-8")
            self.assertEqual(check.validate(sandbox.root, sandbox.write([self.PENDING])), [])

    def test_a_pending_class_scoped_row_that_has_landed_is_refused(self) -> None:
        row = {
            "kind": "member",
            "from": "localPort",
            "to": "boundPort",
            "profiles": ["contour"],
            "status": "pending",
            "task": "B6",
            "target": {"header": "core/net/IListener.hpp", "symbol": "core::net::IListener::boundPort"},
        }
        with TemporaryDirectory() as directory:
            sandbox = ASandbox(directory)
            landed = sandbox.root / "src" / "core" / "net" / "IListener.hpp"
            landed.write_text(self.CLASS_HEADER, encoding="utf-8")
            failures = "\n".join(check.validate(sandbox.root, sandbox.write([row])))
            self.assertIn("core::net::IListener::boundPort", failures)

    def test_the_delivered_arm_reads_the_same_shape(self) -> None:
        # Both arms must read a qualified symbol the same way, or one of them is a hole.
        delivered = {
            "kind": "symbol",
            "from": "net::NetErrorCode::Other",
            "to": "core::net::NetErrorCode::SystemError",
            "profiles": ["contour"],
            "target": {
                "header": "core/net/NetError.hpp",
                "symbol": "core::net::NetErrorCode::SystemError",
                "public": False,
            },
        }
        with TemporaryDirectory() as directory:
            sandbox = ASandbox(directory)
            header = sandbox.root / "src" / "core" / "net" / "NetError.hpp"
            header.write_text(self.ENUM_HEADER, encoding="utf-8")
            self.assertEqual(check.validate(sandbox.root, sandbox.write([delivered])), [])


class ARemovedRowIsAnInverseGate(unittest.TestCase):
    """A `removed` row names what core-cpp no longer has, and the gate asserts it stays gone (R75)."""

    REMOVED = {
        "kind": "removed",
        "from": "core::tui::LanguageId::Endo",
        "profiles": ["contour"],  # the sandbox declares one profile; the real row's is endo
        "note": "an application registers its own language through core::tui::SyntaxHighlighterRegistry",
    }

    BACK = "#pragma once\nnamespace core::tui\n{\nenum class LanguageId : int\n{\n    PlainText,\n    Endo,\n};\n}\n"
    GONE = "#pragma once\nnamespace core::tui\n{\nenum class LanguageId : int\n{\n    PlainText,\n    Cpp,\n};\n}\n"

    def test_a_removed_symbol_that_is_absent_is_accepted(self) -> None:
        with TemporaryDirectory() as directory:
            sandbox = ASandbox(directory)
            (sandbox.root / "src" / "core" / "net" / "Highlighter.hpp").write_text(
                self.GONE, encoding="utf-8"
            )
            self.assertEqual(check.validate(sandbox.root, sandbox.write([self.REMOVED])), [])

    def test_a_removed_symbol_that_came_back_is_refused(self) -> None:
        with TemporaryDirectory() as directory:
            sandbox = ASandbox(directory)
            (sandbox.root / "src" / "core" / "net" / "Highlighter.hpp").write_text(
                self.BACK, encoding="utf-8"
            )
            failures = "\n".join(check.validate(sandbox.root, sandbox.write([self.REMOVED])))
            self.assertIn("core::tui::LanguageId::Endo", failures)
            self.assertIn("removed", failures)

    def test_a_removed_free_function_that_came_back_is_refused(self) -> None:
        row = self.REMOVED | {"from": "core::tui::registerEndoHighlighter"}
        with TemporaryDirectory() as directory:
            sandbox = ASandbox(directory)
            (sandbox.root / "src" / "core" / "net" / "Highlighter.hpp").write_text(
                "#pragma once\nnamespace core::tui\n{\nvoid registerEndoHighlighter(Fn f);\n}\n",
                encoding="utf-8",
            )
            failures = "\n".join(check.validate(sandbox.root, sandbox.write([row])))
            self.assertIn("registerEndoHighlighter", failures)

    def test_the_real_table_carries_the_symbols_B13a_removed(self) -> None:
        removed = {row.source for row in renames.load(TABLE).rows if row.kind == "removed"}
        self.assertIn("core::tui::LanguageId::Endo", removed)
        self.assertIn("core::tui::registerEndoHighlighter", removed)

    def test_every_removed_row_says_what_to_do_instead(self) -> None:
        for row in renames.load(TABLE).rows:
            if row.kind == "removed":
                self.assertTrue(row.note, f"{row.source} is removed with no note saying what replaces it")


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

    def test_a_removed_row_that_claims_to_be_rewritten_is_refused(self) -> None:
        with self.assertRaisesRegex(renames.TableError, "never rewritten"):
            self.loadOne(
                {
                    "kind": "removed",
                    "from": "core::tui::LanguageId::Endo",
                    "profiles": ["contour"],
                    "note": "gone",
                    "apply": "text",
                }
            )

    def test_a_removed_row_that_names_a_target_is_refused(self) -> None:
        with self.assertRaisesRegex(renames.TableError, "names no 'target'"):
            self.loadOne(
                {
                    "kind": "removed",
                    "from": "core::tui::LanguageId::Endo",
                    "profiles": ["contour"],
                    "note": "gone",
                    "target": {"header": "core/tui/GenericSyntaxHighlighter.hpp"},
                }
            )

    def test_a_removed_row_without_a_note_is_refused(self) -> None:
        with self.assertRaisesRegex(renames.TableError, "note"):
            self.loadOne({"kind": "removed", "from": "core::tui::LanguageId::Endo", "profiles": ["contour"]})

    def test_the_real_table_loads(self) -> None:
        table = renames.load(TABLE)
        self.assertGreater(len(table.rows), 100)
        self.assertEqual(set(table.profiles), {"contour", "endo", "tuidu", "fastcached"})


if __name__ == "__main__":
    unittest.main()
