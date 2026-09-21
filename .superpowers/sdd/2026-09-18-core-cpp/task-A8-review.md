# Review — Task A8 (vendoring tool + consumer-smoke CI)

Diff 3f46e34..2177c41, read in full. One focused run of `cmake/CoreCppVendor.cmake` against a
throwaway repository in the scratchpad (nothing in the working tree, index, HEAD or branches was
touched).

### Spec Compliance

**Part I §5, the vendoring contract**

- ✅ `MODE=sync` enumerates with `git ls-tree -r -z` (`cmake/CoreCppVendor.cmake:250-258`) and reads
  blobs with `git -c core.autocrlf=false -c core.eol=lf cat-file blob` (`:321-324`); the two `-c`
  options are on *every* git call, from one place (`:86-97`).
- ✅ The file set is the spec's: named files `:39-46`, `cmake/**` and `src/core/<module>/**`
  `:58-74`, `tests/` excluded (asserted from the consumer side,
  `tests/consumer-vendored/CMakeLists.txt:63-67`).
- ✅ Refusals, all reported at once rather than first-wins (`:288`, `:353-358`): CR byte `:337-346`,
  symlink `:313-316`, submodule `:307-310` — the last is named by the contract and is implemented
  and tested although the brief's five did not list it
  (`tests/cmake/check-vendor-selftest.cmake:295-303`).
- ✅ `MANIFEST` is `# repository / # ref / # commit / # modules / # files` then `<sha256>  <path>`
  sorted by path (`:388-402`); hashes come from `file(SHA256)` over the files as they lie in `DEST`.
- ✅ Its own line endings are *defined*, not accidental: `:382-387` says they are the writing host's
  and that `check` reads with `file(STRINGS)`, which ignores CR; `docs/vendoring.md:46-49` says the
  same to the consumer. The copied files are unaffected — each is a process's stdout, never
  translated.
- ✅ `MODE=check` calls no git and needs no `REF` (`:128-188`), proved by running it with `PATH`
  replaced by an empty directory (`check-vendor-selftest.cmake:345-363`).
- ✅ `sync` refuses a destination it did not write, *before* anything is written (`:216-225`), and
  stages the whole copy under `DEST/.core-cpp-vendor-staging` so a refusal leaves the previous copy
  intact (`:210-227`, `:353-358`, `:360-378`). A first draft that deleted first is called out in the
  report; the code matches the claim.
- ✅ Consumer obligations in `docs/vendoring.md:65-88` match what `tests/consumer-vendored` does,
  command for command, including registering the check as a ctest.

**"Can `check` be fooled?" — run, not reasoned**

- ✅ A stray dot-file, at the root and nested, is refused (`.local-patch: is not in the manifest`,
  `cmake/.sneaky.cmake: is not in the manifest`). The claim at `:101` that `file(GLOB_RECURSE)`
  matches a leading dot holds.
- ✅ A case-only rename cannot hide: `file(GLOB_RECURSE RELATIVE)` returns the on-disk spelling and
  `IN_LIST` is case-sensitive, so the renamed file is refused as unlisted even where `EXISTS` and
  the hash still match.
- ✅ A manifest path with `..` or a leading `/` cannot *hide* a change: the real file is then
  unlisted and refused (`:166-171`). It can point `file(SHA256)` outside `DEST`, which is untidy but
  not exploitable, since the manifest is unsigned anyway and tampering with it is outside the
  threat model the docs state.
- ❌ An **empty or headers-only MANIFEST over an emptied copy passes** — see Minor 3.
- ⚠️ File modes are outside the contract and outside the tool — see Minor 7.

**The brief, step by step**

- ✅ Step 0 `CORE_CPP_TARGETS`: appended only for compiled kinds (`cmake/CoreCppTargets.cmake:258-269`),
  never by `core_cpp_add_test`; both directions asserted by the consumer
  (`tests/consumer-cpm/CMakeLists.txt:131-166`), and the worked instrumentation loop that is its
  reason to exist is in `docs/getting-started/cpm.md:83-96`.
- ✅ Step 1/2 self-test: 14 cases, the brief's five refusals plus submodule, module selection,
  re-sync, no-git and a `file://` remote. Each case also asserts that **no other** refusal phrase
  fired (`check-vendor-selftest.cmake:169-179`) — that is what keeps a tool that refuses everything
  from passing.
- ✅ Step 3a CPM leg: the consumer's directory properties and cache variables are captured before
  `CPMAddPackage` and compared after, with the changed value named
  (`tests/consumer-cpm/CMakeLists.txt:63-102`); `CORE_CPP_TARGETS` is checked both ways; no
  `core-cpp-*-test` target may exist (`:170-174`); and the `INTERFACE_COMPILE_OPTIONS` /
  `INTERFACE_COMPILE_DEFINITIONS` / `INTERFACE_LINK_OPTIONS` assertions (`:151-156`) read the
  no-PUBLIC-flags rule off the configured targets, closing A1's deferred line-based-scanner minor.
- ✅ Step 3b vendored leg: `CORE_CPP_FETCH_DEPS OFF`, `CORE_CPP_WITH_TUI OFF`, `CORE_CPP_WITH_TLS ON`
  (`tests/consumer-vendored/CMakeLists.txt:45-49`), `if(COMMAND CPMAddPackage)` as the proof that
  nothing was fetched (`:54-58`).
- ✅ Step 3c wasm leg: `CORE_CPP_WITH_TUI OFF`, `CORE_CPP_FETCH_DEPS OFF`, `Threads::Threads` refused
  (`tests/consumer-wasm/CMakeLists.txt:55-65`), emsdk pinned to 3.1.56 (`build.yml:747-752`), run
  under node. It links `core::base core::async core::net_types` rather than the brief's `core::net`
  and awaits no `delay`: that is the dispatch's own ruling ("Do not invent Phase B API"), and both
  the file header (`:13-17`) and the CHANGELOG say B5 extends it.
- ✅ `consumer-smoke` is in `ci-ok`'s `needs` (`build.yml:828`), and `ci-ok` treats skipped and
  cancelled as failure (`:832-843`), so the new leg cannot pass by not running.
- ✅ Fold-in 1: `src/core/tui/TerminalOutput.hpp:86-96` now credits `syncGuard()` with the flush on
  the way in and says what a directly constructed guard does instead.
- ✅ Fold-in 2: the caps reserve the terminator's room (`VtParser.cpp:422-434`), the magnitudes are
  unchanged, `MaxCsiParamLength` says why it needs no reservation (`VtParser.hpp:38-42`), two
  regression tests pin the exactly-at-the-cap behaviour (`VtParser_test.cpp:817-830`, `:873-886`)
  and the CHANGELOG states the limit precisely (`CHANGELOG.md:237-242`).
- ✅ Hygiene: the vendor script's internals are `CORE_CPP_VENDOR_*` / `core_cpp_vendor_*`; `MODE`,
  `REF`, `DEST`, `REPO`, `MODULES` are the command line the spec dictates, so the prefix rule is
  satisfied by the contract, not evaded. The three consumer projects are allowlisted by path with a
  reason that is correct — they are other people's builds
  (`tests/cmake/check-cmake-hygiene.cmake:146-158`) — and so is the `source-glob` row for the
  vendor script. SPDX headers on all six new files; explicit source lists; no PUBLIC flag; nothing
  new under `src/`, so no provenance row is owed; the source map is updated.
- ❌ **The release workflow still calls a mode that does not exist** — see Critical 1.

**Could not verify (⚠️)**

- The CI run ids, the local per-preset results and the mutation tables in the report. I read the
  assertions instead and judged them able to fail; the four CPM mutations in the report's table map
  onto assertions that exist, and the two VtParser regression tests do fail against the old bound by
  inspection (the old bound cut at `cap - 5` and `cap - 1`).
- The container leg's behaviour, which has only ever run on a GitHub runner. Its *construction* is
  sound: it proves isolation (`/sys/class/net` holds only `lo`) and the absence of git rather than
  asserting them (`build.yml:727-740`), and there is no `continue-on-error`, no `|| true` and no
  conditional skip, so an unavailable Docker fails the job rather than reporting green.

### Strengths

- The self-test's **negative half** — every case asserting that no *other* refusal fired — is what
  makes the accepting cases worth something, and the data table of scenarios follows the house style
  of `check-layering.cmake`.
- Building each fixture repository **through git's index** (`check-vendor-selftest.cmake:100-118`) is
  the only way the symlink and gitlink cases can run on a Windows host; without it those two cases
  would have been Linux-only and would have rotted.
- The **CR scan through the hex form** (`CoreCppVendor.cmake:337-346`) is the non-obvious correct
  thing: `file(READ)` opens in text mode on Windows and would have passed a CRLF file through. The
  self-test uses the same scan for its own byte comparison, and the report records the first draft
  that got it wrong.
- **Stage, then replace** means a refusal is never destructive, and the occupant check means a
  `DEST` that is not ours is never emptied. Both are tested.
- The vendored leg **proves** its isolation from inside the container instead of claiming it.
- The CPM leg reads the no-PUBLIC-flags rule **off the configured targets**, which catches the leak
  whatever spelling produced it — a real improvement over the line-based hygiene scanner, and
  honestly reported as such.
- The report's "negative result" (a subdirectory cannot change its parent's directory properties, so
  `add_compile_options()` inside core-cpp is invisible to the before/after comparison) is exactly
  the kind of limit a smoke test should state rather than paper over.

### Issues

#### Critical

1. **`.github/workflows/release.yml:46` invokes `-DMODE=export`, which the tool refuses.**
   `cmake/CoreCppVendor.cmake:113` accepts `sync` or `check` only, so the first tag push dies at the
   *Vendor archive* step with `core-cpp-vendor: MODE must be sync or check, not 'export'`, and no
   `core-cpp-vX.Y.Z-vendor.tar.gz` or `SHA256SUMS` is ever attached — the artifacts
   `docs/vendoring.md:94-97` and `docs/contributing/releasing.md:12-13` promise. This is A8's to fix:
   the workflow's own comment says "cmake/CoreCppVendor.cmake arrives with Task A8"
   (`release.yml:40`) and its `test -f` guard (`:43-44`) exists only until it does. Nothing caught it
   because `release.yml` runs on a tag push and `check-release.cmake` looks at the tag and the
   CHANGELOG, not at the workflow's steps. **Fix:** `-DMODE=sync`; the staged directory is empty, so
   the occupant check passes and the tarball then also carries a `MANIFEST` a packager can `check`.
   Worth adding a `# files` line to the release job's log the way the smoke leg does, and — since
   Phase A ends here and `tag v0.1.0` is the next gate — worth a dry run of the job on a throwaway
   tag.

#### Important

2. **`sync` run with the vendored copy's own script silently vendors the *consumer's* repository.**
   `cmake/CoreCppVendor.cmake:206-208` defaults `REPO` to `<script dir>/..`, and `git -C` ascends to
   the enclosing repository (confirmed: `git -C D:/core-cpp/cmake rev-parse --show-toplevel` prints
   `D:/core-cpp`). Inside `contour/vendor/core-cpp` that enclosing repository is **contour**. The
   documented `check` command and the MANIFEST's own header both point at `<dir>/cmake/CoreCppVendor.cmake`
   (`docs/vendoring.md:29`, `CoreCppVendor.cmake:392`), so re-syncing with the script that is right
   there is the natural next thing to type. If the `REF` resolves in the consumer's repository, the
   run does not refuse: `:362-367` empties the vendored copy, fills it with whatever of the
   consumer's tree matches the file set (`CMakeLists.txt`, `cmake/**`), writes a fresh MANIFEST — and
   `check` then passes on the wreckage, so the consumer's own gate stays green until the build
   fails. The one guard today is the MODULES validation (`:273-285`), and `MODULES` is optional.
   **Fix:** refuse unless the resolved repository is a repository *root* — compare
   `git -C REPO rev-parse --show-toplevel` (or `--git-dir` for the bare clone) with `REPO` — and/or
   require the ref's tree to carry `src/core/` and the named files before anything is copied. Cheap,
   and it turns a silent wrong answer into a sentence.

#### Minor

3. **An empty or headers-only MANIFEST over an emptied copy passes.** With `listed` empty,
   `statedCount` is unset so the count check at `CoreCppVendor.cmake:174-177` is skipped, the unlisted
   scan finds nothing, and the tool prints `matches its manifest: 0 file(s), commit unknown` and
   exits 0. I ran both shapes (truncated manifest, and headers with `# files 0`) and both passed.
   It needs a copy that is gone as well as a manifest that is gone, so the build would fail anyway —
   but the consumer's gate is supposed to be the thing that says so. **Fix:** refuse
   `listedCount EQUAL 0`, and refuse a manifest with no `# files` header at all.
4. **Three implemented refusals have no case and are not in `refusalPhrases`**
   (`tests/cmake/check-vendor-selftest.cmake:33-41`): the unparsable manifest line
   (`CoreCppVendor.cmake:161-163`), the `# files` count mismatch (`:174-177`) and the missing
   `MANIFEST` (`:130-134`). Because they are not in the phrase list, a case that produced one of them
   by accident would not be noticed either. **Fix:** three more rows in the table, and the phrases in
   the list.
5. **The MANIFEST is the one file of a byte-exact copy whose bytes depend on the host that wrote
   it.** `# repository ${REPO}` (`:393`) records the exporting machine's absolute path, and the line
   endings are the writing host's (`:382-387`). The consumer commits this file, so two correct syncs
   of the same tag from different machines produce a whole-file diff, and a packager cannot compare
   their MANIFEST with the released one. Documented, so not a surprise — but consider normalising
   `# repository` (the URL, or nothing when `REPO` is a local path) to leave only the line endings.
6. **`README.md:63-64` gives a sync command that cannot be run as written.** The snippet's two lines
   disagree: `check` uses `-P vendor/core-cpp/cmake/CoreCppVendor.cmake` (read from the consumer's
   root, correct), while `sync` uses `-P cmake/CoreCppVendor.cmake`, which from that same root does
   not exist — and from a core-cpp checkout would drop the copy into core-cpp's own tree. Use
   `-P <core-cpp checkout>/cmake/CoreCppVendor.cmake` as `docs/vendoring.md:24-26` does. Issue 2 is
   why the difference is more than cosmetic.
7. **File modes are outside the contract and outside the tool.** A `100755` blob is written as a
   plain file and `check` never compares modes. `git ls-files -s` shows no non-`100644` file today,
   so nothing is lost now; a refusal for mode `100755` (alongside the symlink and gitlink refusals)
   or a line in `docs/vendoring.md` would make that a decision rather than an accident the day a
   script lands in `cmake/`.
8. **Five of the CPM leg's ten watched values are empty on both sides**: `LINK_OPTIONS`,
   `COMPILE_DEFINITIONS`, `CMAKE_CXX_COMPILER_LAUNCHER`, `CMAKE_POSITION_INDEPENDENT_CODE` and
   `CMAKE_INTERPROCEDURAL_OPTIMIZATION` (`tests/consumer-cpm/CMakeLists.txt:50-61`). They are not
   vacuous — they still catch the direction that matters, empty becoming set, which is precisely the
   launcher promise — but the report's "every consumer-cpm assertion proven able to fail" is true
   only of the four mutations in its table. Either mutate one of these five as well, or say in the
   file that these five are watched for appearance rather than for change.

### Assessment

**Task quality:** Needs fixes.

The vendoring tool is well built for the job contour will hand it — staged and non-destructive,
refusing by name and all at once, with a self-test whose negative half makes its accepting cases
mean something, and the three smoke legs assert the right things and can fail. Two things stand
between it and done: the release workflow still calls `MODE=export`, so the release this tool exists
to serve cannot be cut; and `REPO` defaulting to the script's parent directory lets a plausible
mis-invocation from inside a vendored copy silently replace it with the consumer's own files.
