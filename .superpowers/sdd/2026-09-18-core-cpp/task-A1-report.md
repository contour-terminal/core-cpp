# Task A1 report: CMake framework, exit-code-normalising test main, hygiene checks

Status: DONE_WITH_CONCERNS. All four required local configurations are green, pinned clang-format is
clean, and fastcache-cc was selected and served hits on both Windows and WSL.

## Commits (on `master`, base 4abb140)

| SHA | Subject |
|---|---|
| 6a2001e | build: repository metadata, Apache-2.0 license and notice |
| 4ca4ea0 | style: pinned clang-format and clang-tidy, with a runner that refuses other builds |
| 5a79ff6 | build: CMake module framework and exit-code-normalising test main |
| f0c951c | test: CMake and C++ hygiene checks, proven by their own self-test |

Each commit ends with `Signed-off-by: Christian Parpart <christian@parpart.family>`. Commit 5a79ff6 was
verified on its own (WSL clang-debug and gcc-debug, 2/2 tests) before the hygiene files were restored.
No remote was created and nothing was pushed.

## What was implemented

- **Module table** (`cmake/CoreCppModules.cmake`): `core_cpp_module(NAME [DIR] KIND DEPS PLATFORMS WHEN)`
  and `core_cpp_add_modules()`. The walker enters each enabled module's directory in table order. It
  skips a row whose WHEN option is OFF, and a `native` row under Emscripten. It refuses an enabled module
  whose DEPS are disabled. The only row is `testing KIND STATIC PLATFORMS any WHEN CORE_CPP_TESTING`.
  `DIR` is optional (it defaults to NAME), so A3 can map `base` to `src/core`.
- **Targets** (`cmake/CoreCppTargets.cmake`): `core_cpp_add_module(<name> KIND STATIC|INTERFACE|OBJECT
  HEADERS … SOURCES … SOURCES_POSIX|LINUX|BSD|WINDOWS … PUBLIC_LIBS … PRIVATE_LIBS …)`.
  - It creates `core-cpp-<name>` and the alias `core::<name>`, puts public headers in a FILE_SET based
    at `src/`, and adds the generated include directory.
  - It enforces layering: a `core::<x>` link must come from the same module or from one the row lists in
    DEPS. The real target carries the property `CORE_CPP_MODULE`.
  - Platform source lists come from a table: `UNIX`, `LINUX`, `APPLE OR BSD`, and `WIN32`.
  - `core_cpp_add_test(<module> SOURCES… LIBS… LABELS…)` builds `core-cpp-<module>-test` and links it with
    `core::<module>` and `core::testing_main`. It registers `core-cpp.<module>` with
    `SKIP_RETURN_CODE 77` and labels `core-cpp;<module>`.
- **Dependencies** (`cmake/CoreCppDependencies.cmake`): `core_cpp_dependency(<name> WHEN TARGETS
  FIND_PACKAGE CPM|NO_FETCH WRAP)`.
  - Resolution tries a parent target, then `find_package(QUIET)`, then CPM when `CORE_CPP_FETCH_DEPS` is
    on. Otherwise it stops with a FATAL error that names the option.
  - Rows: Threads (`WHEN CORE_CPP_USE_THREADS`, which is off under single-threaded Emscripten; NO_FETCH)
    and Catch2 3.8.0 (`WHEN CORE_CPP_TESTING`).
  - `cmake/CPM.cmake` combines endo's 0.40.8 pin and SHA-256 with fastcached's bounded, status-checked
    download. It is included lazily, and only when the parent has not already loaded CPM.
- **Options** (`cmake/CoreCppOptions.cmake`): every row of Part I §3.
  - `CORE_CPP_WITH_IMAGES` is a `cmake_dependent_option` on TUI.
  - `CORE_CPP_SANITIZERS` is FATAL when core-cpp is not top-level.
  - Under Emscripten, TUI, IMAGES and TLS are shadowed OFF by normal variables, so the parent's cache is
    untouched.
- **Toolchain** (`cmake/CoreCppToolchain.cmake`): data tables applied per target, PRIVATE only.
  - MSVC-driver `/utf-8 /permissive- /Zc:__cplusplus`, and the Windows definitions.
  - The probed union of endo's and fastcached's warnings, each cached as `CORE_CPP_HAS_<flag>`.
  - WERROR, sanitizers and coverage.
  - clang-tidy: `CXX_CLANG_TIDY` is cleared when the option is OFF. When it is ON, the tool's version is
    compared with `.clang-tidy-version` and a mismatch warns.
  - The standard is a PUBLIC `cxx_std_23` usage requirement, with `CXX_EXTENSIONS OFF` and
    `CXX_SCAN_FOR_MODULES OFF`.
  - Single-threaded Emscripten is detected with a `__EMSCRIPTEN_PTHREADS__` probe.
- **Top level** (`cmake/CoreCppTopLevel.cmake`, included only `if(PROJECT_IS_TOP_LEVEL)`, after
  `project()` and before `core_cpp_resolve_dependencies()`).
  - It includes `portable/CompileCache.cmake` first.
  - It then records `CORE_CPP_CXX_COMPILER_LAUNCHER` (INTERNAL cache).
  - It sets `CMAKE_CXX_STANDARD 23` (so CPM-built Catch2 matches), `CMAKE_EXPORT_COMPILE_COMMANDS` and
    `CMAKE_COLOR_DIAGNOSTICS`.
- **Verbatim imports**: `cmake/portable/CompileCache.cmake` and `cmake/FetchTransferBound.cmake` are
  copies of fastcached blobs at eb9c9c68. The git blob hashes are identical (864c1ce0…, b777b0c8…).
  `cmake/portable/README.md` records the provenance, the re-sync-never-edit rule, the launcher order and
  the opt-out.
- **Presets** (`CMakePresets.json`, version 6, CMake ≥ 3.25):
  - Hidden `base`, `unix` and `windows` presets.
  - The configure presets: `clang-debug`, `clang-release`, `gcc-debug`, `gcc-release`,
    `clang-asan-ubsan`, `clang-tsan`, `clang-tidy`, `clang-coverage` (`USE_COMPILER_CACHE=OFF`),
    `clang-tracy`, `appleclang-{debug,release}`, `cl-{debug,release}`, `clangcl-{debug,release}` and
    `cl-release-tls`.
  - A mirroring build and test preset for each. Test presets have `outputOnFailure`,
    `noTestsAction: error` and `timeout: 300`.
  - A `ci-<name>` workflow preset for each.
- **`src/core/Config.hpp.in`** generates `core/Config.hpp`: the version, `CORE_CPP_SKIP_EXIT_CODE` (77),
  and `#cmakedefine01` for WITH_TUI, WITH_IMAGES, WITH_TLS and WITH_TRACY.
- **`src/core/testing/`**:
  - `ExitCode.{hpp,cpp}`: `core::testing::normalisedExitCode` and `SkipExitCode`, exactly as the brief
    gives them.
  - `CatchMain.cpp` (`core::testing_main`, with the target property `CORE_CPP_SKIP_EXIT_CODE` = 77).
  - The merged `SuppressWindowsDialogs.hpp`, the union of the four copies: all CRT report types,
    `_set_abort_behavior`, the invalid-parameter handler, and `SEM_FAILCRITICALERRORS |
    SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX`.
  - endo's `SuppressWindowsDialogsAtStartup.cpp`, as the OBJECT library `core::testing_dialogs`.
    `core::testing_main` puts its object on every test executable's link line through an interface
    `$<TARGET_OBJECTS>` item; this was verified in `build.ninja`.
  - `ExitCode_test.cpp`, which unit-tests every branch.
- **`tests/`**:
  - `ExitCodeFixture.cpp` and `cmake/check-exit-codes.cmake` (test `core-cpp.exit-codes`).
  - The hygiene scanner and its self-test (`core-cpp.cmake-hygiene`, `core-cpp.cmake-hygiene-selftest`).
    All three of these carry the label `hygiene`.
  - On WIN32, `WindowsDialogCanary.cpp`, registered once per mode (`assert`, `abort`,
    `invalid-parameter`) with `WILL_FAIL`, `TIMEOUT 60`, `SKIP_RETURN_CODE 77` and label `canary`.
- **Style**:
  - `.clang-format` is contour's, with `<core/…>` include categories.
  - `.clang-tidy` is contour's, with a `src/core` header filter and fastcached's hook-name IgnoredRegexp.
    That regexp is applied to Function, Method and ClassMethod, and there are typedef and specialization
    lists as well.
  - `.clang-format-version` and `.clang-tidy-version` pin 22.1.8 in the organisation's `key: value`
    format; see concerns.
  - `.clang-format-ignore` is empty.
  - `scripts/tool-versions.py` prints, installs (`--install`) or checks (`--check`) the pins.
    `scripts/clang-format.py` formats in place, or checks with `--check`, and refuses any other build.
- **Metadata**: `.gitattributes` (`* text=auto eol=lf`), `.gitignore` (`/out/`, `__pycache__/`, …),
  `.editorconfig`, `LICENSE` (contour's Apache-2.0 text), and `NOTICE`, which lists every imported file
  and full SHA.

## Files changed (38 files, +5288 lines)

`.clang-format`, `.clang-format-ignore`, `.clang-format-version`, `.clang-tidy`, `.clang-tidy-version`,
`.editorconfig`, `.gitattributes`, `.gitignore`, `CMakeLists.txt`, `CMakePresets.json`, `LICENSE`,
`NOTICE`, `cmake/{CPM,CoreCppDependencies,CoreCppModules,CoreCppOptions,CoreCppTargets,CoreCppToolchain,CoreCppTopLevel,FetchTransferBound}.cmake`,
`cmake/portable/{CompileCache.cmake,README.md}`, `scripts/{clang-format,tool-versions}.py`,
`src/core/Config.hpp.in`,
`src/core/testing/{CMakeLists.txt,CatchMain.cpp,ExitCode.cpp,ExitCode.hpp,ExitCode_test.cpp,SuppressWindowsDialogs.hpp,SuppressWindowsDialogsAtStartup.cpp}`,
`tests/{CMakeLists.txt,ExitCodeFixture.cpp,WindowsDialogCanary.cpp}`,
`tests/cmake/{check-exit-codes,check-cmake-hygiene,check-cmake-hygiene-selftest}.cmake`.

## TDD evidence

### Exit-code test

RED: the fixture was linked with `Catch2::Catch2WithMain`.

```
> cmake --preset clangcl-debug; cmake --build --preset clangcl-debug; ctest --preset clangcl-debug -R exit-codes
1/1 Test #1: core-cpp.exit-codes ..............***Failed    0.09 sec
CMake Error at D:/core-cpp/tests/cmake/check-exit-codes.cmake:23 (message):
  exit code for [fail4]: got 42, want 1
```

The raw Catch2 3.8.0 codes were measured by running the fixture directly: `[pass]` 0, `[fail4]` 42,
`[skipall]` 4, `[mixed]` 42, `[nothing-has-this-tag]` 2. The brief predicted "got 4"; Catch2 3.8 returns
42 on failure.

GREEN: `ExitCode.cpp` and `CatchMain.cpp` were added, and the fixture was linked with
`core::testing_main`.

```
> ctest --preset clangcl-debug -R exit-codes
1/1 Test #2: core-cpp.exit-codes ..............   Passed    0.10 sec
100% tests passed, 0 tests failed out of 1
```

The same test passed through WSL `clang-debug` (and `gcc-debug`).

### Hygiene self-test

RED 1, with no scanner:

```
> ctest --preset clangcl-debug -R cmake-hygiene
core-cpp.cmake-hygiene-selftest ...***Failed
  hygiene-selftest: SCANNER ('D:/core-cpp/tests/cmake/check-cmake-hygiene.cmake') is not set or does not exist.
```

RED 2, with a stub scanner that accepts everything, which is the meaningful RED:

```
> cmake -DSCANNER=…/check-cmake-hygiene.cmake -DWORK_DIR=… -P tests/cmake/check-cmake-hygiene-selftest.cmake
CMake Error at tests/cmake/check-cmake-hygiene-selftest.cmake:131 (message):
  hygiene-selftest:
    case 'missing-spdx' names no rule of the scanner
    … (one line for each of the 14 cases)
    missing-spdx: src/core/foo/Foo.cpp was not refused
    unprefixed-option: CMakeLists.txt was not refused
    … (all 14 cases: "was not refused")
```

GREEN, after implementing the scanner as `{rule, kind, regex, except, reason}` plus an allowlist
`{rule, file, reason}`:

```
-- check-cmake-hygiene: 24 file(s) under D:/core-cpp are clean
-- hygiene-selftest: the clean tree passed and all 14 violations were refused by name
```

Two scanner defects were found and fixed while it was test-driven:

- A multi-value `KIND "cmake;cpp"` passed through PARSE_ARGV stayed a single escaped element.
- A `;` inside a reason or in a source line split the violations list.

The final scanner was also run on an edge-case tree. It reported the right line numbers across blank
lines, a trailing `\`, `[[…]]` and `;`, and it scanned the whole 1748-line verbatim `CompileCache.cmake`.

## Test results (HEAD f0c951c)

| Configuration | Result |
|---|---|
| Windows `clangcl-debug` (clang-cl 22.1.3) | 7/7 passed: testing, exit-codes, cmake-hygiene, cmake-hygiene-selftest, and 3 canaries |
| Windows `cl-debug` (MSVC, VS 2026) | 7/7 passed |
| WSL `clang-debug` (clang 22.1.2) | 4/4 passed |
| WSL `gcc-debug` (GCC 14.3) | 4/4 passed |

These additional presets were also verified, although the brief does not require them:

- Windows `clangcl-release` and `cl-release`: 7 tests, with the canary's `assert` mode reported as
  skipped under NDEBUG, as intended.
- WSL `clang-release`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan`, `clang-coverage` (produces
  `default.profraw`), `clang-tracy`, and `clang-tidy` (with pinned clang-tidy 22.1.8 installed with
  `pip --target` into /tmp and removed afterwards): all 4/4.
- `cmake --list-presets=all` on Windows lists only the cl, clangcl and TLS presets. On WSL it lists only
  the clang, gcc, sanitizer, tidy, coverage and tracy presets. Workflow presets list everywhere, because
  they cannot carry conditions.
- clang-tidy 22.1.8 over every source, run on Windows against the clangcl compile database: clean, with
  header coverage.
- Canary, run by hand in cl-debug: the Debug-CRT `Assertion failed: …` report went to stderr and the
  process exited 3. No dialog appeared.
- Refusal paths each fail naming their cause: `CORE_CPP_FETCH_DEPS=OFF` without Catch2, an unknown
  sanitizer, and sanitizers requested as a subproject.
- core-cpp added as a subproject with default options leaves the parent's `CMAKE_CXX_COMPILER_LAUNCHER`,
  `CMAKE_CXX_STANDARD` and directory `COMPILE_OPTIONS` unchanged. It creates no `core::testing` and
  downloads no CPM.
- `python scripts/clang-format.py --check` passes (8 files, clang-format 22.1.8) on Windows and in WSL.
  With `--binary` pointing at VS's clang-format 22.1.3 it refuses (exit 2). With a deliberate misformat
  it fails and names the file.

## Compiler-cache selection

- **Windows**: `cmake --preset clangcl-debug` (and `cl-debug`) prints
  `-- [cache] Enabling fastcache-cc at 127.0.0.1:6674 (C:/Users/chris/AppData/Local/fastcache-cc/bin/fastcache-cc.exe) for C/C++ compilation`.
  - `Select-String CMakeCache.txt -Pattern 'CMAKE_CXX_COMPILER_LAUNCHER'` finds **nothing**. The verbatim
    module sets `CMAKE_CXX_COMPILER_LAUNCHER` as a normal variable, never in the cache.
  - The record this task adds does name fastcache-cc:
    `CORE_CPP_CXX_COMPILER_LAUNCHER:INTERNAL=…cmake.exe;-E;env;FASTCACHE_ADDR=127.0.0.1:6674;…;…/fastcache-cc.exe`.
  - A clean rebuild with `$env:FASTCACHE_VERBOSE=1` gave **111 `fastcache-cc: HIT` lines and 0 MISS**,
    Catch2 included.
- **WSL**: a daemon answers from WSL:
  `-- [cache] Enabling fastcache-cc at 127.0.0.1:6674 (/home/christianparpart/.local/bin/fastcache-cc)`,
  with debug prefix maps applied. A clean rebuild with `FASTCACHE_VERBOSE=1` gave **110 HIT, 0 MISS**.
- **`-DUSE_COMPILER_CACHE=OFF`** (a throwaway WSL build dir): `-- [cache] Compiler caching disabled by
  USE_COMPILER_CACHE=OFF`. `CORE_CPP_CXX_COMPILER_LAUNCHER` is empty, and `build.ninja` contains no
  `fastcache-cc`. The module defines the launcher as empty, not undefined, by design.

## fastcached SHA

`eb9c9c68da8fadfd43b0b36366919cb462689f48` (`origin/master` after `git -C D:\fastcached fetch origin`,
2026-09-18). It is recorded in `cmake/portable/README.md` and `NOTICE`. contour is at
`6777ff05014f8ff163b071e8b0e942830119db80` and endo at `f774a210ce989e5947b8f61d715068b1dc96088c`. Every
import was read as a blob, and there are no CR bytes in any committed file of this task.

## Self-review findings (all fixed before committing)

- `core_cpp_add_test` read `core-cpp-testing_main`'s property at call time. With A3's `base` row ahead of
  `testing`, base's test registration would have failed configure. It now uses the variable
  `CORE_CPP_SKIP_EXIT_CODE`.
- `check-exit-codes.cmake` ran the fixture directly, so A2's emscripten job would fail under node. It now
  takes `-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}`.
- clang-tidy found `readability-use-concise-preprocessor-directives` (changed to `#ifdef`) and
  `modernize-use-std-print` in the canary (changed to `std::println`). The code was fixed rather than the
  checks disabled.
- clang-cl found `NotExercised` unused in a Debug build; it is now `[[maybe_unused]]`, with the reason.
- The `access-violation` canary mode was removed. ctest reports an exception exit as "Exception:
  SegFault" whatever `WILL_FAIL` says. ctest's children also inherit `SEM_NOGPFAULTERRORBOX`, so that mode
  proved nothing about core-cpp's suppression.
- `CORE_CPP_ENABLED_MODULES` was dropped because nothing read it. Status lines were tidied.

## Concerns

1. **The CMakeCache check in the brief and spec cannot pass literally.** `CompileCache.cmake` (verbatim)
   never caches `CMAKE_CXX_COMPILER_LAUNCHER`. I added the INTERNAL record `CORE_CPP_CXX_COMPILER_LAUNCHER`.
   A2's `compile-cache` job must grep that entry (or `build.ninja`) rather than
   `CMAKE_CXX_COMPILER_LAUNCHER`.
2. **HeaderFilterRegex deviates from the spec's literal `'.*/src/core/.*'`.** It is
   `'.*[/\\]src[/\\]core[/\\].*'`. I measured that the literal form silently drops every header finding
   on Windows, where the path is `D:\core-cpp\src\core/testing/…`.
3. **The version pin files use the organisation's `key: value` format**, not a bare `22.1.8`:
   `version: clang-format version 22.1.8`, and `package:` plus `version:` for clang-tidy, as fastcached
   has them. The contour-workflows format-on-edit hook requires this format and refused the bare form.
   `tool-versions.py` parses it. On this machine the hook still cannot find the pip binary on its PATH,
   which is a local environment matter.
4. **`core::testing` exists only with `CORE_CPP_TESTING=ON`** (module row and Catch2 row both use
   `WHEN CORE_CPP_TESTING`, as the spec's tables say). C1 (endo) and C3 (fastcached) configure core-cpp
   with `CORE_CPP_TESTING OFF`, yet C1 deletes endo's testing helpers in favour of `core::testing`. A4
   or C1 needs a ruling. One option: build the testing module whenever a parent `Catch2::Catch2` exists.
5. **A `-Wno-c2y-extensions` row was added.** Under `-Wpedantic`, clang 22 flags `__COUNTER__` from
   Catch2's `TEST_CASE` and `SECTION` in our own translation units. This cannot be fixed in core-cpp's
   code; the row is documented and matches fastcached's finding. There is no issue to cite yet, because
   the repository is created in A2.
6. **The dialog suppression is narrower than endo's.** endo's product variant (suppress only when
   `ENDO_SUPPRESS_WINDOWS_DIALOGS` is set) and its `probe` mode were not imported; core-cpp's startup TU
   suppresses unconditionally. endo's `endo-bin` product behaviour (C1) will need endo's own
   `WindowsDialogs.cmake` or a core-cpp variant.
7. **Beyond the brief** (flagged for the controller):
   - `KIND OBJECT` in `core_cpp_add_module`, for `core::testing_dialogs`. The table KIND stays
     STATIC|INTERFACE.
   - An optional `DIR` in `core_cpp_module`.
   - An optional `LIBS` in `core_cpp_add_test`.
   - `ExitCode_test.cpp`.
   - A 5th exit-code row, `[nothing-has-this-tag];2`, for the "nothing ran" branch.
   - Hygiene rules beyond the five in the brief: SPDX header, cache-variable and function prefixes,
     CMAKE_* global variables, PUBLIC or INTERFACE flags, source globs, include-by-name, diagnostic
     pragmas, C-style `for(;;)`, and stale allowlist rows.
   - A clang-tidy version warning.
8. **The hygiene rules affect later tasks.** The `c-style-for` rule will refuse the C-style loops that
   imported contour and endo code still contains, which is consistent with Ruling R4. A5's
   `-fexperimental-library` INTERFACE usage requirement will need a `public-flags` allowlist row with its
   reason. Allowlist rows are per file and per rule, not per line.
9. **Left to later tasks, deliberately:**
   - The `CORE_CPP_TARGETS` global property. A8 Step 0 owns it test-first; the sanitizer FATAL message
     already refers to it.
   - The libunicode, stb, OpenSSL and Tracy dependency rows. Declaring them now would make every
     configure fetch libunicode for nothing, because `WITH_TUI` defaults ON.
   - `CHANGELOG.md` (A2). The pins are in `NOTICE` and `cmake/portable/README.md`.
   - Examples wiring: `CORE_CPP_BUILD_EXAMPLES` exists, but there is no `examples/` yet.
   - vcpkg and OpenSSL for `cl-release-tls`: the preset only sets `CORE_CPP_WITH_TLS=ON`.
10. **`cmake/CPM.cmake` includes `FetchTransferBound.cmake`, as fastcached does.** That exports
    `GIT_HTTP_LOW_SPEED_*` into the configure process. When core-cpp is a subproject, this happens only
    if core-cpp itself has to fetch; it is a process-level side effect outside `CoreCppTopLevel.cmake`.
11. **The Task 0 `README.md` contains 3 CR bytes.** This predates this task; A2 replaces the file.
12. **WSL's Python refuses `pip install --user` (PEP 668).** I did not use `--break-system-packages`. The
    clang-tidy preset was verified with a throwaway `pip --target /tmp/...` install, which was removed
    afterwards.

## Pre-review fixes (Rulings R7 and R8), commit 26de633

Commit: `26de633 testing: core::testing without Catch2; fetch bound from the top level only`, signed off.

### What changed

**R7: `core::testing` no longer depends on `CORE_CPP_TESTING` or on Catch2.**

- The module row is `core_cpp_module(NAME testing KIND STATIC PLATFORMS any)`. It has no `WHEN`, so it
  builds natively and in the WebAssembly subset.
- `core::testing` is Catch2-free. It holds `SuppressWindowsDialogs.hpp` plus a new
  `SuppressWindowsDialogs.cpp`. `suppressWindowsDialogs()` moved out of line to give the STATIC library a
  real source, and that also keeps `<Windows.h>` out of every translation unit that includes the header.
  Off Windows the function is a no-op.
- `core::testing_dialogs` (OBJECT) now links `core::testing` for that definition.
- `core::testing_main` is the only Catch2 target. It now owns `ExitCode.{hpp,cpp}` and is built only
  `if(CORE_CPP_CATCH2_MAIN)`.
- The new option `CORE_CPP_CATCH2_MAIN` defaults to `${CORE_CPP_TESTING}`. When `CORE_CPP_TESTING` is ON
  it is forced ON, by a normal variable that shadows the cache entry, with a status line.
- The Catch2 row is `WHEN CORE_CPP_TESTING OR CORE_CPP_CATCH2_MAIN`. `core_cpp_dependency`'s `WHEN` is now
  a multi-value `if()` condition instead of one variable name, and messages print the condition.
- `CORE_CPP_TESTING`'s help text is now "Build core-cpp's own tests".

**R8: `FetchTransferBound.cmake` is included only from `CoreCppTopLevel.cmake`**, after `CompileCache.cmake`
and before any dependency resolution.

- `cmake/CPM.cmake` passes `INACTIVITY_TIMEOUT` only when `FASTCACHED_FETCH_SILENCE_SECONDS` is defined.
  An empty value would break `file(DOWNLOAD)`'s argument list.
- As a subproject, the bootstrap is bounded by the parent's setting when the parent defines one, and is
  otherwise unbounded.
- To enforce this mechanically, the hygiene scanner gained two rules, each with a self-test case (16 rules
  now):
  - `process-environment` refuses `set`/`unset(ENV{…})`. It is allowlisted in `FetchTransferBound.cmake`.
  - `top-level-only-include` refuses including `CompileCache.cmake` or `FetchTransferBound.cmake`. It is
    allowlisted in `CoreCppTopLevel.cmake`.

**Other edits**: `NOTICE` names `SuppressWindowsDialogs.{hpp,cpp}`, and `cmake/portable/README.md` says
who includes `FetchTransferBound.cmake`.

### Commands and output

```
$ python scripts/clang-format.py --check
clang-format.py: 9 file(s) are formatted with clang-format 22.1.8

$ cmake -DROOT=D:/core-cpp -P tests/cmake/check-cmake-hygiene.cmake
-- check-cmake-hygiene: 26 file(s) under D:/core-cpp are clean
$ cmake -DSCANNER=... -DWORK_DIR=... -P tests/cmake/check-cmake-hygiene-selftest.cmake
-- hygiene-selftest: the clean tree passed and all 16 violations were refused by name

# R8 regression probe: FetchTransferBound's include appended to cmake/CPM.cmake, then reverted
  cmake/CPM.cmake:65: [top-level-only-include] the compiler cache and the fetch bound change the whole
  configure, so only cmake/CoreCppTopLevel.cmake includes them

# Windows (after a clean rebuild, see the finding below), ctest --preset <p>
clangcl-debug: 100% tests passed, 0 tests failed out of 7
cl-debug:      100% tests passed, 0 tests failed out of 7
# WSL
clang-debug:   100% tests passed, 0 tests failed out of 4
gcc-debug:     100% tests passed, 0 tests failed out of 4

# pinned clang-tidy 22.1.8 over src/core/testing/*.cpp (clangcl compile database): exit 0, no findings
```

Subproject configure in WSL (`add_subdirectory(/mnt/d/core-cpp)` from a parent). In both cases the parent
asserts that `CMAKE_CXX_COMPILER_LAUNCHER`, `CMAKE_CXX_STANDARD` and `FASTCACHED_FETCH_SILENCE_SECONDS`
are undefined, that `ENV{GIT_HTTP_LOW_SPEED_TIME|LIMIT}` is not exported, and that the directory
`COMPILE_OPTIONS` are unchanged. The calling environment had `GIT_HTTP_LOW_SPEED_TIME` unset.

```
===== defaults (CORE_CPP_TESTING OFF)
-- [core-cpp] module testing: on
-- parent: core::testing_main does not exist
-- parent: no Catch2 target
no Catch2 source and no CPM bootstrap in the build tree
[3/3] Linking CXX static library core-cpp/src/core/testing/libcore-cpp-testing.a
===== CORE_CPP_CATCH2_MAIN=ON
-- CPM: Adding package Catch2@3.8.0 (v3.8.0)
-- [core-cpp] Catch2: fetched by CPMAddPackage(NAME Catch2 VERSION 3.8.0 ...)
-- parent: core::testing_main exists
-- parent: core::testing_main's CORE_CPP_SKIP_EXIT_CODE is 77
-- parent: Catch2 was resolved
[114/114] Linking CXX executable consumer-test      (a consumer TEST_CASE linked to core::testing_main)
1/1 Test #1: consumer-test ....................   Passed
```

With defaults, `core::testing` builds without Catch2. With the option ON, the CPM bootstrap downloads
without a bound, because the parent defines none.

Two more checks:

- **Forcing**: `-DCORE_CPP_TESTING=ON -DCORE_CPP_CATCH2_MAIN=OFF` prints
  `[core-cpp] CORE_CPP_CATCH2_MAIN is ON because CORE_CPP_TESTING is ON`, builds `core-cpp-testing_main`,
  and registers 4 tests.
- **Top level, fresh configure with `--trace-source=cmake/CPM.cmake`**: the bootstrap call is
  `file(DOWNLOAD … EXPECTED_HASH SHA256=78ba32ab… INACTIVITY_TIMEOUT;120 STATUS …)`, and the cache has
  `FASTCACHED_FETCH_SILENCE_SECONDS:STRING=120`. The top-level bound still applies.

### New finding: fastcache-cc loses header dependencies on clang-cl cache hits (upstream, not fixed here)

The first incremental `clangcl-debug` build after this change failed to link:

```
lld-link: error: duplicate symbol: void __cdecl core::testing::suppressWindowsDialogs(void)
>>> defined at …\SuppressWindowsDialogs.cpp:16   core-cpp-testing.lib(SuppressWindowsDialogs.cpp.obj)
>>> defined at core-cpp-testing_main.lib(CatchMain.cpp.obj)
```

`CatchMain.cpp.obj` was stale: it still held the old inline definition, because ninja never recompiled it
after `SuppressWindowsDialogs.hpp` changed. `ninja -t deps` on that object showed `#deps 0`; that object
had been served from fastcache-cc during the earlier clean-rebuild HIT check. The `cl-debug` tree had
never been rebuilt from hits, and there the same object had 43 deps. Controlled comparison after clean,
hit-served rebuilds of both trees:

| Driver | Object | How it was produced | `ninja -t deps` |
|---|---|---|---|
| cl | CatchMain.cpp.obj | HIT | 43 (same as a real compile) |
| cl | SuppressWindowsDialogs.cpp.obj | HIT | 1 |
| clang-cl | CatchMain.cpp.obj | MISS (real compile) | 336 |
| clang-cl | SuppressWindowsDialogs.cpp.obj | HIT | **0** |
| clang (WSL, GCC-style depfile) | CatchMain.cpp.o | after hits | 358 |

So fastcache-cc does not replay the `/showIncludes` stream on a **clang-cl** cache hit. ninja then
records no header dependencies for that object, and a later header edit silently leaves it stale: a wrong
incremental build, caught here only because it became a link error. cl and the GCC-style depfile path are
unaffected. A clean rebuild always produces correct objects, because the cache key covers the
preprocessed input; only incrementality breaks.

This is in fastcached (`fastcache-cc`), which I must not modify, so I have not changed anything there. I
suggest a fastcached issue: "clang-cl cache hits do not replay /showIncludes, so Ninja loses header deps".
Until it is fixed, `clangcl-*` build trees that were populated from the cache can go stale after header
edits. The CI jobs are unaffected, because each builds from a clean tree.

## Fix round 1 (review task-A1-review.md, Important 1 and 2), commits 9ddb86e and 0d80b3e

| SHA | Subject |
|---|---|
| 9ddb86e | build: compile a Catch2 that core-cpp fetches itself as C++23 |
| 0d80b3e | testing: a non-zero Catch2 status is a failure even when no test failed |

Both commits carry the sign-off trailer.

### 1. MSVC link failure on the subproject path (9ddb86e)

**Change.** The Catch2 row in `cmake/CoreCppDependencies.cmake` now has a `WRAP core_cpp_catch2_standard`.
- For `Catch2::Catch2` and `Catch2::Catch2WithMain`, it skips a target that does not exist or is IMPORTED,
  resolves `ALIASED_TARGET`, and sets `CXX_STANDARD 23`, `CXX_STANDARD_REQUIRED ON` and
  `CXX_EXTENSIONS OFF` on the real target.
- A Catch2 the parent provides returns at resolution step 1 and never reaches a WRAP.
- The standard is set as target properties, not CPM `OPTIONS`, so the parent's cache is untouched.
- The comment in `CoreCppTopLevel.cmake` now points at the WRAP for the subproject case.

**Covering test.** A throwaway parent in the scratchpad (`msvc-consumer/`):
`add_subdirectory(D:/core-cpp)` with `CORE_CPP_CATCH2_MAIN ON`, `cl`, Ninja, Debug. It has one
`CHECK(std::string_view{"a"} == std::string_view{"a"})` linked to `core::testing_main`, and it asserts
that `CMAKE_CXX_STANDARD` does not leak into the parent.

RED, before the WRAP:
```
-- consumer: Catch2 CXX_STANDARD=standard-NOTFOUND
-- consumer: Catch2WithMain CXX_STANDARD=standard-NOTFOUND
consumer_test.cpp.obj : error LNK2019: unresolved external symbol "public: static class std::basic_string<…>
  __cdecl Catch::StringMaker<class std::basic_string_view<…>,void>::convert(…)"
consumer-test.exe : fatal error LNK1120: 1 unresolved externals
```

GREEN, in a fresh build dir:
```
-- consumer: Catch2 CXX_STANDARD=23
-- consumer: Catch2WithMain CXX_STANDARD=23
[114/114] Linking CXX executable consumer-test.exe
(catch_tostring.cpp.obj is compiled with /std:c++latest, CMake's MSVC spelling of C++23)
1/1 Test #1: consumer-test ....................   Passed    0.03 sec
100% tests passed, 0 tests failed out of 1
```

**IMPORTED path** (WSL). Catch2 3.8.0 was installed into /tmp and found through `CMAKE_PREFIX_PATH`
(`[core-cpp] Catch2: found by find_package(Catch2 3.8), version 3.8.0`). `--trace-source` on
`CoreCppDependencies.cmake` shows the WRAP taking `continue()` for both imported targets; its
`set_target_properties` (line 177) never runs:
```
CoreCppDependencies.cmake(169):  get_target_property(imported Catch2::Catch2 IMPORTED )
CoreCppDependencies.cmake(171):  continue()
CoreCppDependencies.cmake(169):  get_target_property(imported Catch2::Catch2WithMain IMPORTED )
CoreCppDependencies.cmake(171):  continue()
```

### 2. Ruling R14: a non-zero Catch2 status is a failure (0d80b3e)

**Change.**
- `ExitCode.cpp` ends with `return rawExitCode == 0 ? 0 : 1;`.
- The contract table in `ExitCode.hpp` gains the row "nothing failed, but Catch2 still reported an
  error → 1", with the `-w UnmatchedTestSpec` example.
- `check-exit-codes.cmake` rows are now `"<status>;<fixture argument>..."`, so a row can carry options
  (`list(POP_FRONT)`, arguments expanded unquoted). A new row, `"1;-w;UnmatchedTestSpec;[pass],[nothing-has-this-tag]"`,
  covers the case.
- `ExitCode_test.cpp` gains "nothing failed but Catch2 reported an error is 1", with a raw code of 3,
  alone and together with skips.

RED (`clangcl-debug`, `--clean-first`, tests written before the fix):
```
1/2 Test #1: core-cpp.testing .................***Failed
  CHECK( normalisedExitCode(totalsOf(1, 0, 0), 3) == 1 )   with expansion:  0 == 1
  CHECK( normalisedExitCode(totalsOf(1, 0, 2), 3) == 1 )   with expansion:  0 == 1
2/2 Test #2: core-cpp.exit-codes ..............***Failed
  exit code for -w UnmatchedTestSpec [pass],[nothing-has-this-tag]: got 0, want 1
```

GREEN:
```
core-cpp-testing-test.exe: All tests passed (12 assertions in 9 test cases)      (clangcl-debug and cl-debug)
core-cpp-exit-code-fixture.exe -w UnmatchedTestSpec "[pass],[no-such-tag]"
  No test cases matched '[no-such-tag]'
  -> exit 1                                                                       (clangcl-debug and cl-debug)
```

### Re-runs

```
clangcl-debug (cmake --build --preset clangcl-debug --clean-first, for fastcached#1531): 7/7 passed
cl-debug:    7/7 passed
clang-debug: 4/4 passed (WSL)
gcc-debug:   4/4 passed (WSL)
python scripts/clang-format.py --check: 9 file(s) are formatted with clang-format 22.1.8
check-cmake-hygiene: 26 file(s) under D:/core-cpp are clean
hygiene-selftest: the clean tree passed and all 16 violations were refused by name
```

No other review findings were acted on; the Minor ones are deferred to the final review, as instructed.
