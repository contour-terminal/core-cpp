# Task A3 report: `core::base`, `core::log`, `core::cli`

Status: **DONE**. All eight commits are on `origin/master` (tip `a8c8429`), and `ci-ok` is green on the tip:
https://github.com/contour-terminal/core-cpp/actions/runs/35343591724

## Commits (oldest first)

| SHA | Subject |
|---|---|
| `1709dac` | cmake: compile no SOURCES_POSIX under Emscripten, and select a wasm subset |
| `2e57ff7` | base: import crispy's generic utilities as core, core::log and core::cli |
| `3e63739` | base: nextPowerOfTwo() rounds every width up to a power of two |
| `861baa6` | testing: apply the LOG filter in core::testing_main before the run |
| `1b2401d` | base: link the Tracy client when CORE_CPP_WITH_TRACY is on |
| `fe62488` | log: read the LogSink test's files without std::istreambuf_iterator |
| `efb7172` | docs: describe core::base, core::log and core::cli as they now exist |
| `a8c8429` | docs: emsdk 3.1.56 ships libc++ 17, not 18 |

Every message ends with `Signed-off-by: Christian Parpart <christian@parpart.family>`. Everything is LF-only, and nothing under `.superpowers/` was committed.

## What was imported, and from which SHA

I read every file as a git blob with `-c core.autocrlf=false -c core.eol=lf show`, and none of them had a CR byte (checked with `grep -lU $'\r'`). I never read or touched a working tree in `D:\contour` or `D:\fastcached`. `git -C D:\fastcached fetch origin` ran first. It fetched nothing new, and `origin/master` = `ee71f868547712892b7d9a2ebff60d49c496e25c`. The last commit that touched the imported fastcached files is `a90a854d`.

| Source | SHA | File → destination |
|---|---|---|
| contour `src/crispy/` | `6777ff05014f8ff163b071e8b0e942830119db80` | Assert, Base64, Deferred, Defines, Environment(.hpp/.cpp), Escape, FNV, Flags, Overloaded, Times, UserInfo(.hpp/.cpp), Utils(.hpp/.cpp) → `src/core/` (`core::base`) |
| same | same | LogStore(.hpp/.cpp), LogSink(.hpp/.cpp) → `src/core/log/` (`core::log`) |
| same | same | CLI(.hpp/.cpp), App(.hpp/.cpp) → `src/core/cli/` (`core::cli`) |
| same | same | `testing/Environment.hpp` → `src/core/testing/Environment.hpp` (`core::testing`) |
| same | same | tests: Base64_test, CLI_test, Environment_test, LogSink_test, Times_test, Utils_test |
| fastcached `src/FastCache/Core/` | `ee71f868547712892b7d9a2ebff60d49c496e25c` | Profiling.hpp, Ranges.hpp → `src/core/`; Profiling_test.cpp **and Ranges_test.cpp** |
| contour `cmake/Tracy.cmake` | `6777ff05` | only the version pin: Tracy `v0.14.1`, used for the new dependency row |

I also imported Ranges_test.cpp, which the brief did not list, because Ranges.hpp's own documentation points to that test as the proof that its fallbacks work. Both `CHANGELOG.md` (`### Imported`) and `NOTICE` now record the two pins.

## Namespace and name changes

| From | To |
|---|---|
| `crispy::` | `core::` |
| `crispy::cli::` (and its nested `about`) | `core::cli::` (`core::cli::about`) |
| `crispy::App` | `core::cli::App` (it lives in `src/core/cli/`) |
| `crispy::base64::` | `core::base64::` |
| `crispy::testing::FakeEnvironment` | `core::testing::FakeEnvironment` |
| `logstore::` | `core::log::` (`errorLog()` macro → `(::core::log::errorLog())`) |
| `crispy::fatal`, `SoftRequire` | `core::log::fatal`, `SoftRequire` in `<core/log/Assert.hpp>` (see below) |
| global `::Overloaded` and `crispy::Overloaded` (Utils.hpp) | a single `core::Overloaded` in Overloaded.hpp, which Utils.hpp includes |
| `CRISPY_PACKED/REQUIRES/CONSTEVAL/CONSTEXPR/CONCEPTS_SUPPORTED` | `CORE_*` |
| `FC_ZONE_*`, `FC_FRAME_MARK*`, `FC_THREAD_NAME`, `FC_PLOT` | `CORE_*` |
| `FC_TRACY_ENABLED` | `CORE_CPP_WITH_TRACY` (0/1 from `<core/Config.hpp>`, tested with `#if`) |
| `FastCache::FindOrNull/FindIfOrNull` | `core::findOrNull/findIfOrNull` (camelBack) |
| `FastCache::Ranges::{Iota,FoldLeft}`, `Ranges::Detail::*` | `core::ranges::{Iota,FoldLeft}`, `core::ranges::detail::*` |
| `FC_RANGES_FORCE_FALLBACK` | `CORE_RANGES_FORCE_FALLBACK` |

The hygiene scan's `namespace-directory` rule passes on every file.

## Other changes the import made

- **The Assert.hpp split.** crispy's `Assert.hpp` included `LogStore.hpp` for `fatal()` and `SoftRequire()`. That would give `core::base` a dependency on `core::log`, a cycle in the module table. `.agent/rules/library-hygiene.md` says such an edge is closed by moving the code to the layer that owns it, so I moved those two into `src/core/log/Assert.hpp` (`core::log::fatal`, `core::log::detail::softRequire`, and the `SoftRequire` macro). `Require`, `Guarantee`, `todo`, `unreachable` and `setFailHandler` stay in base. `unreachable()` now calls `std::unreachable()`, because clang-cl rejected `__forceinline` with `-Wlanguage-extension-token`.
- **`gsl::not_null` is removed.** `MessageBuilder` now holds a `Category const&`. I also removed `SourceLocationCustom`, which nothing uses: `std::source_location` is always available in C++23. With it went both of the `file_name`/`function_name` NOLINTs.
- **Generated `core/Config.hpp`, the carried minor from A1.** It is now in `core::base`'s public FILE_SET. `core_cpp_add_module`'s FILE_SET `BASE_DIRS` is `src/` plus `CORE_CPP_GENERATED_INCLUDE_DIR`.
- **Module table.** The rows are `base DIR .`, `log DEPS base`, `cli DEPS base log` and `testing DEPS base log`, all `PLATFORMS any`. `testing` still has no WHEN (Ruling R7). It depends on base for `testing/Environment.hpp` and on log for `testing_main`. `core::base` links `Threads::Threads` PUBLIC except under single-threaded Emscripten, and `Tracy::TracyClient` PUBLIC only when `CORE_CPP_WITH_TRACY` is on.
- **Documentation text.** I rewrote contour-specific wording ("daemon", "vtpty", "contour verbs", "logstore") where it described core-cpp code. I also rewrote Ranges.hpp's rationale block to cite fastcached#1392 and to drop fastcached's `ranges-seam` check, which core-cpp does not have.
- **Fixed on top of the import (commit `3e63739`, test first).** `nextPowerOfTwo()` compared `sizeof(T)` (bytes) against 16, 32 and 64 (bits), so `nextPowerOfTwo(257u)` returned 511. It now uses `numeric_limits<T>::digits`, and CHANGELOG `### Fixed` records it.

## NOLINTs removed (22 in the upstream blobs, 0 remain)

| Where | Resolution |
|---|---|
| App.hpp `static App* _instance; // NOLINT(readability-identifier-naming)` | Code: the static member became a file-scope `currentApp` in App.cpp, and `App::instance()` moved out of line. `.clang-tidy` wants CamelCase for a static member, with no prefix. |
| Assert.hpp `catch (...) // NOLINT(bugprone-empty-catch)` | Code: the catch now calls `std::abort()`, with a comment. |
| Flags.hpp `using value_type` | Already covered by `TypeAliasIgnoredRegexp`; I deleted the comment. |
| Flags.hpp `fromValue` NOLINTNEXTLINE | Deleted. clang-tidy 22.1.8 reports nothing there. |
| Flags.hpp `std::formatter` | Already covered by `StructIgnoredRegexp`. |
| LogStore.hpp `file_name` / `function_name` (×2) | Code: `SourceLocationCustom` is removed. |
| Times.hpp `using iterator`, `using value_type` (×3) | Already covered by `TypeAliasIgnoredRegexp`. |
| Utils.hpp `using sentinel` | Code: renamed `Sentinel`. The name is not one the standard library looks up. |
| Utils.hpp `struct iterator` (×2: EnumerateView, eachElement's Container) | `.clang-tidy`: I added `iterator` to `StructIgnoredRegexp`/`ClassIgnoredRegexp` ("a range's nested iterator class, which generic code names as R::iterator"). |
| Utils.hpp NOLINTBEGIN/END around `difference_type`/`value_type` | Already covered. |
| Utils.hpp `constexpr inline EnumerateFn enumerate` | Code: `views::enumerate` is now a function template. Every call site upstream is `enumerate(x)`, which compiles unchanged. |
| CLI.cpp `readability-container-contains` (×2) | Code: `.count()` became `.contains()`. |
| CLI.cpp `readability-implicit-bool-conversion` | Deleted: that check is disabled in `.clang-tidy`. |
| CLI_test.cpp NOLINTBEGIN/END(misc-const-correctness, modernize-use-designated-initializers) | Deleted. The second check is disabled, and the first reports nothing. |

The pinned clang-tidy 22.1.8 (the `clang-tidy` preset, a clean build of 126 TUs in WSL) found five more things. I fixed them in code: two `#if defined` that should be `#ifdef`, two `#elif defined` that should be `#elifdef`, and a duplicate `#include <concepts>`. CI's `clang-tidy` job is green.

## C-style loops converted (22)

| File | How |
|---|---|
| Base64.hpp encode, Escape.hpp escape/escapeMarkdown (×2) | range-for over `std::ranges::subrange(begin, end)` |
| FNV.hpp byte loop | range-for over `std::span { bytes, sizeof(V) }` |
| Flags.hpp reduce + formatter (×2) | `std::views::iota(size_t{0}, sizeof(E) * 8)`. The shift is now `underlying_type{1} << i`, which also removes UB for 64-bit enums. |
| LogStore.hpp `~Category` (erase+break) | `std::erase_if` (each category is registered once) |
| Utils.hpp joinWithImpl (`for (; it != end; ++it)`) | `while` |
| Utils.hpp startsWith/endsWith | `starts_with`/`ends_with` |
| Utils.hpp unescapeURL | **The body advances the index.** A `while` over an explicit index: +3 over an escape, +1 otherwise. The upstream tests (`%2`, `%`, `%gg`, `A%42C`) pass. |
| App.cpp `operator*(string_view, size_t)` | iota |
| CLI.cpp namePrefix | iota over the size (the body reads `i != 0`) |
| CLI.cpp matchPrefix | `starts_with` |
| CLI.cpp `for (;;)` ×2, UserInfo.cpp `for (;;)` | `while (true)` |
| Environment.cpp lookupInEnviron, Environment_test.cpp ScopedVariable | `while` over the `environ` pointer |
| LogSink_test.cpp ×3 | iota ×2; the `for (auto text = …; getline;)` became `while (getline)` |

## Warnings fixed (all in code; no `-Wno-*` row added, no issue needed for warnings)

- `-Wold-style-cast`: Base64.hpp (8), Utils.hpp `fromHexString`, and App.cpp `(int)` ×4 plus `int(...)` became `static_cast`.
- GCC `-Wshadow`: CLI.hpp `OptionName` constructor parameters, CLI.cpp `ScopedOption`/`ScopedCommand` constructor parameters and the `printOption` lambda parameter. clang-cl reported an `else if` that redeclared `subcmd`.
- MSVC C4244 (int→char in `std::transform`): `toLower`/`toUpper` now `static_cast<T>`.
- `-Wdouble-promotion` (clang, x86-64): `humanReadableBytes` now uses `1024.0L` literals.
- `-Wshorten-64-to-32` (wasm32 only): `readFileAsString` now casts `file_size()` to `size_t`.
- clang-cl `-Wdeprecated-declarations` (`getpid`): the test has a `processId()` helper that calls `_getpid` on Windows.
- clang-cl `-Wunused-function` (`orEmpty` on Windows): guarded with `#ifndef _WIN32`.
- `-Wlanguage-extension-token` (clang-cl, `__forceinline`): replaced by `std::unreachable()`.
- A capture error (`LinesPerThread` odr-used by iota): the constants are now `static constexpr`.
- Redefinition of `WIN32_LEAN_AND_MEAN`, which the toolchain already defines: the `#define` is removed from Utils.cpp.
- GCC 14 `-O2` `-Wnull-dereference` inside libstdc++'s streambuf, inlined from `std::string{istreambuf_iterator…}` in LogSink_test. This one **only showed in CI** (gcc-release preset). I reproduced it in WSL `gcc-release` and fixed it in `fe62488` with a `contentsOf()` helper that uses a string stream.

## Emscripten platform-selection design (commit `1709dac`)

- The module table's `PLATFORMS` column takes `any|native|wasm-subset` (spec §1).
  - `any` means an Emscripten build compiles `SOURCES` plus `SOURCES_EMSCRIPTEN`.
  - `wasm-subset` means it compiles **only** `SOURCES_EMSCRIPTEN`, which lists the subset.
  - `native` means the module is skipped, as before.
- The platform source table has a new `SOURCES_EMSCRIPTEN|EMSCRIPTEN` row, and `SOURCES_POSIX` is now keyed on `CORE_CPP_PLATFORM_POSIX` = `UNIX AND NOT EMSCRIPTEN`. That was the carried minor: Emscripten sets `UNIX`.
- `core_cpp_selected_sources(prefix platforms outVar)` applies these rules. Both `core_cpp_add_module` and `core_cpp_add_test` pass the module row's PLATFORMS, so a module's tests follow the same selection.
- Proof: `tests/cmake/check-platform-sources.cmake` (ctest `core-cpp.platform-sources`, plus a step in the `style` job) runs 8 scenarios, each in its own `cmake -P`: Linux, macOS, FreeBSD, Windows, Emscripten, and wasm-subset on Emscripten, Linux and Windows.
  - With the old `SOURCES_POSIX|UNIX` row it fails exactly the two Emscripten scenarios (`selected 'common.cpp;posix.cpp;emscripten.cpp'`).
- `.agent/rules/platform.md` documents the lists.
- base, log and cli are `any` and need no Emscripten source list. Their POSIX `#ifdef` branches compile against Emscripten's libc:
  - `getpwuid_r` is a stub that returns ENOENT, so there is no password entry.
  - `environ` holds Emscripten's default environment.
  - Emscripten's libc leaves out `pthread_getname_np`, so `threadName()` has an `#elifdef __EMSCRIPTEN__` branch that returns "".
  - LogSink_test's `std::thread` case is compiled only when `!__EMSCRIPTEN__ || __EMSCRIPTEN_PTHREADS__`.
- `ninja -t commands | grep -c pthread` = 0, locally on 3.1.56 and in CI on 3.1.56 and latest (6.0.9).

## TDD evidence

1. **Tests-first import.** With only the eight test sources, the module rows and CMakeLists that register just the tests, `clangcl-debug` failed with 8 × `fatal error: 'core/…​.hpp' file not found` (Base64, Environment, Profiling, Ranges, Times, Utils, log/LogSink, cli/CLI). GREEN: all of `core-cpp.base`, `.log` and `.cli` pass on every configuration.
   - gcc-debug: base 200 assertions / 53 cases, log 52 / 13, cli 18 / 2.
   - emscripten 3.1.56 under node: base 193 / 51 (the two agreement cases that the feature-test macros compile out are missing), log 49 / 12 (the threads case is compiled out), cli 18 / 2.
2. **Platform selection.** RED: all 8 scenarios failed before the signature and table change, and exactly the 2 Emscripten ones fail with the old row. GREEN: 8 of 8.
3. **nextPowerOfTwo.** RED: 5 `static assertion failed` errors, for example `nextPowerOfTwo(unsigned int{257}) == 512U`. GREEN on all four configurations and emscripten.
4. **LOG filter (Step 4b).**
   - The test file is `CatchMain_test.cpp`. It declares a namespace-scope `net` category (disabled) and a `tui` category (enabled), and ctest sets `ENVIRONMENT LOG=net`.
   - RED with `LOG=net`: `CHECK(netLog.isEnabled())` false, `CHECK_FALSE(tuiLog.isEnabled())` !true, and the stdout capture lacked `[net] a line on net`. That is 3 of 16 assertions failed, and ctest `core-cpp.testing` failed.
   - GREEN with `LOG=net`: `All tests passed (16 assertions in 11 test cases)`. Without LOG: 9 passed, 2 skipped, exit 0.
   - Implementation: `applyLogFilter(core::LiveEnvironment {})` before `session.run()` calls `configure()`, sets the standard formatter and enables the console sink. An unset or empty LOG is a no-op. I used `LiveEnvironment` rather than `std::getenv` because clang-tidy's `concurrency-mt-unsafe` rejects `getenv`. `core::testing_main` links `core::log`.

## Local results (final tree `a8c8429`)

| Configuration | Result |
|---|---|
| Windows `clangcl-debug` (`--clean-first`) | build OK, ctest 12/12 passed |
| Windows `cl-debug` | build OK, ctest 12/12 passed |
| WSL `clang-debug` | build OK, ctest 9/9 passed |
| WSL `gcc-debug` | build OK, ctest 9/9 passed |
| extra: WSL `emscripten` (emsdk 3.1.56, node) | build OK, ctest 9/9, 0 commands mention pthread |
| extra: WSL `gcc-release` | build OK, ctest passed (after `fe62488`) |
| extra: WSL `clang-tracy` | Tracy v0.14.1 fetched; `[profiling]` reports ENABLED with `calls == 1` |
| extra: WSL `clang-tidy` preset, pinned 22.1.8 | clean |
| `python scripts/clang-format.py --check` | 45 files formatted with 22.1.8 |
| hygiene scan (`core-cpp.cmake-hygiene`) | clean in every ctest run |
| `python -m mkdocs build --strict` | rc 0 |

## CI

- **Run 1** on `1b2401d`: https://github.com/contour-terminal/core-cpp/actions/runs/35343031442. `ci-ok` **failed**.
  - The failing jobs were linux gcc-14, linux gcc-15 and compile-cache, all the gcc-release preset, on the `-Wnull-dereference` above.
  - Every other job was green, including both emscripten legs, clang-tidy, the new clang-22-tracy leg, the sanitizers, macOS and all Windows legs.
  - This means commits `2e57ff7`..`1b2401d` do not build on those three gcc-release legs. They were already on master when CI found it, so I fixed forward rather than rewrite published history.
- **Run 2** on `a8c8429` (tip): https://github.com/contour-terminal/core-cpp/actions/runs/35343591724. **`ci-ok` success.** Every job succeeded: style, linux (clang-22, gcc-14, gcc-15, clang-22-arm64, clang-22-cxx26, clang-22-tracy), macos (appleclang, llvm-22), windows (cl-release, cl-debug, clangcl-release, cl-release-tls), sanitizers (asan-ubsan, tsan), clang-tidy, emscripten (3.1.56 and latest = 6.0.9), compile-cache and coverage.
  - Both emscripten jobs show `0 command(s) mention pthread` and pass `core-cpp.base`, `.log`, `.cli` and `.testing` under node.
  - The Docs run on the same commit (35343591714) succeeded.

## Issues opened

- https://github.com/contour-terminal/core-cpp/issues/13: `core::cli` reports parse errors by throwing (`ParserError`, `std::invalid_argument`), which the "only OperationCancelled" constraint forbids. The fix is to return `std::expected`. It is a Breaking change for contour and endo, so it is not part of the import. Labels: type/chore, module/cli, breaking-change. `docs/modules/cli.md` links it under "Open work".

## Self-review

- **Layering.** base includes nothing from log or cli. log includes base. cli includes base and log. testing includes base (`FakeEnvironment`) and, in `testing_main`, log. Every one of those edges is a DEPS entry in its row.
- **No PUBLIC flags.** The only PUBLIC links are Threads, Tracy and core:: modules. The hygiene scan passes.
- **Public API docs.** Every header in `core::base`, `core::log` and `core::cli` is covered by its module page. New code has Doxygen comments: the `log/Assert.hpp` functions, `enumerate`, `threadName`, `nextPowerOfTwo`, `times()`, the LogStore free functions and `App::instance`. Many imported functions in Utils.hpp and CLI.hpp still have none, as upstream. That backlog is core-cpp#10 (Doxygen FAIL_ON_WARNINGS).
- **Consumer impact.** These are what Task C0's rename map must carry:
  - `crispy::fatal` → `core::log::fatal`, and `SoftRequire` now needs `<core/log/Assert.hpp>`.
  - `crispy::App` → `core::cli::App`.
  - `CRISPY_*` → `CORE_*`.
  - Global `Overloaded` → `core::Overloaded`.
  - `views::enumerate` is now a function, which is source-compatible at every upstream call site.
  - `core::testing` now links `core::base`, and with it Threads.
- **Docs updated.** `docs/modules/{base,log,cli,testing,index}.md`, the README and AGENT.md module tables, `.agent/reference/source-map.md`, `.agent/rules/platform.md`, `docs/design/profiling.md`, CHANGELOG (Added, Fixed, Imported) and NOTICE.

## Concerns

1. **Interpretation of "keep macros as they are".** I read it as "Defines.hpp's macros stay global macros, with no namespace", and renamed `CRISPY_*` to `CORE_*` to match `FC_*` → `CORE_*` and the macro-naming rule. It also avoids a redefinition clash with contour's remaining crispy headers during the migration. If the names were meant to stay `CRISPY_*`, that is a one-line revert per macro.
2. **The Assert.hpp split** (`fatal`/`SoftRequire` moved to `core::log`) is my design decision. It was forced by the layering. Please confirm it, or suggest another home.
3. **emsdk 3.1.56 ships libc++ 17.0.4, not 18.** Its sysroot `__config` says `_LIBCPP_VERSION 170004`. The Global Constraints, the spec and the plan say "libc++ 18/19". I corrected the rulebook and site docs in `a8c8429`, but left the spec and plan alone because they are approved documents. This matters for later tasks:
   - `__cpp_lib_jthread` is not defined, and `<stop_token>` is experimental on 3.1.56. That affects Task A5's `-fexperimental-library` probe.
   - `ranges::Iota`/`FoldLeft` select their fallbacks on 3.1.56.
4. **Stale probe caches.** A stale local `out/build/emscripten` had `CORE_CPP_HAS_Wno_c2y_extensions=1` cached from an earlier configure, so every compile failed with "unknown warning option". A fresh configure probes it correctly. CI always configures fresh, but a reused local build dir can mislead.
5. **Latent upstream bugs I noticed but did not fix.** Neither is covered by a test.
   - `FNV<char>`'s trivially-copyable overload recurses forever for any `V` other than `char`, because the template `V const&` overload beats the `(U, T)` conversion when it re-hashes each byte.
   - `TimesIterator::operator--(int)` increments.
   - Both are unchanged from crispy.
6. **Tracy's options land in the parent's cache.** The Tracy row's CPM `OPTIONS` (`TRACY_ENABLE`, `TRACY_STATIC`, `TRACY_ONLY_LOCALHOST`) write Tracy's own cache options into the parent's cache. This happens only when `CORE_CPP_WITH_TRACY` is on and core-cpp itself fetches Tracy. It matches contour's behaviour.
7. **The LOG filter under node.** Under Emscripten the LOG test cases skip, because node passes no host environment into WebAssembly.
8. **`nextPowerOfTwo` in contour.** contour's `crispy::nextPowerOfTwo` still has the bug fixed in `3e63739`, and contour sizes hash tables with it. That should be reported to contour, or picked up by the C6 migration.

## Fix round 1 (rulings R24, R27, R28)

Before starting I pulled the controller commit `371e73a`. All four new commits are pushed on top of it (tip `6ebb78d`).

| SHA | Subject |
|---|---|
| `48b261a` | base: FNV hashes a trivially copyable value without recursing forever |
| `6b1a4d7` | base: Times iterators' postfix operators answer the prior position |
| `be119dc` | docs: record A3's renames in the migration map, and allow core::ranges |
| `6ebb78d` | docs: drop the contour-specific wording the import left behind |

### 1. Latent upstream bugs (R24), test first

**a. The `FNV<char>` recursion.**
- Cause: the byte-wise overload called `(*this)(memory, byte)`. The `unsigned char` byte binds that same overload's `V const&` exactly, which beats converting it to `T = char`, so the call recursed forever for every `T` except `unsigned char`.
- Fix: the overload now performs the FNV-1a step on each byte itself (`memory ^= U(byte); memory *= _prime;`).
- Test: new `src/core/FNV_test.cpp`, registered in `core_cpp_add_test(base …)`.
  - `FNV<char, uint32_t>` hashes "a" to 0xE40C292C and "abcd" to 0xCE3479BD.
  - `std::array<char,4>{'a','b','c','d'}` hashes byte-wise to 0xCE3479BD, the same as the string.

**b. `TimesIterator::operator--(int)` incremented. c. `TimesIterator::operator++(int)` and `Times2DIterator::operator++(int)` returned `*this` after the step.**
- Fix: all three now take a copy, step, and return the copy by value.
- Tests, in `Times_test.cpp`:
  - `it++` on `times(5)` answers 0 and leaves the iterator at 1.
  - `it--` from position 2 answers 2 and leaves the iterator at 1.
  - `it++` on the grid `times(2) * times(3)` answers (0,0) and leaves the iterator at (0,1).

**RED** (WSL `clang-debug`, tests added and bodies unchanged, `core-cpp-base-test 'times*'` and `'[fnv]'`):
```
Times_test.cpp:52: FAILED:  CHECK( *prior == 0 )   with expansion: 1 == 0
Times_test.cpp:62: FAILED:  CHECK( *prior == 2 )   with expansion: 3 == 2
Times_test.cpp:63: FAILED:  CHECK( *it == 1 )      with expansion: 3 == 1
Times_test.cpp:71: FAILED:  CHECK( *prior == std::tuple { 0, 0 } )
test cases:  7 | 4 passed | 3 failed   assertions: 10 | 6 passed | 4 failed
[fnv]: Segmentation fault (exit 139); test cases: 2 | 1 passed | 1 failed
```
**GREEN** (after the fixes):
```
times*: All tests passed (10 assertions in 7 test cases)
[fnv]:  exit=0, All tests passed (4 assertions in 2 test cases)
core-cpp-base-test: All tests passed (218 assertions in 59 test cases)
```

### 2. `core::ranges` (R27)

`.agent/rules/cpp-guidelines.md` now includes `ranges` in the list of allowed nested helper namespaces. I did not touch the spec.

### 3. The migration rename map (R28, minor #2)

In `.agent/guides/consumer-migration.md`:
- **Namespaces/includes table.** The wrong generic row (`<crispy/X.hpp>` → `<core/X.hpp>` for every file) is split into four rows:
  - the base files → `<core/X.hpp>`;
  - `LogStore`/`LogSink` → `<core/log/…>`;
  - `CLI`/`App` → `<core/cli/…>`;
  - `testing/Environment.hpp` → `<core/testing/Environment.hpp>`.
  
  It also gains `crispy::App` → `core::cli::App` and `crispy::testing::FakeEnvironment` → `core::testing::FakeEnvironment`.
- **API deltas table.** New rows:
  - `crispy::fatal` → `core::log::fatal` via `<core/log/Assert.hpp>`;
  - `SoftRequire` → the same macro via `<core/log/Assert.hpp>`, while `Require` and `Guarantee` stay in `<core/Assert.hpp>`;
  - `CRISPY_*` → `CORE_*`;
  - the global and `crispy::` `Overloaded` → `core::Overloaded`;
  - `SourceLocationCustom` removed;
  - `views::enumerate` is now a function.
- **fastcached table.** New rows:
  - `<FastCache/Core/{Profiling,Ranges}.hpp>` → `<core/…>`;
  - `FindOrNull`/`FindIfOrNull` → `core::findOrNull`/`findIfOrNull`;
  - `FastCache::Ranges::*` → `core::ranges::*`;
  - `FC_RANGES_FORCE_FALLBACK` → `CORE_RANGES_FORCE_FALLBACK`.
  
  The `FC_*` row now also lists `FC_FRAME_MARK*`, `FC_THREAD_NAME` and `FC_PLOT`.

### 4. Contour-specific wording (R28, minor #3)

- `App.hpp`: the comments on `reparseParameters`, `parseParametersForTesting`, `commandLine` and the `installLogging` example no longer mention the GUI, `contour client`, `contour daemon` or `"contour.daemon"`.
- `Utils_test.cpp`: the `vtpty` `@see` is gone.
- `docs/modules/cli.md`: the key example is now `tool.fetch.timeout`.
- `.agent/guides/profiling-tracy.md`: the status paragraph now describes what exists, including the CI leg.
- `.agent/rules/build-and-toolchain.md`: "FindOrNull … in Task A3" became `core::findOrNull`/`findIfOrNull` in `<core/Ranges.hpp>`.

I left `CLI_test.cpp`'s `contour.capture.*` keys alone. They are upstream test data and were not on the list.

### Commands and results

| Command | Result |
|---|---|
| `python scripts/clang-format.py --check` | 46 files formatted with clang-format 22.1.8 |
| Windows `clangcl-debug` `--clean-first`, then `ctest --preset clangcl-debug` | build OK, 12/12 passed (including `core-cpp.cmake-hygiene`) |
| WSL `clang-debug` build + ctest | build OK, 9/9 passed (including `core-cpp.cmake-hygiene`) |
| extra: WSL `clang-tidy` preset (pinned 22.1.8) | clean |
| extra: WSL `emscripten` (emsdk 3.1.56) | build OK, 9/9 passed, 0 commands mention pthread |
| extra: `python -m mkdocs build --strict` | rc 0 |
| CI Build on `6ebb78d` | https://github.com/contour-terminal/core-cpp/actions/runs/35346057994: **success**, every job including `ci-ok`, both emscripten legs and clang-22-tracy |
| CI Docs on `6ebb78d` | run 35346058049: success |
