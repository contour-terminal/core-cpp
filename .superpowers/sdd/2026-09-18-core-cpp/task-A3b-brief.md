# Brief for Task A3b

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).

### Task A3b: Provenance table
Upstream progress is cheap insurance: measured on 2026-09-18, contour `coro`/`net` had 0 commits in 14 days. fastcached's `Async`/`Net` commits were test hygiene, and its board (LASTRADA-Software project 3) has no open coro/net items.

**Files:** Create `.agent/reference/provenance.md`. Modify `tests/cmake/check-cmake-hygiene.cmake` and its self-test.

- [ ] **Step 1: Check first.** Add a hygiene rule `provenance`: every file under `src/core/**` (and `cmake/portable/**`, `cmake/FetchTransferBound.cmake`) must have a row in `.agent/reference/provenance.md`, or a row marked `origin: core-cpp` for new code. Self-test: a file without a row is refused by name, and a row naming a missing file is refused. Run it and confirm it fails on the current tree.
- [ ] **Step 2: Rows.** Fill in one row per file for everything imported in A1–A3, using the SHAs recorded in NOTICE, CHANGELOG and the task reports: `core-cpp path | upstream repo | upstream path | synced SHA | notes`. Expected: the rule passes.
- [ ] **Step 3: Commit.** Commit `docs: a provenance row for every imported file`. Later import and port tasks (A4–A7, B1–B12) append or bump rows in the same commit as the import.

