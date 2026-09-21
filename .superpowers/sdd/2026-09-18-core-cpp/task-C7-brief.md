# Brief for Task C7

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).


The common steps for every consumer are:
1. Create the worktree with superpowers:using-git-worktrees, using the Global Constraints location. Never pull into or build in the main checkout:
   ```powershell
   git -C D:\<repo> fetch origin
   git -C D:\<repo> worktree add D:\<repo>-worktrees\core-cpp -b <branch> origin/master
   ```
2. Pin with `GIT_TAG v0.1.0` + `VERSION 0.1.0`; iterate locally with `-DCPM_core-cpp_SOURCE=D:/core-cpp`.
3. Build and test with the repo's own presets on Windows and WSL.
4. Run superpowers:requesting-code-review, then open the PR with **contour-workflows:draft-pr**.
5. Drive CI to green with **contour-workflows:fix-ci**.


### Task C7: morph PR-1: CPM, plus timers, base64 and wakeup from core-cpp (`LASTRADA-Software/morph`, branch `build/core-cpp`)
Worktree: `D:\morph-worktrees\core-cpp`. Per morph's `AGENTS.md`, file a tracking issue first and reference it in the PR. Read the worktree's `AGENTS.md`, `CMakePresets.json` and `docs/spec/` index before editing; `docs/spec/` is authoritative.

**Files:**
- **CPM (switch from FetchContent):**
  - Create `cmake/CPM.cmake` (0.40.8 + SHA256, identical to core-cpp's).
  - `CMakeLists.txt:143-153` glaze v7.4.0, and `:557-563` Catch2 v3.8.1 → `CPMAddPackage`. Set `CPM_USE_LOCAL_PACKAGES ON` so Windows still takes vcpkg's glaze and Catch2 via `find_package` first.
  - `docs/CMakeLists.txt:7-12` doxygen-awesome-css v2.3.4 → `CPMAddPackage`.
  - `examples/bank/CMakeLists.txt:43-63` and `examples/common/CMakeLists.txt:135-155`: Lightweight `bbb972a78e1962b968a2c6ad93f7dade736eaa01` → `CPMAddPackage`. Keep calling `morph_demote_interface_includes` immediately after, and keep `cmake/morph_add_rung.cmake`'s EMSCRIPTEN handling. `tests/compile_checks/demote_interface_includes_selftest.cmake` must still pass.
- **Dependency cache:** replace `cmake/DepCache.cmake` (FetchContent source cache, morph#552) with CPM's `CPM_SOURCE_CACHE` (default `${sourceDir}/.cache/cpm`). Rewrite the `scripts/test_dep_cache.sh` self-test to assert:
  - cold, it populates `CPM_SOURCE_CACHE`;
  - warm, re-configuring performs no clone;
  - a cache miss still fetches.

  `drift-guard.yml:78` keeps running it. `ci.yml`, `wasm-ladder.yml` and `wasm-demo.yml` cache `.cache/cpm`, keyed on `hashFiles('cmake/CPM.cmake','**/CMakeLists.txt')`.
- **core-cpp:**
  ```cmake
  CPMAddPackage(NAME core-cpp GITHUB_REPOSITORY contour-terminal/core-cpp GIT_TAG v0.1.0 VERSION 0.1.0
                SYSTEM YES EXCLUDE_FROM_ALL YES
                OPTIONS "CORE_CPP_TESTING OFF" "CORE_CPP_BUILD_EXAMPLES OFF" "CORE_CPP_WITH_TUI OFF"
                        "CORE_CPP_WITH_TLS OFF" "CORE_CPP_FETCH_DEPS OFF")
  target_link_libraries(morph INTERFACE core::base core::coro core::net)
  if(NOT EMSCRIPTEN)
      target_link_libraries(morph INTERFACE core::platform)
  endif()
  ```
  morph stays an INTERFACE target, but its consumers now also build core-cpp's static libraries. Document that in the README and `docs/ARCHITECTURE.md`.
- **`include/morph/core/timeout_scheduler.hpp`**: one class replaces both builds.
  - The public API is unchanged: `Handle schedule(std::chrono::milliseconds, std::function<void()>)`, `void cancel(Handle)`, non-copyable and non-movable, and the destructor drops pending callbacks without firing them.
  - morph's own `pending` map (handle → callback) stays under a mutex, so `cancel()` releases the callback and its captures immediately in both builds, as documented today.
  - Native: owns a `std::thread` running a `core::net::PlatformLoop`. `schedule` → `loop.post(...addTimer...)`. `cancel` erases, then posts `cancelTimer`. The destructor does `loop.stop()` and join.
  - WebAssembly: a HostDriven `PlatformLoop` with no thread. `schedule` → `addTimer` directly. `cancel` now also clears the browser timer, which today is left to fire into nothing.
  - Callback exceptions are logged via `morph::log` and swallowed, as today.
- **base64:** delete `include/morph/net/detail/base64.hpp`. `ws_handshake.hpp` uses `core::base64::encode`, and `tests/net/test_base64.cpp` is retargeted at `core::base64` (keeping the RFC 4648 vectors).
- **WakeupPipe** (nested in `include/morph/net/socket_server.hpp:~470`) → `core::platform::Wakeup`.
- **Spec and docs:** update `docs/spec/core/backend.md` (WakeupPipe, base64), every `docs/spec` passage on TimeoutScheduler (grep), `docs/spec/security.md`, `docs/ARCHITECTURE.md` and the README dependency list. `scripts/check_spec_citations.sh` and spec-sync must pass.

- [ ] **Tests first:**
  - In `tests/test_timeout_scheduler.cpp` add: "cancel releases captures immediately" (a `weak_ptr` observes expiry right after `cancel()`), and "destructor drops pending without firing".
  - `tests/test_timeout_scheduler.cpp`, `tests/test_client_execute_deadline.cpp`, `tests/net/test_base64.cpp`, `tests/net/test_socket_server.cpp` and `tests/net/test_ws_handshake.cpp` must pass before and after.
- [ ] Implement in three commits, each green:
  - `build: fetch dependencies with CPM instead of FetchContent`
  - `core: TimeoutScheduler is one implementation over core-cpp's event-loop timers`
  - `net: base64 and the wakeup pipe come from core-cpp`
- [ ] **Verify:**
  - Windows MSVC (vcpkg preset) and WSL GCC 15 and Clang presets; `ctest`.
  - `bash scripts/test_dep_cache.sh` and `bash scripts/check_spec_citations.sh`.
  - CI: `ci`, `drift-guard`, `spec-sync`, `wasm-ladder` and `wasm-demo` green. The WebAssembly gates now compile core-cpp's WebAssembly subset with emsdk 3.1.56.
- [ ] Open the draft PR titled "build: dependencies through CPM, and timers, base64 and the wakeup pipe from core-cpp".

