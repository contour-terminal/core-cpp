# Task A3b review: provenance table

## Spec Compliance

- ✅ `.agent/reference/provenance.md` created with columns `core-cpp path | upstream repo | upstream path | synced SHA | notes`, exactly as dispatched.
- ✅ Repos named `contour-terminal/contour`, `contour-terminal/endo`, `LASTRADA-Software/fastcached` — verified by grep, no other spellings used.
- ✅ Synced SHAs are full 40-hex. The only short (7-char) hashes in the table are core-cpp's own local commit references inside the *notes* column (`48b261a`, `6b1a4d7`, `fe62488`, `26de633` — all four resolve in `git log`), not upstream sync SHAs; this doesn't violate the "full 40-hex SHA" requirement, which is about the synced-SHA column.
- ✅ 52 rows, exact 1:1 correspondence with the 52 files actually under `src/core/**`, `cmake/portable/**` and `cmake/FetchTransferBound.cmake` (diffed by hand — identical sets, no dup, no missing, no extra).
- ✅ `provenance` hygiene rule added to `tests/cmake/check-cmake-hygiene.cmake`, covering exactly the three scoped paths, refusing (a) a scoped file with no row, named by the file, and (b) a row naming a missing file, named by the table.
- ✅ Self-test written first: RED1 (self-test fails before the rule exists, quoted in the report) → GREEN1 (self-test passes once the rule exists, table not yet real) → RED2 (real tree fails, 52 violations, before the table was populated) → GREEN2 (real tree clean). I independently re-ran both scripts natively on Windows (`cmake -P`, not through ctest) against the real tree and got the same results: `check-cmake-hygiene: 69 file(s) ... are clean` and `hygiene-selftest: the clean tree passed and all 22 violations were refused by name`.
- ✅ `.agent/rules/library-hygiene.md` gained the "Provenance" paragraph; `AGENT.md` links the table next to `consumers.md`/`source-map.md`.
- ✅ Commit `6dda244` ends with the correct `Signed-off-by` trailer; nothing under `.superpowers/` was committed; all five changed files are LF-only (verified with `tr -cd '\r' | wc -c` → 0 for each, byte-level, not just the report's grep claim).
- ✅ CI: `gh run view 35347656114` independently confirms `conclusion: success`, `headSha` matches `6dda244`.

## Strengths

- Every SHA in the table resolves in its named upstream repo, and I spot-checked upstream path content against 6 rows (Assert.hpp split, Profiling.hpp, Ranges.hpp, SuppressWindowsDialogsAtStartup.cpp, CompileCache.cmake, FetchTransferBound.cmake) plus the two multi-way merges (`Overloaded.hpp`'s `crispy::Overloaded` in `Utils.hpp`, and the four-way `SuppressWindowsDialogs.hpp` merge across contour/coro, endo and fastcached) — all check out exactly as the notes describe. The two `Assert.hpp` rows' split is real: the single upstream file genuinely contains both symbol groups (`Require`/`Guarantee`/`unreachable`/`setFailHandler` and `fatal`/`SoftRequire`).
- The `provenance` rule's table parser is genuinely robust to the one thing I'd have expected to break it: several notes cells contain literal `;` characters (e.g. the two `Assert.hpp` rows, `Ranges.hpp`, `Utils.hpp`, both `SuppressWindowsDialogs.*` rows). I wrote and ran an isolated `file(STRINGS)` test confirming CMake escapes embedded semicolons per line, so the table read back as exactly one list element per physical line — no accidental row-splitting.
- `scanned` is built from `file(GLOB_RECURSE ... RELATIVE ...)`, which CMake always returns as forward-slash paths regardless of host OS — I confirmed this by running the scanner and self-test natively on Windows with a backslash-style `ROOT` (`D:/core-cpp`), getting identical results to the WSL run in the report. The "Windows vs. Linux path spellings" concern in the dispatch is a non-issue by construction.
- Honest, well-reasoned "Concerns" section in the report (Overloaded.hpp's inferred upstream file, the deliberate Assert.hpp split, cmake/portable/README.md's in-scope-by-path status, no duplicate-row detection, plain-text vs. real Markdown parsing) — all five concerns match what I found by independent inspection, and none of them are things the brief actually required.

## Issues

None found — Critical, Important, or Minor.

## Assessment

Task quality: **Approved**
