#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Refuses a tracked text file that is not clean UTF-8, or that carries the scar of a bad repair.

    python scripts/check-text-encoding.py          # every tracked file
    python scripts/check-text-encoding.py --root <dir>

Nothing else in this tree reads bytes as TEXT. clang-format reformats, clang-tidy reads the
preprocessed translation unit, `mkdocs build --strict` checks links and the nav, and the vendoring
tool refuses CR bytes and symlinks. So a script that mangles UTF-8 corrupts the tree silently --
and one did: a repair that decoded a file as cp1252 and re-encoded it as UTF-8 turned 72 em-dashes
in two other lanes' files into U+00E2 U+20AC U+201D, and every gate stayed green because that is valid
UTF-8 (core-cpp#40). Markdown is the worst case: the docs site renders the garbage with no warning.

Three refusals, each naming the file and the line:

    invalid       the bytes do not decode as UTF-8.
    replacement   U+FFFD is present: something already decoded bytes it could not read and
                  wrote down that it had lost them. A source that means the character spells it
                  as an escape (`U'\\uFFFD'`), which says so where a literal cannot.
    double        a UTF-8 sequence that was read as cp1252 or Latin-1 and encoded again: a lead
                  character U+00C2..U+00F4 followed by a continuation byte's image -- U+0080..U+00BF,
                  or the cp1252 characters that stand for 0x80..0x9F. A mangled em-dash is U+00E2 U+20AC U+201D.

A file with a NUL byte is binary and is not read. The scan fails CLOSED: no file read, or no
tracked file at all, is the instrument broken rather than the tree clean.

ALLOW lists a file that must carry one of these for a stated reason, and a row that no longer
matches anything is refused as stale, so the list cannot outlive what it excuses.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent

# The cp1252 characters that a byte 0x80..0x9F becomes when UTF-8 is mis-read as cp1252. Spelled as
# code points rather than as characters, so that no formatter or editor can turn them into the text
# this file exists to refuse -- and so that this file passes its own check.
_CP1252_HIGH = "".join(
    map(
        chr,
        (
            0x20AC,
            0x201A,
            0x0192,
            0x201E,
            0x2026,
            0x2020,
            0x2021,
            0x02C6,
            0x2030,
            0x0160,
            0x2039,
            0x0152,
            0x017D,
        )
        + (0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153)
        + (0x017E, 0x0178),
    )
)
_LEAD = chr(0x00C2) + "-" + chr(0x00F4)
_CONTINUATION = chr(0x0080) + "-" + chr(0x00BF)
DOUBLE_ENCODED = re.compile(f"[{_LEAD}][{_CONTINUATION}{_CP1252_HIGH}]")
REPLACEMENT = chr(0xFFFD)

# path -> reason. Each row must still match a refusal, or it is refused as stale.
ALLOW: dict[str, str] = {
    ".superpowers/sdd/2026-09-18-core-cpp/task-B6-report.md": "quotes the double-encoded em-dash that core-cpp#40 is about, verbatim, as the record of what happened",
}


def tracked_files(root: Path) -> list[str]:
    """:return: every file git tracks under root, as posix paths relative to it."""
    listing = subprocess.run(["git", "-C", str(root), "ls-files", "-z"], capture_output=True, check=True)
    return [path for path in listing.stdout.decode("utf-8").split("\0") if path]


def scan_text(data: bytes) -> list[tuple[str, int, str]]:
    """Scans one file's bytes.

    :param data: the file's contents.
    :return: (kind, line, detail) per refusal; empty for a clean file or a binary one.
    """
    if b"\0" in data:
        return []
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        line = data.count(b"\n", 0, error.start) + 1
        return [("invalid", line, f"byte 0x{data[error.start]:02x} does not decode as UTF-8")]
    found = []
    for index, line in enumerate(text.split("\n"), start=1):
        if REPLACEMENT in line:
            found.append(("replacement", index, "U+FFFD, a character something could not read"))
        match = DOUBLE_ENCODED.search(line)
        if match:
            found.append(("double", index, f"{match.group()!r} is UTF-8 read as cp1252 and encoded again"))
    return found


def check(root: Path, files: list[str], allow: dict[str, str]) -> tuple[list[str], int]:
    """Runs the check.

    :param root: the tree.
    :param files: the tracked paths to read.
    :param allow: path -> reason.
    :return: (problems, how many text files were read).
    """
    problems = []
    read = 0
    matched_allow = set()
    for path in files:
        full = root / path
        if not full.is_file():
            continue
        data = full.read_bytes()
        if b"\0" in data:
            continue
        read += 1
        refusals = scan_text(data)
        if refusals and path in allow:
            matched_allow.add(path)
            continue
        for kind, line, detail in refusals:
            problems.append(f"{path}:{line}: [{kind}] {detail}")
    for path in sorted(set(allow) - matched_allow):
        problems.append(
            f"{path}: [stale-allow] allowed ({allow[path]}), but it carries nothing to allow; drop the row"
        )
    return problems, read


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=REPOSITORY_ROOT)
    arguments = parser.parse_args()
    files = tracked_files(arguments.root)
    problems, read = check(arguments.root, files, ALLOW)
    if read == 0:
        print(
            f"check-text-encoding: read no text file among {len(files)} tracked -- the scan is broken, not the tree clean"
        )
        return 1
    for problem in problems:
        print(problem)
    if problems:
        print(f"check-text-encoding: {len(problems)} problem(s) in {read} text file(s)")
        return 1
    print(
        f"check-text-encoding: {read} text file(s), all clean UTF-8 with no replacement character or double encoding"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
