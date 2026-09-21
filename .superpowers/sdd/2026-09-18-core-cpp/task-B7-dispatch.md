# Task B7: IOCP is the Windows backend, and it can park on a waitable handle

Windows is the platform where contour's design and fastcached's disagree most. contour waits on
events with `WaitForMultipleObjects`, which caps at 64 handles and cannot express a completion.
fastcached uses a completion port, which scales and expresses one — but cannot, on its own, wait on
a console handle. This task takes fastcached's port and gives it the one thing it lacked, so that
**one backend serves both a server's sockets and a TUI's console input**. That is the merge;
everything else is detail.

Read the plan's Task B7 and the design spec's **§2 item 2** (the IOCP readiness bridging bullets)
and **§2 item 3** (G1–G5). **Read B3's, B4's and B6's reports first** — B3 built the interface you
implement, B4 owns the loop that drives you, B6 owns the socket that sits on you.

## The four jobs, and the order they matter in

1. **`IocpBackend` implementing B3's `IoBackend`**, with a `completionPort()` returning non-null.
   **You add that member; the interface does not declare it yet** — `IoBackend.hpp` at `HEAD` has
   no `completionPort` and no `#if defined(_WIN32)` member beyond `DefaultHandleKind`. An earlier
   draft of this dispatch said B3 had already declared it, which was wrong and would have sent you
   hunting, or opening a defect against a lane that did nothing wrong. The interface's own comment
   is the truthful version: `BackendKind::Iocp` *"arrives in Task B7; `makeBackend` answers null"*.
2. **The readiness bridge** — the part that does not exist upstream:
   - a **waitable HANDLE** (console input, `Wakeup`'s event, `SystemPipe`) gets a threadpool wait
     whose callback **only posts to the port**;
   - **socket readability** is a zero-byte `WSARecv` (fastcached `Net/IocpSocket.cpp:555-561`);
   - **socket writability** goes through `WSAEventSelect` plus a threadpool wait.
3. **`IocpSocket` and `IocpListener`** on B6's `ISocket`/`IListener`.
4. **`makeDefaultBackend()` returns IOCP on Windows**; `makeBackend(BackendKind::Wfmo)` still
   answers the old one, which stays one release as the fallback (core-cpp#6).

## `ReadinessHandler::slot` — your obligation, and why it was left to you

The spec declares `detail::ReadinessSlotRef slot {}` on `ReadinessHandler`. **B3 omitted it and I
upheld the omission**: nothing in B3 read or wrote it, and a public field with no reader has no
defined meaning — the first writer would have defined it, which is backwards from "the spec is a
contract". You are the first writer. Adding it now is a public-header change that costs nothing
because nothing has shipped.

So `slot` means what IOCP needs it to mean, and the spec already says what that is:
**`lpOverlapped` points to a backend-owned refcounted slot, never into the handler.** A completion
can arrive after the handler is gone; a pointer into the handler is a use-after-free with a kernel
on the other end of it. Refcount it, generation-check it, and **write down the ownership rule in the
header beside the field**, because the next backend to want one will read that and not this brief.

Record the field in `.agent/reference/provenance.md`'s `IoBackend.hpp` row as B7's addition, and say
in the CHANGELOG that it arrived with its first user rather than ahead of one.

## Ruling R101 binds you too: a watched direction wins

IOCP is completion-based, so `Readiness` is something you synthesise rather than something a
kernel hands you — which makes it easy to synthesise the wrong thing. **R101 (see `task-B3-fixround2.md`) rules that a watched direction wins
and that `Readiness::Failed` is best-effort. It landed in `7cd86fc`** -- but that sentence is a
state, and states go stale: an earlier draft of this dispatch said it had not landed, which was true
when written and false thirty minutes later. **Check `selectReadinessCallback` yourself before you
rely on the ordering, and say what you found.** One `git show HEAD:src/core/net/IoBackend.hpp`. A failed completion on a direction the caller is watching dispatches **that
direction's callback**, so the caller's read or write returns the error through its own path.
`onError` is for a failure arriving with neither direction set, and nothing above you may branch on
`Failed` for correctness. Synthesising `Failed` where a direction is watched would reintroduce, on
the platform that has to fake it, the exact defect the ruling removed from poll and epoll.

## G1–G5, and the one that is not a comment

The spec's five thread-affinity guarantees are **asserted, not documented**:

- **G1: exactly one thread dequeues the port.** A second thread calling `wait()` is an assert, and
  it gets a test — a `canary`, Debug, `WILL_FAIL`. IOCP will happily let you dequeue from four
  threads; that is its selling point elsewhere and it is forbidden here, because Rule 1 says the
  loop resumes and there is one loop.
- **G3: helper threads only post.** Your threadpool wait callbacks are helper threads. They call
  `PostQueuedCompletionStatus` and nothing else — no resume, no member of a handler, no allocation
  you would have to reason about.
- **G4: a SOCKET is associated with exactly one port.** Associating twice is undefined and silent.
- **G5**: `assertTeardownIsSerialisedWithDispatch` in every socket, listener and dial destructor.

## The upstream hazards, each with its issue

fastcached at **`0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`**, read as blobs
(`git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show 0708dd54:<path>`):
`Async/IocpReactor.{hpp,cpp}`, `Net/IocpSocket.{hpp,cpp}` (**`IocpListener` lives in these two** --
declared at `Net/IocpSocket.hpp:129`, implemented at `Net/IocpSocket.cpp:687`; there is no
`Net/IocpListener.*`),
`Async/IocpReactor_test.cpp`, `Net/IocpSocket_test.cpp`, `Net/IocpConnector_test.cpp`.

Three of its scars are load-bearing and **each must survive into your version with its issue cited**:

- **[fastcached#465](https://github.com/LASTRADA-Software/fastcached/issues/465): the operation
  holds the Impl, not the socket.** A completion outlives its socket; if the op reaches back
  through the socket it reaches through a dangling pointer.
- **[fastcached#884](https://github.com/LASTRADA-Software/fastcached/issues/884): retire
  semantics — if bytes already arrived, the data wins over a later stop.** Same invariant B6 has,
  in the one place where the kernel and the stop token race for real.
- **`IocpSocket.cpp:327-334`, the `MSG_PEEK` classification**: how a zero-byte completion is told
  apart from EOF. Read what it is doing before you reimplement it; getting it wrong turns a live
  connection into a spurious EOF, which is the kind of defect that reproduces once a week in
  production and never in a test.

`NtAssociateWaitCompletionPacket` is an **optimisation taken only if a startup probe succeeds** —
`GetProcAddress`, never a link against ntdll. It is not the path; the threadpool wait is the path,
and the probe's failure must be ordinary.

## Tests, first

- **`BackendParity` on IOCP**, the same matrix B3 built: a waitable HANDLE (the `Wakeup` event
  always; console input where there is one, serialised by the named mutex and **`SKIP` — never
  `SUCCEED` — where there is no console**), socket read readiness, socket write readiness.
- **`IocpBackend_test`**: detach with a packet in flight, under clang-cl ASan, and it must not be
  a use-after-free; the G1 assert when a second thread calls `wait()`.
- fastcached's `IocpSocket_test` (13 cases) and `IocpReactor_test`, renamed.
- **A teardown gate**, in the shape B4's `LoopTeardown_test` established.

Write the case, run it, **capture the RED verbatim**, then implement, then the GREEN. Say how many
failures you expect and from which cases before you run — *it is only a check if you commit to the
answer first*.

## What this session has learned that bears on you

- **A local green is not evidence about something your local run cannot reach.** You are the one
  task whose whole subject is a platform you CAN run here, which is a luxury the last three lanes
  did not have — use it, and still say which configurations covered what.
- **A dispatch's list is a starting point, never an inventory.** Enumerate what B6 actually left
  you rather than trusting the sentence above that says it.
- **Report a defect in another lane's file, do not fix it.** Five lanes share this checkout.
- **Never a bare `git commit`, `git commit -a` or `git add`.** `git commit --only -- <paths>`; a
  private `GIT_INDEX_FILE` only for hunks of a file whose other hunks are not yours, and then the
  fourth step that is not optional: snapshot `git status --porcelain` before, `git reset -- <your
  paths>` after, **diff the snapshots**. Build shared-file content from `git show HEAD:<path>`,
  never the worktree copy. `git show --stat` after every commit.

## Then

- `python scripts/clang-format.py --check <paths>` (a bare run is an error),
  `python scripts/python-style.py --all --check` if any Python changed (which likewise refuses a
  bare run, exit 2),
  the `clang-tidy` preset, `ctest -L hygiene`, `mkdocs build --strict`.
- **Windows is your platform**: `cl-debug`, `clangcl-debug` and `clangcl-debug` with
  `-DCORE_CPP_SANITIZERS=address` as a top-level build, plus `clangcl-release`. Then WSL
  `clang-debug` and `gcc-release`, because the interface is shared even though the backend is not.
- `CHANGELOG.md` under **Added** and **Changed** — the Windows default moving is a behaviour change
  every Windows consumer sees, and it needs a sentence saying how to get the old one back.
- `.agent/rules/async-and-net.md` gains G1 and G4 as stated rules with their reasoning, and the
  `slot` ownership rule, each citing its origin as a full URL.
- Under R73, watch the newest head; a superseded run reports `cancelled` with **zero jobs**
  (`gh run view <id> --json jobs --jq '.jobs | length'`).

## Report

`D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-B7-report.md`: RED/GREEN per test, how you
proved the G1 canary can die, what `slot` means and who enforces it, which of fastcached's three
scars you kept verbatim and which you rewrote and why, and anything B6 left you that this brief got
wrong. Return only status, the commit range, a one-line test summary, and concerns.
