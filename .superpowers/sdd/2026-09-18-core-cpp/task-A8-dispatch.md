# Dispatch: Task A8 (vendoring tool + consumer-smoke CI)

## Where this fits

core-cpp (D:\core-cpp, github.com/contour-terminal/core-cpp, branch `master`) is the shared C++23 library of the Contour projects. A8 is the **last task of Phase A**. Everything else is imported and green: `core::base`, `core::log`, `core::cli`, `core::platform`, `core::async`, `core::net` (+ `net_types`, `net_tls`) and `core::tui` (+ `tui_output`).

A8 proves core-cpp behaves as a library inside someone else's build:
- contour will carry a **verbatim copy** in `vendor/core-cpp`, so the vendoring tool must produce and check a byte-exact tree;
- endo, tuidu, fastcached, Lightweight and morph consume it **through CPM**, so a consumer's flags, cache variables and compiler launcher must survive untouched;
- morph also builds it for **WebAssembly**, so the subset must configure and link for a consumer under emsdk.

## Requirements

Read your brief first: `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A8-brief.md`. It is your requirements, with the exact steps and values to use verbatim, including the self-test's five refusal cases and the three smoke legs. Part I §5 of the spec (`D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md`) is the vendoring contract: the file set, the MANIFEST format, the git blob reads and the consumer obligations. `global-constraints.md` binds you, including: WSL is shared with other Claude sessions, so only `out/build/<preset>` trees and the processes you start are yours.

## What the tree already gives you

- **Module table:** `cmake/CoreCppModules.cmake` (rows carry `KIND`, `DEPS`, `PLATFORMS`, `WHEN`, and since A6 a target may have its own row with its own authoritative `DEPS`).
- **Layering check:** `tests/cmake/check-layering.cmake`, 12 configure-time scenarios, registered as `core-cpp.layering`.
- **Hygiene checks:** `tests/cmake/check-cmake-hygiene.cmake` and its self-test, plus `check-platform-sources.cmake` and `check-release.cmake`. Follow their style for the vendor self-test: a data table of scenarios, each proving a refusal by name.
- **CI:** `.github/workflows/build.yml` has the `style`, `linux`, `macos`, `windows`, `sanitizers`, `clang-tidy`, `emscripten`, `compile-cache` and `coverage` jobs, with `ci-ok` depending on the required ones. The `consumer-smoke` job does **not** exist yet; A2 deliberately left it to you (Ruling R15). Add it and make `ci-ok` depend on it.
- **Emscripten:** the `emscripten` preset and its CI matrix (emsdk 3.1.56 and latest) build the WebAssembly subset and run its tests under node.

## Rulings and cautions

- **The vendored copy is byte-exact.** Read blobs with `git -c core.autocrlf=false -c core.eol=lf cat-file blob`, refuse CR bytes, symlinks and submodules, and hash with `file(SHA256)`. `MODE=check` must work with no git available, because a consumer runs it in its own CI.
- **`CORE_CPP_TARGETS`** is the global property a parent reads to apply its own sanitizers or coverage per target. The CPM smoke asserts it is non-empty and that every entry is a real, compiled, non-test target.
- **The CPM smoke's job is to catch us changing a consumer's build.** Assert the consumer's directory `COMPILE_OPTIONS`, `LINK_OPTIONS`, `INCLUDE_DIRECTORIES`, `CMAKE_CXX_FLAGS*` and `CMAKE_CXX_COMPILER_LAUNCHER` are unchanged across the `CPMAddPackage`, and that no `core-cpp-*-test` target exists when `CORE_CPP_TESTING` is OFF. A failure here is the point of the test, so make the message say which variable changed and to what.
- **The vendored leg runs offline** (`docker run --network none`), with `CORE_CPP_FETCH_DEPS OFF`, `CORE_CPP_WITH_TUI OFF`, `CORE_CPP_WITH_TLS ON`. If Docker is unavailable on the runner, an equivalent that provably has no network is acceptable — say in the job what proves it.
- **The WebAssembly leg mirrors morph:** an INTERFACE library over an executable, linking only `core::base core::async core::net`, with `CORE_CPP_WITH_TUI OFF` and `CORE_CPP_FETCH_DEPS OFF`, built with `emcmake` under emsdk 3.1.56 and run with node. Until B5 lands there are no timers, so the executable awaits what exists today (a `core::async` Task and `core::net_types`); the brief says B5 extends it. Do not invent Phase B API.
- **Do not weaken a smoke test to make it pass.** If a leg fails because core-cpp really does leak state into a consumer, fix core-cpp and say so in the report.

## Also fold in (two Minor items from A7's re-review)

1. `src/core/tui/TerminalOutput.hpp:84-86`: the `SyncGuard` class doc credits the constructor with a flush that `TerminalOutput::syncGuard()` actually performs. Correct the doc to say where the flush happens.
2. The VtParser caps do not allow for the in-flight terminator, so clean behaviour ends at cap − 6 for a paste and cap − 2 for a DCS string. Either reserve the terminator's room in the cap, or say the limit precisely in the comment and the CHANGELOG. Do not change the cap's magnitude.

## Verification

- **Windows:** in PowerShell, `& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation`, then `clangcl-debug` (`--clean-first` after header edits) and `cl-debug`.
- **WSL:** `wsl -d Ubuntu-26.04 -- bash -lc 'cd /mnt/d/core-cpp && cmake --preset <p> && cmake --build --preset <p> && ctest --preset <p>'` for `clang-debug` and `gcc-release`, plus `ctest -L hygiene`.
- Run the vendor self-test and both consumer smokes locally, and say in the report which ones ran locally versus only in CI.
- `python scripts/clang-format.py --check` and `mkdocs build --strict`.
- Push, then watch CI yourself with `gh run watch <id> -R contour-terminal/core-cpp --exit-status`, and run `gh workflow run portability.yml -R contour-terminal/core-cpp` and watch it. `ci-ok` must be green **including the new `consumer-smoke` job**. Fix forward if red.

## Commits

Small and semantic. The brief names the main subject. Each message ends with `Signed-off-by: Christian Parpart <christian@parpart.family>`.

Also update, in the same round: `docs/vendoring.md` (the commands and the consumer's obligations), the CHANGELOG `[Unreleased]`, and the README where it points a consumer at either mechanism.

## Report contract

Write the full report to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A8-report.md`: what you built, TDD evidence (the self-test failing before the script exists, then passing; each smoke assertion proven able to fail), local results per preset, CI run ids and conclusions, files changed, self-review, concerns.

Then reply in under 15 lines: Status (DONE | DONE_WITH_CONCERNS | BLOCKED | NEEDS_CONTEXT), commits, a one-line test summary, concerns, and the report path.

Messages in this session can be delayed by hours. Files and `gh` are your source of truth, and you never sit idle waiting for a notification. Never dispatch subagents. Self-review your own diff before reporting.
