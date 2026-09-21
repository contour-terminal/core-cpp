# Review: Task A5b — rename `core::coro` to `core::async`; `Generator` moves to base

Base `36c4eeb`, head `e45f730`.

### Spec Compliance

- ✅ Module renamed per the brief's table: directory `src/core/coro/` → `src/core/async/`
  (`git mv`, history follows: `git log --follow` on `src/core/async/StopToken.hpp` walks back
  through `12bb267` into the pre-rename `coro:` commits). Namespace `core::coro` → `core::async`
  everywhere in code (`src/core/async/{Awaitable,Cancellation,StopToken,Task,UniqueCoroHandle,
  WhenAll,WhenAny}.hpp` and their tests).
- ✅ Targets/tests/labels/macros follow the table exactly:
  `cmake/CoreCppModules.cmake:99` (`core_cpp_module(NAME async KIND INTERFACE ...)`),
  `src/core/async/CMakeLists.txt:8,20,25` (`core_cpp_add_module(async ...)`,
  `core_cpp_add_test(async ...)`, `core_cpp_add_test(async NAME async-fallback ... DEFINITIONS
  CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK)`), which mechanically derive ctest `core-cpp.async` /
  `core-cpp.async-fallback` and label `async` from `core_cpp_add_test`'s own convention
  (`.agent/rules/testing.md:14`). `CORE_CORO_FORCE_STOP_TOKEN_FALLBACK` →
  `CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK` (`src/core/async/StopToken.hpp:47,58`). The two macros
  and the CMake probe variable the brief's table didn't spell out by name
  (`CORE_CORO_STOP_TOKEN_IS_STD`/`_HAS_THREADS`, `CORE_CPP_CORO_STOP_TOKEN_PROBE`) were renamed
  too, consistently, and nothing was left half-renamed.
- ✅ `Generator` moved to base per the table: `git mv src/core/coro/Generator{,_test}.*
  src/core/` (history follows), namespace `core::coro` → `core` (`src/core/Generator.hpp:19,242`),
  `CORE_GENERATOR_FORCE_FALLBACK` and `detail::GeneratorFallback` unchanged
  (`src/core/Generator.hpp:37,242,243`). `src/core/CMakeLists.txt:18,32` adds `Generator.hpp` and
  `Generator_test.cpp` to `core::base`. `FileSystem.hpp:4` includes `<core/Generator.hpp>`, and its
  virtual method's return type is the unqualified `Generator<DirectoryEntry>`
  (`FileSystem.hpp:163`), resolved the same way the old `coro::Generator` was, from the enclosing
  `core::platform` namespace — confirmed by build success, not just inspection.
- ✅ `platform` no longer depends on the coroutine module anywhere: `cmake/CoreCppModules.cmake:106`
  (`DEPS base log`, dropped `coro`), `src/core/platform/CMakeLists.txt:81` (`PUBLIC_LIBS
  core::base`, dropped `core::coro`). Grepped the whole tree for a remaining `platform`-to-`async`
  edge; none found.
- ✅ The hygiene rule extension does what Step 1 of the brief asks, correctly targeted. TDD
  evidence is real: `tests/cmake/check-cmake-hygiene-selftest.cmake:34` adds the exact case
  described (`namespace core::Async` overwriting `src/core/Base64.hpp`, a directory-correct but
  wrong-case *nested* segment), the report's quoted RED (`namespace-directory: src/core/Base64.hpp
  was not refused`) matches what the pre-change scanner would produce, and the extension itself
  (`tests/cmake/check-cmake-hygiene.cmake:253-256`, `elseif(declared MATCHES "[A-Z]")`) is the
  minimal fix. Verified by hand that the total violation-case count the GREEN log cites (23) matches
  `grep -c` over the selftest's case list. Confirmed no false-positive path: the check only runs
  once per file, against the *first* declared namespace (`expectedNamespace` is cleared right after
  matching, `check-cmake-hygiene.cmake:231,257`), so `detail`, other nested helpers, namespace
  aliases (explicitly excluded by `CORE_CPP_HYGIENE_NAMESPACE_ALIAS_REGEX`) and `std::formatter`
  specializations (which use out-of-line `struct std::formatter<...>`, never reopening `namespace
  std`, e.g. `src/core/Flags.hpp:156`) are never examined by this code path, clean or not — this is
  unchanged pre-existing behaviour, not a new gap the rename introduced.
- ✅ `.clang-tidy` gains `readability-identifier-naming.NamespaceCase: lower_case`
  (`.clang-tidy:190`), and CI's `clang-tidy` job is reported green on `e45f730` (24 other jobs were
  already green on the first push; the tidy job failure and its fix are discussed under Issues).
- ✅ Docs: `docs/modules/coro.md` → `docs/modules/async.md` via `git mv` (71% similarity), nav
  updated (`mkdocs.yml:108`), zero remaining links to `coro.md` anywhere in the tree. `base.md`
  gained a "Generator" section (`docs/modules/base.md:30-53`) with the same content the old
  `coro.md` had; `async.md`'s own section was fully removed, replaced by one linking sentence
  (`async.md:7-10`) — this is a straight move, not a duplication (verified: `Generator` appears
  in `async.md` exactly once, in that link sentence). The implementer's self-flagged "duplicates
  prose" concern does not describe the tree as it stands; see Issues/Minor.
- ✅ CHANGELOG per Ruling R36: no **Breaking** heading anywhere in the file (checked); `[Unreleased]`
  entries rewritten to `core::async`/`core::Generator` throughout (`CHANGELOG.md:55-88`); the import
  rows still name contour's upstream `src/coro/...` at `6777ff05` (`CHANGELOG.md:135`) — upstream
  path untouched, only the core-cpp destination and namespace changed.
- ✅ The four fold-in items, verified individually:
  1. `src/core/async/StopToken.hpp:5-7`: "libc++ before 20 has `<stop_token>` only behind
     `-fexperimental-library` (emsdk 3.1.56's libc++ 17 among them)" — broadened as asked.
  2. `docs/modules/async.md:29` and `CHANGELOG.md:24`: "AppleClang 17 (measured in CI)" in both
     places, "likely AppleClang" gone tree-wide.
  3. `src/core/async/StopToken_test.cpp:451-452`: "... so that the destructor meets it running"
     replaced with "...; the gate makes it likely, not certain, that the destructor meets it
     running."
  4. The 114-column line is gone; longest non-URL line in `async.md` is 98 columns (checked with
     `awk '{print length}' | sort -rn`); the one 130-column line is a bare doc link, consistent
     with the rest of the file's convention of not wrapping URLs.
- ✅ Disambiguation of the two senses of "coro" is correct throughout. Re-ran the brief's exact
  empty-grep myself (`rg -n "core::coro|core/coro|core-cpp-coro|CORE_CORO_|core-cpp\.coro"
  --glob '!docs/superpowers/**' --glob '!.superpowers/**'`): no output. A broader case-insensitive
  sweep for any `coro` outside the word "coroutine" turns up only legitimate survivors: contour's
  own `src/coro/...` upstream paths (provenance, NOTICE, CHANGELOG import rows, `async.md`'s "read
  from contour's `src/coro`"), and identifiers that merely contain the substring —
  `UniqueCoroHandle`, `ThisCoroStopToken`/`thisCoroStopToken()` — which are core-cpp's own type and
  function names, not module-name references, and were correctly left alone.
  `.agent/rules/async-and-net.md:175`'s `coro::IExecutor` → `async::IExecutor`: verified
  independently against `git -C D:/fastcached` `origin/master` — `IExecutor` lives in
  `FastCache::Async`, nothing in fastcached is spelled `coro::`, so the file's original
  "coro::IExecutor" was core-cpp's own shorthand for its own future module (matching the file's
  established convention of bare `coro`/`net` as shorthand elsewhere), not a literal fastcached
  name. The rename to `async::` is the correct resolution, not a guess.
- ⚠️ Local Windows/WSL build and CI results are per the implementer's report; I did not re-run the
  matrix (out of scope per the review brief) beyond the two focused checks above (the hygiene
  selftest counts, and the fastcached grep). The controller states CI run 35386108380 on `e45f730`
  is green, which I did not re-verify against GitHub directly.
- ✅ `App.cpp`'s renamed alias (`namespace CLI = core::cli;` → `namespace cli = core::cli;`,
  `src/core/cli/App.cpp:40`) has no public surface: it's a `.cpp`-local alias, not in any header or
  `FILE_SET HEADERS`, used only at its own six call sites in the same translation unit. No consumer
  impact. Provenance row updated to note the fix (`.agent/reference/provenance.md:67`).

### Strengths

- The rename is genuinely complete and precise at every layer checked: CMake module table, target
  names, ctest names/labels (derived, not hand-duplicated), preprocessor macros (including three
  the brief's table didn't spell out, renamed anyway for consistency), docs, CHANGELOG, NOTICE,
  provenance, and the rulebook — with git history preserved through both `git mv`s.
- The disambiguation of core-cpp's own module name versus upstream contour/fastcached's `coro`/
  `IExecutor` is careful rather than mechanical: the `async-and-net.md:175` case was resolved by
  checking fastcached's actual source rather than guessing from the surrounding prose, and the
  resolution is correct.
- The hygiene rule extension is minimal and precisely scoped to the gap it closes (a nested,
  wrong-case segment on an otherwise-matching first namespace), and its self-test proves both the
  RED and the new violation without touching what the existing 22 cases already prove.
- The self-review is unusually candid and useful: it surfaces a real process bug (a `python3`
  heredoc introducing CRLF into three files via Bash-on-Windows), explains root cause, shows how it
  was found (`od -c` diffing against `git show HEAD:<path>`) and audited the rest of the diff for
  the same defect — this is exactly the kind of check Global Constraints' "refuse CR bytes" rule
  exists for, done without being asked.
- The CI-driven fix (clang-tidy's `invalid case style for namespace 'CLI'`) is diagnosed to its
  actual root cause (`.clang-tidy` is not a tracked input to a cached translation unit, so a
  `.clang-tidy`-only edit doesn't invalidate `App.cpp.o`) rather than dismissed as a fluke, and the
  fix is re-verified across the full matrix, not just the failing preset.

### Issues

#### Critical

None.

#### Important

None.

#### Minor

- **`docs/modules/base.md:30-53`'s Generator section is not actually a duplicate**, despite the
  report's own "Concerns" section flagging it as one. `async.md` no longer contains the Generator
  prose at all — it has one linking sentence pointing at `base.md#generator`
  (`docs/modules/async.md:7-10`). The content simply moved with the code, matching the module-page
  convention the report itself cites ("a module page documents its own headers self-containedly").
  No action needed; if anything, correct the report's characterization for whoever reads it later,
  since a future reader might go looking for a duplicate to trim and find none.
- **`README.md`'s async row and `docs/modules/index.md`/`provenance.md` row-ordering drift** are
  both self-flagged, correctly scoped (fixed only where the implementer's own edit touched), and
  genuinely out of this task's blast radius. No action needed now; worth a follow-up sweep if
  README's other module rows are also stale, but that's not an A5b regression.
- **The pushed history contains one red Build run** (35385000948, `clang-tidy` job) before the
  fix in `e45f730`. This is disclosed plainly in the report and doesn't affect the final green
  state, but it's worth noting for anyone bisecting CI history later. No action needed for this
  task's gate.

### Assessment

**Task quality: Approved.** Every named risk checks out against the actual diff and the current
tree, not just the report's prose: the two senses of "coro" are correctly told apart everywhere
sampled including a fastcached-verified judgment call, the hygiene rule extension is minimal,
correctly self-tested, and free of the false-positive modes the review was asked to check, the
Generator move and platform de-dependency are exact, the module/target/test/macro rename table is
followed exhaustively (including unlisted-but-implied macros), CHANGELOG follows Ruling R36 with no
Breaking entry, and all four fold-in items landed as specified. The only findings are Minor and
none require a fix before closing the task.
