# Brief for Task B1

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).


**Every B task follows this pattern:**
1. Port the named tests from `D:\fastcached\src\FastCache\{Async,Net}` (renamed per the Part I §2 rename map, into core names) and/or adapt the contour tests.
2. Build and confirm the new cases FAIL.
3. Implement.
4. Confirm PASS on Windows (`clangcl-debug`, `cl-debug`) and WSL (`clang-debug`, `gcc-debug`, `clang-tsan`, `clang-asan-ubsan`).
5. Push, and require CI `ci-ok` green.
6. Commit.

**Sources:** every fastcached path named in Phase B is read as `git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show origin/master:<path>`, never from the `D:\fastcached` working tree.

Implementation must preserve the lifetime rules in `.agent/rules/wire-and-protocol.md` of fastcached `origin/master` (§Sockets, §Dialing and the reactor, §Socket and coroutine lifetime) and `D:\fastcached\AGENT.md` (grep: lifetime, ParkedWork, teardown, IOCP). Each rule carried over is written to `.agent/rules/async-and-net.md` in the same task that implements it.


### Task B1: coro grafts
**Files:** Modify `src/core/coro/Task.hpp`, `WhenAll.hpp`, `WhenAny.hpp`. Create `DetachedTask.hpp`, `SyncRun.hpp`, `ParkedWork.hpp`, `IExecutor.hpp`, `ResumeOn.hpp`, `ThreadPoolExecutor.hpp`, `AsyncQueue.hpp` + tests.

**Sources:** fastcached `Async/{Task,DetachedTask,SyncRun,ParkedWork,IExecutor,ResumeOn,ThreadPoolExecutor,AsyncQueue}.hpp` and their tests.

- [ ] **Tests first:**
  - Task: fastcached `SyncRun` `logic_error`; symmetric-transfer depth; a non-default-constructible `T`; awaiting an rvalue Task empties it.
  - Update contour's "named local keeps owning after co_await" case to the new rule.
  - `unownedRoot` propagates through `whenAll`/`whenAny` runners.
  - `ParkedWork` primitive cases.
  - `ThreadPoolExecutor_test`.
  - `AsyncQueue_test`, plus a stop-aware `pop` that throws `OperationCancelled`.
  - A compile-time check that `executor.submit(ParkedWork{})` selects the owning overload (#1041).
- [ ] **Implement:**
  - `unownedRoot` set at every `await_suspend`.
  - The awaiter takes ownership.
  - `release()`.
  - `T{}` only under `if constexpr (std::is_default_constructible_v<T>)`.
  - `DetachedTask`'s promise has `stopToken()` returning a never-stop token.
  - `ThreadPoolExecutor.hpp` is guarded with `#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)` → `#error "core::coro::ThreadPoolExecutor needs threads; single-threaded Emscripten has none"`. It is excluded from the WebAssembly FILE_SET and header self-check. Everything else in coro stays in the subset; the `emscripten` job must pass.
- [ ] Commit `coro: teardown-safe ownership, detached tasks, executors and AsyncQueue from fastcached`.

