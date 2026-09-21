# Dispatch: Task A5 (`core::coro` import + StopToken alias/fallback)

## Where this fits

core-cpp (D:\core-cpp, github.com/contour-terminal/core-cpp, branch `master`) is the shared C++23 library replacing copies of coro/net/platform/tui across contour, endo, fastcached, tuidu. Phase A imports the code as-is, one module per task. A1–A4 are done:
- A1: CMake framework
- A2: docs/CI
- A3: `core::base`, `core::log`, `core::cli`
- A3b: provenance
- A4: `core::platform`, testing helpers, `core::coro::Generator`

A5 fills `core::coro` with contour's Task stack plus a StopToken that falls back where std lacks it. Phase B (B1) later grafts fastcached's executors onto it.

## Requirements

Read your brief first: `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A5-brief.md`. It is your requirements, with the exact values to use verbatim. It names `global-constraints.md` and the spec; both are binding.

## What earlier tasks give you

- **The `coro` module already exists.**
  - `src/core/coro/CMakeLists.txt`: `core_cpp_add_module(coro KIND INTERFACE HEADERS Generator.hpp)` + `core_cpp_add_test(coro SOURCES Generator_test.cpp)`.
  - Module table row: `cmake/CoreCppModules.cmake:106` (`PLATFORMS any`, so the whole module builds and its tests run under single-threaded Emscripten in CI).
  - Read `cmake/CoreCppTargets.cmake` for what `core_cpp_add_test` supports (per-platform source lists, labels). Extend it only if needed, following its existing style.
- **The fallback precedent is `src/core/coro/Generator.hpp`**, and A3's `src/core/Ranges.hpp` does the same (`CORE_RANGES_FORCE_FALLBACK`):
  - `<version>` is included first, so the choice does not depend on include order.
  - The fallback is always defined under `detail::`.
  - A `CORE_*_FORCE_FALLBACK` macro selects it.
  - Mirror that shape for StopToken.
- **Contour's copy.** Contour's `src/coro/Cancellation.hpp` @ `6777ff05` already aliases `StopToken`/`StopSource`/`StopCallback` to std, with an `#error` when `__cpp_lib_jthread` is missing.
  - Those aliases move to the new `StopToken.hpp`, and the `#error` goes.
  - Cancellation.hpp keeps `thisCoroStopToken`/`HasStopToken`/`OperationCancelled` over the new aliases.
- **Windows dialogs are already merged.** `coro/testing/SuppressWindowsDialogs.hpp` was merged into `src/core/testing/SuppressWindowsDialogs.hpp` in A1 (provenance row exists). Verify that and do nothing else for it.
- **Test main.** Contour's `src/coro/test_main.cpp` is NOT imported: every test binary links `core::testing_main`, via `core_cpp_add_test`. Contour's `src/coro/README.md` content belongs in `docs/modules/coro.md`; fold in what is still true, and cite the origin.
- **Provenance.** Every file you add under `src/core/` needs a row in `.agent/reference/provenance.md`: `core-cpp path | upstream repo | upstream path | synced SHA (full 40 hex) | notes`. The hygiene check refuses files without one. New files get `origin: core-cpp`.
- **Exceptions.** Recoverable errors use `std::expected`. Exceptions are for unrecoverable conditions only, plus `core::coro::OperationCancelled`; see `.agent/rules/cpp-guidelines.md`.
- **Naming.** The std-compatible members of the fallback must carry std's snake_case names (`request_stop`, `stop_requested`, `stop_possible`, `get_token`, …) so code compiles against either branch.
  - No NOLINT is allowed. If clang-tidy's naming check flags them, extend the IgnoredRegexp in `.clang-tidy` (it already exempts coroutine/STL hooks).

## Rulings you must follow

**Ruling R33: the macro build is a separate test executable.** Compile `CORE_CORO_FORCE_STOP_TOKEN_FALLBACK` into its own test executable, not into extra TUs of `core-cpp-coro-test`.
- Name it `core-cpp-coro-fallback-test`, registered as ctest `core-cpp.coro-fallback`, with the same labels as `core-cpp.coro`.
- It compiles ALL the coro test sources with the macro defined: StopToken, Task, WhenAll, WhenAny.
- Why:
  - Mixing TUs with and without the macro in one binary gives inline functions in Cancellation.hpp/Task.hpp two different definitions (an ODR violation).
  - Compiling everything this way also exercises Task/whenAll/whenAny cancellation over the fallback.
- This satisfies the brief's "compiles the StopToken tests TWICE".

**Threads and Emscripten.**
- StopToken cases that need threads (concurrent `request_stop` vs callback destruction, blocking `~StopCallback`) are compiled only where threads exist: `#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)`, or a native-only source list.
- The rest of the coro suite must run under node in the `emscripten` CI job on emsdk 3.1.56 and latest.
- Measured facts:
  - emsdk 3.1.56 ships libc++ 17.0.4, which does not define `__cpp_lib_jthread` without `-fexperimental-library`, so the fallback is live there.
  - Do NOT add `-fexperimental-library` or any INTERFACE compile flag.

**Single-threaded Emscripten** (`__EMSCRIPTEN__` && !`__EMSCRIPTEN_PTHREADS__`): the fallback uses no atomics and no waiting, only plain state.

## Local builds (all must be green before you report)

**Windows.** Build in PowerShell after:
```
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
```
Then run `cmake --preset clangcl-debug`, `cmake --build --preset clangcl-debug`, `ctest --preset clangcl-debug`, and the same for `cl-debug`.

The local fastcache-cc predates the fix for fastcached#1531: on clangcl trees, header edits can be missed on cache hits. Use `cmake --build --preset clangcl-debug --clean-first` after editing headers.

**WSL.** Run `wsl -d Ubuntu-26.04 -- bash -lc 'cd /mnt/d/core-cpp && cmake --preset <p> && cmake --build --preset <p> && ctest --preset <p>'` for `clang-debug`, `gcc-debug` and `clang-tsan`. clang-tsan is required for the concurrent StopToken cases.

**Format.** Run `python scripts/clang-format.py --check`.

**Push and CI.**
- Push to `origin master` when local is green. Watch CI with `gh run watch <id> -R contour-terminal/core-cpp --exit-status`; poll it yourself, and do not wait idle for notifications.
- The task is done only when `ci-ok` is green, including the `emscripten` job on both emsdk versions.
- If CI fails, fix forward with a new commit.

## Commits

- Small and semantic. Each ends with the trailer `Signed-off-by: Christian Parpart <christian@parpart.family>`.
- The brief names the main commit subject. A separate commit for StopToken is fine, e.g. `coro: StopToken aliases std::stop_token, with a fallback where the library lacks it`.
- Record contour `6777ff05` coro imports in CHANGELOG.md/NOTICE if they are not already listed.

## Report contract

Write the full report to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A5-report.md`. It covers:
- what you implemented;
- TDD evidence (RED command + failing output, GREEN command + passing output);
- the local results for each preset;
- the CI run id + conclusion;
- files changed;
- self-review;
- concerns.

Then reply in under 15 lines: Status (DONE | DONE_WITH_CONCERNS | BLOCKED | NEEDS_CONTEXT), commits (short SHA + subject), a one-line test summary, concerns, and the report path.

You never dispatch subagents: no helpers, and no reviewers. Review comes from the controller after your report.
