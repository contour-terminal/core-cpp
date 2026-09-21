# Task A3b report: provenance table

Status: **DONE**. Commit `6dda244` is on `origin/master`, and the `Build` workflow on it is green:
https://github.com/contour-terminal/core-cpp/actions/runs/35347656114 (`success`, 0 non-success jobs).

## Commit

| SHA | Subject |
|---|---|
| `6dda244` | docs: a provenance row for every imported file |

Ends with `Signed-off-by: Christian Parpart <christian@parpart.family>`. LF only (`grep -cU $'\r'` is
0 on every changed and new file). Nothing under `.superpowers/` was committed.

## What was built

- **`.agent/reference/provenance.md`**: one row per file, columns `core-cpp path | upstream repo |
  upstream path | synced SHA | notes`. 52 rows, matching every file currently under `src/core/**`,
  `cmake/portable/**` and `cmake/FetchTransferBound.cmake` exactly (verified: `find` over those
  paths lists 52 files; the table has 52 `| \`...\`` rows; no duplicate path).
- **The `provenance` hygiene rule** (`tests/cmake/check-cmake-hygiene.cmake`): a rule over the tree
  against the table, not a line, following the same shape as `namespace-directory` and
  `stale-allowlist`. It parses the table's first column (stripping backticks and the header/
  separator rows) and refuses (a) a scoped file with no row, named by the file, and (b) a row
  naming a file that no longer exists, named by the table. Declared in `CORE_CPP_HYGIENE_RULES` so
  `LIST_RULES` and the self-test's rule/case handshake see it.
- **Self-test cases** (`tests/cmake/check-cmake-hygiene-selftest.cmake`): a `provenance.md` fixture
  added to the clean tree (6 rows, `origin: core-cpp`, for the existing `src/core/foo/*` and
  `src/core/{Top,Base64}.hpp` fixtures), plus two `provenance` violation cases: an extra file with
  no row, and a table row naming a nonexistent file.
- **`.agent/rules/library-hygiene.md`**: new "Provenance" section — import tasks add rows in the
  same commit, the hygiene rule enforces the table mechanically, Task B12b's catch-up check and
  consumer migrations' delta checks read it.
- **`AGENT.md`**: links `.agent/reference/provenance.md` next to `consumers.md`/`source-map.md`.

## Row counts per upstream

| Upstream | Rows | SHA(s) |
|---|---|---|
| contour-terminal/contour | 41 | `6777ff05014f8ff163b071e8b0e942830119db80` |
| contour-terminal/endo | 1 (+ named in 2 merge notes) | `f774a210ce989e5947b8f61d715068b1dc96088c` |
| LASTRADA-Software/fastcached | 6 (+ named in 2 merge notes) | `eb9c9c68da8fadfd43b0b36366919cb462689f48` (CompileCache/FetchTransferBound + the `SuppressWindowsDialogs` merge), `ee71f868547712892b7d9a2ebff60d49c496e25c` (Profiling/Ranges + tests) |
| origin: core-cpp | 4 (+ split/merge notes) | - |

(`SuppressWindowsDialogs.{hpp,cpp}` each name contour as primary and list contour's
`coro/testing` copy, endo's and fastcached's copies in notes — a 4-way merge, per the brief's
"adapted" rule.) All four SHAs were resolved read-only: `git -C D:\contour rev-parse
6777ff05014f8ff163b071e8b0e942830119db80`, and likewise for endo and both fastcached SHAs — all
four resolve exactly to the 40-hex commits NOTICE/CHANGELOG already record. `D:\contour`,
`D:\endo` and `D:\fastcached` were never modified. Every upstream path was confirmed against a
read-only `git ls-tree -r --name-only <sha>` of the respective repo.

## RED/GREEN evidence (WSL `cmake -P`, no build needed for these scripts)

**RED 1 — self-test, before the rule existed** (only the fixture/cases were added):
```
CMake Error at tests/cmake/check-cmake-hygiene-selftest.cmake:147 (message):
  hygiene-selftest:
    case 'provenance' names no rule of the scanner
    case 'provenance' names no rule of the scanner
    provenance: src/core/foo/Extra.cpp was not refused
    provenance: .agent/reference/provenance.md was not refused
```

**GREEN 1 — self-test, after the rule was implemented** (table not yet created in the real tree):
```
-- hygiene-selftest: the clean tree passed and all 22 violations were refused by name
```

**RED 2 — the real tree, before `.agent/reference/provenance.md` existed:**
```
CMake Error ... check-cmake-hygiene: 52 violation(s):
    cmake/FetchTransferBound.cmake:-: [provenance] ... no row in .agent/reference/provenance.md
    cmake/portable/CompileCache.cmake:-: [provenance] ...
    ... (52 files, one line each)
```

**GREEN 2 — the real tree, after the table was written:**
```
-- check-cmake-hygiene: 69 file(s) under /mnt/d/core-cpp are clean
```

## Local results

| Configuration | Result |
|---|---|
| WSL `clang-debug`, `ctest --preset clang-debug -R hygiene` | 2/2 passed (`core-cpp.cmake-hygiene`, `core-cpp.cmake-hygiene-selftest`) |
| WSL `clang-debug`, full `ctest --preset clang-debug` | 9/9 passed |
| Windows `clangcl-debug`, `ctest --preset clangcl-debug -R hygiene` | 2/2 passed |
| Windows `clangcl-debug`, full `ctest --preset clangcl-debug` | 12/12 passed |
| `python scripts/clang-format.py --check` (WSL and Windows) | 46 files formatted with clang-format 22.1.8, clean both times |

No `.cpp`/`.hpp` files were touched, so clang-format had nothing new to reformat; it was run to
confirm the gate stays green.

## CI

Push to `origin/master` at `6dda244`. `Build` run
[35347656114](https://github.com/contour-terminal/core-cpp/actions/runs/35347656114): **success**,
0 non-success jobs (`gh run view --json conclusion,status,jobs`). This repository's CI does not
gate a docs-only commit's `Docs` workflow the same way `ci-ok` gates a code change; the `Build`
workflow is what ran and it is green.

## Concerns

1. **Two files' upstream path is inferred, not from a report.** `Overloaded.hpp`'s row names
   `src/crispy/Utils.hpp` (verified with `git show 6777ff05:src/crispy/Utils.hpp | grep
   Overloaded`, which finds `struct Overloaded` there) because A3's report says the type was
   merged from "the global `::Overloaded` and `crispy::Overloaded` (Utils.hpp)"; I did not find a
   separate upstream file for the ambient global one, so I attributed the row to the one file I
   could verify.
2. **`src/core/log/Assert.hpp` and `src/core/Assert.hpp` name the same upstream path and SHA.**
   That is deliberate: A3 split crispy's one `Assert.hpp` into two core-cpp files to close a
   base→log layering cycle, and each row's notes cross-reference the split.
3. **`cmake/portable/README.md` is in scope** (it is under `cmake/portable/`) even though it is
   core-cpp's own prose, not a copy — the rule catches every file by path, not by kind, so it got a
   row (`origin: core-cpp`) rather than an exemption.
4. **The rule does not enforce "exactly one row" beyond what the two required behaviors cover** (a
   missing row, and a row naming a missing file). It does not detect a path listed twice with
   different upstream data; I checked the current table has no duplicate paths by hand
   (`sort | uniq -d` is empty) rather than adding an untested third check, to keep the rule scoped
   to what the brief specifies and tests.
5. **Parsing is a plain-text table scan, not a Markdown parser.** It recognizes the header by exact
   text (`core-cpp path`) and the separator by `^[:-]+$` after backtick/whitespace stripping; a
   future edit that reorders columns or renames the header would silently stop being recognized as
   the header (and would then be read as a bogus row). This matches the existing scanner's general
   style (regex over lines, not a real parser).

## Report path

`D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A3b-report.md`
