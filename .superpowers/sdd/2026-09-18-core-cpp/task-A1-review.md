# Task A1 review (spec + quality), 4abb140..26de633

### Spec Compliance
- ✅ **Spec compliant.** Every file in the brief is present. The five interfaces match the brief: `core_cpp_add_module`, `core_cpp_add_test`, `core_cpp_module`, `core_cpp_dependency`, and `core::testing_main` with the `CORE_CPP_SKIP_EXIT_CODE` property. They also match spec §3: `3.25...3.31`, C++23 with `CXX_EXTENSIONS OFF`, the resolver order, `CORE_CPP_`/`core_cpp_` prefixes, modules included by absolute path, PRIVATE-only flags, and 77/1/2/0 exit codes. Rulings R7–R11 are implemented as written.
  - **Verbatim copies:** `cmake/portable/CompileCache.cmake` and `cmake/FetchTransferBound.cmake` are byte-identical to fastcached `eb9c9c68`. I checked with both `diff` and `cmp`.
  - **Extras from concern 7, all accepted:**
    - `KIND OBJECT` is needed so the linker keeps the startup translation unit.
    - `LIBS`, `ExitCode_test.cpp`, the 5th exit-code row and the clang-tidy version warning are small and useful.
    - Each of the 16 hygiene rules maps to a stated spec or Global Constraint.
    - `DIR` is a little speculative (it is for A3's `base`, set to `.`), but it costs about 3 lines.
  - **Deviation from Step 6b, harmless:** with `-DUSE_COMPILER_CACHE=OFF`, the verbatim module leaves `CMAKE_CXX_COMPILER_LAUNCHER` defined but empty, not unset. The result is the same: no launcher.
- ⚠️ **Cannot verify from the diff:**
  - **Reported runs I did not re-run:** the 4-configuration results, the fastcache-cc HIT counts, `cmake --list-presets` on both platforms, and `clang-format.py` refusing 22.1.3.
  - **`Signed-off-by` trailers:** the review package lists commit subjects only, and I was told not to re-run git.
  - **CPM SHA-256 against endo's file:** not compared. The reported CPM fetch succeeded with `EXPECTED_HASH`, so the hash is correct for CPM 0.40.8.
  - **LICENSE text against contour's:** not compared.
  - **fastcached has moved on:** `origin/master` is now `b5ded89c`, past the pinned `eb9c9c68`. I did not check whether `CompileCache.cmake` changed; A2's drift check will catch it. The pin as recorded is consistent.
  - **Upstream bug for the controller:** the report found that fastcache-cc drops clang-cl `/showIncludes` deps on cache hits. That is a fastcached defect. Filing an issue there is outward-facing, so it needs your decision.

### Strengths
- **Subproject safety holds together.**
  - `CoreCppTopLevel.cmake` is the only file that touches global state, and it is included only under `PROJECT_IS_TOP_LEVEL` (`D:/core-cpp/CMakeLists.txt:485-487`).
  - R8 is enforced mechanically by the `top-level-only-include` and `process-environment` rules. Each rule has its own self-test case.
- **The dependency resolver is sound.**
  - It tries parent target → `find_package(QUIET)` → CPM.
  - A NO_FETCH row gets a different FATAL message from an `CORE_CPP_FETCH_DEPS=OFF` stop, and both name the condition.
  - It loads CPM lazily, and only when the parent has not already loaded it (`D:/core-cpp/cmake/CoreCppDependencies.cmake:103-127`).
- **The hygiene self-test proves every refusal.**
  - A rule without a case fails, and so does a case without a rule (the `LIST_RULES` handshake, selftest `:74-99`).
  - Each refusal must name both the rule and the file (`:122-128`).
  - A stale allowlist row is itself refused.
- **The exit-code contract is tested from outside the binary**, including "2 = nothing ran", with a unit test for each branch as well.
- **The startup-object link item works.** `$<TARGET_OBJECTS>` is used as an interface link item (`D:/core-cpp/src/core/testing/CMakeLists.txt:26-27`). My MSVC probe's link line showed `SuppressWindowsDialogsAtStartup.cpp.obj` placed ahead of the libraries.
- **The toolchain is data tables applied per target.** It uses PRIVATE flags, a probe per flag, and clears `CXX_CLANG_TIDY`.
- **`SuppressWindowsDialogs` is defined out of line**, so `<Windows.h>` stays out of every file that includes the header.
- **The report is candid.** It records the R9–R11 deviations and explains why the access-violation canary mode was dropped.

### Issues

#### Critical (Must Fix)
None.

#### Important (Should Fix)

1. **MSVC link failure in the R7 consumer path.**
   - **Where:** `D:/core-cpp/cmake/CoreCppDependencies.cmake:155-159`, with `D:/core-cpp/cmake/CoreCppTopLevel.cmake:30-35`.
   - **Cause:** Only the top-level build sets `CMAKE_CXX_STANDARD 23`. When core-cpp is a subproject with `CORE_CPP_CATCH2_MAIN=ON` and fetches Catch2 itself, Catch2 builds at its own `cxx_std_14`, which is MSVC's default standard. Catch2 then leaves out C++17-only definitions (`catch_tostring.cpp:131`). Meanwhile `core::testing_main` propagates `cxx_std_23` to the consumer's test code, so the two disagree.
   - **Probe that reproduces it:**
     - Setup: a throwaway parent in the scratchpad that runs `add_subdirectory(D:/core-cpp)` with `CORE_CPP_CATCH2_MAIN=ON` and `cl`, and has one `CHECK(std::string_view{"a"} == std::string_view{"a"})`.
     - Result: `LNK2019: unresolved external symbol Catch::StringMaker<std::string_view>::convert`.
     - Scope: GCC and Clang default to gnu++17, which hides it. That is why the reported WSL subproject run passed.
   - **The code already names this failure mode.** The comment in `CoreCppTopLevel.cmake` describes it, but only the top-level path guards against it.
   - **Suggested fix:**
     - Give the Catch2 row a `WRAP` that sets `CXX_STANDARD 23` on the real `Catch2` and `Catch2WithMain` targets. It must skip IMPORTED targets, because `WRAP` also runs after `find_package`.
     - Do not pass the standard through CPM `OPTIONS`, which would write a global cache entry.
     - Add a regression for it, or make sure A8's consumer-smoke covers MSVC.

2. **`normalisedExitCode` swallows a failing Catch2 status** (plan-mandated: the brief dictates the function body).
   - **Where:** `D:/core-cpp/src/core/testing/ExitCode.cpp:13-15`.
   - **Problem:** When test cases ran, none failed and not all were skipped, it returns 0 whatever `rawExitCode` was. Catch2 3.8 returns `UnmatchedTestSpecExitCode` 3 in exactly that situation (`catch_session.cpp:339-342`).
   - **Reproduced** with the existing binary:
     - `out/build/cl-debug/tests/core-cpp-exit-code-fixture.exe -w UnmatchedTestSpec "[pass],[no-such-tag]"` prints `No test cases matched '[no-such-tag]'` and exits **0**.
     - ctest never passes `-w`, so this only affects manual and filtered CI runs.
   - **Fix:** end with `return rawExitCode == 0 ? 0 : 1;`. Add a fixture row for this case and a unit test.

#### Minor (Nice to Have)

1. **The line-based scanner can be bypassed for its most important rule.**
   - `public-flags` (`D:/core-cpp/tests/cmake/check-cmake-hygiene.cmake:75-77`) misses:
     - a visibility keyword on the next line: `target_compile_options(x` newline `PUBLIC -Wfoo)`;
     - a visibility keyword held in a variable, as in `target_compile_options(${t} ${usage} …)`. `D:/core-cpp/cmake/CoreCppTargets.cmake:138-147` already uses that idiom for sources, includes and libraries;
     - `set_property(TARGET … INTERFACE_COMPILE_OPTIONS …)`.
   - `unprefixed-cache-variable` (`:51-54`, with `CACHE` on a continuation line) and `c-style-for` (`:91-93`) have the same blind spot.
   - The allowlist is per file, not per line (the implementer's concern 8).
   - **Suggestion:** add a configure-time check that every core-cpp target's `INTERFACE_COMPILE_OPTIONS`, `INTERFACE_COMPILE_DEFINITIONS` and `INTERFACE_LINK_OPTIONS` are empty. This fits naturally with A8's `CORE_CPP_TARGETS`.
2. **`-Wno-c2y-extensions` is too broad.** It is set at `D:/core-cpp/cmake/CoreCppToolchain.cmake:101` and applies to library targets too, although its only documented cause is Catch2 macros in test code. Scoping it to test executables would keep the warning on for library code.
3. **A typo in `WHEN` fails silently.**
   - `D:/core-cpp/cmake/CoreCppModules.cmake:61`: an undefined `WHEN` variable turns the module off without a word.
   - `D:/core-cpp/cmake/CoreCppDependencies.cmake:133`: an undefined condition means the dependency is never resolved.
   - For module rows, a FATAL on `NOT DEFINED ${when}` would catch it.
4. **The generated `core/Config.hpp` is in no FILE_SET.**
   - It is generated at `D:/core-cpp/CMakeLists.txt:494-495` and reaches targets only as a `BUILD_INTERFACE` include (`CoreCppTargets.cmake:141-143`).
   - `ExitCode.hpp`, which is a FILE_SET header, includes it. That contradicts spec §3's "install-ready through FILE_SET".
   - It belongs to whichever target ends up owning `Config.hpp`, probably `base` in A3.
5. **The same four-line row split is repeated.**
   - It appears at `D:/core-cpp/cmake/CoreCppToolchain.cmake:131-134` and `:153-156`, at `check-cmake-hygiene.cmake:165-169`, and in the self-test at `:52-55` and `:111-114`.
   - `CoreCppTargets.cmake:60` and `CoreCppToolchain.cmake:179` split the same format a different way (`string(REPLACE "|" ";")`).
   - One helper, or one convention, would do.
6. **The canary hard-codes the skip code.** `D:/core-cpp/tests/WindowsDialogCanary.cpp:27` writes `77` instead of using `CORE_CPP_SKIP_EXIT_CODE`, which is reachable through `core::testing`'s include directories. `D:/core-cpp/tests/CMakeLists.txt:40` registers the variable, so the two could drift apart.
7. **Exit-code failures give no context.** `D:/core-cpp/tests/cmake/check-exit-codes.cmake:22` discards the fixture's output, as the brief wrote it. A mismatch then says only "got X, want Y". Capturing the output and printing it on mismatch costs nothing.
8. **The framework's refusal paths have no automated test.** They were only checked by hand, per the report:
   - layering: `CoreCppTargets.cmake:72-91`;
   - a disabled dependency in `DEPS`: `CoreCppModules.cmake:69-75`;
   - `CORE_CPP_FETCH_DEPS=OFF`: `CoreCppDependencies.cmake:109-113`;
   - sanitizers requested by a subproject: `CoreCppOptions.cmake:42-46`.
9. **POSIX sources will reach the WebAssembly build.** `"SOURCES_POSIX|UNIX"` (`D:/core-cpp/cmake/CoreCppTargets.cmake:44`) is true under Emscripten, so POSIX sources will compile into the WebAssembly subset. Whoever adds `wasm-subset` and `SOURCES_EMSCRIPTEN` (spec §1) needs to account for that.

### Assessment
**Task quality:** Needs fixes

**Reasoning:** The framework is well designed, matches the brief, the spec and R7–R11, and its self-tests prove the refusals they claim to. But two defects are confirmed by running them. The R7 consumer path fails to link under MSVC because the fetched Catch2 builds at C++14. The plan-mandated exit-code normaliser turns Catch2's unmatched-test-spec failure into a pass. Both fixes are small.

### Review side effects
- The MSVC probe was built in the session scratchpad and deleted afterwards.
- I only ran the existing `out/build/cl-debug` fixture binary; nothing was rebuilt there.
- `git status` in D:/core-cpp is clean. The one exception is this file, which the controller asked for.
