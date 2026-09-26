#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Every `## Open work` entry leads with a core-cpp issue, and that issue is open.

    python scripts/check-open-work.py            # the grammar, offline: what every ctest runs
    python scripts/check-open-work.py --online   # and each entry's issue is still open

`.agent/rules/README.md` defines the grammar. An `## Open work` section is a list of top-level
bullets, and each one's LEADING reference is the core-cpp issue that tracks it:

    - **[core-cpp#123](https://github.com/contour-terminal/core-cpp/issues/123)** — what is left.

**An entry whose issue has closed is a rule that has gone false**, and the expensive version is an
entry saying something cannot be done yet, which tells the next session not to try. Until this
check (core-cpp#12) keeping entries true was a review question; fastcached enforces the same
grammar with a ctest (fastcached#957).

Two halves, because only one of them needs the network:

    offline    every entry's leading reference is a core-cpp issue link whose text and URL agree;
               a section with no entries is refused too -- the heading goes with its last entry
    --online   every leading issue is open, asked of the GitHub API (GH_TOKEN or GITHUB_TOKEN
               is used when set). A lookup that cannot be answered -- no network, a rate limit,
               no token -- is neither a pass nor a refusal: the run exits 77, SKIPPED, naming
               what it could not ask. An unanswered question is not an open issue, and it is
               not a closed one either. A 404 or a 410 IS an answer -- the issue does not exist,
               or was deleted -- and is refused like a closed one

The ctest runs the offline half, so a local run never fails for want of a network; CI's `style`
job runs both. `--states FILE` answers the online half from a JSON object of issue number to state
instead of the network, which is how the self-test reaches it.

Exit status: 0 when every entry holds; 1 naming every one that does not; 77 when nothing was
refused but at least one issue's state could not be learnt.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
REPOSITORY = "contour-terminal/core-cpp"

# Where the grammar is used: the rulebook, the documentation and the top-level documents. A heading
# is `## Open work` exactly; the design plan's `## Open work after v0.1.0` is history, not a section.
SCANNED = (".agent", "docs")
TOP_LEVEL = ("AGENT.md", "CONTRIBUTING.md", "README.md")
HEADING = "## Open work"

# The exit status ctest reads as a skip (CORE_CPP_SKIP_EXIT_CODE in cmake/CoreCppTargets.cmake).
SKIP_EXIT_CODE = 77

# The state of an issue GitHub says does not exist (404) or has been deleted (410). A definite
# answer, so refused -- never folded into "could not ask", which only skips.
MISSING = "missing"

LEADING = re.compile(
    r"^- \*\*\[core-cpp#(?P<text>\d+)\]\(https://github\.com/contour-terminal/core-cpp/issues/(?P<url>\d+)\)\*\*"
)


@dataclass(frozen=True)
class Entry:
    """One top-level bullet of an `## Open work` section."""

    path: str
    line: int
    text: str


def markdown_files(root: Path) -> list[Path]:
    """The Markdown files the grammar applies to, in a stable order.

    :param root: the repository root.
    :return: every `.md` under SCANNED, and the TOP_LEVEL documents that exist.
    """
    files = [root / name for name in TOP_LEVEL if (root / name).is_file()]
    for directory in SCANNED:
        files.extend(sorted((root / directory).rglob("*.md")))
    return files


def open_work_entries(root: Path) -> tuple[list[Entry], list[str]]:
    """Reads every `## Open work` section.

    :param root: the repository root.
    :return: the entries, and the problems found while reading (an empty section).
    """
    entries: list[Entry] = []
    problems: list[str] = []
    for path in markdown_files(root):
        relative = path.relative_to(root).as_posix()
        inside = False
        heading_line = 0
        count = 0
        fenced = False
        lines = path.read_text(encoding="utf-8").splitlines()
        for number, line in enumerate(lines, start=1):
            # A code block's lines are not headings or bullets, whatever they start with.
            if line.startswith("```"):
                fenced = not fenced
                continue
            if fenced:
                continue
            if line.startswith("## ") or line.startswith("# "):
                if inside and count == 0:
                    problems.append(
                        f"{relative}:{heading_line}: an Open work section with no entries; delete the heading"
                    )
                inside = line.rstrip() == HEADING
                heading_line = number
                count = 0
                continue
            if inside and line.startswith("- "):
                entries.append(Entry(relative, number, line))
                count += 1
        if inside and count == 0:
            problems.append(
                f"{relative}:{heading_line}: an Open work section with no entries; delete the heading"
            )
    return entries, problems


def grammar_problems(entries: list[Entry]) -> list[str]:
    """Refuses an entry that does not lead with a well-formed core-cpp issue link.

    :param entries: the entries to check.
    :return: one line per refused entry.
    """
    problems = []
    for entry in entries:
        match = LEADING.match(entry.text)
        if match is None:
            problems.append(
                f"{entry.path}:{entry.line}: does not lead with **[core-cpp#N](https://github.com/{REPOSITORY}/issues/N)**"
            )
        elif match["text"] != match["url"]:
            problems.append(
                f"{entry.path}:{entry.line}: says core-cpp#{match['text']} and links issue {match['url']}"
            )
    return problems


def issue_number(entry: Entry) -> int | None:
    """:return: the entry's leading issue number, or None where the grammar refused it."""
    match = LEADING.match(entry.text)
    return int(match["text"]) if match and match["text"] == match["url"] else None


def github_state(number: int) -> str:
    """Asks the GitHub API for an issue's state.

    :param number: the core-cpp issue number.
    :return: "open", "closed", or MISSING when GitHub answers 404 or 410.
    :raises OSError: when the question could not be answered -- no network, a rate limit, no
        permission, a server error.
    """
    request = urllib.request.Request(
        f"https://api.github.com/repos/{REPOSITORY}/issues/{number}",
        headers={"Accept": "application/vnd.github+json", "User-Agent": "core-cpp-check-open-work"},
    )
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)["state"]
    except urllib.error.HTTPError as error:
        # HTTPError is a URLError, so without this a "no such issue" read as "could not ask". A
        # 401, 403 or 429 is a question not answered, and a 5xx one GitHub failed to answer.
        if error.code in (404, 410):
            return MISSING
        raise


def state_problems(entries: list[Entry], states: dict[int, str] | None) -> tuple[list[str], list[str]]:
    """Refuses an entry whose issue is closed, and lists those whose state could not be learnt.

    :param entries: the entries to check.
    :param states: issue number to state, or None to ask GitHub.
    :return: one line per refused entry, and one per entry left unanswered.
    """
    problems = []
    unanswered = []
    for entry in entries:
        number = issue_number(entry)
        if number is None:
            continue
        try:
            state = states[number] if states is not None else github_state(number)
        except (KeyError, OSError, ValueError, urllib.error.URLError) as error:
            unanswered.append(
                f"{entry.path}:{entry.line}: core-cpp#{number}: its state could not be read ({error!r})"
            )
            continue
        if state == MISSING:
            problems.append(
                f"{entry.path}:{entry.line}: core-cpp#{number} does not exist (GitHub answers 404 or 410); "
                "the entry must lead with the issue that tracks it"
            )
        elif state != "open":
            problems.append(
                f"{entry.path}:{entry.line}: core-cpp#{number} is {state}, so this entry has gone false; "
                "delete it, or reopen the issue if the work is not done"
            )
    return problems, unanswered


def main(argv: list[str] | None = None) -> int:
    """Runs the check. :return: the exit status."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=REPOSITORY_ROOT, help="the repository to check")
    parser.add_argument("--online", action="store_true", help="also require every leading issue to be open")
    parser.add_argument(
        "--states", type=Path, help="a JSON object of issue number to state, instead of GitHub"
    )
    arguments = parser.parse_args(argv)

    entries, problems = open_work_entries(arguments.root)
    problems += grammar_problems(entries)
    unanswered: list[str] = []
    if arguments.online or arguments.states:
        states = None
        if arguments.states:
            states = {int(key): value for key, value in json.loads(arguments.states.read_text()).items()}
        refused, unanswered = state_problems(entries, states)
        problems += refused

    halves = "grammar and state" if arguments.online or arguments.states else "grammar"
    if problems:
        print(f"check-open-work: {len(problems)} problem(s) over {len(entries)} entries ({halves}):")
        for problem in problems:
            print(f"  {problem}")
        return 1
    if unanswered:
        print(
            f"check-open-work: SKIPPED -- {len(unanswered)} issue state(s) could not be read, so the state half is unanswered:"
        )
        for line in unanswered:
            print(f"  {line}")
        return SKIP_EXIT_CODE
    print(f"check-open-work: {len(entries)} Open work entries hold ({halves})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
