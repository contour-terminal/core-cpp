#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Every `tree-level` check has a `style` job step, and every such step covers a real one.

    python scripts/check-tree-level-coverage.py          # both directions, over this tree

Ruling R96 took the checks whose input is the source tree out of the 24 build jobs -- their answer
cannot differ between platforms, and one violated provenance row reporting as 22 failed jobs is
correct behaviour and unusable triage. CI's per-job `ctest` excludes the `tree-level` label and the
`style` job runs exactly those, once.

That arrangement has one hole, and it is the reason this file exists: **the correspondence is held
by hand.** A new `tree-level` check that nobody adds a `style` step for is excluded from every
build job and run by nothing -- it does not fail, it does not skip, it is simply absent, and its
absence looks exactly like a green run. That is the same shape as the two checks R96 itself nearly
deleted (`core-cpp.upstream-drift` and its self-test, registered in ctest by Task B12b and never
added to `style`).

So the link is made explicit rather than inferred. Each covering step carries a marker naming the
ctest it stands for:

    # covers: core-cpp.cmake-hygiene
    - name: CMake and C++ hygiene
      run: cmake -DROOT="$GITHUB_WORKSPACE" -P tests/cmake/check-cmake-hygiene.cmake

The step's command is NOT matched against the test's command. They differ on purpose -- the step
runs before anything is configured, so a drifted table is the first thing a pull request is told
rather than the twenty-second -- and a checker that compared command lines would forbid that.

BOTH DIRECTIONS ARE REFUSED, and the second is the one that rots quietly:

    a labelled test with no step        runs nowhere in CI
    a step covering an unknown test     the test was renamed or lost its label, and the step is
                                        now covering nothing while looking like it covers something

AND THE STYLE JOB ITSELF MUST GATE (core-cpp#39). Every tree-level check runs there and nowhere
else, so if `style` is dropped from `ci-ok`'s `needs:` -- or `ci-ok` is renamed -- all of them stop
gating and `ci-ok`, the one required check, still goes green. An absent required check is
indistinguishable from a passing one at the branch-protection layer, so this refuses a workflow
whose `ci-ok` job does not exist or does not need `style`. It runs IN the style job, which makes
it self-referential in the right direction: the job proves it is still wired in.

Exit status: 0 when the two sets agree, 1 when they do not, naming every difference and what to do
about it.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent

# The label R96 defined. A check carries it when its input is the source tree.
TREE_LEVEL = "tree-level"

# What a covering step declares. One name per marker; a step may carry several.
#
# The line must be NOTHING BUT the marker. An unanchored pattern also matches prose that merely
# mentions the syntax -- the step below this file's own comment about `# covers:` markers was read
# as a marker covering a backtick, which is a gate failing on a true negative (Ruling R81).
COVERS = re.compile(r"^[ \t]*#[ \t]*covers:[ \t]*(\S+)[ \t]*$", re.M)

# set_tests_properties(<names...> PROPERTIES <...>). Non-greedy to the closing parenthesis, which
# is safe only while no property value contains one -- verify_parse() below is what makes that
# assumption fail loudly instead of silently dropping a name.
PROPERTIES_BLOCK = re.compile(r"set_tests_properties\((.*?)\)", re.S)


def labelled_tests(tests_cmake: str) -> set[str]:
    """The test names carrying the tree-level label in @p tests_cmake."""
    names: set[str] = set()
    blocks = 0
    for match in PROPERTIES_BLOCK.finditer(tests_cmake):
        body = match.group(1)
        if "PROPERTIES" not in body:
            continue
        subjects, properties = body.split("PROPERTIES", 1)
        if TREE_LEVEL not in properties:
            continue
        blocks += 1
        names.update(subjects.split())
    verify_parse(tests_cmake, blocks)
    return names


def verify_parse(tests_cmake: str, blocks: int) -> None:
    """Refuses a parse that saw fewer label sites than the file contains.

    PROPERTIES_BLOCK stops at the first `)`, so a property value containing one would truncate the
    block and drop every name after it -- silently, which is the failure mode this whole file
    exists to prevent. The file's own count of the label is the control.
    """
    # Counting the label inside a LABELS value, not the bare word: the header comment discusses
    # `tree-level` in prose, and a control that counted those would fire on an edit to a comment.
    written = tests_cmake.count(f';{TREE_LEVEL}"')
    if blocks != written:
        raise SystemExit(
            f"check-tree-level-coverage: parsed {blocks} label site(s) but the file writes "
            f"{written}. A set_tests_properties() value now contains a parenthesis and the parse "
            f"truncated. Fix the parser rather than the count: a name dropped here is a check that "
            f"silently stops being covered."
        )


def style_job(workflow: str) -> str:
    """The `style` job's text, from its key to the next job at the same indentation."""
    start = workflow.index("\n  style:")
    following = re.search(r"\n  [a-z][a-z0-9-]*:\n", workflow[start + 1 :])
    return workflow[start : start + 1 + following.start()] if following else workflow[start:]


# The one required check, and the job whose gating the tree-level checks depend on.
GATE_JOB = "ci-ok"
STYLE_JOB = "style"


def gate_needs(workflow: str) -> list[str] | None:
    """The jobs the `ci-ok` job needs, or None when there is no `ci-ok` job.

    Reads both spellings YAML allows for `needs:` -- the inline list this workflow uses and a block
    list -- because a reformat from one to the other must not read as the job needing nothing.
    """
    start = re.search(rf"\n  {re.escape(GATE_JOB)}:\n", workflow)
    if start is None:
        return None
    following = re.search(r"\n  [a-z][a-z0-9-]*:\n", workflow[start.end() :])
    job = workflow[start.end() : start.end() + following.start()] if following else workflow[start.end() :]
    inline = re.search(r"^    needs:[ \t]*\[([^\]]*)\]", job, re.M)
    if inline:
        return [name.strip() for name in inline.group(1).split(",") if name.strip()]
    single = re.search(r"^    needs:[ \t]*([A-Za-z0-9_-]+)[ \t]*$", job, re.M)
    if single:
        return [single.group(1)]
    block = re.search(r"^    needs:[ \t]*\n((?:      - [^\n]*\n)+)", job, re.M)
    if block:
        return [line.strip()[2:].strip() for line in block.group(1).splitlines()]
    return []


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tests", type=Path, default=REPOSITORY_ROOT / "tests" / "CMakeLists.txt")
    parser.add_argument(
        "--workflow", type=Path, default=REPOSITORY_ROOT / ".github" / "workflows" / "build.yml"
    )
    arguments = parser.parse_args()

    labelled = labelled_tests(arguments.tests.read_text(encoding="utf-8"))
    covered = set(COVERS.findall(style_job(arguments.workflow.read_text(encoding="utf-8"))))

    uncovered = sorted(labelled - covered)
    dangling = sorted(covered - labelled)
    needs = gate_needs(arguments.workflow.read_text(encoding="utf-8"))
    ungated = needs is None or STYLE_JOB not in needs

    for name in uncovered:
        print(
            f"NO STYLE STEP  {name} carries the {TREE_LEVEL} label, so every build job excludes it "
            f"-- and no step in the style job covers it, so it runs NOWHERE in CI.\n"
            f"               Add a step to the style job with '# covers: {name}'."
        )
    for name in dangling:
        print(
            f"COVERS NOTHING {name} is named by a '# covers:' marker in the style job, but no test "
            f"carries the {TREE_LEVEL} label under that name.\n"
            f"               It was renamed or lost the label; the step now covers nothing while "
            f"looking like it covers something."
        )

    if needs is None:
        print(
            f"NO GATE        the workflow has no '{GATE_JOB}' job, so nothing requires the "
            f"{STYLE_JOB} job -- and with it every {TREE_LEVEL} check -- to pass (core-cpp#39)."
        )
    elif ungated:
        print(
            f"NOT GATING     '{GATE_JOB}' needs {needs} and not '{STYLE_JOB}', so every "
            f"{TREE_LEVEL} check runs and nothing requires it to pass (core-cpp#39).\n"
            f"               Put '{STYLE_JOB}' back in {GATE_JOB}'s needs:."
        )

    if uncovered or dangling or ungated:
        print(
            f"\n{len(uncovered) + len(dangling) + int(ungated)} mismatch(es) between the "
            f"{TREE_LEVEL} label, the style job and the required check (Ruling R96, core-cpp#39)."
        )
        return 1

    print(
        f"{len(labelled)} {TREE_LEVEL} check(s), each covered by a style job step, and "
        f"'{GATE_JOB}' needs '{STYLE_JOB}'."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
