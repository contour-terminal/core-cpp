# Brief for Task B5

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


### Task B5: Timers
- [ ] Tests first:
  - fastcached `SleepUntil_test` (null loop and elapsed deadline resolve inline).
  - `DeadlineTimer_test` without the poll-interval cases.
  - `InterruptibleSleep_test`, rewritten: a cancel wakes promptly with `ManualClock` frozen and leaves nothing parked.
  - `DeadlineTimer` is destroyable from inside its own callback.
- [ ] Implement `addTimer`/`cancelTimer` (no frame) and `DeadlineTimer(EventLoop&, tp, cb, state)` over them. `interruptibleSleepUntil(EventLoop*, StopToken, tp) -> Task<WakeReason>`. `nextWakeStep`. Keep the deprecated `wakeBound` overload for one release.
- [ ] **Real-browser smoke.** Add `tests/wasm/HostDrivenTimer_smoke.cpp` (Emscripten only, `-sASYNCIFY`): a `PlatformLoop` spawns a coroutine that awaits `delay(20ms)` and sets a flag; `main` calls `emscripten_sleep(10)` in a loop, bounded to 2 s, until the flag is set; the test fails on timeout. Run it under node in the `emscripten` job on both emsdk versions, and extend consumer-smoke (c) to the same scenario.
- [ ] Commit `net: callback timers; DeadlineTimer and interruptible sleep no longer poll`.

