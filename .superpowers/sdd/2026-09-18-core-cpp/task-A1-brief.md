# Brief for Task A1

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).

### Task A1: CMake framework, exit-code-normalising test main, hygiene checks

**Files:**
- Create: `CMakeLists.txt`, `CMakePresets.json`
- Create in `cmake/`: `CoreCppOptions.cmake`, `CoreCppModules.cmake`, `CoreCppTargets.cmake`, `CoreCppDependencies.cmake`, `CoreCppToolchain.cmake`, `CoreCppTopLevel.cmake`
- Copy into `cmake/`:
  - `CPM.cmake` (0.40.8 + SHA256, from `D:\endo\cmake\EndoThirdParties.cmake:8-25`)
  - `FetchTransferBound.cmake` (verbatim from fastcached `origin/master:cmake/FetchTransferBound.cmake`)
  - `portable/CompileCache.cmake` (**verbatim** from fastcached `origin/master:cmake/portable/CompileCache.cmake`, read as a blob)
  - `portable/README.md` (new: provenance SHA, the "verbatim; re-sync, never edit" rule, the launcher order, the opt-out)
- Create: `src/core/Config.hpp.in`
- Create in `src/core/testing/`: `CMakeLists.txt`, `CatchMain.cpp`, `ExitCode.hpp`, `ExitCode.cpp`, `SuppressWindowsDialogs.hpp` (merge of `D:\contour\src\crispy\SuppressWindowsDialogs.hpp`, `D:\contour\src\coro\testing\SuppressWindowsDialogs.hpp`, `D:\endo\src\testing\SuppressWindowsDialogs.hpp`, `D:\fastcached\src\tests\WindowsErrorPopups.hpp`), `SuppressWindowsDialogsAtStartup.cpp` (endo)
- Create in `tests/`: `CMakeLists.txt`, `ExitCodeFixture.cpp`, `WindowsDialogCanary.cpp` (endo), `cmake/check-exit-codes.cmake`, `cmake/check-cmake-hygiene.cmake`, `cmake/check-cmake-hygiene-selftest.cmake`
- Copy: `.clang-format` (from `D:\contour\.clang-format`, IncludeCategories replaced per Part I §3), `.clang-tidy` (from `D:\contour\.clang-tidy`, `HeaderFilterRegex '.*/src/core/.*'`, naming IgnoredRegexps from `D:\fastcached\.clang-tidy`)
- Create: `.clang-format-version`, `.clang-tidy-version`, `.clang-format-ignore` (empty), `.gitattributes`, `.gitignore`, `.editorconfig`, `LICENSE`, `NOTICE`, `scripts/tool-versions.py`, `scripts/clang-format.py`

**Interfaces produced:**
- `core_cpp_add_module(<name> KIND STATIC|INTERFACE HEADERS … SOURCES … SOURCES_POSIX … SOURCES_LINUX … SOURCES_BSD … SOURCES_WINDOWS … PUBLIC_LIBS … PRIVATE_LIBS …)`: creates `core-cpp-<name>` + `core::<name>`.
- `core_cpp_add_test(<name> SOURCES … SOURCES_POSIX … SOURCES_WINDOWS … LABELS …)`
- `core_cpp_module(NAME … KIND … DEPS … PLATFORMS any|native WHEN <option>)`: the module table.
- `core_cpp_dependency(<name> WHEN … TARGETS … FIND_PACKAGE … CPM … NO_FETCH WRAP <fn>)`
- `core::testing_main`: target property `CORE_CPP_SKIP_EXIT_CODE` = 77.
- `core::testing::normalisedExitCode(Catch::Totals const&, int rawExitCode) -> int`

- [ ] **Step 1: Write the failing exit-code test.** `tests/ExitCodeFixture.cpp` defines four cases selected by tag: `[pass]` passes; `[fail4]` has 4 failing `CHECK`s; `[skipall]` does `SKIP("x")`; `[mixed]` has a `SECTION` that skips and one that fails. `tests/cmake/check-exit-codes.cmake` runs the fixture with each tag and asserts the codes:
  ```cmake
  foreach(_row "[pass];0" "[fail4];1" "[skipall];77" "[mixed];1")
      list(GET _row 0 _tag)  list(GET _row 1 _want)
      execute_process(COMMAND "${FIXTURE}" "${_tag}" RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
      if(NOT _rc EQUAL _want)
          message(FATAL_ERROR "exit code for ${_tag}: got ${_rc}, want ${_want}")
      endif()
  endforeach()
  ```
  Register it as `add_test(NAME core-cpp.exit-codes COMMAND ${CMAKE_COMMAND} -DFIXTURE=$<TARGET_FILE:core-cpp-exit-code-fixture> -P …)` with label `hygiene`.
- [ ] **Step 2: Run it and confirm it fails.** Configure with `cmake --preset clangcl-debug`, build, then `ctest --preset clangcl-debug -R exit-codes`. Expected: FAIL, "exit code for [fail4]: got 4, want 1", while the fixture still uses `Catch2WithMain`.
- [ ] **Step 3: Implement `ExitCode.cpp` and `CatchMain.cpp`.**
  ```cpp
  // ExitCode.cpp
  namespace core::testing
  {
  int normalisedExitCode(Catch::Totals const& totals, int rawExitCode) noexcept
  {
      if (totals.assertions.failed > 0 || totals.testCases.failed > 0)
          return 1;
      if (totals.testCases.total() > 0 && totals.testCases.skipped == totals.testCases.total())
          return SkipExitCode; // 77, from ExitCode.hpp: inline constexpr int SkipExitCode = CORE_CPP_SKIP_EXIT_CODE;
      if (totals.testCases.total() == 0 && rawExitCode != 0)
          return 2;
      return 0;
  }
  } // namespace core::testing

  // CatchMain.cpp
  namespace
  {
  Catch::Totals lastTotals {};
  class TotalsListener final: public Catch::EventListenerBase
  {
    public:
      using Catch::EventListenerBase::EventListenerBase;
      void testRunEnded(Catch::TestRunStats const& stats) override { lastTotals = stats.totals; }
  };
  } // namespace
  CATCH_REGISTER_LISTENER(TotalsListener)

  int main(int argc, char* argv[])
  {
      core::testing::suppressWindowsDialogs();
      auto session = Catch::Session {};
      if (auto const rc = session.applyCommandLine(argc, argv); rc != 0)
          return rc;
      return core::testing::normalisedExitCode(lastTotals, session.run());
  }
  ```
  Link the fixture with `core::testing_main`.
- [ ] **Step 4: Run it and confirm it passes.** Run `ctest --preset clangcl-debug -R exit-codes` and the same through WSL `clang-debug`. Expected: PASS.
- [ ] **Step 5: Hygiene check, test first.** `check-cmake-hygiene-selftest.cmake` writes a temporary tree with one violating file per rule (`option(FOO`, `add_compile_options(`, `set(CMAKE_CXX_FLAGS`, `add_library(x a.cpp)` without a type, `// NOLINT`). It asserts that `check-cmake-hygiene.cmake` refuses each one by name, and passes on a clean tree. Run it and confirm it fails, implement the scanner as a data table of `{regex, allowedFile, reason}`, then run it and confirm it passes. Register both with label `hygiene`.
- [ ] **Step 6: Presets.** Add the hidden `base`/`unix`/`windows` presets and `clang-debug`, `clang-release`, `gcc-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan`, `clang-tidy`, `clang-coverage`, `clang-tracy`, `appleclang-{debug,release}`, `cl-{debug,release}`, `clangcl-{debug,release}`, `cl-release-tls`. Build and test presets mirror them with `outputOnFailure`, `noTestsAction: error`, `timeout 300`, and `ci-<name>` workflow presets. Verify with `cmake --list-presets` on both platforms.
- [ ] **Step 6b: Compiler cache.** In `CoreCppTopLevel.cmake`, `include("${CMAKE_CURRENT_LIST_DIR}/portable/CompileCache.cmake")` first. The top-level `CMakeLists.txt` includes `CoreCppTopLevel.cmake` before `core_cpp_resolve_dependencies()`.
  - **Verify locally on Windows**, where fastcached answers on `127.0.0.1:6674` and fastcache-cc is at `%LOCALAPPDATA%\fastcache-cc\bin`: `cmake --preset clangcl-debug` prints the module's status line selecting fastcache-cc, `Select-String out/build/clangcl-debug/CMakeCache.txt -Pattern 'CMAKE_CXX_COMPILER_LAUNCHER'` names `fastcache-cc`, and a clean rebuild with `$env:FASTCACHE_VERBOSE=1` reports `HIT`s.
  - **In WSL** (`~/.local/bin/fastcache-cc`): the module reports whether a daemon answers from WSL. If none does, it falls back to no launcher without failing; that is acceptable locally, and CI's `compile-cache` job covers the positive path.
  - `-DUSE_COMPILER_CACHE=OFF` leaves `CMAKE_CXX_COMPILER_LAUNCHER` unset.
- [ ] **Step 7: Windows dialog canary.** Register it `WILL_FAIL` + `TIMEOUT 60` on WIN32. Run `ctest --preset cl-debug -R canary`. Expected: PASS (the canary died instead of hanging).
- [ ] **Step 8: Verify format.** Run `python scripts/clang-format.py --check`. It must refuse a non-22.1.8 binary and pass.
- [ ] **Step 9: Commit.** Commit `build: CMake module framework, exit-code-normalising test main, hygiene checks`.

