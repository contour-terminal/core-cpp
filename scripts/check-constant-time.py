#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Holds core::constantTimeEquals to its guarantee in the code this build generated: its compare
loop branches on nothing but its index.

    python scripts/check-constant-time.py --objdump <objdump> --objects "<obj>|<obj>|..."

It disassembles the object that defines core::constantTimeEquals (ConstantTime.cpp's, picked out
of the base library's objects), finds each loop of that function -- a conditional branch back to an
earlier address -- and refuses any loop whose body holds another conditional branch, or holds no
call (the value barrier): either is a compiler that found a way to stop at the first difference.
The branches before the loop (the length check) and after it are allowed; a length is public.

**Toolchain dependence, said rather than hidden.** It reads GNU objdump's AT&T x86-64 and AArch64
listings, which is what GCC and Clang builds on Linux, macOS (with binutils) and the BSDs produce;
it fails closed: no objdump, no ConstantTime.cpp object, an object objdump cannot read (LTO
bitcode among them, which no preset here builds), or no core::constantTimeEquals in it is a
failure, because each would otherwise read as a pass. The one skip (77) is an architecture whose
branch mnemonics it has no row for. The property is one of optimized code: a Debug build's bounds
assertions are branches in the loop, so the registration runs it only for an optimized build type.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

SKIP = 77
FUNCTION = "core::constantTimeEquals("

# One row per architecture: what a conditional branch, an unconditional one and a call are.
ARCHITECTURES = {
    "x86-64": {
        "conditional": re.compile(r"^j(?!mp)[a-z]+$"),
        "call": re.compile(r"^call[qw]?$"),
    },
    "aarch64": {
        "conditional": re.compile(r"^(b\.[a-z]+|cbn?z|tbn?z)$"),
        "call": re.compile(r"^blr?$"),
    },
}

INSTRUCTION = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s)*\s*([a-z][a-z0-9.]*)\s*(.*)$")
TARGET = re.compile(r"^(?:0x)?([0-9a-f]+)\b")


def architecture_of(listing: str) -> str | None:
    """:return: the row of the listing's object format, or None for one without a row."""
    if "x86-64" in listing:
        return "x86-64"
    if "aarch64" in listing or "arm64" in listing:
        return "aarch64"
    return None


def function_body(listing: str) -> list[tuple[int, str, str]]:
    """:return: (address, mnemonic, operands) of each instruction of constantTimeEquals."""
    body: list[tuple[int, str, str]] = []
    inside = False
    for line in listing.splitlines():
        if re.match(r"^[0-9a-f]+ <", line):
            inside = FUNCTION in line
            continue
        if not inside:
            continue
        match = INSTRUCTION.match(line)
        if match:
            body.append((int(match.group(1), 16), match.group(2), match.group(3)))
    return body


def check_listing(listing: str) -> tuple[int, list[str]]:
    """Judges one objdump listing.

    :return: (exit status, what it found): 0 and the loops it accepted, 1 and the refusals, or
             77 and why it could not judge.
    """
    architecture = architecture_of(listing)
    if architecture is None:
        return SKIP, ["no branch table for this object's architecture"]
    row = ARCHITECTURES[architecture]
    body = function_body(listing)
    if not body:
        return 1, [f"no {FUNCTION}...) in the listing: nothing was judged"]
    loops = []
    for address, mnemonic, operands in body:
        target = TARGET.match(operands)
        if row["conditional"].match(mnemonic) and target and int(target.group(1), 16) <= address:
            loops.append((int(target.group(1), 16), address))
    if not loops:
        return 1, ["no loop in constantTimeEquals: the compare was turned into something else"]
    problems = []
    for start, end in loops:
        inner = [(a, m) for a, m, _ in body if start <= a < end]
        branches = [f"{a:x}: {m}" for a, m in inner if row["conditional"].match(m)]
        calls = [m for _, m in inner if row["call"].match(m)]
        if branches:
            problems.append(f"loop {start:x}..{end:x} branches on more than its index: {', '.join(branches)}")
        if not calls:
            problems.append(f"loop {start:x}..{end:x} calls no value barrier")
    if problems:
        return 1, problems
    return 0, [
        f"loop {start:x}..{end:x}: one back edge, a barrier call, no other branch" for start, end in loops
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--objdump", default="")
    parser.add_argument("--objects", default="", help="the base library's objects, '|'-separated")
    arguments = parser.parse_args()
    objects = [Path(o) for o in arguments.objects.split("|") if Path(o).name.startswith("ConstantTime.cpp.")]
    if not arguments.objdump or not Path(arguments.objdump).exists():
        print(f"check-constant-time: REFUSED -- no objdump in this toolchain ({arguments.objdump!r})")
        return 1
    if not objects:
        print("check-constant-time: REFUSED -- no ConstantTime.cpp object among the base library's")
        return 1
    disassembly = subprocess.run(
        [arguments.objdump, "-d", "-C", "--no-show-raw-insn", str(objects[0])],
        capture_output=True,
        text=True,
        check=False,
    )
    if disassembly.returncode != 0:
        print(
            f"check-constant-time: REFUSED -- objdump could not read {objects[0]} (LTO bitcode?): "
            f"{disassembly.stderr.strip()}"
        )
        return 1
    status, findings = check_listing(disassembly.stdout)
    verdict = {0: "ok", 1: "REFUSED", SKIP: "SKIPPED"}[status]
    for finding in findings:
        print(f"check-constant-time: {verdict} -- {finding}")
    return status


if __name__ == "__main__":
    sys.exit(main())
