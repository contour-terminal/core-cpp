# SPDX-License-Identifier: Apache-2.0
"""What the libclang pass must prove: it renames a member by its declaration, not by its spelling.

libclang's Python bindings are optional, so these cases skip where they are missing -- loudly, by
name, and never silently (Ruling R69). The `style` CI job installs them and refuses a skip, so the
pass is tested for real on every push.
"""

from __future__ import annotations

import io
import json
import sys
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from tempfile import TemporaryDirectory

import semantic_rename

if not semantic_rename.bindingsAvailable():
    print(
        "semantic_rename_test: SKIPPING every case -- python -m pip install libclang "
        f"(no clang.cindex for {sys.executable})",
        file=sys.stderr,
    )

needsLibclang = unittest.skipUnless(
    semantic_rename.bindingsAvailable(), "libclang's Python bindings (clang.cindex) are not installed"
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


def writeFixture(root: Path, translationUnits: dict[str, str]) -> Path:
    """Writes the two headers, the given translation units, and a compile database over them."""
    (root / "isocket.hpp").write_text(ISOCKET_HPP, encoding="utf-8")
    (root / "other.hpp").write_text(OTHER_HPP, encoding="utf-8")
    entries = []
    for name, text in translationUnits.items():
        (root / name).write_text(text, encoding="utf-8")
        entries.append({"directory": str(root), "file": name, "command": f"clang++ -std=c++20 -c {name}"})
    database = root / f"compile_commands.{len(translationUnits)}.json"
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


@needsLibclang
class ItRenamesByDeclarationNotBySpelling(unittest.TestCase):
    def test_only_the_call_on_the_declaring_class_is_renamed(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            database = writeFixture(root, {"main.cpp": MAIN_CPP})
            edits = semantic_rename.collectEdits([database], [root / "isocket.hpp"], [READ_ROW])
            with redirect_stdout(io.StringIO()):
                semantic_rename.applyEdits(edits)
            result = (root / "main.cpp").read_text(encoding="utf-8")
            self.assertIn("sock.read(buffer, 8)", result)
            self.assertIn("other.Read(buffer, 8)", result)

    def test_the_declaration_itself_is_renamed_when_it_is_under_the_decl_paths(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            database = writeFixture(root, {"main.cpp": MAIN_CPP})
            edits = semantic_rename.collectEdits([database], [root / "isocket.hpp"], [READ_ROW])
            with redirect_stdout(io.StringIO()):
                semantic_rename.applyEdits(edits)
            renamed = (root / "isocket.hpp").read_text(encoding="utf-8")
            self.assertIn("int read(char* data, int size);", renamed)
            self.assertIn("int Read(char* data, int size);", (root / "other.hpp").read_text(encoding="utf-8"))

    def test_a_row_whose_scope_matches_nothing_edits_nothing(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            database = writeFixture(root, {"main.cpp": MAIN_CPP})
            row = semantic_rename.Row(
                kind="member",
                source="Read",
                target="read",
                profiles=("fastcached",),
                apply="semantic",
                scope="FastCache::IListener",
            )
            self.assertEqual(semantic_rename.collectEdits([database], [root / "isocket.hpp"], [row]), {})


@needsLibclang
class ItUnionsSeveralCompileDatabases(unittest.TestCase):
    """fastcached's Windows and Linux builds see different files; one database alone misses half."""

    WINDOWS_CPP = "int onWindows(FastCache::ISocket& sock) { char b[4]; return sock.Read(b, 4); }\n"
    LINUX_CPP = "int onLinux(FastCache::ISocket& sock) { char b[4]; return sock.Read(b, 4); }\n"

    def test_edits_from_both_databases_are_applied(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            header = '#include "isocket.hpp"\n'
            windows = writeFixture(root, {"windows.cpp": header + self.WINDOWS_CPP})
            linux = writeFixture(
                root,
                {"linux.cpp": header + self.LINUX_CPP, "extra.cpp": header + "int unused() {}\n"},
            )
            edits = semantic_rename.collectEdits([windows, linux], [root / "isocket.hpp"], [READ_ROW])
            with redirect_stdout(io.StringIO()):
                semantic_rename.applyEdits(edits)
            self.assertIn("sock.read(b, 4)", (root / "windows.cpp").read_text(encoding="utf-8"))
            self.assertIn("sock.read(b, 4)", (root / "linux.cpp").read_text(encoding="utf-8"))

    def test_the_same_edit_seen_twice_is_applied_once(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            shared = '#include "isocket.hpp"\n' + self.WINDOWS_CPP
            first = writeFixture(root, {"windows.cpp": shared})
            second = writeFixture(root, {"windows.cpp": shared, "extra.cpp": '#include "isocket.hpp"\n'})
            edits = semantic_rename.collectEdits([first, second], [root / "isocket.hpp"], [READ_ROW])
            offsets = [edit.offset for edit in edits[root / "windows.cpp"]]
            self.assertEqual(len(offsets), len(set(offsets)), f"duplicated edits at {offsets}")
            with redirect_stdout(io.StringIO()):
                semantic_rename.applyEdits(edits)
            self.assertIn("sock.read(b, 4)", (root / "windows.cpp").read_text(encoding="utf-8"))


@needsLibclang
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
            database = writeFixture(root, {"main.cpp": source})
            row = semantic_rename.Row(
                kind="member",
                source="Write",
                target="writeSome",
                profiles=("fastcached",),
                apply="semantic",
                scope="FastCache::ISocket",
            )
            edits = semantic_rename.collectEdits([database], [root / "isocket.hpp"], [row])
            with redirect_stdout(io.StringIO()):
                semantic_rename.applyEdits(edits)
            renamed = (root / "main.cpp").read_text("utf-8")
            self.assertIn("return a.writeSome(d, 2) + b.writeSome(d, 2);", renamed)


class ItRefusesToRunWithoutTheBindings(unittest.TestCase):
    """The one case that must not skip: a missing dependency is reported, never worked around."""

    def test_the_availability_probe_answers(self) -> None:
        self.assertIsInstance(semantic_rename.bindingsAvailable(), bool)

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
