# Task A3 review: `core::base`, `core::log`, `core::cli`

Range `2a7b79f..a8c8429` (8 commits). Reviewed against the brief, the Global Constraints, spec
Part I §1 and §3, and the dispatch additions (R22, R23, R25 accepted; R24 not pre-empted).

## How it was checked

- **Import fidelity.**
  - I read every imported file as an upstream blob: contour `6777ff05` `src/crispy/…` and
    fastcached `origin/master` = `ee71f868` `src/FastCache/Core/…`.
  - I diffed each one against its core-cpp counterpart. For the tests I also ran a normalised
    diff: I applied the namespace map with `sed`, then diffed with `diff -w -B`.
  - Every remaining hunk is one of these: the namespace map, a NOLINT, loop or warning fix, a
    stated design change, or a documentation rewrite. I list the unstated ones below. None of
    them changes behaviour.
- **Grep checks on `src/`.**
  - `grep -rn NOLINT src`: 0 hits. Upstream had 21 NOLINT lines across App.hpp, Assert.hpp,
    CLI.cpp, Flags.hpp, LogStore.hpp, Times.hpp, Utils.hpp and CLI_test.cpp. fastcached had 0.
  - C-style `for(;;)` loops: 0 hits, single-line or multi-line (`rg -U`).
  - No new `#pragma`s.
  - No CR bytes.
  - No `gsl`, `crispy`, `logstore`, `FastCache` or `FC_` identifiers are left in code. Some
    contour wording remains in comments; see Minor.
- **CI.**
  - Run 35343591724 is on head `a8c8429`. `ci-ok` passed, and all 20 jobs succeeded.
  - Both emscripten legs passed: emcc 3.1.56 and 6.0.9. Each logged `0 command(s) mention
    pthread` and passed `core-cpp.base`, `.log`, `.cli` and `.testing` under node.
  - The clang-22-tracy leg fetched Tracy v0.14.1 with the three OPTIONS, and every test passed.
    That includes the `CHECK(calls == 1)` enabled branch of Profiling_test.
  - clang-tidy compiled all 15 new TUs. The style job ran `check-platform-sources` (8 of 8).
  - The Docs run 35343591714 on the same SHA succeeded.
- **One focused compile** (WSL, scratchpad, g++-14 and clang++, `-Wconversion -Wsign-conversion
  -Wshadow -Wold-style-cast -Werror`).
  - `nextPowerOfTwo` on `uint8_t` and `uint16_t` compiles clean and gives the right answers:
    65→128 and 257→512. The CI tests never instantiate these widths.
  - `Flags<E>::reduce` on 8-bit and 64-bit enums, including bit 63, compiles clean and runs
    correctly.

## Spec Compliance

### Delivered as specified

- **§1 module rows.**
  - `base DIR .`, `log DEPS base` and `cli DEPS base log` are in place, all `STATIC`,
    `PLATFORMS any`. They are real targets `core-cpp-<name>` with the aliases `core::<name>`.
  - base links Threads (not under single-threaded wasm) and, optionally, Tracy.
  - The tests are `core-cpp-<m>-test` / `core-cpp.<m>`.
- **§1 WebAssembly subset.**
  - base, log and cli build fully under single-threaded Emscripten.
  - No `std::thread` is compiled there: the LogSink_test thread case is gated on
    `!__EMSCRIPTEN__ || __EMSCRIPTEN_PTHREADS__`.
  - Threads is not linked.
  - `PLATFORMS any|native|wasm-subset` and `SOURCES_EMSCRIPTEN` are implemented as the spec
    describes (`cmake/CoreCppModules.cmake:37`, `cmake/CoreCppTargets.cmake:75-89`).
- **Carried minor: `SOURCES_POSIX` under Emscripten.** It is no longer compiled there, because
  the row is keyed on `CORE_CPP_PLATFORM_POSIX = UNIX AND NOT EMSCRIPTEN`
  (`cmake/CoreCppTargets.cmake:50-53`). `tests/cmake/check-platform-sources.cmake` proves it
  with 8 scenarios, as ctest and in the style job.
- **§3 Tracy row.**
  - Tracy is pinned to `v0.14.1`, the version contour pins at `6777ff05`; I verified this in
    `cmake/Tracy.cmake`.
  - The row is `WHEN CORE_CPP_WITH_TRACY` with target `Tracy::TracyClient`, and it resolves as
    §3 says: parent, then `find_package(QUIET)`, then CPM.
  - base links Tracy only when the option is on (`src/core/CMakeLists.txt:10-12`).
  - Global Constraint "option + row + CHANGELOG": the option existed, and the row and the
    CHANGELOG entry are added.
  - R23 (options in the parent's cache on opt-in only) is implemented as ruled and commented at
    `cmake/CoreCppDependencies.cmake:189-194`.
- **Carried minor: generated `core/Config.hpp`.**
  - It is in base's public FILE_SET (`src/core/CMakeLists.txt:30`).
  - `BASE_DIRS` gains `CORE_CPP_GENERATED_INCLUDE_DIR` (`cmake/CoreCppTargets.cmake:160-163`).
  - Profiling.hpp tests the 0/1 macro with `#if`, which is correct for `#cmakedefine01`.
- **Namespace map.** Applied exactly as ruled:
  - `crispy::`, `crispy::cli::`, `crispy::base64::` and `logstore::`.
  - `FC_*`→`CORE_*`, and `FC_TRACY_ENABLED`→`CORE_CPP_WITH_TRACY`.
  - R22: `CRISPY_*`→`CORE_*`, and `fatal`/`SoftRequire` moved to `<core/log/Assert.hpp>`.
- **`gsl::not_null`.** It is replaced by `Category const&` in `MessageBuilder`. No consumer at
  the pinned SHAs assigns a `MessageBuilder`; I grepped contour and endo.
- **R3 and R4.** Every NOLINT is removed. Each finding is fixed in code or by a `.clang-tidy`
  STL-hook entry: `iterator` was added to `Struct`/`ClassIgnoredRegexp`, which the spec's
  "IgnoredRegexps cover … STL hooks" allows. Every C-style loop is converted.
- **Tests.**
  - Every upstream `*_test.cpp` for an imported file is brought in: crispy's Base64, CLI,
    Environment, LogSink, Times and Utils, plus fastcached's Profiling_test.
  - Ranges_test was added. It is Ranges.hpp's own upstream test, so it is in scope.
  - Each is registered with `core_cpp_add_test(base|log|cli …)`.
- **Step 4b.**
  - `CatchMain_test.cpp` asserts that `net` is enabled and `tui` is not, under `LOG=net`, which
    the ctest ENVIRONMENT sets.
  - `CatchMain.cpp` applies the filter before `session.run()`.
  - `core::testing_main` links `core::log`.
- **Docs and records.**
  - Updated: CHANGELOG (Added, Fixed, Imported rows with the full SHAs), NOTICE, the
    `docs/modules/{base,log,cli,testing,index}.md` pages, the README and AGENT tables, and
    source-map.
  - Spot-checked and accurate: the Escape function names, the `Command`/`Option`/`parse`/`get<T>`
    example in cli.md, and the `Category` example in log.md.
- **Other constraints.**
  - Every commit carries the trailer, and nothing under `.superpowers/` is committed.
  - fastcached `ee71f868` descends from `cc8992b0`, so it meets the Global Constraints' "or
    later" rule.

### Deviations, all justified; none is an issue

- **Step 4b reads `LOG` through `core::LiveEnvironment {}`,** not `std::getenv`, which
  `concurrency-mt-unsafe` rejects. This matches the repo's environment-seam rule.
  - It also applies the standard formatter and enables the console sink. That matches what
    endo's `test_main` at `f774a210` does through `crispy::App::customizeLogStoreOutput()`.
  - An empty `LOG` is a no-op instead of `configure("")`. This is documented.
- **The `nextPowerOfTwo` fix is beyond the brief.** It is its own commit (`3e63739`), made test
  first, with a `### Fixed` entry.
- **Removals.** `SourceLocationCustom` is removed: C++23 always has `std::source_location`, and
  no consumer uses the class. `App::_instance` became a file-scope `currentApp`.
- **`views::enumerate` is now a function template.** Every consumer call site is a call; I
  grepped contour and endo.

### Extra or unrequested, and not covered by a ruling

- **`FastCache::Ranges` → `core::ranges` (`src/core/Ranges.hpp:162`).**
  - The spec (§1) and `.agent/rules/cpp-guidelines.md:109-110` list the allowed nested helper
    namespaces as `detail`, `base64`, `views` and `testing`. `ranges` is not on that list.
  - The brief's namespace map gives no mapping for fastcached's `Ranges`.
  - The report records the rename but does not raise it as a concern. See Minor 1.

### ⚠️ Cannot verify

- **Local runs.** I did not repeat the Windows `clangcl-debug`/`cl-debug` and WSL
  `clang-debug`/`gcc-debug` runs, the local `clang-format --check`, or the local mkdocs build.
  CI covers the equivalent legs: cl-debug, clangcl-release, clang-22, gcc-14/15, style and
  Docs.
- **RED states.** The four TDD red states exist only in the report. Step 5's single import
  commit carries tests and code together, and `861baa6` carries the LOG test with its
  implementation.
- **Step 4b under WebAssembly.** The LOG test cases SKIP under node, because node passes no host
  environment into wasm. The filter is therefore proven natively only.

## Strengths

- **The layering fix for `Assert.hpp`.**
  - It removes the base→log edge instead of hiding it.
  - `Require`/`Guarantee`/`todo`/`unreachable` stay in base. `fatal`/`SoftRequire` move to the
    layer that owns logging, and the header's `@file` says why.
  - The `catch (...)` now aborts explicitly, which behaves the same as upstream's fall-through
    abort.
- **The platform source selection.**
  - It is data-driven: one new table row, and one `wasm-subset` branch in
    `core_cpp_selected_sources`.
  - Modules and tests share it, and a hermetic `cmake -P` proof runs one scenario per process.
  - The proof fails on exactly the two Emscripten scenarios against the old row.
- **The `nextPowerOfTwo` fix.**
  - It is a real latent bug in the upstream code (`sizeof` in bytes compared against bit
    counts), found and fixed test first.
  - The CHANGELOG example values are right; I checked that 0x10001 became 0x1fe01.
  - The report flags that contour still carries the bug.
- **UB removed in `Flags`.**
  - `underlying_type{1} << i` replaces `1 << i`, which was UB for i ≥ 32 on 64-bit enums.
  - iota over `size_t` replaces the `0u` counter.
- **Honest reporting.** The report is honest about the gcc-release break on master
  (`2e57ff7..1b2401d`) and about the libc++ 17 finding (R25). It opens core-cpp#13 instead of
  silently leaving `throw` in `core::cli`.
- **Fidelity.** Every documentation rewrite I checked is either accurate, or keeps the upstream
  meaning with the consumer-specific nouns removed; Minor 3 lists the exceptions. Every NOLINT
  resolution in the report's table matches the code.

## Issues

### Critical

None.

### Important

None.

### Minor

1. **`core::ranges` is a nested namespace the spec does not list**
   (`src/core/Ranges.hpp:162`; spec §1; `.agent/rules/cpp-guidelines.md:109-110`).
   - It is public API that fastcached's migration will bake in (`core::ranges::Iota`,
     `FoldLeft`).
   - Inside `namespace core`, and in any consumer TU that has both `using namespace std;` and
     `using namespace core;`, an unqualified `ranges::` now resolves to it, or becomes
     ambiguous, instead of meaning `std::ranges`. Nothing is affected today. I grepped contour
     at the pin.
   - Get a ruling before 0.1.0 and record it: either add `ranges` to the allowed list in both
     the spec's rulebook copy and cpp-guidelines, or put `Iota`/`FoldLeft` in `core` or
     `core::views`.
2. **The consumer-migration rename map was not updated** (`.agent/guides/consumer-migration.md:62-107`).
   - The row `<crispy/X.hpp>` → `<core/X.hpp>` (line 76) is now wrong for LogStore, LogSink,
     CLI and App, which moved to `<core/log/…>` and `<core/cli/…>`.
   - The A3 API deltas that Task C0 must carry exist only in the uncommitted report. Add them to
     the guide:
     - `crispy::fatal`/`SoftRequire` → `core::log::…` in `<core/log/Assert.hpp>`
     - `crispy::App` → `core::cli::App`
     - `CRISPY_*` → `CORE_*`
     - the global `::Overloaded` → `core::Overloaded`
     - `logstore::SourceLocationCustom` is removed
     - `FastCache::FindOrNull`/`FindIfOrNull` → `core::findOrNull`/`findIfOrNull`
     - `FastCache::Ranges::*` → `core::ranges::*`
     - `FC_RANGES_FORCE_FALLBACK` → `CORE_RANGES_FORCE_FALLBACK`
3. **Stale or consumer-specific text remains.** The report says contour wording was rewritten
   wherever it described core-cpp code. These places were missed:
   - `src/core/cli/App.hpp:52-53, 60, 73-74, 89`: "`contour client`", "GUI boot", "GUI event
     loop", "`contour daemon --background`" and `"contour.daemon"`.
   - `src/core/Utils_test.cpp:322-323`: "the /.flatpak-info walk in vtpty", and
     `@see vtpty::parseFlatpakInfo`, which names a symbol core-cpp does not have.
   - `docs/modules/cli.md:19`: the example key `contour.capture.timeout`.
   - `.agent/guides/profiling-tracy.md:8-13`: "Status: `<core/Profiling.hpp>` arrives … in Task
     A3 … Until then the clang-tracy preset … has nothing to instrument". This is now false.
   - `.agent/rules/build-and-toolchain.md:117`: "`FindOrNull`, which `core/Ranges.hpp` brings …
     in Task A3". The name is now `findOrNull`, and the task is done.
4. **The `enumerate` Doxygen gives the wrong reason** (`src/core/Utils.hpp:142-143`).
   - It says the change to a function is "so that it is spelled like one: `enumerate(xs)`, not
     `xs | enumerate`". Upstream's `EnumerateFn` was not pipeable either.
   - The real reason was the global-constant naming rule (NOLINT removal). State that, or drop
     the sentence.
5. **`[[nodiscard]]` is missing on value-returning functions that A3 rewrote and documented.**
   `views::enumerate` (`src/core/Utils.hpp:147`) and `nextPowerOfTwo`
   (`src/core/Utils.hpp:700`) break the Global Constraint "`[[nodiscard]]` on value-returning
   functions".
6. **The `nextPowerOfTwo` test does not pin the 8- and 16-bit widths**
   (`src/core/Utils_test.cpp`, TEST_CASE `utils.nextPowerOfTwo`).
   - The fix adds a `digits > 8` branch that only `uint16_t` exercises, and no test
     instantiates `uint8_t` or `uint16_t`.
   - I verified locally that both are correct and warning-clean. Add, for example,
     `nextPowerOfTwo(std::uint16_t{257}) == 512` and `nextPowerOfTwo(std::uint8_t{65}) == 128`.
7. **`core::testing_main` links `core::log` PUBLIC** (`src/core/testing/CMakeLists.txt:24`).
   - Only `CatchMain.cpp` uses log. No public header of `testing_main` (`ExitCode.hpp`) includes
     it.
   - PRIVATE is enough: a STATIC library still propagates the link as `$<LINK_ONLY:…>`, and
     PRIVATE stops log's usage requirements from reaching every consumer's test targets.
8. **`CORE_CPP_WITH_TRACY=ON` under Emscripten is not refused or forced off**
   (`cmake/CoreCppOptions.cmake:51-58` forces only TUI, IMAGES and TLS).
   - The Tracy client needs threads. Opting in on a single-threaded wasm build would bring in
     `Threads::Threads`/`-pthread` through `Tracy::TracyClient`, which is exactly what the subset
     forbids, and nothing would say why.
   - Add Tracy to the forced-off list, or fail with a message. It is opt-in, so this is Minor.
9. **Master is not bisectable across three commits.**
   - `2e57ff7..1b2401d` do not build on the gcc-release legs (`-Wnull-dereference`), as the
     report says.
   - `2e57ff7..861baa6` also fail under the `clang-tracy` preset: Profiling.hpp includes
     `<tracy/Tracy.hpp>` before base links Tracy in `1b2401d`.
   - The history is published and cannot be rewritten. Record it; do not act on it.
10. **Global Constraints not yet applied to imported API have no tracking issue.**
    - `bool` parameters remain in API surfaces: `Sink(bool, Writer)`, `Sink::setEnabled(bool)`,
      `core::log::enable(name, bool)` and `App::installLogging(…, bool showProcessId)`. Many
      imported value-returning functions lack `[[nodiscard]]`, for example `Deferred::get`,
      `startsWith`/`endsWith` and `unescapeURL`.
    - Keeping them is reasonable for the migration, but unlike exceptions (#13) and Doxygen (#10)
      nothing tracks them. Open one issue, as was done for #13.
11. **Already-known latent upstream bugs (R24; listed, not pre-empted).**
    - `FNV<char>` recurses forever when it hashes a trivially-copyable `V` other than `char`
      (`src/core/FNV.hpp:54-62`): the `V const&` template beats the `(U, T)` overload for each
      `unsigned char` byte.
    - `TimesIterator::operator--(int)` increments (`src/core/Times.hpp:42`).
    - **The same family, possibly not yet known:**
      - `TimesIterator::operator++(int)` (`:34`) and `Times2DIterator::operator++(int)` (`:136`)
        return `*this` by reference after a pre-increment. That gives post-increment the
        semantics of pre-increment.
      - `TimesIterator::operator--(int)` returns a reference as well.
      - Fix them all under the R24 follow-up.

## Assessment

The import is faithful. The only changes from upstream are the namespace map, the NOLINT, loop
and warning fixes, and the stated design changes. The Assert split, the Emscripten source
selection, the Tracy row, the Config.hpp FILE_SET, `enumerate` and `nextPowerOfTwo` are all
correct. CI is fully green on the head, both emscripten legs included, and it ran the new tests.
The findings are about documentation, test coverage and tracking, and none of them makes the
task untrustworthy. I would fix Minors 1–3 before Phase C starts, because Phase C depends on the
namespace names and the migration map.

**Task quality: Approved**
