# SPDX-License-Identifier: Apache-2.0
"""What the libclang pass must prove: it renames a member by its declaration, not by its spelling.

libclang's Python bindings are optional, so these cases skip where they are missing -- loudly, by
name, and never silently (Ruling R69). The `style` CI job installs them and refuses a skip, so the
pass is tested for real on every push.
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
import unittest
import unittest.mock
from contextlib import redirect_stdout
from pathlib import Path
from tempfile import TemporaryDirectory

import semantic_rename

if not semantic_rename.bindings_available():
    print(
        "semantic_rename_test: SKIPPING every case -- python -m pip install libclang "
        f"(no clang.cindex for {sys.executable})",
        file=sys.stderr,
    )

needs_libclang = unittest.skipUnless(
    semantic_rename.bindings_available(), "libclang's Python bindings (clang.cindex) are not installed"
)

ISOCKET_HPP = """\
#pragma once
namespace FastCache
{
struct ISocket
{
    int Read(char* data, int size);
    int Write(char const* data, int size);
};
}
"""

OTHER_HPP = """\
#pragma once
namespace Other
{
struct Reader
{
    int Read(char* data, int size);
    int Write(char const* data, int size);
};
}
"""

MAIN_CPP = """\
#include "isocket.hpp"
#include "other.hpp"

int use(FastCache::ISocket& sock, Other::Reader& other)
{
    char buffer[8];
    return sock.Read(buffer, 8) + other.Read(buffer, 8);
}
"""


def write_fixture(root: Path, translation_units: dict[str, str]) -> Path:
    """Writes the two headers, the given translation units, and a compile database over them."""
    (root / "isocket.hpp").write_text(ISOCKET_HPP, encoding="utf-8")
    (root / "other.hpp").write_text(OTHER_HPP, encoding="utf-8")
    entries = []
    for name, text in translation_units.items():
        (root / name).write_text(text, encoding="utf-8")
        entries.append({"directory": str(root), "file": name, "command": f"clang++ -std=c++20 -c {name}"})
    database = root / f"compile_commands.{len(translation_units)}.json"
    database.write_text(json.dumps(entries), encoding="utf-8")
    return database


READ_ROW = semantic_rename.Row(
    kind="member",
    source="Read",
    target="read",
    profiles=("fastcached",),
    apply="semantic",
    scope="FastCache::ISocket",
)


@needs_libclang
class ItRenamesByDeclarationNotBySpelling(unittest.TestCase):
    def test_only_the_call_on_the_declaring_class_is_renamed(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            database = write_fixture(root, {"main.cpp": MAIN_CPP})
            edits = semantic_rename.collect_edits([database], [root / "isocket.hpp"], [READ_ROW])
            with redirect_stdout(io.StringIO()):
                semantic_rename.apply_edits(edits)
            result = (root / "main.cpp").read_text(encoding="utf-8")
            self.assertIn("sock.read(buffer, 8)", result)
            self.assertIn("other.Read(buffer, 8)", result)

    def test_the_declaration_itself_is_renamed_when_it_is_under_the_decl_paths(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            database = write_fixture(root, {"main.cpp": MAIN_CPP})
            edits = semantic_rename.collect_edits([database], [root / "isocket.hpp"], [READ_ROW])
            with redirect_stdout(io.StringIO()):
                semantic_rename.apply_edits(edits)
            renamed = (root / "isocket.hpp").read_text(encoding="utf-8")
            self.assertIn("int read(char* data, int size);", renamed)
            self.assertIn("int Read(char* data, int size);", (root / "other.hpp").read_text(encoding="utf-8"))

    def test_a_row_whose_scope_matches_nothing_edits_nothing(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            database = write_fixture(root, {"main.cpp": MAIN_CPP})
            row = semantic_rename.Row(
                kind="member",
                source="Read",
                target="read",
                profiles=("fastcached",),
                apply="semantic",
                scope="FastCache::IListener",
            )
            self.assertEqual(semantic_rename.collect_edits([database], [root / "isocket.hpp"], [row]), {})


@needs_libclang
class ItUnionsSeveralCompileDatabases(unittest.TestCase):
    """fastcached's Windows and Linux builds see different files; one database alone misses half."""

    WINDOWS_CPP = "int onWindows(FastCache::ISocket& sock) { char b[4]; return sock.Read(b, 4); }\n"
    LINUX_CPP = "int onLinux(FastCache::ISocket& sock) { char b[4]; return sock.Read(b, 4); }\n"

    def test_edits_from_both_databases_are_applied(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            header = '#include "isocket.hpp"\n'
            windows = write_fixture(root, {"windows.cpp": header + self.WINDOWS_CPP})
            linux = write_fixture(
                root,
                {"linux.cpp": header + self.LINUX_CPP, "extra.cpp": header + "int unused() {}\n"},
            )
            edits = semantic_rename.collect_edits([windows, linux], [root / "isocket.hpp"], [READ_ROW])
            with redirect_stdout(io.StringIO()):
                semantic_rename.apply_edits(edits)
            self.assertIn("sock.read(b, 4)", (root / "windows.cpp").read_text(encoding="utf-8"))
            self.assertIn("sock.read(b, 4)", (root / "linux.cpp").read_text(encoding="utf-8"))

    def test_the_same_edit_seen_twice_is_applied_once(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            shared = '#include "isocket.hpp"\n' + self.WINDOWS_CPP
            first = write_fixture(root, {"windows.cpp": shared})
            second = write_fixture(root, {"windows.cpp": shared, "extra.cpp": '#include "isocket.hpp"\n'})
            edits = semantic_rename.collect_edits([first, second], [root / "isocket.hpp"], [READ_ROW])
            offsets = [edit.offset for edit in edits[root / "windows.cpp"]]
            self.assertEqual(len(offsets), len(set(offsets)), f"duplicated edits at {offsets}")
            with redirect_stdout(io.StringIO()):
                semantic_rename.apply_edits(edits)
            self.assertIn("sock.read(b, 4)", (root / "windows.cpp").read_text(encoding="utf-8"))


@needs_libclang
class EditsAreAppliedBackToFront(unittest.TestCase):
    """A replacement that is longer than what it replaces invalidates every offset after it."""

    def test_two_edits_on_one_line_both_land(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            source = (
                '#include "isocket.hpp"\n'
                "int use(FastCache::ISocket& a, FastCache::ISocket& b)\n"
                "{\n"
                "    char d[2];\n"
                "    return a.Write(d, 2) + b.Write(d, 2);\n"
                "}\n"
            )
            database = write_fixture(root, {"main.cpp": source})
            row = semantic_rename.Row(
                kind="member",
                source="Write",
                target="writeSome",
                profiles=("fastcached",),
                apply="semantic",
                scope="FastCache::ISocket",
            )
            edits = semantic_rename.collect_edits([database], [root / "isocket.hpp"], [row])
            with redirect_stdout(io.StringIO()):
                semantic_rename.apply_edits(edits)
            renamed = (root / "main.cpp").read_text("utf-8")
            self.assertIn("return a.writeSome(d, 2) + b.writeSome(d, 2);", renamed)


OVERRIDE_HPP = """\
#pragma once
namespace FastCache
{
struct ISocket
{
    virtual int Read(char* data, int size) = 0;
    virtual ~ISocket() = default;
};
struct TcpSocket: ISocket
{
    int Read(char* data, int size) override;
};
struct TlsSocket: TcpSocket
{
    int Read(char* data, int size) override;
};
}
"""

OVERRIDE_CPP = """\
#include "override.hpp"

int viaBase(FastCache::ISocket& s)
{
    char b[4];
    return s.Read(b, 4);
}

int viaDerived(FastCache::TcpSocket& s)
{
    char b[4];
    return s.Read(b, 4);
}

int viaGrandchild(FastCache::TlsSocket& s)
{
    char b[4];
    return s.Read(b, 4);
}
"""


@needs_libclang
class ItFollowsVirtualOverrides(unittest.TestCase):
    """An interface's method is renamed across the hierarchy that implements it, or not at all.

    Renaming `ISocket::Read` and leaving `TcpSocket::Read` **override** behind produces an override
    of a virtual that no longer exists -- a hard compile error -- plus every call site typed to a
    concrete socket. That is the exact job Task C4 commissioned this pass for, and the old fixture
    of two *unrelated* classes could not see it (controller ruling R91).
    """

    ROW = semantic_rename.Row(
        kind="member",
        source="Read",
        target="read",
        profiles=("fastcached",),
        apply="semantic",
        scope="FastCache::ISocket",
    )

    def collect(self, root: Path):
        (root / "override.hpp").write_text(OVERRIDE_HPP, encoding="utf-8")
        (root / "main.cpp").write_text(OVERRIDE_CPP, encoding="utf-8")
        database = root / "compile_commands.json"
        database.write_text(
            json.dumps(
                [{"directory": str(root), "file": "main.cpp", "command": "clang++ -std=c++20 -c main.cpp"}]
            ),
            encoding="utf-8",
        )
        edits = semantic_rename.collect_edits([database], [root / "override.hpp"], [self.ROW])
        with redirect_stdout(io.StringIO()):
            semantic_rename.apply_edits(edits)

    def test_the_override_declarations_are_renamed_with_the_base(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.collect(root)
            header = (root / "override.hpp").read_text(encoding="utf-8")
            self.assertEqual(header.count("int read(char* data, int size)"), 3, header)
            self.assertNotIn("Read", header)

    def test_a_call_typed_to_the_derived_class_is_renamed(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.collect(root)
            body = (root / "main.cpp").read_text(encoding="utf-8")
            self.assertEqual(body.count("s.read(b, 4)"), 3, body)
            self.assertNotIn("s.Read(", body)

    def test_an_unrelated_class_with_the_same_member_is_still_untouched(self) -> None:
        # The axis the old fixture did test, which must keep holding now that overrides are followed.
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "other.hpp").write_text(OTHER_HPP, encoding="utf-8")
            self.collect(root)
            self.assertIn("int Read(char* data, int size);", (root / "other.hpp").read_text(encoding="utf-8"))


@needs_libclang
class ItRefusesToActOnATranslationUnitItCouldNotParse(unittest.TestCase):
    """A stale compile database used to yield a confident partial rename and say nothing (I6)."""

    BROKEN_CPP = '#include "isocket.hpp"\n#include "gone.hpp"\nint use(FastCache::ISocket& s) { return s.Read(nullptr, 0); }\n'

    def database(self, root: Path) -> Path:
        (root / "isocket.hpp").write_text(ISOCKET_HPP, encoding="utf-8")
        (root / "broken.cpp").write_text(self.BROKEN_CPP, encoding="utf-8")
        path = root / "compile_commands.json"
        path.write_text(
            json.dumps(
                [
                    {
                        "directory": str(root),
                        "file": "broken.cpp",
                        "command": "clang++ -std=c++20 -c broken.cpp",
                    }
                ]
            ),
            encoding="utf-8",
        )
        return path

    def test_a_parse_error_refuses_the_run(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(semantic_rename.ParseError) as raised:
                semantic_rename.collect_edits([self.database(root)], [root / "isocket.hpp"], [READ_ROW])
            self.assertIn("broken.cpp", str(raised.exception))
            self.assertIn("gone.hpp", str(raised.exception))

    def test_the_refusal_can_be_overridden_deliberately(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            edits = semantic_rename.collect_edits(
                [self.database(root)], [root / "isocket.hpp"], [READ_ROW], allow_parse_errors=True
            )
            self.assertTrue(edits, "with errors allowed the pass still does what it can")


class ItRefusesToRunWithoutTheBindings(unittest.TestCase):
    """The one case that must not skip: a missing dependency is reported, never worked around."""

    def test_collecting_edits_without_the_bindings_raises(self) -> None:
        # M7: `assertIsInstance(bindings_available(), bool)` could not fail, so the class docstring
        # promised something nothing tested. These two run whether or not libclang is installed.
        with unittest.mock.patch.object(semantic_rename, "bindings_available", lambda: False):
            with self.assertRaises(semantic_rename.TableError) as raised:
                semantic_rename.collect_edits([Path("nothing.json")], [Path(".")], [READ_ROW])
            self.assertIn("libclang", str(raised.exception))

    def test_main_refuses_and_says_how_to_install_them(self) -> None:
        output = io.StringIO()
        with unittest.mock.patch.object(semantic_rename, "bindings_available", lambda: False):
            with contextlib.redirect_stdout(output):
                status = semantic_rename.main(
                    ["--profile", "fastcached", "--compile-db", "x.json", "--decl-paths", "src"]
                )
        self.assertEqual(status, 1)
        self.assertIn("pip install libclang", output.getvalue())
        self.assertIn("nothing was rewritten", output.getvalue())

    def test_the_table_carries_the_semantic_rows_this_pass_owns(self) -> None:
        import renames

        table = renames.load(Path(__file__).resolve().parent / "renames.json")
        semantic = [row for row in table.rows if row.apply == "semantic"]
        self.assertTrue(semantic, "no row is marked for the semantic pass")
        self.assertTrue(all(row.scope for row in semantic))
        rows = [(r.source, r.target, r.scope) for r in semantic]
        self.assertIn(("Read", "read", "FastCache::ISocket"), rows)


if __name__ == "__main__":
    unittest.main()
