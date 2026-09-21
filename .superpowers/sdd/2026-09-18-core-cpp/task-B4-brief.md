# Brief for Task B4

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


### Task B4: EventLoop absorbs the IReactor contract
**Files:** Modify `src/core/net/EventLoop.{hpp,cpp}`. Create `PlatformLoop.hpp`, `testing/TestLoop.hpp`, `detail/{WorkerIdentity,ParkTable}.hpp`.

- [ ] Tests first:
  - contour `EventLoop_test`.
  - fastcached `IReactor_test`, `TestReactor_test` → `TestLoop_test` (FIFO order, thread-safe submit, `cancelPending` ownership transfer on every backend including submissions).
  - `ReactorClockRefresh_test` → `ClockRefresh_test`.
  - `ParkedWork_test` → `LoopTeardown_test` over `BackendMatrix` (the six #1025 proofs).
  - `spawn` of 100k tasks is O(1) per completion (assert that `spawnedCount()` drops without a sweep).
  - `runOnce` bounded turn; `IdlePolicy::Return`.
- [ ] **Host-driven mode, test first.** Over `ManualHostScheduler`:
  - `loop.post(f)` runs `f` only once the host pumps;
  - a spawned coroutine awaiting `loop.delay(50ms)` resumes after the host pumps at the armed deadline (ManualClock);
  - `run()` and `blockOn()` assert on a host-driven backend (a Debug death test labelled `canary`).
- [ ] Implement the `runOnce` turn, the teardown order and `ParkId` cancellation (Part I §2). `teardownIsSerialisedWithDispatch() == !running() || isOnWorkerThread()`. After each turn on a host-driven backend, call `backend.armWakeAt(nextDeadline)`.
- [ ] Add `EventLoop`, `PlatformLoop`, `TestLoop` and HostDriven to the WebAssembly FILE_SET. Expected: the `emscripten` job passes.
- [ ] Commit `net: one EventLoop carries fastcached's reactor contract on every backend`.

