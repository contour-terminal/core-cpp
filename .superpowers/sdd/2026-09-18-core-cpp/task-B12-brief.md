# Brief for Task B12

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


### Task B12: TUI runtime on the EventLoop
- [ ] Tests: endo `TuiRuntime_test` and `Modal_test`, adapted to `TuiRuntime(EventLoop&, Terminal&)`. Add: input arriving while the loop waits resumes on the loop thread on IOCP (Windows) and epoll (WSL).
- [ ] Delete `tui/runtime/{EventSource,PollEventSource,WithTimeout}.hpp`, `platform/PollHelpers.hpp` and `testing/MockEventSource.hpp` (replaced by `ScriptedBackend`). `TerminalEventSource` becomes an input adapter; the agent wakeup → `loop.post()`; the interrupt path → `SignalHandler` → `Wakeup` → `waitReadable`.
- [ ] Commit `tui: the runtime is composed on core::net::EventLoop; the second scheduler is gone`.

