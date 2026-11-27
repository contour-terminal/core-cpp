#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Proves that check-constant-time.py accepts the loop the compilers emit and refuses an early exit.

    python scripts/check-constant-time-selftest.py       # quiet unless something fails
    python scripts/check-constant-time-selftest.py -v    # one line per case

The accepted listings are the gcc-release and clang-release x86-64 code of core::constantTimeEquals
as of v0.24.0, trimmed; each refusal plants one defect into one of them: a branch on the data inside
the loop, a loop without its barrier call, no loop at all. An architecture without a row is a skip.
"""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path

CHECKER = Path(__file__).resolve().parent / "check-constant-time.py"


def load_checker():
    """:return: check-constant-time.py, loaded by path because its name has hyphens."""
    spec = importlib.util.spec_from_file_location("check_constant_time", CHECKER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CHECK = load_checker()

HEAD = "ConstantTime.cpp.o:     file format elf64-x86-64\n\nDisassembly of section .text:\n\n"
SIGNATURE = "core::constantTimeEquals(std::span<std::byte const, 18446744073709551615ul>, std::span<std::byte const, 18446744073709551615ul>)"

CLANG = (
    HEAD
    + f"""0000000000000000 <{SIGNATURE}>:
   0:	cmp    %rcx,%rsi
   3:	jne    3d <x+0x3d>
  10:	test   %rsi,%rsi
  13:	je     40 <x+0x40>
  20:	movzbl (%rbx,%r12,1),%ecx
  25:	xor    (%r15,%r12,1),%cl
  29:	or     %al,%cl
  2e:	call   33 <x+0x33>
  33:	inc    %r12
  36:	cmp    %r12,%r14
  39:	jne    20 <x+0x20>
  3b:	jmp    42 <x+0x42>
  45:	call   4a <x+0x4a>
  4c:	sete   %al
  5a:	ret

0000000000000060 <core::secureZero(std::span<std::byte, 18446744073709551615ul>)>:
  60:	test   %rsi,%rsi
  80:	movb   $0x0,(%rdi,%rcx,1)
  ab:	cmp    %rcx,%rsi
  ae:	jne    80 <y+0x20>
"""
)

GCC = (
    HEAD
    + f"""0000000000000000 <{SIGNATURE}>:
   2:	cmp    %rcx,%rsi
   5:	jne    70 <x+0x70>
  30:	movzbl (%rbx),%edi
  33:	xor    (%r12),%dil
  3f:	or     %eax,%edi
  45:	call   4a <x+0x4a>
  4a:	cmp    %rbp,%rbx
  4d:	jne    30 <x+0x30>
  60:	call   65 <x+0x65>
  67:	sete   %al
  6e:	ret
"""
)


class ConstantTimeTest(unittest.TestCase):
    def test_the_compilers_loops_are_accepted(self):
        for name, listing in (("clang", CLANG), ("gcc", GCC)):
            with self.subTest(name):
                status, findings = CHECK.check_listing(listing)
                self.assertEqual(status, 0, findings)

    def test_a_branch_on_the_data_in_the_loop_is_refused(self):
        early = CLANG.replace(
            "  29:	or     %al,%cl\n", "  29:	or     %al,%cl\n  2b:	jne    3d <x+0x3d>\n"
        )
        status, findings = CHECK.check_listing(early)
        self.assertEqual(status, 1)
        self.assertIn("branches on more than its index", findings[0])

    def test_a_loop_without_its_barrier_is_refused(self):
        bare = GCC.replace("  45:	call   4a <x+0x4a>\n", "")
        status, findings = CHECK.check_listing(bare)
        self.assertEqual(status, 1)
        self.assertIn("calls no value barrier", findings[0])

    def test_no_loop_is_refused(self):
        unrolled = GCC.replace("  4d:	jne    30 <x+0x30>\n", "")
        status, _ = CHECK.check_listing(unrolled)
        self.assertEqual(status, 1)

    def test_secure_zeros_loop_is_not_judged(self):
        # Its back edge is another function's: only constantTimeEquals is held to the rule.
        status, findings = CHECK.check_listing(CLANG)
        self.assertEqual(len(findings), 1, findings)

    def test_an_aarch64_loop_is_judged_by_its_own_row(self):
        listing = (
            HEAD.replace("elf64-x86-64", "elf64-littleaarch64")
            + f"""0000000000000000 <{SIGNATURE}>:
   0:	cmp	x1, x3
   4:	b.ne	40 <x+0x40>
  10:	ldrb	w8, [x2], #1
  14:	eor	w0, w0, w8
  18:	bl	0 <core::detail::valueBarrier(unsigned char)>
  1c:	subs	x1, x1, #0x1
  20:	b.ne	10 <x+0x10>
"""
        )
        self.assertEqual(CHECK.check_listing(listing)[0], 0)
        early = listing.replace("  18:\tbl", "  16:\tcbnz\tw8, 40 <x+0x40>\n  18:\tbl")
        self.assertEqual(CHECK.check_listing(early)[0], 1)

    def test_a_listing_without_the_function_is_refused(self):
        status, findings = CHECK.check_listing(
            HEAD + "0000000000000000 <core::secureZero(std::span<std::byte>)>:\n"
        )
        self.assertEqual(status, 1)
        self.assertIn("nothing was judged", findings[0])

    def test_a_missing_objdump_object_or_unreadable_object_is_refused(self):
        for arguments in (
            ["--objdump=/nonexistent/objdump", "--objects=ConstantTime.cpp.o"],
            [f"--objdump={sys.executable}", "--objects=Other.cpp.o|More.cpp.o"],
            [f"--objdump={sys.executable}", "--objects=/nonexistent/ConstantTime.cpp.o"],
        ):
            with self.subTest(arguments):
                run = subprocess.run(
                    [sys.executable, str(CHECKER), *arguments], capture_output=True, text=True, check=False
                )
                self.assertEqual(run.returncode, 1, run.stdout)
                self.assertIn("REFUSED", run.stdout)

    def test_an_architecture_without_a_row_is_a_skip(self):
        status, _ = CHECK.check_listing(
            HEAD.replace("elf64-x86-64", "elf32-littleriscv") + CLANG[len(HEAD) :]
        )
        self.assertEqual(status, CHECK.SKIP)


if __name__ == "__main__":
    unittest.main()
