## Plan amendment (user request, 2026-09-18): upstream catch-up, scaled to the measured risk. Apply after Task A3 completes.

Measured: contour `coro`/`net` have had 0 commits in 14 days. endo `tui`/`platform`/`testing` had 4, 7 and 3 in 14 days. fastcached `Net` had 49 and `Async` 14 in 14 days, but those were test hygiene (loop conversions, bounded waits: #1446/#1452/#1453). The fastcached board (LASTRADA-Software project 3) has 13 open items, all cluster/raft/fleet/CI business logic and none touching Async/Net (user confirmed: "not moving at all on coro/net side"). So this is cheap insurance, not a gate expected to fire.

### Global Constraints addition
- **Upstream sync discipline.** Every import or port reads the upstream at its current `origin/master` (fetched at task start) and records the synced SHA per file in `.agent/reference/provenance.md` (`core-cpp path | upstream repo | upstream path | synced SHA`).

### New Task A3b (before A4): the provenance table
Create `.agent/reference/provenance.md` with rows for everything imported so far. The SHAs are in NOTICE, CHANGELOG and the A1–A3 reports. Later import tasks append their rows.

### New Task B12b (before B13): one upstream catch-up check
For every provenance row, run `git log --oneline <synced SHA>..origin/master -- <upstream path>`.
- If a commit changes behaviour, a contract or a test that still applies after the merge, port it (test-first) and bump the row.
- Otherwise record "not applicable" with a reason (for example test-only loop conversions already done in core-cpp style).
- The report gets a table of commit → ported/N-A.

### Delta check in C1, C3, C4, C6
- At task start, run the same `git log` over the consumer paths being replaced, against their provenance SHAs.
- If it is non-empty with anything applicable, port it into core-cpp first (patch release v0.1.x) and bump the pin; otherwise note "delta: none" in the PR body.
- There is no per-rebase gate.
