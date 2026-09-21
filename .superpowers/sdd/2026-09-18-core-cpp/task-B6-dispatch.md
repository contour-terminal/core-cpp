# Task B6: one frame-free, stop-aware socket contract, and one POSIX socket

contour's `PosixSocket` and fastcached's `Epoll`/`KqueueSocket` are three implementations of the
same idea. This task makes them one, on B3's `IoBackend` and B4's `EventLoop`, and gives every
consumer a single `ISocket` whose operations are awaitables rather than frames.

Read the plan's Task B6 and the design spec's **§2 item 7** and **item 5** (cancellation)
(`docs/superpowers/plans/2026-09-18-core-cpp.md`, `docs/superpowers/specs/2026-09-18-core-cpp-design.md`).
**Read B3's and B4's reports first** — what they built, and what each deliberately left for you.

## The contract, and the three rules that decide it

`read` / `write` / `writeVectored` / `waitReadable` / `cancelRead` / `shutdownWrite` /
`handshakeIfNeeded` / `setReceiveDeadline`, plus contour's `readWithFd`. Each returns a
`ResultAwaitable<R>`, not a `Task`. **You add `core::async::asTask(aw)`** for callers that must
store one — it does not exist yet, and nothing in `src/` defines it. The plan says *"Add
`core::async::asTask`"* (`:860`); an earlier draft of this dispatch said it already existed, which
was wrong and would have sent you hunting for it or concluding B1 under-delivered.

Three rules from the spec govern every one of them, and they are the ones a merge gets wrong:

1. **A cancel from the flow's own token throws `OperationCancelled`. A cancel from the *resource*
   — `close()`, `cancelRead()`, a closed listener — returns `NetErrorCode::Cancelled` as a
   **value**.** Two different mechanisms for two different facts; a merge that collapses them
   makes a closed socket indistinguishable from a cancelled flow.
2. **If a receive already completed with bytes, the data wins** (LASTRADA-Software/fastcached#884).
   A stop arriving after the bytes must not discard them. This is the invariant an earlier gate
   found `whenAny` violating, and it has the same shape here.
3. **`close()` touches no member after completing** (`.agent/rules/async-and-net.md`), and
   `assertTeardownIsSerialisedWithDispatch` runs in every socket, listener and dial destructor
   (spec §2 rule G5).

One read and one write operation per socket. **You build the public `core::net::contract::*` slot
guards** -- they do not exist at `HEAD`; spec §2 item 7 and plan `:161` put them in your scope.
They are how that rule becomes enforced rather than hoped for, which is the reason they are yours.

## Ruling R101, made because of you: `onError` is diagnostic, never load-bearing

**You are the first NON-TEST code that will set `ReadinessHandler::onError`** -- four places
already do (`BackendParity_test.cpp:873` and `:1347`, `IoBackend_test.cpp:41`, and
`EventLoop.cpp:120`, which sets it to `nullptr` deliberately and says why). An earlier draft said
"the first code in this repository", which a grep refutes in one command. B3's
review found what would have happened when you did. Read the ruling in `task-B3-fixround2.md`
before you write a handler.

The short version: `selectReadinessCallback` returned exactly one callback, and `Failed` with
`onError` set beat a watched direction. So a peer hangup arriving as `POLLIN|POLLHUP` — **a socket
with buffered data still unread** — would have returned `onError` alone on poll and epoll, the
reader would never have been woken, and those bytes would never have been read. kqueue and Wfmo
return `onReadable` and are correct.

What you may rely on -- it landed in `7cd86fc`, and **verify that for yourself rather than trust
this line**, since a dispatch records a state it cannot keep:

- **A watched direction always wins.** A hangup on a direction you are watching wakes that
  direction's callback, on every backend. That is the one portable guarantee and it is the one
  your reader and your dial actually need.
- **`Readiness::Failed` is best-effort and not portable.** kqueue and Wfmo do not set it for a
  hangup, by design and correctly. **Do not branch on it for correctness.**
- **`onError` fires only when `Failed` arrives with neither direction set** — the `POLLERR`-only
  failed connect. Treat it as a fast path that carries the errno sooner, never as the thing that
  tells you a connection failed.
- **Your dial checks `SO_ERROR`**, which is the portable connect idiom on every platform and does
  not consult which callback fired. A dial written to depend on `onError` is wrong even where it
  works.

If you find yourself writing a handler whose correctness depends on which callback ran, stop —
that is the shape this ruling exists to forbid, and it is the shape that made a data-loss bug sit
undetected behind a null pointer.

**One shape-level fact B3 flagged specifically for you, so you meet it in a brief rather than
discover it.** `selectReadinessCallback` returns **exactly one** callback. The reorder changes
*which* one fires; it never fires both. So a handler that wants to know a failure *accompanied*
readable data cannot be told — it has to look. That is consistent with `Failed` being best-effort
and nobody is proposing to change it, but if your socket wants that fact, get it from the `read`
that follows, not from the dispatch.

Which is the POSIX idiom stated properly, and B3's framing is better than mine: **you never trust
`revents` to tell you what went wrong — you do the `read`/`write` and let *that* report.** Waking
the watched direction and letting the I/O call fail is what every platform already does. Write your
sockets that way and R101 costs you nothing.

## Sources

fastcached at **`0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`**, read as blobs
(`git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show 0708dd54:<path>`):
`Net/EpollSocket.*`, `Net/KqueueSocket.*` and their tests, `Net/ISocket.hpp`,
`Net/IoAwaitable.hpp`, `Net/SocketContract.hpp`, `WaitReadable_test.cpp`, `CancelRead_test.cpp`,
`SocketDecorator_test.cpp`. Plus contour's `Socket_test.cpp`, `UnixSocket_test.cpp`,
`posix/FdPassing_test.cpp` and the current `posix/PosixSocket.*`, already in the tree and already
repaired by the Phase A gate — **read what that gate fixed before you replace it**, or you will
re-introduce a stale `errno` on a zero-length write, a listener without `FD_CLOEXEC`, and an
`isClosed()` that does not latch on EOF.

Record the pin and a row per imported file in `.agent/reference/provenance.md`. Every renamed
public symbol gets a `renames.json` row in the same commit; `removed` rows are **fully qualified**
(`core::net::X`, never bare — `core::tui::runtime` still has its own `FdInterest` and friends until
B12). Base that file on `git show HEAD:<path>`, never the worktree copy: it is generated as well
as hand-edited.

## Tests, first

- fastcached's `EpollSocket_test` + `KqueueSocket_test` → **one** `ReactorSocket_test` over B3's
  `BackendMatrix`. Two suites becoming one is the point of the task; if a case only makes sense
  for one backend, that is a finding about the contract, not a reason for a second suite.
- `WaitReadable_test` — the **#677 count semantics**, exactly.
- `CancelRead_test`; `IoAwaitable_test` with the three stop-token cases spelled out above
  (cancel throws; `close()` yields a `Cancelled` **value**; completed bytes beat a later stop).
- `SocketDecorator_test`.
- contour's `Socket_test` (`FdWakePolicy`, loopback pair, `peerAddress`), `UnixSocket_test`,
  `FdPassing_test`.
- The read-slot, write-slot and empty-read-buffer **canaries**: Debug, `WILL_FAIL`, label `canary`.
  A canary that cannot die is not a canary — prove each one fails.

Write the case, run it, capture the RED verbatim, then implement, then the GREEN.

## What this session has learned that bears on you

- **A dispatch's list is a starting point, never an inventory.** Mine have been wrong in both
  directions today. Enumerate what is in the tree rather than implementing this list.
- **It is only a check if you commit to the answer first** — say how many failures you expect and
  from which cases, before running.
- **Ask the platform where the answer is portable.** The lane before you wrote a parity case that
  armed a second registration on a descriptor the first was still watching, and asked whether the
  second was dispatched to: Linux and FreeBSD `poll(2)` fill every matching `pollfd`, macOS
  reports the descriptor once, and `IoBackend` promises neither. Its own words — *"I wrote the
  careless form one case away from the comment explaining why it is careless."* You are writing a
  socket suite that must pass on four backends and five platforms; assert what the contract
  promises, never what your machine happens to do.
- **You cannot run macOS or BSD here.** `Portability` dispatches FreeBSD on demand
  (`gh workflow run portability.yml --ref master`) and macOS runs on every Build. Put a hypothesis
  in the commit message and let CI refute it rather than guessing twice.

## Then

- `python scripts/clang-format.py --check <paths>` (a bare run is now an error), `ruff format
  --check`, the `clang-tidy` preset, `ctest -L hygiene`, `mkdocs build --strict`.
- WSL `clang-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan`; Windows `cl-debug` and
  `clangcl-release`. **Run `gcc-release` yourself** — and note that clang diagnoses
  `-Woverloaded-virtual` too; the toolchain file is `CORE_CPP_GCC_OR_CLANG`, and only MSVC is
  silent.
- `CHANGELOG.md` under **`Breaking`** with migrations. contour, endo, tuidu and fastcached all
  have socket callers.
- `.agent/rules/async-and-net.md` gains the three rules above and the slot-guard discipline, each
  citing its origin as a full URL.

## Concurrency

Four other lanes share this checkout. **Never a bare `git commit`, `git commit -a` or `git add`.**
`git commit --only -- <paths>` for files only you touched; a private `GIT_INDEX_FILE` for hunks of
a shared file — and then the fourth step, which is not optional: snapshot `git status --porcelain`
before, `git reset -- <your paths>` after, and diff the snapshots. Build shared-file content from
`git show HEAD:<path>`, never the worktree. `git show --stat` after every commit.

Your commit will usually get no CI run of its own; watch the newest head. A superseded run reports
`cancelled` with **zero jobs** — `gh run view <id> --json jobs --jq '.jobs | length'`.

## Report

`D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-B6-report.md`: RED/GREEN per test, how you
proved each canary can die, which cases could not run here and which CI run covered them, and what
you inherited from B3 and B4 versus what you decided. Return only status, the commit range, a
one-line test summary, and concerns.
