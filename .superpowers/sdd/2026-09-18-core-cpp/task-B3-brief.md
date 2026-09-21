# Brief for Task B3

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


### Task B3: `IoBackend` with callback dispatch
**Files:**
- Create `src/core/net/IoBackend.hpp`, `src/core/net/backend/{Poll,Epoll,Kqueue,Wfmo}Backend.{hpp,cpp}`, `DefaultBackend.cpp`, `testing/{ScriptedBackend,NullBackend}.hpp`.
- Delete the `EventSource`/`DefaultEventSource`/`*EventSource` files.
- `WaitChunking.hpp` → `backend/`.

- [ ] Tests first:
  - contour `EventSourceParity_test` → `BackendParity_test`, run over a `BackendMatrix` of every backend built on this OS.
  - fastcached `EpollReactor_test` pure cases: `selectReadinessCallback` (ex-`SelectEpollCallback`), #475 batch withdrawal, the EBADF fixpoint probe.
  - `KqueueReactor_test` → `KqueueBackend_test`. `setInterest` must report the kernel's refusal (#1054/#1057).
- [ ] Implement the Part I §2 interface; callbacks never resume. Wfmo = contour's Windows event source behind the new interface.
- [ ] **HostDrivenBackend, test first on every OS.** Create `backend/HostDrivenBackend.{hpp,cpp}`, `IHostScheduler.hpp`, `testing/ManualHostScheduler.hpp`, and, under `__EMSCRIPTEN__`, `backend/EmscriptenHostScheduler.cpp`. Tests with `ManualHostScheduler`:
  - `attach`/`setInterest` return `Unsupported`;
  - `wait()` returns immediately;
  - `wake()` from the same turn schedules exactly one pump (coalesced);
  - `armWakeAt(t)` schedules a pump at `t - now` ms, clamped at 0.

  `makeDefaultBackend()` returns HostDriven over `EmscriptenHostScheduler` under single-threaded Emscripten.
- [ ] Commit `net: IoBackend replaces EventSource; backends dispatch, never resume` and `net: a host-driven backend so an event loop can run inside the browser's`.

