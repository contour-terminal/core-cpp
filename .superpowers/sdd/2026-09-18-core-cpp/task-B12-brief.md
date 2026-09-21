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

- **B6 owns `ISocket` and is BUILDING a frameless readiness park — it does not exist at your base,
  and this bullet originally said it did.** Measured at `5d7a5ae`: `ParkEntry` offers
  `onDeadline(ParkedWork, tp)`, `onCallback(TimerCallback, void* state, tp)` and
  `onReadiness(ParkedWork, ...)`. So there is a readiness park and there is a frameless park, and
  **there is no frameless readiness park** — `onReadiness` takes a coroutine handle. An earlier
  draft named `ParkEntry::onReady`, which is wrong in the name *and* in the kind, and it cost the
  lane that read it a design decision: **your input pump has to be a coroutine, because of the
  code rather than by choice.** B6's approved scope expansion adds the missing variant for the same
  reason. Do not design toward it; note it as the follow-up that would simplify the pump.
- **B7a owns the IOCP backend and the waitable-HANDLE bridge**, which is what `waitReadable` on a
  console handle resolves to on Windows. **B7b, which flips `makeDefaultBackend()` to IOCP, may not
  have landed when you start** — so state in your report which Windows backend your runtime was
  actually tested against, Wfmo or IOCP, rather than which one it will eventually use.
- **B4's loop asserts `teardownIsSerialisedWithDispatch()`** on `spawn`, `resumeSoon`,
  `requestStop`, `registerPark`, `unregisterPark` and `wakeReasonOf`. An input pump resuming from a
  reader thread trips these rather than misbehaving quietly.
- **Every entry point that files work asks the loop for the turn that will run it — but with one of
  two primitives, and which one is decided by what was filed.** *Ready work wakes; a park arms.*
  `post`, `submit`, `spawn`, `stop`, `requestStop`, `requestCancel` and `resumeSoon` file work that
  is ready now and carries no time, so they call `_backend.wake()`. `registerPark` files a park
  **with a time**, so it calls `armHostWake()`, and `addTimer`, `delay` and `sleepUntil` inherit
  that through it. `HostDrivenBackend::wake()` is `scheduleAt(now())`, so waking for a park destroys
  its deadline — which is why the two are not interchangeable.

  **An earlier version of this bullet said "the six loop entry points that wake the backend" and
  named a list.** The list was wrong in `.agent/rules/async-and-net.md` first, wrong in a code
  comment second, and had been copied into this brief third — a wrong enumeration outliving two
  corrections because each copy reads exactly like an audit. **Derive the family from
  `src/core/net/EventLoop.cpp` yourself** and say in your report what you found; if it disagrees
  with this paragraph, this paragraph is the thing that is wrong.

  Your agent wakeup path through `loop.post()` is ready-now work and wakes. The interrupt path
  (`SignalHandler` -> `Wakeup` -> `waitReadable`) files nothing on the loop at all — it signals a
  handle the loop is already watching — and should say so in a comment rather than being read as a
  member that forgot to wake.

## You are NOT behind B6, and the ledger said you were

I wrote *"B12 (TUI runtime on the loop) needs B6 and B4"* into the progress ledger, and it is
wrong. Measured, not reasoned:

```
grep -rn "ISocket|PosixSocket|makeLoopbackPair" src/core/tui/     -> no matches
grep -n "^#include" src/core/net/WithTimeout.hpp                  -> Task, WhenAny, EventLoop only
grep -n "^#include" src/core/tui/runtime/TuiRuntime.hpp           -> async, platform, tui. no net.
```

**Nothing in `src/core/tui/` touches a socket**, and `core::net::withTimeout` -- which replaces
`tui/runtime/WithTimeout.hpp` -- is socket-free and landed in Task A6. Everything you consume
(`EventLoop::waitReadable`, `post`, `delay`, `HandleKind::Waitable`, `testing::ScriptedBackend`)
came from B3, B4 and B7a, all landed. **B6 is the critical path for B7b, B8, B9, B10 and B11. It is
not yours.**

What you *are* behind is the settled `EventLoop`: B4's `blockOn` fix and B5's wake/arm fix are
unpushed as this is written. You start from `origin/master` **after** that push, so the semantics
under you are final rather than in flight.

## Three things from other lanes that are yours and appear nowhere in the plan

(This heading said "Two" over three bullets until I counted them. In a brief whose subject is
enumerations that stop matching what they enumerate, that is not an amusing coincidence — it is the
cheapest possible demonstration that **a count written beside a list is a claim about the list**,
and the only way to keep it true is to derive it from the list every time you touch either.)

- **`cmake/CoreCppModules.cmake`'s `tui` row does not list `net`, and configure refuses a link the
  module table does not carry.** Extend the row in the same change that adds the include, or your
  first build fails for a reason that looks nothing like its cause. (From the lane that owns
  `core::tui`.)
- **`renames.json` needs a second set of rows in your commit.** `core::tui::runtime` still declares
  `FdInterest`, `FdToken`, `WaitOutcome`, `FdRegistration` and `FdRegistry`, and the table already
  carries those names once under `core::net::`. The collision is live *until you delete them* --
  which is why B3's `removed` rows had to be `core::`-rooted and fully qualified (ruling R104). Your
  rows are the `core::tui::runtime::`-rooted half, in the same commit as the deletion.
- **`runtime/PollEventSource.cpp` is the one file in `core::tui` that still picks its platform with
  an `#ifdef`.** You delete it, so `.agent/rules/platform.md`'s rule -- *an OS difference is an
  injected implementation, never an `#ifdef` in logic* -- lands in your task by construction. Say so
  in the report; it is a rulebook claim that becomes true because of this commit.

## The four deferred runtime defects are yours

core-cpp#16, #17, #18 and #19 were found in Task A7's review, deferred here because **B12 deletes
the code they live in before v0.1.0 ships**. Read them before you start: if your rewrite does not
happen to remove one of them, it has to fix it, and "the file is gone" is only an answer for the
ones whose file is actually gone.
