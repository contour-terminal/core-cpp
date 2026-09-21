# Task B12b, part 1: the upstream catch-up checker, and the stable rows

`.agent/reference/provenance.md` has ~381 rows: for every file under `src/core/`, which upstream
file and commit it came from. Task B12b's job, before v0.1.0, is to catch up every row whose
upstream has moved since it was synced. Today that is described as a manual sweep, which at 381
rows is a sweep nobody will do twice.

**This part builds the mechanism and applies it to the rows that are stable.** Part 2 runs it
again at B13 time, over the `core::net` and `core::async` rows, which Phase B is still rewriting.

Read the plan's Task B12b (`docs/superpowers/plans/2026-09-18-core-cpp.md`) and the preamble of
`.agent/reference/provenance.md` — especially the paragraphs headed *"A `-` in the notes column
does not mean byte-identical to upstream"* and *"So a row is not a licence to re-sync by
overwriting."* Those two are the trap this task exists to avoid, and they are not optional
reading.

## What to build

A checker — `scripts/check-upstream-drift.py`, or a name you argue for — that:

1. Parses `.agent/reference/provenance.md` into rows: core-cpp path, upstream repository, upstream
   path, synced SHA, notes.
2. For each distinct upstream repository, runs `git -C <checkout> fetch origin` **once**, then per
   row `git log --oneline <synced SHA>..origin/master -- <upstream path>`.
3. Reports, per row: no drift, or the list of upstream commits that touched it since.
4. Exits non-zero only on a *malformed* row (a path that no longer exists, a SHA that is not an
   ancestor, an unparseable line) — **not** on drift. Drift is information, not a failure; a
   nightly that cries wolf gets muted.
5. Takes `--repo <name>` and `--path-prefix <p>` so B12b part 2 can ask only about `core::net` and
   `core::async`.

The local checkouts are `D:\contour`, `D:\endo` and `D:\fastcached`. **Read them as blobs and
never modify them** — `git -C <path> fetch origin` is the only write-shaped command permitted, and
for fastcached it is the *only* command permitted beyond reads (a standing user requirement: all
fastcached work happens in a worktree under `D:\fastcached-worktrees\`, never in `D:\fastcached`).

Self-test it the way this repository self-tests its other checkers: a temporary git repository, a
row that drifted, a row that did not, a row whose path was deleted upstream, and a malformed row.
`tests/cmake/check-cmake-hygiene-selftest.cmake` is the shape to follow. Register both under
`ctest -L hygiene`, **with a `TIMEOUT`** — it spawns git against three repositories, so bound it
from measurement, not from a round number.

## What to apply it to now

Run it over the rows whose upstream is **contour** or **endo**, and over `cmake/portable/
CompileCache.cmake` and `cmake/FetchTransferBound.cmake` (fastcached, but verbatim-synced files
rather than ported ones).

I have already measured the headline, so treat this as a result to confirm or refute rather than
to discover: `git log <pin>..origin/master` over contour's `src/crispy` and endo's `src/tui`,
`src/platform` and `src/testing` returned **zero commits** on all three. If your checker agrees,
say so and record it. **If it disagrees, your checker is right and my one-line measurement was
wrong** — I ran it at directory granularity, and a row-level run can see a file that moved, was
deleted, or was touched by a commit outside those directories.

For `cmake/portable/CompileCache.cmake` the rule is different and stricter: it is a **verbatim**
copy of fastcached's, never edited here, and `downstream.yml` is supposed to fail on drift. Check
whether it has drifted, and whether that nightly diff actually exists and works — *a gate that
does not report reads as passed*.

Do **not** port anything in this part. If a row has drifted, record it in the report with the
upstream commits and a one-line judgement of whether it looks behavioural or cosmetic; the porting
decision is mine, and the porting work belongs to whichever task owns that module.

## Then

- `ruff format --check` (a pin landed in `ac9e2e2`), `ctest -L hygiene`, `mkdocs build --strict`
  if you touch `docs/`.
- No CHANGELOG entry: a maintainer script is not something a consumer observes. Say so rather than
  omitting it silently.
- `.agent/reference/provenance.md`'s preamble should point at the checker, so the next person runs
  it instead of re-inventing the sweep. That file is edited by several lanes — read every hunk
  first and commit through a private index.

## Concurrency

Three other lanes are live in this checkout: `src/core/async/` (the executor grafts),
`src/core/net/` (the `IoBackend` rewrite — that directory is **no longer yours**), and
`tools/migrate/`. Yours is `scripts/`, `tests/cmake/` and the provenance preamble.

- **Never a bare `git commit`, `git commit -a` or `git add`.** `git commit --only -- <pathspecs>`
  for files only you touched; a private index (`export GIT_INDEX_FILE=$(mktemp)`,
  `git read-tree HEAD`, `git apply --cached`, `git commit`, unset) for hunks of a shared file.
  `git show --stat` after every commit.
- **Never `git pull --rebase`**; `git fetch origin` then push.
- Your commit will usually get no CI run of its own. Watch the newest head, and say plainly if you
  close with no green run covering your commits.
- `ctest -L hygiene` is currently red in the shared tree on provenance rows for other lanes'
  untracked files. Not yours — and worth noticing that your checker's failure mode must not be
  confused with that one.

## Report

Write to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-B12b-1-report.md`: the self-test
cases and their REDs, the per-row drift table for the rows you ran, whether my zero-drift
measurement held at row granularity, the `CompileCache.cmake` verdict and whether
`downstream.yml`'s drift diff is real, and the bound you measured for the ctest. Return only
status, the commit range, a one-line test summary, and concerns.
