1. **`src/core/Environment.cpp:129`: identical-write short-circuit checks the last duplicate entry, but readers see the first.**
   ADDRESSED. `src/core/Environment.cpp:128-133`: `unchanged` is now computed only at the first
   match (`!removed && value && substr == *value`) and forced to `false` at any later match, since
   `removed` is already `true` by then (`removed = true;` runs at the end of every matching
   iteration). Traced all four cases: single matching entry equal to value -> `unchanged` stays
   `true`, no publish (correct short-circuit preserved). Single matching entry different from
   value -> `unchanged` false, publishes (unchanged). Two-or-more matching entries, first equal to
   value, later one different (`X=same`, `X=old`) -> `unchanged` forced `false` at the second
   match, so it publishes and collapses to one entry (matches the finding's required fix and the
   pre-regression `48aae7b` behaviour). Two-or-more matching entries, first different, second equal
   (`X=old`, `X=same`, the reported bug case) -> `unchanged` is `false` after the first match
   (`old != same`) and stays forced `false` at the second -> publishes, so the reader now gets the
   correct, most-recent-looking value collapsed to one entry. Removal path (`value` is
   `std::nullopt`) is untouched: `unchanged` can never become `true` there, so behaviour is
   unchanged.
   Test: `src/core/Environment_test.cpp:354-386`, `TEST_CASE("the process-environment writer
   publishes for a variable named twice")`, POSIX-only (`#ifndef _WIN32`). Uses a new
   `InstalledBlock` RAII fixture (`Environment_test.cpp:213-243`) that installs a synthetic
   `environ` block containing two entries for `Name` (a real duplicate scenario, as the finding
   requires) and restores the pre-test block pointer (`_saved`, captured via the default member
   initializer `processEnviron()` before the constructor body runs, restored in `~InstalledBlock()`
   at line 231) regardless of what `setProcessEnvironmentVariable()` published during the test.
   Restoration confirmed correct: `_saved` is initialized in declaration order before `_extra`
   and `_block`, i.e. before the ctor body overwrites `processEnviron()`, so it captures the true
   pre-test pointer; the destructor unconditionally restores it, including on a Catch2 `REQUIRE`
   failure (stack unwinds through the `SECTION`-local `installed` object). The published blocks
   `setProcessEnvironmentVariable()` leaks into the process-lifetime `publishedEnvironment()`
   singleton are not reachable through `processEnviron()` after restore, so nothing observable
   leaks into later tests; this leaking-forever design is pre-existing and documented at
   `Environment.cpp:99-104`, not something this fix introduced.
   Section 1 ("the value only the second entry holds", `Environment_test.cpp:366-374`) is the one
   that exercises the regression: `X=old` then `X=same`, `setProcessEnvironmentVariable(Name,
   "same")`. Manually confirmed this fails on the pre-fix code (last-match comparison: last entry
   is `X=same`, so old code's `unchanged` becomes `true` from the last match, wrongly skipping the
   publish; `live.get(Name)` stays `old`) and passes on the fixed code. This matches the report's
   claimed RED output at `Environment_test.cpp:372` and `:373` exactly (verified those are the
   `CHECK(live.get(Name) == "same")` and `CHECK(entriesNaming(Name) == 1)` lines in the current
   file).
   Section 2 ("the value the first entry already holds", `:376-385`) installs `X=same` then
   `X=old` and checks a publish still happens (`processEnviron() != before`) and collapses to one
   entry. Traced: this also would have passed under the pre-fix code (last-match comparison against
   `X=old` gives `unchanged = false` there too), so it is a guard for the other half of the rule,
   not a second regression reproduction — consistent with the report's own description ("passed
   before the fix too").

2. **`.agent/rules/platform.md:94` contradicted the extended list.**
   ADDRESSED. `.agent/rules/platform.md:94-99` now enumerates what single-threaded Emscripten
   actually lacks (`std::thread`, blocking wait, `Threads::Threads`, sockets, child processes,
   signal handlers) and states explicitly that the filesystem is allowed, explaining why
   (Emscripten's virtual filesystem, which `PosixFileInfoProvider` `lstat()`s, and in-memory pipes
   for `platformRead()`). No longer contradicts `PosixFileInfoProvider`'s presence in the subset
   list.

3. **Nit: `docs/modules/platform.md:46`** unqualified claim about relative symlink resolution.
   ADDRESSED. `docs/modules/platform.md:46` now ends with "(3.1.56 at least)", matching the hedge
   already in `FileInfoProvider.hpp:61`.

4. **Nit: `Environment.hpp:130`**: `unsetProcessEnvironmentVariable()`'s doc relied on "the
   guarantees of `setProcessEnvironmentVariable()`" for the fork/exec restriction.**
   ADDRESSED. `src/core/Environment.hpp:130-136` now states the fork()/exec() restriction and its
   reason directly in `unsetProcessEnvironmentVariable()`'s own doc comment, plus the "removing an
   unset variable ... publishes nothing" clarification.

### New Breakage in the Fix Diff

None. Traced the `unchanged`/`removed` state machine across all matching-entry-count and
value/nullopt combinations (see finding 1 above); found no case where it now under- or
over-publishes relative to what readers observe. The new `InstalledBlock` test fixture correctly
restores the process's `environ` pointer via RAII, including under Catch2's exception-based
assertion failures, so it does not leak state into other tests in the binary. No signature changes
to public API; the two doc-only header edits and two rules/docs edits are text-only and consistent
with surrounding content.

### Out-of-Scope Observations

The fix report (`task-A4-report.md`) ends its "Fix round 2" section with an unfilled template
placeholder, `FIXROUND2_CI`, in the `### CI` section (report line 527), instead of the CI run's
actual status or link. Every other claim in the report (RED/GREEN test output, local build/test
commands across `clangcl-debug`, `clang-debug`, `gcc-debug`, `clang-tidy`, `clang-format --check`,
`mkdocs build --strict`) is concrete and was checked against the diff, but the CI claim itself is
not actually substantiated by the report as written — it names no run, no link, no result. This is
about the report's completeness, not the code diff, so it does not change the diff verdict, but it
means "the emscripten job ran in CI" (report line 523) is currently just prose, not evidence.

### Verdict

**Fix round:** All findings addressed, no new Critical/Important breakage.
(Note: the fix report's own CI section is an unfilled placeholder — see Out-of-Scope
Observations — so the CI claim specifically remains unverified by the report itself, though this
is a report gap, not a diff defect.)
