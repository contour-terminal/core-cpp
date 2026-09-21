# Brief for Task C8

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


### Task C8: morph PR-2: coroutines on `core::coro` (branch `feat/coroutines`, based on C7)
Worktree: `D:\morph-worktrees\core-cpp-coro`. File a tracking issue first.

- [ ] **Step 1: Spec first.** Write `docs/spec/core/coroutines.md` (authoritative) and link it from `docs/spec/core/{bridge,completion}.md`. It specifies:
  - **Client side:**
    - `Completion<T>` is awaitable through `operator co_await() &&` (rvalue only; awaiting consumes it).
    - `co_await` yields `T`, or rethrows the stored `exception_ptr`.
    - The coroutine resumes on the completion's executor, the same place `then()` handlers run.
    - If the awaiting promise satisfies `core::coro::HasStopToken`, a stop request detaches the continuation (via `CallbackToken`) and resumes with `core::coro::OperationCancelled` on that executor.
    - `morph::async::spawn(exec::IExecutor&, core::coro::Task<void>)` starts a detached task whose every resumption is posted to that executor. This is the Qt/QML entry point.
  - **Model side:**
    - An action handler may return `core::coro::Task<R>`; `model::ActionTraits` detects it.
    - The bridge drives it on the model's strand through `morph::exec::StrandCoroExecutor : core::coro::IExecutor`, which posts `h.resume()` onto the strand. Every resumption therefore runs on the model's strand.
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
- [ ] **Step 5: Verify.** CI `ci`, `drift-guard`, `spec-sync`, `wasm-*` and `mutation` (no score regression) green. Run superpowers:requesting-code-review, then open the draft PR titled "core: coroutine support: awaitable completions and Task-returning model handlers on core::coro".
- [ ] **Step 6: Follow-up issue.** File one listing the remaining overlap with core-cpp:
  - `morph::exec` executors and strand vs `core::coro` executors;
  - `morph::log` vs `core::log`;
  - `FileIoOps` vs `core::platform::FileSystem`;
  - the global `DateTime::now()` override vs `core::platform::IWallClock` injection;
  - `morph::net` on Windows via `core::net` sockets.

  Include the constraints (header-only surface, single-threaded WebAssembly).

