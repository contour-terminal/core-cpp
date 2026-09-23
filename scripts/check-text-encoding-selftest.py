#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Proves that check-text-encoding.py refuses what it claims to, and accepts what is clean.

    python scripts/check-text-encoding-selftest.py       # quiet unless something fails
    python scripts/check-text-encoding-selftest.py -v    # one line per case

Each refusal gets a file that plants exactly that defect, and one case plants text that is
non-ASCII and CORRECT -- an em-dash, an accented letter, a multiplication sign -- because a check
that refused every byte above 0x7F would pass every refusal here and fail every real document.
The instrument's own failure modes are cases too: a binary file is not read, an allowed file is
excused, an allow row that excuses nothing is refused, a scan that read nothing says so, and a
tree git cannot list is a skip rather than a traceback.
"""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

CHECKER = Path(__file__).resolve().parent / "check-text-encoding.py"


def load_checker():
    """:return: check-text-encoding.py, loaded by path because its name has hyphens."""
    spec = importlib.util.spec_from_file_location("check_text_encoding", CHECKER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CHECK = load_checker()

EM_DASH = chr(0x2014)
# What an em-dash's three UTF-8 bytes become when read as cp1252 and encoded again.
MOJIBAKE_EM_DASH = EM_DASH.encode("utf-8").decode("cp1252")


class TextEncodingTest(unittest.TestCase):
    def run_on(self, files: dict[str, bytes], allow: dict[str, str] | None = None) -> tuple[list[str], int]:
        """Writes files into a scratch tree and runs the check over them.

        :param files: path -> bytes.
        :param allow: the allow list to run with.
        :return: the check's (problems, files read).
        """
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for path, data in files.items():
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                (root / path).write_bytes(data)
            return CHECK.check(root, list(files), allow or {})

    def test_clean_non_ascii_text_is_accepted(self):
        text = f"# Title {EM_DASH} caf{chr(0xE9)}, 2{chr(0xD7)}3, Stra{chr(0xDF)}e, {chr(0xE4)}{chr(0xF6)}{chr(0xFC)}\n"
        problems, read = self.run_on({"docs/page.md": text.encode("utf-8")})
        self.assertEqual(problems, [])
        self.assertEqual(read, 1)

    def test_invalid_utf8_is_refused_with_its_line(self):
        problems, _ = self.run_on({"src/a.cpp": b"int a;\nint b; // \xff\n"})
        self.assertEqual(len(problems), 1)
        self.assertIn("src/a.cpp:2: [invalid]", problems[0])

    def test_a_replacement_character_is_refused(self):
        problems, _ = self.run_on({"src/b.cpp": f"char32_t c = U'{chr(0xFFFD)}';\n".encode("utf-8")})
        self.assertEqual(len(problems), 1)
        self.assertIn("src/b.cpp:1: [replacement]", problems[0])

    def test_a_double_encoded_em_dash_is_refused(self):
        problems, _ = self.run_on({"docs/c.md": f"one\nit is {MOJIBAKE_EM_DASH} broken\n".encode("utf-8")})
        self.assertEqual(len(problems), 1)
        self.assertIn("docs/c.md:2: [double]", problems[0])

    def test_a_double_encoded_latin_letter_is_refused(self):
        mangled = f"caf{chr(0xE9)}".encode("utf-8").decode("latin-1")
        problems, _ = self.run_on({"docs/d.md": mangled.encode("utf-8")})
        self.assertEqual(len(problems), 1)
        self.assertIn("[double]", problems[0])

    def test_a_binary_file_is_not_read(self):
        problems, read = self.run_on({"assets/e.png": b"\x89PNG\x00\xff\xfe"})
        self.assertEqual(problems, [])
        self.assertEqual(read, 0)

    def test_an_allowed_file_is_excused_and_a_stale_row_is_not(self):
        files = {"records/f.md": f"quoting {MOJIBAKE_EM_DASH}\n".encode("utf-8"), "g.md": b"clean\n"}
        problems, _ = self.run_on(files, {"records/f.md": "quotes it", "g.md": "excuses nothing"})
        self.assertEqual(len(problems), 1)
        self.assertIn("g.md: [stale-allow]", problems[0])

    def test_a_tree_git_cannot_list_is_a_skip_that_says_why(self):
        # A release tarball has no .git. The check cannot know what is tracked there, and a
        # traceback reads as a defect in the tree; exit 77 is ctest's "skipped", with the reason.
        with tempfile.TemporaryDirectory() as scratch:
            (Path(scratch) / "a.md").write_bytes(b"clean\n")
            run = subprocess.run(
                [sys.executable, str(CHECKER), "--root", scratch], capture_output=True, text=True, check=False
            )
        self.assertEqual(run.returncode, 77, run.stdout + run.stderr)
        self.assertIn("SKIPPED", run.stdout)
        self.assertIn("git", run.stdout)
        self.assertNotIn("Traceback", run.stderr)

    def test_the_real_tree_is_read_rather_than_skipped(self):
        # A scan that read nothing would report nothing; the real tree has hundreds of text files.
        root = CHECKER.parent.parent
        try:
            files = CHECK.tracked_files(root)
        except (OSError, subprocess.CalledProcessError) as error:
            self.skipTest(f"no git checkout to list: {error}")
        _, read = CHECK.check(root, files, CHECK.ALLOW)
        self.assertGreater(read, 100)


if __name__ == "__main__":
    verbosity = 2 if "-v" in sys.argv else 1
    result = unittest.TextTestRunner(verbosity=verbosity).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(TextEncodingTest)
    )
    sys.exit(0 if result.wasSuccessful() else 1)
