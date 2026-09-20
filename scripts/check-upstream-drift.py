#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reports which provenance rows have an upstream that moved since they were synced.

    python scripts/check-upstream-drift.py                       # every row, every upstream
    python scripts/check-upstream-drift.py --repo endo           # only rows from one upstream
    python scripts/check-upstream-drift.py --path-prefix src/core/net/   # only part of the tree
    python scripts/check-upstream-drift.py --no-fetch            # trust the checkouts as they are

`.agent/reference/provenance.md` records, for every file under `src/core/`, `cmake/portable/` and
`cmake/FetchTransferBound.cmake`, which upstream file and commit it came from. Task B12b's job
before v0.1.0 is to catch up every row whose upstream has moved since. At ~390 rows that is a sweep
nobody does twice by hand, so this does the reading and the `git log`, and leaves the judgement.

**This reports. It does not re-sync, and its output is not a licence to.** Read the two paragraphs
in that file's preamble headed "A `-` in the notes column does not mean byte-identical to upstream"
and "So a row is not a licence to re-sync by overwriting" before acting on anything here: every
import applied this repository's own rewrites, so overwriting a file with its upstream reverts them
silently -- including the `NOLINT` and diagnostic pragmas the rules forbid. Re-syncing means merging
the upstream delta into what is here.

**What a clean run does NOT prove.** This compares HISTORY, not CONTENT: it asks whether upstream
touched the path a row names since the SHA that row names. So it proves the upstreams have not
moved -- never that the rows are RIGHT. A row naming the wrong upstream path reports clean for as
long as that wrong path sits untouched, and so does a row whose local file has drifted from the
upstream it claims, because nothing here reads either file's bytes. For the verbatim copies under
`cmake/portable/` the content question is `downstream.yml`'s, which diffs them against upstream
`master` nightly (`cmake/portable/README.md`). For the ported files under `src/core/` nothing
answers it today, and a checker that did would have to compare against each row's recorded
rewrites rather than against the upstream bytes, which is why it is not simply a `diff`.

Exit status, which is the part that decides whether a nightly can run this:

    0   every row read, whatever it found. DRIFT IS NOT A FAILURE -- it is the answer to the
        question, and a gate that reddens on it teaches people to mute it.
    1   at least one row is MALFORMED on its UPSTREAM side, which is a defect in the table
        rather than news about upstream: an unparseable line, a synced SHA the checkout does not
        have or that is not an ancestor of the upstream branch, or an upstream path absent at its
        own synced SHA -- a row naming a file that was never there.

        A row whose CORE-CPP file is gone is reported as stale and does not redden this:
        check-cmake-hygiene's `provenance` rule already refuses exactly that, and one defect that
        reddens two gates gets both of them ignored.
    77  the run could not answer for at least one requested upstream, because its checkout is
        missing. Everything reachable is still reported; the exit code says the answer is partial,
        which ctest shows as a skip rather than as a pass (.agent/rules/testing.md: a gate that
        does not report reads as passed).

A deletion upstream is drift, not malformedness: the row was true when it was written, and "this
file is gone at master" is exactly the kind of news this exists to deliver.

The upstream checkouts default to siblings of this repository (`../contour`, `../endo`,
`../fastcached`), which is the layout on the machines that have them; `--checkout repo=path`
overrides one. They are read, never written: `git fetch origin` is the only command here that
touches a checkout at all, and `--no-fetch` skips even that. For fastcached this is not a
preference but a standing requirement -- all fastcached work happens in a worktree under
`fastcached-worktrees/`, and `D:\\fastcached` itself is never checked out, built or committed in.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
PROVENANCE = REPOSITORY_ROOT / ".agent" / "reference" / "provenance.md"

# Rows whose "upstream repo" cell is this were written here and have no upstream to catch up with.
NO_UPSTREAM = "origin: core-cpp"

# Where each upstream is expected to be checked out, relative to this repository's parent. Override
# with --checkout; this is a default for the machines that have them, not a claim that they exist.
DEFAULT_CHECKOUTS = {
    "contour-terminal/contour": "contour",
    "contour-terminal/endo": "endo",
    "LASTRADA-Software/fastcached": "fastcached",
}

# The exit status ctest reads as a skip (CORE_CPP_SKIP_EXIT_CODE in cmake/CoreCppTargets.cmake).
SKIP_EXIT_CODE = 77


@dataclass
class Row:
    """One parsed line of the provenance table."""

    line_number: int
    core_path: str
    repo: str
    upstream_path: str
    sha: str
    notes: str


@dataclass
class Finding:
    """What the checker concluded about one row."""

    row: Row
    commits: list[str] = field(default_factory=list)  # upstream commits since the synced SHA
    deleted_upstream: bool = False
    malformed: str | None = None  # why the row's UPSTREAM side is wrong, if it is
    stale: str | None = None  # the core-cpp file is gone; another gate owns that


def strip_cell(cell: str) -> str:
    """Returns a table cell without its surrounding whitespace and backtick quoting."""
    return cell.strip().strip("`").strip()


def parse_provenance(text: str) -> tuple[list[Row], list[str]]:
    """Parses the provenance table.

    @return The rows that name an upstream, and a list of complaints about lines that look like
            table rows but are not parseable. Rows with no upstream are dropped rather than
            complained about: they are most of the table and there is nothing to check.
    """
    rows: list[Row] = []
    complaints: list[str] = []
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if not stripped.startswith("|"):
            continue
        if re.match(r"^\|[\s:-]+\|", stripped):
            continue  # the header's underline
        cells = [strip_cell(c) for c in stripped.strip("|").split("|")]
        # The header is the row whose FIRST CELL is the column's name -- not any row that happens
        # to mention it. This was a substring test over the whole line, and
        # `src/core/async/ThreadPoolExecutor.hpp`'s notes say "a row keys on its core-cpp path",
        # so that row was dropped as though it were the header: not checked, and not reported
        # either, which is the one answer this checker must never give.
        if cells and cells[0] == "core-cpp path":
            continue
        if len(cells) < 5:
            complaints.append(f"{PROVENANCE.name}:{number}: expected 5 columns, found {len(cells)}")
            continue
        core_path, repo, upstream_path, sha, notes = cells[0], cells[1], cells[2], cells[3], cells[4]
        if repo == NO_UPSTREAM:
            continue
        if not re.fullmatch(r"[0-9a-f]{40}", sha):
            complaints.append(
                f"{PROVENANCE.name}:{number}: {core_path}: synced SHA '{sha}' is not a full 40-character hash"
            )
            continue
        rows.append(Row(number, core_path, repo, upstream_path, sha, notes))
    return rows, complaints


def matches_repo(full_name: str, wanted: str) -> bool:
    """Whether `--repo <wanted>` selects rows whose upstream is @p full_name.

    Either the whole `owner/name` or the bare `name`, and nothing looser: every repository here is
    under the `contour-terminal` owner, so a substring test would make `--repo contour` select
    endo's 220 rows along with contour's 108 -- which it silently did until a row count gave it
    away.
    """
    return wanted.lower() in {full_name.lower(), full_name.rsplit("/", 1)[-1].lower()}


def git(checkout: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    """Runs one git command in @p checkout, capturing its output and never raising on failure."""
    return subprocess.run(
        ["git", "-C", str(checkout), *arguments],
        capture_output=True,
        text=True,
        check=False,
    )


def check_sha(checkout: Path, sha: str, upstream_ref: str, cache: dict[str, str | None]) -> str | None:
    """Validates a synced SHA once per distinct SHA, not once per row.

    An import task syncs a whole module at one commit, so the ~390 rows name **four** distinct SHAs
    between them. Asking git the same two questions 390 times cost more than every other call here
    put together -- the run took 92 seconds on WSL, most of it spawning git to re-answer what it had
    already said.
    @return Why the SHA is unusable, or None if it is fine.
    """
    if sha not in cache:
        if git(checkout, "cat-file", "-e", f"{sha}^{{commit}}").returncode != 0:
            cache[sha] = f"synced SHA {sha[:12]} is not a commit in this checkout"
        elif git(checkout, "merge-base", "--is-ancestor", sha, upstream_ref).returncode != 0:
            cache[sha] = f"synced SHA {sha[:12]} is not an ancestor of {upstream_ref}"
        else:
            cache[sha] = None
    return cache[sha]


def check_row(checkout: Path, row: Row, upstream_ref: str, sha_cache: dict[str, str | None]) -> Finding:
    """Answers one row: malformed, drifted, or clean."""
    finding = Finding(row=row)

    if not (REPOSITORY_ROOT / row.core_path).exists():
        # Reported, not failed: check-cmake-hygiene's `provenance` rule already refuses a row whose
        # core-cpp file is gone, in almost these words. Two gates reddening for one defect teaches
        # people to read neither, and it would put this one in the red for a module being rewritten
        # -- which is the state it is least useful in. What only THIS checker can see is the
        # upstream side of the row, and that is what its exit status is about.
        finding.stale = f"names {row.core_path}, which does not exist here (check-cmake-hygiene owns this)"
        return finding
    if any(character in row.upstream_path for character in "{}*?"):
        # One cell, one upstream file. The preamble already settles this: "A file adapted from more
        # than one upstream file (a merge) names its primary upstream in the table and lists the
        # others in notes." A brace or a glob makes the row unreadable by `git log -- <path>`, so
        # the row silently stops being checkable rather than reporting anything.
        finding.malformed = (
            f"upstream path '{row.upstream_path}' is a pattern, not a file; a merged file names its"
            " primary upstream here and the others in notes (provenance.md preamble)"
        )
        return finding
    if unusable := check_sha(checkout, row.sha, upstream_ref, sha_cache):
        finding.malformed = unusable
        return finding
    if git(checkout, "cat-file", "-e", f"{row.sha}:{row.upstream_path}").returncode != 0:
        # The row names a file that was not there at the commit it claims to have synced from, so
        # the row is wrong about its own past. A file deleted LATER is a different thing entirely,
        # and is reported as drift below.
        finding.malformed = f"{row.upstream_path} does not exist at its own synced SHA {row.sha[:12]}"
        return finding

    log = git(checkout, "log", "--oneline", f"{row.sha}..{upstream_ref}", "--", row.upstream_path)
    if log.returncode != 0:
        finding.malformed = f"git log failed: {log.stderr.strip()}"
        return finding
    finding.commits = [line for line in log.stdout.splitlines() if line.strip()]
    if finding.commits:
        finding.deleted_upstream = (
            git(checkout, "cat-file", "-e", f"{upstream_ref}:{row.upstream_path}").returncode != 0
        )
    return finding


def resolve_checkouts(overrides: list[str]) -> dict[str, Path]:
    """Maps each upstream repository name to the checkout to read it from."""
    checkouts = {repo: REPOSITORY_ROOT.parent / name for repo, name in DEFAULT_CHECKOUTS.items()}
    for override in overrides:
        repo, separator, path = override.partition("=")
        if not separator:
            raise SystemExit(f"--checkout wants repo=path, not '{override}'")
        checkouts[repo] = Path(path)
    return checkouts


def report(findings: list[Finding], unavailable: dict[str, Path], complaints: list[str]) -> int:
    """Prints the outcome and returns the process exit status."""
    malformed = [f for f in findings if f.malformed]
    stale = [f for f in findings if f.stale]
    checked = [f for f in findings if not f.malformed and not f.stale]
    drifted = [f for f in checked if f.commits]
    clean = [f for f in checked if not f.commits]

    for finding in drifted:
        row = finding.row
        gone = " (DELETED upstream)" if finding.deleted_upstream else ""
        print(f"\nDRIFT  {row.core_path}{gone}")
        print(f"       {row.repo} {row.upstream_path}, {len(finding.commits)} commit(s) since {row.sha[:12]}")
        for commit in finding.commits:
            print(f"         {commit}")

    for finding in malformed:
        print(f"\nMALFORMED  {PROVENANCE.name}:{finding.row.line_number}: {finding.row.core_path}")
        print(f"           {finding.malformed}")
    for complaint in complaints:
        print(f"\nMALFORMED  {complaint}")

    for finding in stale:
        print(f"\nSTALE ROW  {PROVENANCE.name}:{finding.row.line_number}: {finding.stale}")

    print(f"\n{len(checked)} row(s) checked: {len(clean)} up to date, {len(drifted)} drifted.")
    if stale:
        print(f"{len(stale)} stale row(s) left unchecked: their core-cpp file is gone.")
    if unavailable:
        for repo, path in sorted(unavailable.items()):
            print(f"NOT CHECKED: {repo} -- no checkout at {path}")
    if complaints or malformed:
        print(f"{len(malformed) + len(complaints)} malformed row(s): the table is wrong, not upstream.")
        return 1
    if unavailable:
        print("The answer is partial: an upstream above was not read at all.")
        return SKIP_EXIT_CODE
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--provenance", type=Path, default=PROVENANCE, help="the table to read")
    parser.add_argument("--repo", action="append", default=[], help="only rows from this upstream")
    parser.add_argument("--path-prefix", action="append", default=[], help="only core-cpp paths under this")
    parser.add_argument("--checkout", action="append", default=[], help="repo=path for an upstream checkout")
    parser.add_argument(
        "--upstream-ref", default="origin/master", help="the upstream branch to compare against"
    )
    parser.add_argument("--no-fetch", action="store_true", help="do not run `git fetch origin` first")
    arguments = parser.parse_args()

    rows, complaints = parse_provenance(arguments.provenance.read_text(encoding="utf-8"))
    if arguments.repo:
        rows = [r for r in rows if any(matches_repo(r.repo, name) for name in arguments.repo)]
    if arguments.path_prefix:
        rows = [r for r in rows if any(r.core_path.startswith(p) for p in arguments.path_prefix)]

    checkouts = resolve_checkouts(arguments.checkout)
    wanted = sorted({row.repo for row in rows})
    unavailable = {
        repo: checkouts.get(repo, Path("<unknown>"))
        for repo in wanted
        if not (checkouts.get(repo) and (checkouts[repo] / ".git").exists())
    }

    findings: list[Finding] = []
    for repo in wanted:
        if repo in unavailable:
            continue
        checkout = checkouts[repo]
        if not arguments.no_fetch:
            # The one command here that touches a checkout, and the only one permitted against
            # D:\fastcached at all.
            print(f"fetching {repo} in {checkout} ...", file=sys.stderr)
            fetched = git(checkout, "fetch", "origin")
            if fetched.returncode != 0:
                print(
                    f"  fetch failed, reading the checkout as it stands: {fetched.stderr.strip()}",
                    file=sys.stderr,
                )
        sha_cache: dict[str, str | None] = {}
        for row in [r for r in rows if r.repo == repo]:
            findings.append(check_row(checkout, row, arguments.upstream_ref, sha_cache))

    return report(findings, unavailable, complaints)


if __name__ == "__main__":
    sys.exit(main())
