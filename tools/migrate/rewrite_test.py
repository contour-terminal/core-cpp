# SPDX-License-Identifier: Apache-2.0
"""What the mechanical codemod must do, and the four things it must never do.

Run with the whole migration suite:

    python -m unittest discover -s tools/migrate -p '*_test.py'
"""

from __future__ import annotations

import io
import json
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from tempfile import TemporaryDirectory

import renames
import rewrite

TABLE = Path(__file__).resolve().parent / "renames.json"


def rows_for(profile: str) -> list[renames.Row]:
    return renames.load(TABLE).text_rows(profile)


class RewriteIsIdempotent(unittest.TestCase):
    """A codemod a reviewer cannot re-run is a codemod that must land in one perfect commit."""

    # One row of every category the map has, in the shapes contour actually writes them.
    FIXTURE = """\
// SPDX-License-Identifier: Apache-2.0
#include <crispy/CLI.hpp>
#include <crispy/LogStore.hpp>
#include <coro/Task.hpp>
#include <net/EventLoop.hpp>

#include "crispy/Utils.hpp"

struct CRISPY_PACKED Header
{
    int field;
};

coro::Task<void> serve(net::EventLoop& loop, net::IListener& listener)
{
    auto const port = listener.localPort();
    logstore::errorLog()("port {}", crispy::toInteger<int>("1"));
    co_return;
}
"""

    def test_a_second_run_changes_nothing(self) -> None:
        rows = rows_for("contour")
        once, first_report = rewrite.rewrite_text(self.FIXTURE, rows)
        twice, second_report = rewrite.rewrite_text(once, rows)
        self.assertNotEqual(once, self.FIXTURE, "the first run must change the fixture")
        self.assertEqual(twice, once, "the second run must change nothing")
        self.assertEqual(sum(second_report.values()), 0, f"second run still applied {second_report}")

    def test_the_fixture_exercises_every_category(self) -> None:
        _, report = rewrite.rewrite_text(self.FIXTURE, rows_for("contour"))
        kinds = {row.kind for row, count in report.items() if count}
        self.assertEqual(kinds, {"include", "namespace", "symbol", "member", "macro"}, f"applied: {report}")

    def test_a_half_converted_tree_converges(self) -> None:
        """A tree where somebody already ran the tool over half the files is a tree it must accept."""
        rows = rows_for("contour")
        converted, _ = rewrite.rewrite_text(self.FIXTURE, rows)
        mixed = converted + "\n" + self.FIXTURE
        once, _ = rewrite.rewrite_text(mixed, rows)
        twice, report = rewrite.rewrite_text(once, rows)
        self.assertEqual(twice, once)
        self.assertEqual(sum(report.values()), 0)
        self.assertEqual(once, converted + "\n" + converted)


class TheCliTypesAreRenamed(unittest.TestCase):
    """The map is not a namespace prefix swap: tuidu's snapshot spells the cli types in another case."""

    def test_crispy_cli_command_becomes_core_cli_Command(self) -> None:
        result, _ = rewrite.rewrite_text("auto c = crispy::cli::command {};\n", rows_for("tuidu"))
        self.assertEqual(result, "auto c = core::cli::Command {};\n")

    def test_every_drifted_cli_type_has_a_row(self) -> None:
        # Through tuidu's `namespace cli = crispy::cli;` alias, in both the spellings the tree has.
        for source, expected in [
            ("cli::command", "core::cli::Command"),
            ("cli::option", "core::cli::Option"),
            ("cli::option_list", "core::cli::OptionList"),
            ("cli::optionList", "core::cli::OptionList"),
            ("cli::flag_store", "core::cli::FlagStore"),
            ("cli::flagStore", "core::cli::FlagStore"),
            ("cli::value", "core::cli::Value"),
            ("cli::verbatim", "core::cli::Verbatim"),
            ("cli::help_display_style", "core::cli::HelpDisplayStyle"),
            ("cli::helpDisplayStyle", "core::cli::HelpDisplayStyle"),
        ]:
            with self.subTest(source=source):
                result, _ = rewrite.rewrite_text(f"x({source});\n", rows_for("tuidu"))
                self.assertEqual(result, f"x({expected});\n")

    def test_a_cli_name_that_did_not_drift_is_left_alone(self) -> None:
        result, _ = rewrite.rewrite_text("cli::parse(x);\ncli::helpText(y);\n", rows_for("tuidu"))
        self.assertEqual(result, "cli::parse(x);\ncli::helpText(y);\n")


class TheNamespaceRegexIsAnchored(unittest.TestCase):
    """`net::` is the single most dangerous row in the map; every neighbour of it is a case here."""

    def test_std_net_is_untouched(self) -> None:
        result, _ = rewrite.rewrite_text("std::net::Socket s;\n", rows_for("contour"))
        self.assertEqual(result, "std::net::Socket s;\n")

    def test_another_projects_net_is_untouched(self) -> None:
        result, _ = rewrite.rewrite_text("endo::net::Thing t;\n", rows_for("contour"))
        self.assertEqual(result, "endo::net::Thing t;\n")

    def test_a_namespace_ending_in_net_is_untouched(self) -> None:
        result, _ = rewrite.rewrite_text("mynet::Thing t;\nSUBNET::x;\n", rows_for("contour"))
        self.assertEqual(result, "mynet::Thing t;\nSUBNET::x;\n")

    def test_a_string_literal_is_data_and_is_left_alone(self) -> None:
        source = 'auto const s = "net::EventLoop";\nauto const r = R"(net::x)";\nchar const c = \'n\';\n'
        result, _ = rewrite.rewrite_text(source, rows_for("contour"))
        self.assertEqual(result, source, "a codemod may change what the code says, never what it sends")

    def test_a_comment_follows_the_code_it_documents(self) -> None:
        source = "// net::EventLoop drives it.\n/* net::ISocket too. */\nnet::EventLoop loop;\n"
        result, _ = rewrite.rewrite_text(source, rows_for("contour"))
        self.assertEqual(
            result,
            "// core::net::EventLoop drives it.\n/* core::net::ISocket too. */\ncore::net::EventLoop loop;\n",
        )

    def test_the_bare_namespace_is_rewritten(self) -> None:
        result, _ = rewrite.rewrite_text("net::EventLoop loop;\ncoro::Task<int> t;\n", rows_for("contour"))
        self.assertEqual(result, "core::net::EventLoop loop;\ncore::async::Task<int> t;\n")

    def test_a_namespace_definition_is_left_to_a_human(self) -> None:
        # `namespace net { ... }` in a consumer may be its own namespace; the plan lists these as
        # hand edits. Only `using namespace` and qualified uses are mechanical.
        source = "namespace net\n{\nstruct Thing {};\n}\n"
        result, _ = rewrite.rewrite_text(source, rows_for("contour"))
        self.assertEqual(result, source)

    def test_a_using_directive_is_rewritten(self) -> None:
        result, _ = rewrite.rewrite_text("using namespace coro;\n", rows_for("contour"))
        self.assertEqual(result, "using namespace core::async;\n")


class ProfilesSelectRows(unittest.TestCase):
    def test_endo_has_no_crispy_cli_type_rows(self) -> None:
        result, _ = rewrite.rewrite_text("cli::command c;\n", rows_for("endo"))
        self.assertEqual(result, "cli::command c;\n", "the PascalCase drift is tuidu's snapshot alone")

    def test_fastcached_rewrites_its_own_includes_only(self) -> None:
        source = "#include <FastCache/Core/Profiling.hpp>\n#include <crispy/CLI.hpp>\n"
        result, _ = rewrite.rewrite_text(source, rows_for("fastcached"))
        self.assertEqual(result, "#include <core/Profiling.hpp>\n#include <crispy/CLI.hpp>\n")

    def test_an_unknown_profile_is_refused(self) -> None:
        with self.assertRaises(renames.TableError):
            rows_for("nosuchconsumer")

    def test_a_semantic_row_is_not_applied_textually(self) -> None:
        # `Read` is far too common a word to rewrite by text; semantic_rename.py owns those rows.
        result, _ = rewrite.rewrite_text("sock.Read(buffer);\n", rows_for("fastcached"))
        self.assertEqual(result, "sock.Read(buffer);\n")


class ARemovedRowCanNeverBeARewriteSource(unittest.TestCase):
    """A `removed` symbol has no replacement, so rewriting it would produce code that cannot compile.

    The kind exists to document a removal and to catch a re-introduction, never to edit anything
    (controller ruling R75). "The rewrite tools skip it" is made structural rather than
    conventional: the loader refuses to hand one out, and building a pattern for one raises.
    """

    def test_no_profile_is_handed_a_removed_row(self) -> None:
        table = renames.load(TABLE)
        self.assertTrue([row for row in table.rows if row.kind == "removed"], "the table has no removed row")
        for profile in table.profiles:
            with self.subTest(profile=profile):
                kinds = {row.kind for row in table.text_rows(profile)} | {
                    row.kind for row in table.semantic_rows(profile)
                }
                self.assertNotIn("removed", kinds)

    def test_a_removed_symbol_survives_the_codemod_untouched(self) -> None:
        source = "auto id = core::tui::LanguageId::Endo;\ncore::tui::registerEndoHighlighter(f);\n"
        for profile in ("contour", "endo", "tuidu", "fastcached"):
            with self.subTest(profile=profile):
                result, _ = rewrite.rewrite_text(source, rows_for(profile))
                self.assertEqual(result, source)

    def test_the_loader_is_a_door_of_its_own(self) -> None:
        # R93: text_rows()/semantic_rows() filtered on `apply`, not on `kind`, so they excluded a
        # removed row only *because* the schema forced its apply -- the schema door seen from
        # downstream, not a second door. A Row built directly, bypassing the schema, proves it.
        table = renames.Table(
            profiles={"contour": "c"},
            rows=[
                renames.Row(
                    kind="removed",
                    source="core::tui::LanguageId::Endo",
                    target="X",
                    profiles=("contour",),
                    apply="text",
                )
            ],
        )
        self.assertEqual(table.text_rows("contour"), [])
        self.assertEqual(table.semantic_rows("contour"), [])

    def test_building_a_pattern_for_a_removed_row_raises(self) -> None:
        row = renames.Row(kind="removed", source="core::tui::LanguageId::Endo", target="", profiles=("endo",))
        with self.assertRaises(renames.TableError):
            rewrite._patterns_for(row)


class TheCodemodChangesOnlyWhatItWasAskedTo(unittest.TestCase):
    """The boundaries the tool declares absolute, under the inputs six consumer trees actually hold.

    Each case here is a review finding that the tool got wrong (I1-I4): a raw string holding C++,
    a comment that mentions raw-string syntax, a file that is not UTF-8, and a file with CRLF.
    """

    def test_an_include_inside_a_raw_string_is_not_rewritten(self) -> None:
        # I1. The include pass used to run on the raw text, before literals were masked, so a
        # line-initial directive inside embedded C++ test data was rewritten -- the one thing the
        # tool says it never does. Highlighter and parser suites are full of these.
        source = 'static char const* Source = R"(\n#include <net/EventLoop.hpp>\n)";\n'
        result, _ = rewrite.rewrite_text(source, rows_for("contour"))
        self.assertEqual(result, source, "a codemod may change what the code says, never what it sends")

    def test_a_real_include_beside_a_raw_string_is_still_rewritten(self) -> None:
        source = '#include <net/EventLoop.hpp>\nstatic char const* S = R"(\n#include <net/ISocket.hpp>\n)";\n'
        result, _ = rewrite.rewrite_text(source, rows_for("contour"))
        self.assertEqual(
            result,
            '#include <core/net/EventLoop.hpp>\nstatic char const* S = R"(\n#include <net/ISocket.hpp>\n)";\n',
        )

    def test_a_quoted_include_is_not_read_as_a_string_literal(self) -> None:
        result, _ = rewrite.rewrite_text('#include "net/EventLoop.hpp"\n', rows_for("contour"))
        self.assertEqual(result, "#include <core/net/EventLoop.hpp>\n")

    def test_an_unterminated_raw_string_in_a_comment_masks_nothing(self) -> None:
        # I2. The masker ran over comments, and its raw-string rule is the one that is not
        # line-confined, so a comment mentioning R"( swallowed every line up to the next )".
        source = (
            '// a raw string starts with R"( and ends with the mirror\n'
            "net::EventLoop loop;\n"
            "int f() { return 1; }\n"
            'auto s = R"(hello)";\n'
            "net::ISocket* p;\n"
        )
        result, _ = rewrite.rewrite_text(source, rows_for("contour"))
        self.assertIn("core::net::EventLoop loop;", result)
        self.assertIn("core::net::ISocket* p;", result)
        self.assertIn('auto s = R"(hello)";', result)

    def test_a_crlf_file_keeps_its_line_endings(self) -> None:
        # I4. Reading with universal newlines and writing "\n" rewrote every line of every changed
        # file, including untouched ones, which makes the diff unreviewable on a Windows checkout.
        with TemporaryDirectory() as directory:
            path = Path(directory) / "Crlf.cpp"
            original = b"#include <net/EventLoop.hpp>\r\nnet::EventLoop loop;\r\nint untouched = 1;\r\n"
            path.write_bytes(original)
            with redirect_stdout(io.StringIO()):
                rewrite.main(["--profile", "contour", str(path)])
            after = path.read_bytes()
            self.assertEqual(after.count(b"\r\n"), 3, f"line endings changed: {after!r}")
            self.assertEqual(after.count(b"\n"), after.count(b"\r\n"), "a bare LF crept in")
            self.assertIn(b"core::net::EventLoop loop;\r\n", after)
            self.assertIn(b"int untouched = 1;\r\n", after)

    def test_an_lf_file_keeps_its_line_endings(self) -> None:
        with TemporaryDirectory() as directory:
            path = Path(directory) / "Lf.cpp"
            path.write_bytes(b"net::EventLoop loop;\nint untouched = 1;\n")
            with redirect_stdout(io.StringIO()):
                rewrite.main(["--profile", "contour", str(path)])
            self.assertNotIn(b"\r", path.read_bytes())

    def test_a_file_that_is_not_utf8_is_skipped_counted_and_fatal(self) -> None:
        # I3. One latin-1 byte aborted the run mid-tree with a traceback, after partial writes.
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "A_Plain.cpp").write_bytes(b"net::EventLoop a;\n")
            (root / "B_Latin.cpp").write_bytes(b"// \xe9 copyright\nnet::EventLoop b;\n")
            (root / "C_Plain.cpp").write_bytes(b"net::EventLoop c;\n")
            output = io.StringIO()
            with redirect_stdout(output):
                status = rewrite.main(["--profile", "contour", str(root)])
            printed = output.getvalue()
            self.assertEqual(status, 1, "a half-converted tree must not report success")
            self.assertIn("B_Latin.cpp", printed)
            self.assertIn("not UTF-8", printed)
            # The files either side of it were still converted: one bad byte is not a stop order.
            self.assertIn(b"core::net::EventLoop a;", (root / "A_Plain.cpp").read_bytes())
            self.assertIn(b"core::net::EventLoop c;", (root / "C_Plain.cpp").read_bytes())
            self.assertEqual((root / "B_Latin.cpp").read_bytes(), b"// \xe9 copyright\nnet::EventLoop b;\n")


class TheToolReportsAndStaysInsideItsPath(unittest.TestCase):
    def test_it_reports_what_it_changed_per_file(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Serve.cpp").write_text("net::EventLoop loop;\n", encoding="utf-8")
            (root / "Untouched.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
            output = io.StringIO()
            with redirect_stdout(output):
                status = rewrite.main(["--profile", "contour", str(root)])
            self.assertEqual(status, 0)
            printed = output.getvalue()
            self.assertIn("Serve.cpp", printed)
            self.assertIn("namespace net -> core::net", printed)
            self.assertNotIn("Untouched.cpp", printed)
            self.assertIn("1 file changed", printed)

    def test_a_pending_row_is_applied_and_said_out_loud(self) -> None:
        # A row whose target a task still owes is rewritten anyway, and the run says so.
        #
        # **The table is the case's own, not `renames.json`.** It used to borrow whichever row
        # happened to be pending there -- `localPort` -> `boundPort`, task B6's -- and the day
        # Task B8 delivered that rename this case failed, having asserted a warning about a row
        # that no longer needed one. The behaviour is the tool's; a live table that happens to
        # have nothing pending is not evidence that the tool stopped warning.
        table = {
            "version": 1,
            "profiles": {"contour": "a consumer, for this case alone"},
            "rows": [
                {
                    "kind": "member",
                    "from": "localPort",
                    "to": "boundPort",
                    "profiles": ["contour"],
                    "status": "pending",
                    "task": "B6",
                    "target": {
                        "header": "core/net/IListener.hpp",
                        "symbol": "core::net::IListener::boundPort",
                    },
                }
            ],
        }
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Serve.cpp").write_text("auto const p = listener.localPort();\n", encoding="utf-8")
            pending = root / "pending.json"
            pending.write_text(json.dumps(table), encoding="utf-8")
            output = io.StringIO()
            with redirect_stdout(output):
                rewrite.main(["--profile", "contour", "--table", str(pending), str(root)])
            self.assertIn("boundPort", (root / "Serve.cpp").read_text(encoding="utf-8"))
            self.assertIn("warning: applied", output.getvalue())
            self.assertIn("task B6 still owes", output.getvalue())

    def test_dry_run_writes_nothing(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "Serve.cpp"
            source.write_text("net::EventLoop loop;\n", encoding="utf-8")
            with redirect_stdout(io.StringIO()):
                self.assertEqual(rewrite.main(["--profile", "contour", "--dry-run", str(root)]), 0)
            self.assertEqual(source.read_text(encoding="utf-8"), "net::EventLoop loop;\n")

    def test_it_never_walks_outside_the_given_path(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            inside = root / "inside"
            outside = root / "outside"
            inside.mkdir()
            outside.mkdir()
            (inside / "A.cpp").write_text("net::EventLoop loop;\n", encoding="utf-8")
            (outside / "B.cpp").write_text("net::EventLoop loop;\n", encoding="utf-8")
            with redirect_stdout(io.StringIO()):
                rewrite.main(["--profile", "contour", str(inside)])
            self.assertEqual((outside / "B.cpp").read_text(encoding="utf-8"), "net::EventLoop loop;\n")
            self.assertEqual((inside / "A.cpp").read_text(encoding="utf-8"), "core::net::EventLoop loop;\n")

    def test_only_cplusplus_sources_are_touched(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "notes.md").write_text("net::EventLoop\n", encoding="utf-8")
            (root / "A.hpp").write_text("net::EventLoop loop;\n", encoding="utf-8")
            with redirect_stdout(io.StringIO()):
                rewrite.main(["--profile", "contour", str(root)])
            self.assertEqual((root / "notes.md").read_text(encoding="utf-8"), "net::EventLoop\n")
            self.assertEqual((root / "A.hpp").read_text(encoding="utf-8"), "core::net::EventLoop loop;\n")

    def test_a_path_that_does_not_exist_is_refused(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            status = rewrite.main(["--profile", "contour", str(Path(__file__).parent / "nosuchdir")])
        self.assertEqual(status, 1)


class TheIncludeMapIsExplicit(unittest.TestCase):
    """endo keeps half of `<platform/...>`; a prefix rule would move the half that stays."""

    def test_a_header_core_cpp_delivers_moves(self) -> None:
        result, _ = rewrite.rewrite_text("#include <platform/Wakeup.hpp>\n", rows_for("endo"))
        self.assertEqual(result, "#include <core/platform/Wakeup.hpp>\n")

    def test_a_header_that_stays_in_endo_does_not_move(self) -> None:
        for header in ["platform/Process.hpp", "platform/Pipe.hpp", "platform/InstallPaths.hpp"]:
            with self.subTest(header=header):
                source = f"#include <{header}>\n"
                result, _ = rewrite.rewrite_text(source, rows_for("endo"))
                self.assertEqual(result, source)

    def test_the_quoted_form_is_rewritten_too(self) -> None:
        result, _ = rewrite.rewrite_text('#include "coro/Task.hpp"\n', rows_for("endo"))
        self.assertEqual(result, "#include <core/async/Task.hpp>\n")


if __name__ == "__main__":
    unittest.main()
