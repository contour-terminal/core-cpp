#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Every visible configure preset is run by a workflow, or is allowlisted with a reason.

    python scripts/check-preset-coverage.py          # all three directions, over this tree

**The configurations that exist locally and the ones CI runs are different sets, and until this
file nothing said so.** `CMakePresets.json` is what a developer reads to learn how the project is
built; `.github/workflows/` is what actually builds it. Neither refers to the other, so a preset
can be offered, documented, used by a lane for hours and run by no job -- and nothing anywhere is
red, because a configuration that is absent from CI does not fail there, it is simply not present.

That is not hypothetical and it is not a small gap. When this check was written, three visible
presets were referenced by no workflow, and all three were Debug:

    gcc-debug           GCC with assertions: clang-debug covered clang, gcc-release covered GCC
    appleclang-debug    the ONLY Debug configuration macOS had
    clangcl-debug       the only Windows Debug leg with clang-cl

The middle one is the one that shows the cost. macOS ran `appleclang-release` and `clang-release`
and nothing else, so `NDEBUG` was defined in every macOS job and **all 30 runtime assertions in
`src/core` were compiled out of the platform** -- 19 of them in the shared event-loop code, among
them the twelve `teardownIsSerialisedWithDispatch()` thread-affinity checks in `EventLoop.cpp` and
`ReadyBatch`'s re-entrancy trap -- and both canaries abstain with 77 under `NDEBUG`. kqueue is
macOS-exclusive, so those shared checks had never once been evaluated with kqueue underneath them,
on the platform Ruling R101 exists because of.

That is runtime `assert()` only. The 122 `static_assert`s fire at compile time and
`Require()`/`Guarantee()` are not `NDEBUG`-gated, so neither family was ever dark; a grep for
`assert` answers 152 and overstates the loss by a factor of five.

## Why an allowlist rather than "every preset must have a leg"

Some presets legitimately have no job. `clang-coverage` is driven by a job that names it, but a
preset for a local-only tool, a one-off investigation or a platform CI has no runner for should not
force a leg into existence. **What it must not do is disappear quietly**, so the exemption is
written down with a reason, in ALLOWLIST below, and the reason is required rather than optional: an
entry with an empty reason is refused, because "somebody once decided this" is what a bare name
says and it is not a decision anybody can review later.

## Three directions, and the second and third are the ones that rot

    a visible preset no workflow names, not allowlisted     runs nowhere; the hole this closes
    an allowlist entry for a preset a workflow DOES name    a stale exemption: the leg was added
                                                            and nobody removed the excuse, so the
                                                            next preset to go dark inherits an
                                                            allowlist that looks maintained
    a workflow naming a preset CMakePresets.json lacks      a rename or a typo; that leg fails at
                                                            run time on one platform, which is the
                                                            slowest possible way to learn it

A hidden preset is not in scope: it cannot be configured by name, so there is nothing for a job to
run. `CMakePresets.json` is the only source for that -- the check never infers visibility from a
name.

Exit status: 0 when the three agree, 1 when they do not, naming every difference and what to do
about it.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent

# Presets that deliberately have no workflow leg, each with the reason it does not.
#
# A reason is required and is checked for emptiness. Adding a row here is a decision that somebody
# reviews; adding a bare name is a decision nobody can.
ALLOWLIST: dict[str, str] = {}

# How a workflow names a preset. Both spellings are in use and neither is optional:
#
#     run: cmake --preset clang-tidy          a step that names one directly
#     preset: clang-debug                     a matrix `include:` entry
#     preset: [cl-release, cl-debug]          a matrix list
#
# `${{ matrix.preset }}` is deliberately not matched: it is an indirection, and the names it
# resolves to are the matrix entries this already reads.
PRESET_FLAG = re.compile(r"--preset[ \t]+([A-Za-z0-9_][A-Za-z0-9_.-]*)")
PRESET_KEY = re.compile(r"(?<![\w.])preset:[ \t]*[\"']?([A-Za-z0-9_][A-Za-z0-9_.-]*)")
PRESET_LIST = re.compile(r"(?<![\w.])preset:[ \t]*\[([^\]]*)\]")

# A YAML comment starts at a `#` that begins the line or follows whitespace.
COMMENT = re.compile(r"(?:^|(?<=\s))#.*$")


def strip_comment(line: str) -> str:
    """Removes a YAML comment from a workflow line.

    Ruling R81's hazard in this file's own terms: an unanchored pattern also matches prose that
    merely MENTIONS the syntax, and a comment reading `preset: clang-debug` would be taken for a
    leg that does not exist. That is the dangerous direction -- it INVENTS coverage -- so comments
    go before anything is matched. A `#` inside a quoted scalar would be stripped too; no workflow
    here has one, and that error is the safe direction, since it reports a preset as uncovered
    rather than inventing a job for it.

    :param line: one line of a workflow file.
    :return: the line with any trailing comment removed.
    """
    return COMMENT.sub("", line)


def visible_presets(presets_file: Path) -> list[str]:
    """Returns the configure presets a caller can name, in the file's own order.

    Order is kept so the report reads like the file rather than like a set.
    """
    document = json.loads(presets_file.read_text(encoding="utf-8"))
    return [
        preset["name"] for preset in document.get("configurePresets", []) if not preset.get("hidden", False)
    ]


def referenced_presets(workflow_dir: Path) -> dict[str, set[str]]:
    """Returns every preset each workflow names, keyed by preset.

    The value is the set of workflow file names, so a failure can say WHERE rather than only
    whether -- "referenced" with no witness is the kind of answer that sends a reader looking.
    """
    found: dict[str, set[str]] = {}
    for workflow in sorted(workflow_dir.glob("*.yml")) + sorted(workflow_dir.glob("*.yaml")):
        lines = workflow.read_text(encoding="utf-8").splitlines()
        text = "\n".join(strip_comment(line) for line in lines)
        names: set[str] = set()
        names.update(PRESET_FLAG.findall(text))
        names.update(PRESET_KEY.findall(text))
        for group in PRESET_LIST.findall(text):
            names.update(part.strip().strip("\"'") for part in group.split(",") if part.strip())
        for name in names:
            found.setdefault(name, set()).add(workflow.name)
    return found


def verify_parse(presets: list[str], referenced: dict[str, set[str]]) -> list[str]:
    """Refuses a run whose inputs did not parse into something usable.

    A checker that read nothing reports no differences, which is the same output as a tree with no
    differences. Both sides are asked for a floor before any comparison is believed -- the same
    reason `check-tree-level-coverage.py` verifies its own parse.
    """
    problems = []
    if not presets:
        problems.append(
            "CMakePresets.json yielded no visible configurePresets. Either the file moved or the "
            "parse is wrong; either way this check cannot answer and must not report success."
        )
    if not referenced:
        problems.append(
            "no workflow named any preset. The spellings this reads are `--preset <name>`, "
            "`preset: <name>` and `preset: [<names>]`; if the workflows now say something else, "
            "PRESET_FLAG / PRESET_KEY / PRESET_LIST need to learn it."
        )
    return problems


def check(root: Path) -> list[str]:
    """Compares the three sets and returns one message per difference.

    :param root: the repository root to read.
    :return: the failures, empty when the tree agrees.
    """
    presets = visible_presets(root / "CMakePresets.json")
    referenced = referenced_presets(root / ".github" / "workflows")

    failures = verify_parse(presets, referenced)
    if failures:
        return failures

    known = set(presets)

    for name in presets:
        if name in referenced or name in ALLOWLIST:
            continue
        failures.append(
            f"preset '{name}' is referenced by no workflow and is not allowlisted: it runs "
            f"NOWHERE in CI, which is indistinguishable from a leg that passes. Add a job that "
            f"names it in .github/workflows/, or add it to ALLOWLIST in {Path(__file__).name} "
            f"with the reason it has no leg."
        )

    for name, reason in ALLOWLIST.items():
        if name not in known:
            failures.append(
                f"ALLOWLIST names '{name}', which is not a visible preset in CMakePresets.json. "
                f"It was renamed, hidden or removed; drop the entry."
            )
        elif name in referenced:
            failures.append(
                f"ALLOWLIST excuses '{name}' from having a leg, but "
                f"{', '.join(sorted(referenced[name]))} now runs it. Remove the entry: a stale "
                f"exemption makes the allowlist look maintained while the next preset to go dark "
                f"inherits its credibility."
            )
        elif not reason.strip():
            failures.append(
                f"ALLOWLIST entry '{name}' has an empty reason. A bare name records that somebody "
                f"once decided this and gives a later reader nothing to review."
            )

    for name, workflows in sorted(referenced.items()):
        if name not in known:
            failures.append(
                f"{', '.join(sorted(workflows))} names preset '{name}', which CMakePresets.json "
                f"does not define as a visible configure preset. That leg fails at run time, on "
                f"one platform, which is the slowest way to find a rename."
            )

    return failures


def main() -> int:
    """Entry point.

    :return: 0 when the sets agree, 1 otherwise.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--root",
        type=Path,
        default=REPOSITORY_ROOT,
        help="the repository to check (default: the one this script lives in)",
    )
    arguments = parser.parse_args()

    failures = check(arguments.root)
    if failures:
        print(f"check-preset-coverage: {len(failures)} problem(s)", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    presets = visible_presets(arguments.root / "CMakePresets.json")
    exempt = len(ALLOWLIST)
    print(
        f"check-preset-coverage: {len(presets) - exempt} of {len(presets)} visible preset(s) are "
        f"run by a workflow, {exempt} allowlisted"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
