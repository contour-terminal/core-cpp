# Brief for Task A8

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).

### Task A8: Vendoring tool + consumer-smoke CI

**Files:** Create `cmake/CoreCppVendor.cmake`, `tests/consumer-cpm/{CMakeLists.txt,main.cpp}`, `tests/consumer-vendored/{CMakeLists.txt,main.cpp}`, `tests/cmake/check-vendor-selftest.cmake`. Modify `.github/workflows/build.yml` (the `consumer-smoke` job).

- [ ] **Step 0: `CORE_CPP_TARGETS`, test first.** The consumer-cpm smoke asserts that `CORE_CPP_TARGETS` is non-empty and that every entry is a real, non-test, compiled target. Implement it in `core_cpp_add_module` (INTERFACE targets are excluded).
- [ ] **Step 1: Self-test first.** `check-vendor-selftest.cmake` makes a temp git repo with 3 files, then:
  - sync, check, expect OK;
  - flip a byte, check, expect refusal naming the file;
  - add an unlisted file, expect refusal;
  - a blob containing `\r`: sync, expect refusal;
  - a symlink: sync, expect refusal.

  Run: expected FAIL (no script).
- [ ] **Step 2:** Implement `MODE=sync|check` per Part I §5. Enumerate with `git ls-tree -r -z`, read with `git -c core.autocrlf=false -c core.eol=lf cat-file blob`, hash with `file(SHA256)`. The `MODULES` list selects `src/core/<m>/**`. Expected: PASS.
- [ ] **Step 3: consumer-smoke.**
  - (a) **CPM consumer.** It sets `-Wall -Wextra -Werror` (MSVC `/W4 /WX`) at directory scope, then `CPMAddPackage(NAME core-cpp SOURCE_DIR …)`. It asserts that its directory `COMPILE_OPTIONS`/`LINK_OPTIONS`/`INCLUDE_DIRECTORIES`, `CMAKE_CXX_FLAGS*` and `CMAKE_CXX_COMPILER_LAUNCHER` are unchanged (core-cpp installs no launcher when not top-level) and that no `core-cpp-*-test` target exists. It then builds and runs `main.cpp` (Task + loopback echo + LogStore + `MockTerminalOutput`).
  - (b) **Vendored consumer.** Export with `MODULES=base;log;cli;platform;async;net;testing`, then `add_subdirectory(... SYSTEM EXCLUDE_FROM_ALL)` with `CORE_CPP_FETCH_DEPS OFF`, `CORE_CPP_WITH_TUI OFF`, `CORE_CPP_WITH_TLS ON`. It runs inside `docker run --network none` using an image with the toolchain.
  - (c) **WebAssembly consumer, mirroring morph.**
    - `tests/consumer-wasm` is a CPM consumer built with `emcmake` (emsdk 3.1.56).
    - It links only `core::base core::async core::net`, with `CORE_CPP_WITH_TUI OFF` and `CORE_CPP_FETCH_DEPS OFF`.
    - It is an `INTERFACE` library, like morph's, over an executable that awaits a coroutine `delay` on a HostDriven `PlatformLoop`, run under node with `-sASYNCIFY`.
    - Until B5 lands it only builds base and async; the task that adds B5 extends it.
- [ ] **Step 4:** Push. Expected: both smoke legs green.
- [ ] **Step 5:** Commit `build: verbatim vendoring tool and consumer smoke tests for CPM and vendored use`.

