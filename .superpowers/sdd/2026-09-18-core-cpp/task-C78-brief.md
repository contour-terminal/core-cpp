# Brief for Task C7+C8: morph moves onto core-cpp v0.2.1 in ONE pull request

The plan text for both tasks follows verbatim. Where it disagrees with the rulings after it, the rulings win.

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
  target_link_libraries(morph INTERFACE core::base core::async core::net)
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

### Task C8: morph PR-2: coroutines on `core::async` (branch `feat/coroutines`, based on C7)
Worktree: `D:\morph-worktrees\core-cpp-async`. File a tracking issue first.

- [ ] **Step 1: Spec first.** Write `docs/spec/core/coroutines.md` (authoritative) and link it from `docs/spec/core/{bridge,completion}.md`. It specifies:
  - **Client side:**
    - `Completion<T>` is awaitable through `operator co_await() &&` (rvalue only; awaiting consumes it).
    - `co_await` yields `T`, or rethrows the stored `exception_ptr`.
    - The coroutine resumes on the completion's executor, the same place `then()` handlers run.
    - If the awaiting promise satisfies `core::async::HasStopToken`, a stop request detaches the continuation (via `CallbackToken`) and resumes with `core::async::OperationCancelled` on that executor.
    - `morph::async::spawn(exec::IExecutor&, core::async::Task<void>)` starts a detached task whose every resumption is posted to that executor. This is the Qt/QML entry point.
  - **Model side:**
    - An action handler may return `core::async::Task<R>`; `model::ActionTraits` detects it.
    - The bridge drives it on the model's strand through `morph::exec::StrandCoroExecutor : core::async::IExecutor`, which posts `h.resume()` onto the strand. Every resumption therefore runs on the model's strand.
    - **Not re-entrant:** the next action for the same model starts only after the current handler's Task completes. ExecuteOrderGate ordering is unchanged.
    - `setExecuteDeadline` requests stop on the handler's stop source, which raises `OperationCancelled` at the next `co_await`. The client's Completion rejects with the existing deadline error.
  - **Timers:** `morph::async::delay(TimeoutScheduler&, std::chrono::milliseconds)` is a stop-aware awaiter that resumes through the awaiting context's executor.
  - **Scope:** the wire protocol and remote backends are unchanged. WebAssembly runs all of it on the single main thread.
- [ ] **Step 2: Tests first.**
  - `tests/test_coroutine_client.cpp`:
    - awaiting yields the value;
    - an error is rethrown;
    - resumption happens on the executor (`MainThreadExecutor::runFor`);
    - a stop request cancels the await and releases captures;
    - awaiting an lvalue `Completion` fails to compile (a `requires` check in `tests/compile_checks/`).
  - `tests/test_coroutine_model.cpp`:
    - a Task handler's result reaches the client;
    - a handler can await another model's `execute`;
    - a second action waits until the first Task completes (ordering log);
    - a deadline cancels a suspended handler;
    - a handler exception reaches `onError`;
    - every resumption runs on the model's strand (strand-id assertion).
  - `tests/test_async_delay.cpp`: `delay` resumes on the executor after the time elapses (ManualClock or a real short delay, bounded), and a stop cancels it.
  - Run them. Expected: FAIL (headers missing).
- [ ] **Step 3: Implement.**
  - New `include/morph/core/coroutine.hpp`: the awaiter, `spawn`, `StrandCoroExecutor` and `delay`.
  - `completion.hpp`: add `operator co_await() &&`.
  - `model.hpp`: `ActionTraits` detects `Task<R>`.
  - `bridge.hpp:~1521`: the synchronous `model.execute(*sharedAction)` call site branches on Task handlers and drives them on the strand.
  - `detail/execute_order_gate.hpp`: holds the gate until the Task completes.
  - Expected: PASS on Windows MSVC and WSL GCC 15 and Clang.
- [ ] **Step 4: Demonstrate.** Convert one bank or ladder model action to a Task handler and one GUI flow to `co_await` + `spawn`. The ladder's WebAssembly build then compiles and links the coroutine path. `wasm-ladder` and `wasm-demo` must be green.
- [ ] **Step 5: Verify.** CI `ci`, `drift-guard`, `spec-sync`, `wasm-*` and `mutation` (no score regression) green. Run superpowers:requesting-code-review, then open the draft PR titled "core: coroutine support: awaitable completions and Task-returning model handlers on core::async".
- [ ] **Step 6: Follow-up issue.** File one listing the remaining overlap with core-cpp:
  - `morph::exec` executors and strand vs `core::async` executors;
  - `morph::log` vs `core::log`;
  - `FileIoOps` vs `core::platform::FileSystem`;
  - the global `DateTime::now()` override vs `core::platform::IWallClock` injection;
  - `morph::net` on Windows via `core::net` sockets.

  Include the constraints (header-only surface, single-threaded WebAssembly).

---


# Rulings (2026-09-24), superseding the plan text above

1. **One branch, one PR** (user ruling, 2026-09-22). C7 and C8 go on a single branch, `build/core-cpp`, from origin/master (05222af3, which the user just pulled into D:\morph), with one tracking issue and one PR. Keep the plan's commit structure inside that branch:
   - C7's three commits: CPM, TimeoutScheduler, then base64 and wakeup.
   - Then C8's commits: the spec first, then tests, implementation, and the demonstration.
   Every commit is green.
2. **Pin v0.2.1**, not v0.1.0: `GIT_TAG v0.2.1 VERSION 0.2.1`. Iterate against a local core-cpp worktree at that tag (`git -C D:/core-cpp worktree add --detach D:/core-cpp-wt-morph v0.2.1`) via CPM_core-cpp_SOURCE, with USE_COMPILER_CACHE=OFF (fastcached#1597).
3. **Module names:** `core::async`, not `core::coro`. So `core::async::Task`, `core::async::OperationCancelled`, `core::async::IExecutor`, and `HasStopToken` wherever core-cpp spells it. Read core-cpp's headers for the exact names: `src/core/async/*.hpp` and `src/core/net/EventLoop.hpp`, where `PlatformLoop` and `HostDrivenBackend` live.
4. **Worktrees only:**
   - Never modify D:\morph again. The user pulled it; from here on only `fetch` and `worktree add` may run against it.
   - Your worktree is D:\morph-worktrees\core-cpp.
   - Follow morph's AGENTS.md: file a tracking issue first, and treat docs/spec/ as authoritative.
5. **No regression**, in stability, portability or performance. That covers:
   - morph's full native test suite on Windows MSVC (vcpkg preset) and on WSL GCC 15 and Clang;
   - the WebAssembly gates (wasm-ladder and wasm-demo) with emsdk 3.1.56;
   - drift-guard, spec-sync, mutation (with no score regression) and test_dep_cache.sh.

   If core-cpp's WebAssembly subset or any core-cpp API falls short, report it to me with evidence. Don't work around it in morph.
6. **Scratch files** go under scratchpad\C78\ only. Never wait on a notification without a bound; poll logs and processes instead.
7. **Outward actions you may take:** the tracking issue, pushing the branch, and a **draft** PR with a "Consumer impact" section. Nothing else: no merge and no ready-for-review.
8. **Commits** end with `Signed-off-by: Christian Parpart <christian@parpart.family>`. Windows writes CRLF, so write files with write_bytes. MSVC /O2 cannot tail-call a call inside a co_await full-expression (C4737), so keep such calls out.
9. **Final step:** after the PR is open, file the follow-up issue from C8 Step 6.
