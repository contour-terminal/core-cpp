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


---

# Checked against the tree before you were dispatched: two of the five files you must delete are not where the plan says

Measured with `find src -iname ...`, not by reading the plan. **This is the eighth source-list
correction in this project — run the equivalent yourself and say what it still gets wrong.**

| Plan says delete | Reality |
|---|---|
| `src/core/tui/runtime/EventSource.hpp` | **exists** |
| `src/core/tui/runtime/PollEventSource.hpp` | **exists — and so does `PollEventSource.cpp`, which the plan does not name** |
| `src/core/tui/runtime/WithTimeout.hpp` | **exists** |
| `src/core/tui/platform/PollHelpers.hpp` | **MISSING.** It is at **`src/core/tui/runtime/posix/PollHelpers.hpp`** |
| `src/core/tui/testing/MockEventSource.hpp` | **MISSING.** It is at **`src/core/tui/runtime/testing/MockEventSource.hpp`** |

**Why the two moved, which matters more than the paths:** endo had them at
`src/tui/runtime/platform/PollHelpers.hpp` and `src/tui/runtime/testing/MockEventSource.hpp`.
Task A7's import renamed `platform/` to `posix/` under the private-directory convention, and the
plan's list was written against **endo's** layout with a `src/core/tui/` prefix pasted on. So it is
not a typo — it is a whole-list transformation that was right for three rows and wrong for two, and
the two it got wrong are the two that had moved.

## `TerminalEventSource` is three files, not one

The plan says only *"`TerminalEventSource` becomes an input adapter."* In the tree:

```
src/core/tui/runtime/TerminalEventSource.hpp
src/core/tui/runtime/posix/TerminalEventSource.cpp
src/core/tui/runtime/windows/TerminalEventSource.cpp
```

**Two platform implementations**, and the Windows one is the interesting half: it is the code whose
console-handle parking is the entire reason B7's IOCP backend has to bridge waitable HANDLEs at all.
Converting it to an adapter over `loop.waitReadable(inputHandle, HandleKind::Waitable)` is the
point where the merge either works on Windows or does not, and **no local preset proves it on both
platforms at once** — you have `cl-debug` and `clangcl-release` here, and WSL for the POSIX side,
so say which configuration covered which file.

## What you inherit that the brief predates

- **B6 owns `ISocket` and a frameless readiness park** (`ParkEntry::onReady`); your input pump is a
  consumer of that mechanism, not a second one.
- **B7a owns the IOCP backend and the waitable-HANDLE bridge**, which is what `waitReadable` on a
  console handle resolves to on Windows. **B7b, which flips `makeDefaultBackend()` to IOCP, may not
  have landed when you start** — so state in your report which Windows backend your runtime was
  actually tested against, Wfmo or IOCP, rather than which one it will eventually use.
- **B4's loop asserts `teardownIsSerialisedWithDispatch()`** on `spawn`, `resumeSoon`,
  `requestStop`, `registerPark`, `unregisterPark` and `wakeReasonOf`. An input pump resuming from a
  reader thread trips these rather than misbehaving quietly.
- **The six loop entry points that wake the backend** — `post`, `submit`, `schedule`, `spawn`,
  `requestCancel`, `stop` — share *"the loop is asked for a turn"*. The agent wakeup path going
  through `loop.post()` is a member of that family; the interrupt path
  (`SignalHandler` -> `Wakeup` -> `waitReadable`) is not, and should say why in a comment.
