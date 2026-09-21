# Re-review — Task A8, fix round 1 (2177c41 → 8d1fc18)

Diff read in full (`review-A8-fix1.diff`, 8 commits, 20 files). Three focused runs, all read-only,
all against a clean working tree at `8d1fc18` (nothing in the tree, index, HEAD or branches was
touched; the two mutations were made on *copies* of the tool in the scratchpad):

1. The vendor self-test at HEAD — `28 case(s) were accepted or refused as expected`, exit 0.
2. Two guard mutations, to test the mutation↔case mapping the report claims.
3. A probe of the tool's new module-table reader against the *real* `cmake/CoreCppModules.cmake`.

### Finding Verdicts

**1 — the release path (`MODE=export`). ADDRESSED.**
`.github/workflows/release.yml:46` is `-DMODE=sync`; the `test -f` placeholder guard is gone and
`:47` prints the manifest header, `:48-49` produce the tarball and `SHA256SUMS`. The dispatch asked
for the step to be *run*, not read, and it was: `task-A8-report.md:319-341` shows the five lines
executed verbatim against a throwaway tag in a clone — `378 file(s)` copied, the `# ref` / `# commit`
/ `# files 378` header, a 741026-byte `core-cpp-v0.0.0-a8-dryrun-vendor.tar.gz`, the `SHA256SUMS`
line, `sha256sum -c` → `OK`, and a `MODE=check` of the unpacked copy (`matches its manifest: 378
file(s)`). The dead mode is also pinned by a case now
(`tests/cmake/check-vendor-selftest.cmake:638`, phrase `MODE must be sync or check`).

**2 — silent destruction from a vendored copy's own script. ADDRESSED.**
Two guards: the repository-root check at `cmake/CoreCppVendor.cmake:306-332` (compared through
`core_cpp_vendor_same_directory()`, `:119-134`, which resolves symlinks and is case-insensitive on
Windows) and the core-cpp-tree check at `:451-463`. Both fire before anything is written — the
root check runs before `DEST` is looked at (`:307` comment is accurate; `stagingDir` is created at
`:351`). The case is `refuses-a-sync-from-inside-a-vendored-copy`
(`check-vendor-selftest.cmake:570-631`), and it is a real fixture, not a sketch: a core-cpp-shaped
repository carrying the tool itself, vendored into a consumer repository *with the tool's own
`MODE=sync``, so the copy has a genuine `MANIFEST` and a genuine
`<copy>/cmake/CoreCppVendor.cmake`. It asserts the refusal, the absence of the staging directory,
and that the copy's **own `MODE=check` still accepts it** afterwards (`:621-628`).

Verified by mutation, not by reading — with `:321 if(NOT sameRoot)` forced to `if(FALSE)` on a copy
of the tool, exactly one case goes red and it is this one:

```
refuses-a-sync-from-inside-a-vendored-copy: a sync with the copy's own script accepted what it
  must refuse: -- core-cpp-vendor: v0.0.1 (55c1f7ee…) copied into …/a-consumer/vendor/core-cpp:
  6 file(s), modules base;log
```

That run also settles a question the report does not raise: the tree guard **cannot** catch this
sub-case on its own. `git ls-tree` run with `-C <copy>` lists paths relative to the copy, so the
tree it sees is the copy's own and looks like core-cpp. The repository-root guard is the only thing
standing between a consumer and a silently rewritten copy, and it is the one proven.

**3 — an empty or headers-only MANIFEST passes `check`. ADDRESSED.**
`cmake/CoreCppVendor.cmake:229-262` now requires `# repository`, `# ref`, a 40-hex `# commit`, and
a `# files` count that is present, numeric, non-zero and equal to the lines below it. Two cases:
`refuses-an-emptied-copy-beside-an-emptied-manifest` (`:471`, phrase `header line`) and
`refuses-a-manifest-that-lists-no-file` (`:473`, phrase `never empty`), plus
`refuses-a-manifest-whose-count-is-not-a-number` (`:469`). Verified by mutation — with
`:253 elseif(statedCount EQUAL 0)` forced to `elseif(FALSE)`, exactly one case goes red:

```
refuses-a-manifest-that-lists-no-file: check accepted what it must refuse:
  -- core-cpp-vendor: … matches its manifest: 0 file(s), commit 0123456789…
```

which is verbatim the defect the review observed.

**4 — refusals with no case. ADDRESSED** (with a caveat recorded under New Breakage).
The three the review named now have cases and phrases: the unparsable line (`:465`,
`is neither a header nor`), the count mismatch (`:467`, `but lists`) and the missing MANIFEST
(`:463`, `does not exist, so`). `refusalPhrases` (`check-vendor-selftest.cmake:62-83`) grew from 8
to 21, and the negative half of every case checks against it, so a case firing a refusal it did not
earn now fails. The three command-line refusals gained a case too (`:633-652`).

**5 — commands that cannot be run as written. ADDRESSED.**
`README.md:63-67` now uses `-P /path/to/core-cpp/cmake/CoreCppVendor.cmake` for `sync` and the
copy's own script for `check`, with a comment saying why the two differ;
`docs/vendoring.md:22-35` quotes the `MODULES` argument so a shell cannot eat its semicolons, and
`cmake/CoreCppVendor.cmake:9,32` say the same in the usage text. `task-A8-report.md:410-425`
records every command on both pages executed, with output.

**6 — the MANIFEST's host-dependent bytes. ADDRESSED at the writer, which the item preferred.**
`cmake/CoreCppVendor.cmake:626-629`: `file(CONFIGURE … @ONLY NEWLINE_STYLE UNIX)`, with the
content's own `@` routed through a variable holding one so `@ONLY` substitution cannot eat a
`git@github.com:` REPO. The round-trip is sound (`@` → `@CORE_CPP_VENDOR_AT@` → `@`), `@ONLY`
leaves `${…}` alone, and `#cmakedefine` cannot be hit because every header line has a space after
`#`. The self-test asserts the written manifest carries no CR byte
(`check-vendor-selftest.cmake:352-356`) on every accepting case — note this assertion can only go
red on a Windows host, which the report's Windows run covers. `# repository <absolute path>`
remains host-dependent; that was review Minor 5's "consider", not on this fix list, and is
explicitly left alone (`task-A8-report.md:434-435`).

**7 — file modes outside the contract. ADDRESSED.** `docs/vendoring.md:68-71`, one paragraph,
including the forward obligation ("nothing in it may become executable without this line changing
first"). The contract is not widened. Also in the CHANGELOG (`:191`).

**8 — five watched values empty on both sides. ADDRESSED.**
`tests/consumer-cpm/CMakeLists.txt:45-59` gives all five a real value: a compile definition, a
`-L` / `/LIBPATH:` search path, `CMAKE_POSITION_INDEPENDENT_CODE ON`,
`CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF`, and a launcher of the consumer's own
(`"${CMAKE_COMMAND}" -E env`), which is the one that makes the launcher promise a real assertion
rather than a watched empty string. The comparison at `:104-116` is unchanged, so all ten values
are now change-detecting rather than appearance-detecting.

**9 — a failed sync leaves its staging directory inside a good copy. ADDRESSED.**
`core_cpp_vendor_unstage()` (`cmake/CoreCppVendor.cmake:74-78`) and a call before every
`message(FATAL_ERROR)` reachable after `file(MAKE_DIRECTORY "${stagingDir}")` at `:351` — I checked
each: `:364` (clone), `:387` (ref shape), `:405` (ls-tree), `:439` (not a core-cpp tree), `:457`
(unknown modules), `:492` (omitted modules), `:582` (blob refusals), and `:120` inside
`core_cpp_vendor_git()`, which covers the `rev-parse` of the 40-hex path and the module-table read.
The macro reads a top-level variable, so it works from inside the function too, and is a no-op
before staging exists. The case is `a-failed-sync-leaves-the-copy-alone`
(`check-vendor-selftest.cmake:525-568`): it runs *both* shapes of failure over a good copy — one
before the tree is read (unresolvable ref, the exact `v9.9.9` scenario the review observed) and one
after every blob is staged (a CR byte) — and asserts after each that the staging directory is gone
**and that the copy's own `MODE=check` still accepts it**. That is the stronger assertion the item
asked for.

`docs/vendoring.md:53-54`'s promise now matches the behaviour. Its first clause ("writes nothing
into `DEST` until the whole copy is legal") is still literally an overstatement — see New Breakage.

**10 — the WebAssembly pthread guard could never fire. ADDRESSED.**
`tests/consumer-wasm/CMakeLists.txt:60-113` collects core-cpp's targets through
`BUILDSYSTEM_TARGETS`/`SUBDIRECTORIES`, filters to `^core-cpp-`, **refuses to run at all if it
collected none** (`:74-78` — the assertion cannot pass by having nothing to look at, which was the
old defect's shape), and scans `LINK_LIBRARIES` and `INTERFACE_LINK_LIBRARIES` for
`Threads::Threads` or `-pthread`. The RED at `task-A8-report.md:459-473` is the strongest form
available: it injects `Threads::Threads` and keeps a probe for the old guard in the same run, which
prints `the OLD guard … is FALSE here` while the new one names the target and property. The CI
`ninja -t commands | grep pthread` check is kept alongside.

**11 — the flag assertion skipped INTERFACE targets. ADDRESSED.**
`tests/consumer-cpm/CMakeLists.txt:170-182`: the `INTERFACE_COMPILE_OPTIONS` /
`INTERFACE_COMPILE_DEFINITIONS` / `INTERFACE_LINK_OPTIONS` loop is now its own `foreach` over
`coreCppTargetsInTree` (collected at `:127-144`, which includes INTERFACE libraries because
`BUILDSYSTEM_TARGETS` does) rather than over `published`. 11 targets against 9 published; the two
extra are `core-cpp-async` and `core-cpp-net_types`, exactly the ones the CHANGELOG's claim was
about. RED at `task-A8-report.md:482-491` injects an `INTERFACE` option on `core-cpp-async` — the
target the old loop never looked at — and the leg fails. The status line at `:198-201` now reports
both counts, so the difference is visible in the log.

**12 — a branch accepted as a REF. ADDRESSED.**
`cmake/CoreCppVendor.cmake:370-395`: a 40-hex SHA goes through `rev-parse --verify <sha>^{commit}`,
anything else must resolve as `refs/tags/<REF>^{commit}`. Three cases —
`refuses-a-branch-as-the-ref`, `refuses-head-as-the-ref`, `refuses-a-sha-the-repository-has-not`
(`check-vendor-selftest.cmake:431-437`) — plus both accepting shapes
(`syncs-a-tag-then-checks`, `syncs-a-full-commit-sha-then-checks`, `:421-427`). The two callers
still work: `release.yml:46` passes the tag, `build.yml:702` passes `$GITHUB_SHA` (40 hex).

**13 — the echo server's short read. ADDRESSED, and the fix is right.**
`tests/consumer-shared/ConsumerSmoke.hpp:73-96`: the server now takes the expected length and
accumulates to it, mirroring the client. The judgement asked for: the fix is correct independently
of how the RED was obtained. `read()` returning a prefix is permitted by the interface, the old
code echoed that prefix and closed, and the client then blocked to the 120 s ctest timeout with
`FAIL` on a line that does not explain itself. Modelling the short read (a 5-byte cap,
`task-A8-report.md:499-509`) is the only honest way to exercise it, and the RED output is
diagnostic rather than cosmetic — `ok the server flow echoed the whole request` beside
`FAIL the client flow read its own bytes back` is the review's point stated by the test itself.
The loop is bounded (`*read == 0` → `co_return`), the buffer cannot overrun
(`checks.expect(greeting.size() <= BufferSize)` at `:126-128`), and both legs keep `TIMEOUT 120`.

**14 — the self-test fails instead of skipping without git. ADDRESSED.**
`tests/cmake/check-vendor-selftest.cmake:36-51` prints `check-vendor-selftest: SKIPPED …` and
exits with the skip code where `cmake_language(EXIT)` exists (CMake 3.29; the project's floor is
3.25), returning otherwise; `tests/CMakeLists.txt:70-73` sets **both** `SKIP_RETURN_CODE` and
`SKIP_REGULAR_EXPRESSION`, so whichever ctest can see, the run reports as skipped and neither path
reads as a pass. `CORE_CPP_SKIP_EXIT_CODE` is really defined (`cmake/CoreCppTargets.cmake:57` = 77),
so neither property is empty.

**15 — `MODULES` omitting an unconditional module. ADDRESSED.**
`cmake/CoreCppVendor.cmake:465-499` reads the ref's own table and requires every `core_cpp_module()`
row without a `WHEN` to be in `MODULES`. Case `refuses-modules-that-omit-an-unconditional-module`
(`check-vendor-selftest.cmake:449-451`).

*How it fails if the table's shape changes* — I ran the reader against the real
`cmake/CoreCppModules.cmake`:

```
matched 14 row-like fragments
  6 × SKIP (no NAME):  core_cpp_module(${arg_NAME}) / core_cpp_module()   ← message() strings
  UNCONDITIONAL: base log cli testing async platform net
  conditional:   tui
  ⇒ the documented base;log;cli;platform;async;net;testing is accepted, nothing omitted
```

Correct today, and correct for the right reasons: `core_cpp_module_target(` never matches the
literal `core_cpp_module(`, so `net_types`/`net_tls`/`tui_output` are invisible (they are targets,
not directories); the multi-line `tui_output` row is irrelevant but would have parsed, since
`[^)]*` crosses newlines; and the six `message(FATAL_ERROR "core_cpp_module(${arg_NAME}): …")`
fragments survive the scan only because `${arg_NAME}` puts no space after `NAME`.

That last one is the thin ice. The two directions are not symmetric: a *phantom* row (someone
writing `core_cpp_module(NAME x)` inside a message) causes a false refusal, which is loud and
immediate; a *missed* row (a row carrying a `)` inside a value, a row generated by a loop or macro,
a rename of the table function) silently restores the original defect. The fixture table
(`check-vendor-selftest.cmake:105-106`) is two clean single-line rows with no comments, no
`message()` strings and no target rows, so it proves the mechanism and pins **none** of that shape —
it would not catch a shape change. The real backstop is CI's `consumer-smoke (vendored)` leg, which
syncs the real repository with the documented list (`build.yml:702-704`); that catches the loud
direction, not the silent one. The report's Concern 3 says roughly this, honestly. The finding as
written is delivered; the residual risk belongs in the design log, not in this round.

**16 — SyncGuard. ADDRESSED, and it did not change `syncGuard()` or double-flush.**
`src/core/tui/TerminalOutput.cpp:300-309`: the constructor's first statement is `_output->flush()`.
`syncGuard()` (`:253-256`) is now `return SyncGuard(*this);` — guaranteed elision, so exactly one
construction and exactly one flush; its observable behaviour (flush, then `CSI ?2026h`) is
byte-for-byte what it was. The move constructor (`:322-324`) steals without flushing, which is
right; move-assignment (`:326-338`) ends the old region with its own flush, unchanged. The header's
paragraph documenting the asymmetry is replaced by one describing the symmetry
(`TerminalOutput.hpp:87-92`). The new case `a directly constructed SyncGuard flushes what came
before it` (`TerminalOutput_test.cpp:68-85`) asserts the capture *inside* the region, and the report
shows it failing against the old code with `"" == "before"` (`task-A8-report.md:542-546`).

**17 — the minors. ADDRESSED.** The ~120 duplicated lines are `tests/consumer-shared/ConsumerSmoke.hpp`
(which is why item 13 was one fix, not two); the CR scan is a `string(FIND)` walk with an even-offset
test (`CoreCppVendor.cmake:552-570` — correct: `0d` cannot overlap itself, so advancing by `at + 1`
misses nothing, and the empty-file and no-hit paths are right); the redundant `size() >=` guard
before `ends_with()` is gone (`VtParser.cpp:652`); the two `list(JOIN … ";" …)` no-ops are gone
(`:457-459` and `:575-576` now join with `", "`, and the manifest's `# modules` line uses `${MODULES}`
directly, which produces identical bytes).

### New Breakage in the Fix Diff

All Minor. Nothing Critical or Important.

1. **Minor — the self-test's new header over-claims its own coverage.**
   `tests/cmake/check-vendor-selftest.cmake:11-13` (commit `8d1fc18`): "Every refusal the script
   implements has a case here, bar the two named at `refusalPhrases` below that no case can reach
   portably." At least six more have neither a case nor a phrase: `cloning … failed` (`:365`),
   `listing the tree … failed` (`:406`), the two `core_cpp_vendor_git()` wrappers at `:318` and
   `:466`, `reading blob … failed`, and the unparsable `ls-tree` line. Calling these "git-failure
   wrappers rather than refusals" would be defensible, except that `resolving the commit` is one too
   and it *does* have a case and a phrase — so the file does not apply that distinction
   consistently. Separately, `CoreCppVendor.cmake:578-580` (`has no file of the vendored set for
   modules …`) is now **unreachable**: the tree guard at `:451-463` refuses any ref without
   `cmake/CoreCppModules.cmake`, which is always in the file set, so `copied` can never be empty.
   Either name these the way the two named ones are named, or drop the word "every".

2. **Minor — a stale attribution survives in the very comment item 16 was about.**
   `src/core/tui/TerminalOutput.cpp:317`, in `~SyncGuard`: "syncGuard() flushes on the way in, so
   the two ends match." The flush on the way in is the constructor's now. The header and the
   CHANGELOG were both updated; this one line was not.

3. **Minor — the new hygiene allowlist row is avoidable, so it is not as narrow as it can be.**
   `tests/cmake/check-cmake-hygiene.cmake:147-148` exempts the whole of
   `check-vendor-selftest.cmake` from the `source-glob` rule (the rule is per-file, not per-line)
   for one `file(GLOB)` at `:218`, inside `core_cpp_selftest_empty()`, which preserves the
   `MANIFEST` while deleting everything else. The reason given is true as far as it goes — but both
   call sites (`:393-394` and `:397-400`) **overwrite that MANIFEST on the next line**, so
   `file(REMOVE_RECURSE "${copy}")` followed by the same `file(WRITE)` is exactly equivalent and
   needs no glob and no allowlist row at all. An escape hatch that can be zero should be zero.

4. **Minor — two documentation gaps left by the round's own changes.**
   - `docs/vendoring.md:53` still says `sync` "writes nothing into `DEST` until the whole copy is
     legal", although the staging directory is `DEST/.core-cpp-vendor-staging` (`:351`). The
     *consequent* ("a refusal leaves the previous copy exactly as it was") is now true and proven,
     which is what item 9 asked for; the first clause is the sentence that misled the previous
     round's reader and is still there.
   - `docs/vendoring.md:64-67` and `CHANGELOG.md:178-181` list only the `# files` family among the
     new `check` refusals. Three more are now implemented and undocumented: a missing
     `# repository` line, a missing `# ref` line, and a `# commit` that is not 40 lowercase hex
     (`CoreCppVendor.cmake:231-245`), plus the unparsable-line refusal at `:217`. A consumer can
     hit all of them by hand-editing a manifest.

### Out-of-Scope Observations

- **The replacement phase is the one window that is not transactional.**
  `CoreCppVendor.cmake:590-606` empties `DEST` and *then* renames the staged entries in; a
  `file(RENAME)` failure there (a permission error, a locked file on Windows) aborts with `DEST`
  half-replaced. Nothing in this round's findings covers it and the exposure is small, but it is
  the only remaining path on which a `sync` can damage a copy.
- **`# repository` still records the exporting machine's absolute path**, so two correct syncs of
  the same tag from different machines still differ in that one line. Deliberately left (review
  Minor 5 said "consider"); worth deciding before v0.1.0, since contour commits the file.
- **`CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND}" -E env`** in the CPM leg puts a fork in front of
  every translation unit of that job. It is what makes the launcher assertion real, and the report
  names the trade-off; no action unless the job's time shows it.

### Verdict

**Fix round:** All findings addressed, no new Critical/Important breakage.

The implementer's specific claim about the inherited self-test holds up under mutation, which was
the thing worth checking: the test runs (28 cases, green at `8d1fc18`), and the two guards I broke
on copies of the tool each turned exactly one case red, each the right one. The
`refuses-a-sync-from-inside-a-vendored-copy` case in particular is not the sketch the previous
round had — and the mutation run showed *why* that matters, since the tree guard alone cannot catch
that invocation. The four Minor items above are cleanup: a comment, a sentence, an avoidable
allowlist row, and two doc omissions.
