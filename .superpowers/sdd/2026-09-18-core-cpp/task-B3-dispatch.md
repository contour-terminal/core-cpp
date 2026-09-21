# Task B3: `IoBackend` replaces `EventSource` — backends dispatch, the loop resumes

This is the pivot of Phase B. contour's `EventSource` is a **token registry**: `wait()` returns
two vectors of `FdToken`s and the loop looks up what to do with them. fastcached's reactors are
**callback dispatchers**: readiness invokes a handler that only enqueues, and the loop resumes on
its own thread. The merged design is the second shape, and every task after this one — B4's
`EventLoop`, B5's timers, B6's sockets, B7's IOCP — is written against it.

Read the plan's Task B3 (`docs/superpowers/plans/2026-09-18-core-cpp.md`, the "Task B3" section)
and the design spec's §2, especially **Rules 1–3** and the `IoBackend` / `IHostScheduler` /
`HostDrivenBackend` declarations (`docs/superpowers/specs/2026-09-18-core-cpp-design.md`). The
spec's interface block is a **contract**: implement it exactly, including the `[[nodiscard]]`s and
the `noexcept`s.

## Rule 1 is the whole task

> **Backends dispatch, the loop resumes.** Backend, completion, stop and threadpool callbacks only
> enqueue (`EventLoop::resumeSoon`). Coroutines resume only on the loop thread. This behaviour is
> identical on every OS.

A test that a handler callback never resumes a coroutine is worth more than any other case here,
because the bug it prevents is a coroutine resuming inside a backend's iteration over its own
ready list — which is a use-after-free the moment the resumed frame detaches its own handler.
Write it as an assertion the scripted backend can make on every path, not as one case.

Rule 3's thread-affinity guarantees are asserted **on every backend**: G1 (exactly one thread
dequeues), G2 (every resumption happens in turn step 2 of `runOnce`), G3 (helper threads only
post). G4 and G5 arrive with B6/B7.

## What moves, and where it goes

Per Ruling R40 there is **no `backend/` directory**: a file's directory says which platform it is
for, and CMake's per-platform source lists select it. No `#ifdef` chooses a backend.

| Create | Directory |
|---|---|
| `IoBackend.hpp`, `IHostScheduler.hpp`, `HostDrivenBackend.{hpp,cpp}` | `src/core/net/` (portable, public) |
| `PollBackend.{hpp,cpp}`, `DefaultBackend.cpp` | `posix/` |
| `EpollBackend.{hpp,cpp}`, `DefaultBackend.cpp` | `linux/` |
| `KqueueBackend.{hpp,cpp}`, `DefaultBackend.cpp` | `bsd/` |
| `WfmoBackend.{hpp,cpp}`, `DefaultBackend.cpp` | `windows/` |
| `EmscriptenHostScheduler.cpp` | `emscripten/` (`SOURCES_EMSCRIPTEN`) |
| `ScriptedBackend.hpp`, `NullBackend.hpp`, `ManualHostScheduler.hpp` | `testing/` |

Delete `EventSource.hpp`, `DefaultEventSource.{hpp,cpp}`, `PollEventSource.hpp`,
`posix/PollEventSource.cpp`, `windows/PollEventSource.cpp`, `linux/EpollEventSource.{hpp,cpp}`,
`bsd/KqueueEventSource.{hpp,cpp}`, `testing/ScriptedEventSource.hpp` and
`testing/EventSourceBackends.hpp`. `detail/WaitChunking.hpp` **stays where it is**: it is pure
logic, tested on every OS.

`WfmoBackend` is contour's Windows event source (`WSAEventSelect` + `WaitForMultipleObjects`)
behind the new interface — a port, not a rewrite. It stays one release as IOCP's fallback (B7
makes IOCP the default), so do not let it rot: it is in the parity matrix like every other.

## Scope boundary — read this before you touch `EventLoop`

`EventLoop`, the sockets and the listeners all consume `EventSource` today. **B4 rewrites
`EventLoop`'s contract; you do not.** Your job is to leave `EventLoop` compiling and its existing
tests passing over the new interface, with the smallest adaptation that is honest. If an
`EventLoop` behaviour cannot survive the shape change without a redesign, say so in the report
and leave a failing-by-design note for B4 rather than inventing B4's answer. Same for the
sockets: adapt their readiness calls, do not merge them — that is B6.

If that adaptation turns out to be larger than the backends themselves, stop and report it. It
would mean B3 and B4 are one task, which is my call and not yours.

## Sources

fastcached at **`0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`**, read as blobs:

```
git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show 0708dd54:src/FastCache/Async/<file>
```

`EpollReactor.{hpp,cpp}`, `EpollReactor_test.cpp`, `KqueueReactor.{hpp,cpp}`,
`KqueueReactor_test.cpp`, `IReactor.hpp` (for the contract the backends serve),
`ReactorWorkerIdentity.hpp`. Take their **reasoning**, not their shape: `IReactor` is B4's
`EventLoop`, not your `IoBackend`.

Three fastcached bugs are load-bearing and must arrive with tests:

- **#475 — `detach()` withdraws from an in-flight batch.** A handler detached during dispatch must
  not be called later in the same `wait()`'s ready list. This is the use-after-free Rule 1 exists
  to prevent, reached from the other side.
- **#1054 / #1057 — `setInterest` must report the kernel's refusal.** Kqueue will refuse
  registrations the caller believes succeeded; swallowing that leaves a flow parked forever.
  `setInterest` returns `std::expected<void, NetError>` for exactly this reason.
- **The EBADF fixpoint probe** from `EpollReactor_test`: a closed descriptor in the set must be
  found and removed rather than spinning the pump.

Record the pin and a row per imported file in `.agent/reference/provenance.md`, and the fastcached
row in `CHANGELOG.md` and `NOTICE` if B1 has not added it by then.

## Tests, first

- **`BackendParity_test`** — contour's `EventSourceParity_test` (1131 lines at contour `6777ff05`; the 1243 figure an earlier draft gave was core-cpp's own copy, so the number was ours and the credit contour's -- and it is 1588 today, which is why a line count of a moving file does not belong in a brief) rewritten over a
  `BackendMatrix` of every backend built on this OS, so Poll, Epoll (Linux), Kqueue (BSD/macOS)
  and Wfmo (Windows) answer identically. This file is the reason the two poll backends' disagreement
  about `FdInterest::None` was invisible for a whole gate pass; it is now `Interest::None`, it is
  documented as "mute the fd without detaching it", and A12 made every backend agree. **Keep that
  case and keep it in the matrix.**
- The scripted backend's `detach` is **idempotent** (A12 fixed that too) and its counters do not
  under-report. **Carry that guarantee into `IoBackend.hpp`** and document it there: an earlier
  draft of this dispatch cited `EventSource.hpp` as the authority, which line 45 of this same
  document tells you to delete.
- fastcached's three cases above.
- `HostDrivenBackend` over `testing::ManualHostScheduler`, on **every** OS — it is a portable
  backend, and the browser is only one of its hosts:
  - `attach` and `setInterest` return `NetErrorCode::Unsupported`;
  - `wait()` returns immediately and never blocks;
  - `wake()` twice in one turn schedules exactly **one** pump (coalesced);
  - `armWakeAt(t)` schedules a pump at `t - now` milliseconds, clamped at 0;
  - `isHostDriven()` is true, and false for every other backend.
- `makeDefaultBackend()` returns the OS's default — Epoll on Linux, Kqueue on BSD/macOS, Wfmo on
  Windows (IOCP arrives in B7), HostDriven over `EmscriptenHostScheduler` under single-threaded
  Emscripten. `makeBackend(BackendKind)` returns `nullptr` for a kind this OS does not build,
  and a test asserts that for a kind that is genuinely absent here.

Write the case, run it, capture the RED verbatim, then implement, then the GREEN. Where a case
cannot be driven on this machine (BSD kqueue), say so and use CI — `Portability` runs FreeBSD
nightly and can be dispatched on a commit.

## WebAssembly

`IoBackend`, `IHostScheduler`, `HostDrivenBackend`, `ManualHostScheduler`, `NullBackend` and
`ScriptedBackend` are in the subset; the readiness backends are not. No `std::thread`, no
blocking wait, no `Threads::Threads`, nothing newer than libc++ 17 without a `__cpp_lib_*` guard.
The `emscripten` CI job must be green before this task is done.

## Then

- `python scripts/clang-format.py --check <paths>` on what you touched, the `clang-tidy` preset clean,
  `ctest -L hygiene` (the layering check reads the module table — a new include edge must be in
  it), `mkdocs build --strict`.
- Local presets: WSL `clang-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan`; Windows
  `cl-debug` and `clangcl-release`. Build `clangcl-release` before every push, not only
  `cl-debug`. TSan matters here: a backend that resumes on the wrong thread is what it catches.
- `CHANGELOG.md` under **`Breaking`** with migrations — `EventSource` → `IoBackend`,
  `FdInterest` → `Interest`, `makeDefaultEventSource` → `makeDefaultBackend`, `FdToken`/
  `WaitOutcome` gone. contour, endo and tuidu all have callers; the spec's §2 "contour/endo/tuidu
  deltas" list is the migration text.
- `tools/migrate/renames.json` gains a row per renamed public symbol **in the same commit**
  (Task C0 creates the table; if it is not there yet, say so in the report).
- `.agent/rules/async-and-net.md` gains Rules 1 and 3 and the three fastcached bugs, each citing
  its origin as a full URL. The plan requires the rule to be written in the task that implements
  it.
- `docs/modules/net.md` and `docs/design/threading.md` if it exists yet.

## Concurrency

Other agents share this checkout and branch. Yours is `src/core/net/` (everything backend-shaped)
— I will name the others in the dispatch message. One working tree, one index, one local
`master`.

- **Never `git pull --rebase`** — it refuses with another session's unstaged work, and stashing
  would take their edits. `git fetch origin`, then push; it is a fast-forward.
- For `CHANGELOG.md`, `NOTICE`, `.agent/reference/provenance.md`, `tools/migrate/renames.json`
  and `docs/`, read every hunk with `git diff -- <file>` and stage only your own with
  `git apply --cached` from a trimmed patch (`git add -p` is interactive and unavailable).
  Confirm with `git diff --cached -- <file>` and check `git show --stat` before pushing.
- Never run a formatter over a file another session is editing.
- Report anything of theirs that looks broken; do not fix it.

## Report

Write to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-B3-report.md`: RED/GREEN per test,
how you proved Rule 1 holds on every backend, what the `EventLoop` adaptation cost and what you
left for B4, which cases could not run here and which CI run covered them, and the CI run IDs.
Return only status, the commit range, a one-line test summary, and concerns.

Two commits, as the plan names them:
`net: IoBackend replaces EventSource; backends dispatch, never resume`, then
`net: a host-driven backend so an event loop can run inside the browser's`.

Push, watch `Build` and `Portability` to green.
