# Task C3+C4 report: fastcached onto core-cpp v0.2.0, re-pinned to v0.2.1, v0.3.0, v0.4.0, v0.4.2, v0.4.3 and v0.5.0

**v0.5.0 round (2026-09-26): the pin is v0.5.0 (`7f5f741`), head `784f94d0`, still a draft.**

The Breaking notes were checked against fastcached, and none of them reaches it:
- **WFMO removal (#6):** fastcached names none of the removed types; its loops are `PlatformLoop`s on IOCP. Commit 3's message no longer lists WFMO.
- **`ProcessEnvironment` (#7):** fastcached uses no environment seam.
- **`cli::parse` (#13):** `core::cli` is not linked.

Gates (fresh trees, cache off, C: at 2 GB or more, each tree deleted after):
- clangcl-debug, clean tree: the four error-popup canaries first (4/4), then 5061/5061.
- cl-debug: 5060/5061. `required-context-table-selftest` timed out at 60 s under the parallel run, and passed alone in 9 s.
- WSL clang-debug: 5121/5121.
- TSan: clean on all four targets.
- CI Build 36232132847: 26 passed, 1 skipped.

**v0.4.3 round (2026-09-26): the pin is v0.4.3 (`76ef2d0`), head `c52f38fb`, still a draft.** It carries core-cpp#52, park reuse. There are no signature changes and nothing changed in fastcached.

Gates (fresh trees, cache off, C: at 2 GB or more before each, each tree deleted after):
- clangcl-debug on a clean tree: 5061/5061. The four error-popup canaries ran alone first, 4/4.
- cl-debug: 5061/5061.
- WSL clang-debug: 5121/5121.
- TSan: clean on all four targets.
- CI Build 36211818916: 26 passed, 1 skipped.

The PR body carries impl-perf's v0.4.0 parity table and the 0.4.3 result: syscalls, malloc and total CPU are at parity or better, user-CPU ranges overlap base on 5 of 6 rows, and mc c64 misses by 0.10 µs.

**v0.4.2 round (2026-09-26): the pin is v0.4.2 (`1d7e951`), head `0da433ac`, still a draft.** The pin is folded into commit 1, and commits 1 and 3 now say v0.4.2 in their messages. v0.4.2 fixes core-cpp#51, the clang-cl `-O0` `DetachedTask` fault that reached fastcached's three shutdown tasks; nothing changed in fastcached for it. `ScopedCapture` is not used here.

Gates (fresh trees, cache off, C: at 2 GB or more before each, each tree deleted after):
- clangcl-debug, the #51 leg on a clean tree: 1042 steps, 5061/5061.
- cl-debug: 1764 steps, 5061/5061.
- WSL clang-debug: 5121/5121.
- TSan: clean on all four targets.
- CI Build 36202823521: 26 passed, 1 skipped.

**v0.4.0 round (2026-09-25): the pin is v0.4.0 (`e3647c0`), head `9452d2a8`, still a draft. CI and every gate are green. TSan was interrupted when WSL's filesystem went read-only, and it passed after the user restored WSL.** See *The v0.4.0 round* below.

**v0.3.0 round (2026-09-25): the pin is v0.3.0 (`1ae59fd`), head `093cede2`, still a draft. The full matrix and CI are green; the parity counts are impl-perf's.** See *The v0.3.0 round* below.

**v0.2.1 round (2026-09-24): the pin is v0.2.1, head `a8053d48`, draft. The whole gate matrix passed on `c7106927`. One flaky case is open and treated as a stability regression, not an accepted risk:** see *The v0.2.1 round* below. The v0.2.0 text that follows is kept as it was.

**Status: DONE, draft PR left for the user's merge decision.**
[LASTRADA-Software/fastcached#1598](https://github.com/LASTRADA-Software/fastcached/pull/1598) is a
draft, never marked ready. It is on branch `claude/1596-core-cpp`, head `69fddee2`, and pins core-cpp
at `GIT_TAG v0.2.0` / `VERSION 0.2.0`. Every local gate on the matrix passed with the compiler cache
off. CI's `sccache-smoke-redis` on Windows-cl-release failed once and then passed on a re-run of the
same job. Final tally: 1 failure in 7 CI runs (6 passes, including 5 consecutive) + 50/50 local,
failure logs armed, cause not found. It is listed under *Open risks* in the PR, and CI is 30/30
green on 69fddee2.

## Identity

| | |
|---|---|
| Tracking issue | LASTRADA-Software/fastcached#1596 |
| Pull request | LASTRADA-Software/fastcached#1598 (draft) |
| Branch | `claude/1596-core-cpp`, based on origin/master `6eaea2cb` |
| Worktrees | `D:\fastcached-worktrees\core-cpp` (the branch); `core-cpp-wsl` (detached; relative git pointers, so WSL git and the run-check checks work); `core-cpp-percommit` (detached, for per-commit checkouts); `core-cpp-bench-base` (detached at origin/master) |
| core-cpp under test | `v0.2.0` = `ec47681`: fetched by the pin in local-gate, and `D:\core-cpp-wt-fc` checked out at the tag for the `CPM_core-cpp_SOURCE` legs |
| Compiler cache | OFF in every build (fastcached#1597). The WSL fastcache-cc is `0.2.0-748-g73fb0457` and the Windows one `0.2.0-739-gd4451c3b`; neither contains ca8dfc32 |

## Commits

| # | SHA | Subject |
|---|---|---|
| 1 | `b5f901e7` | tui: the live-stats view links core-cpp's terminal UI, and vendor/endo goes |
| 2 | `c1ed7388` | async, net: the moved API's members are spelled as core-cpp spells them |
| 3 | `40f1576f` | async, net: the coroutines, the event loop and every socket are core-cpp's |
| 4 | `8ad937a8` | cache: an idle expiry cycle parks once per interval, and a stop cancels it |
| 5 | `7b2ab0a2` | consensus: PeerTransportOptions::stopWakeBound goes, because nothing reads it |
| 6 | `d39f423d` | net: every listener a loop drives binds as ClientListenOptions says |
| 7 | `69fddee2` | test(smoke): a failing sccache smoke prints what the daemon and sccache logged |

All seven are signed off. The WIP commits were rewritten into 3, 4 and 5. The pin moved to v0.2.0 in
commit 1. Commit 4 had a RED of 599 resumptions against 0. Commit 6's RED was predicted and observed
as a compile failure in `NativeListen_test.cpp` only. For commit 7, a forced failure printed only its
reason before the change, and both logs after it.

## Gates

| Gate | Tree | Result |
|---|---|---|
| local-gate: gate-clang-debug + clang-tidy 22.1.8 + clang-format 22.1.8 | d39f423d | passed, 5137/5137; `LOCAL GATE PASSED` |
| local-gate: gate-gcc-release | d39f423d | passed, 5136/5136 |
| hygiene | d39f423d | passed, 243/243 |
| clang-asan-ubsan | b51784c1 | passed, 5120/5120 |
| TSan gate | b51784c1 | passed; all 4 targets clean |
| clangcl-debug | b51784c1 | passed, 5060/5060 |
| cl-debug | b51784c1 | passed, 5060/5060 |
| clangcl-release (`_mt` launcher) | b51784c1 | passed, 5059/5059 |
| cl-release (built to reproduce the smoke) | d39f423d | built; the redis smoke passed in every local run (sccache 0.14.0 and 0.18.0, including 10/10 at `--threads=4`) |
| script hygiene after commit 7 | 69fddee2 | passed, 22/22 |
| per-commit clang Debug, full ctest, WSL mirror | b5f901e7 | passed, 5341/5341 |
| | c1ed7388 | 5340/5341; `e2e-helpers-selftest` timed out under load and passed alone at that commit (76.7 s) |
| | 40f1576f | passed, 5117/5117 |
| | 8ad937a8 | passed, 5118/5118 |
| | 7b2ab0a2 | passed, 5118/5118 |
| CI (PR #1598) | 69fddee2 | 30 passed, 2 skipped (Deploy to Pages: not for a PR; Draft GitHub release: `v*` tags only) |

b51784c1 differs from d39f423d only in clang-format whitespace in three files. That is the finding
of CI's *Check C++ style*, and it is now fixed inside commits 4 and 5.

The first local-gate run failed on `byte-order-qualifier`, which timed out at 60 s under
`--parallel 32` beside another session's builds, at a load average around 44. Run alone it took 15 s.
The re-run used `FASTCACHE_GATE_JOBS=16`.

## Findings on the way

1. **SleepUntil_test**: fastcached has no copy of it after the migration. The swap deletes it with
   `src/FastCache/Async`, and that copy is the one core-cpp's CHANGELOG names.
2. **A v0.2.0 test adjustment**: `the dashboard's event source parks rather than resolving inline`
   read `pendingTimers() >= 1` and got 0. A `schedule(now)` made in step 2 is found due by the same
   turn's step 5 and queued, so after one tick it is a submission (G2). The check now counts both.
   This is the loop's documented contract, not a core-cpp defect. The fix is folded into commit 3.
3. **Windows-cl-release `sccache-smoke-redis`**: it failed once on d39f423d. sccache 0.18.0 stored
   both objects, 0 write errors and 0.226 s average write, then missed on the second compile. It
   passed on the re-run of the same job, on master (2 runs), and in the same run's other three
   Windows jobs. Locally it passed on cl-release and clangcl-release with sccache 0.14.0 and
   0.18.0, and 10/10 at `--threads=4`. No passing CI run prints smoke stats, so there is nothing
   to compare the 0.226 s with. Reading core-cpp's cross-thread submit and IOCP wake coalescing
   found no lost wakeup. Commit 7 is there so the next failure shows which side lost the GET.
   Sampling afterwards: five consecutive CI re-runs on 69fddee2 passed (jobs 107547893185,
   107562069557, 107566994281, 107571795954, 107577488716), and so did 50/50 local runs at
   `--threads=4` with sccache 0.18.0.
4. **The scratch-path collision**: another agent's endo PR body overwrote
   `scratchpad\pr-body.md`, and that body reached #1598 for about a minute before it was restored.
   Scratch files are now under `scratchpad\C3C4\`.

## Parity

Taken as it stands from `task-perf-report.md` (impl-perf, recorded by team-lead). Per request,
core-cpp makes fewer syscalls, allocates less, and spends no more total CPU than fastcached's
reactor. User CPU is 0.3-1.2 us higher, and the largest single item is core-cpp#47. The PR
body carries the tables and the method.

## Earlier benchmark numbers, superseded

The interim getbench figures in this lane (-6.8% on v0.1.0, -2.5% on the perf branch) were built
with the compiler cache against a changed core-cpp tree, and are void under fastcached#1597. The
9-round A/A (geomean -0.9%) overlapped other lanes' builds: 12 of 51 load samples caught build
processes, with a peak load of 18.25. None of these is used in the PR.

## core-cpp v0.2.1 heads-up: close() resuming through the loop (G2)

A grep only; nothing is changed.

**Nothing in fastcached needs `close()` to resume a parked waiter inline.** Every site that mentions
inline resumption is defending against it, and stays correct when resumption moves to the next turn:

- *Hop to the reactor before closing*, so that inline resumption cannot run connection code on a
  foreign thread: `RaftPeerTransport.cpp` 520-553 (`CloseSockets`), `RaftPeerServer.cpp` 516-525
  (`Shutdown`), `FrameEndpoint.cpp` 2723-2731. Under G2 the hop is no longer needed for that reason,
  though it is still where a socket should be closed from.
- *Lock released before closing*, because the resumed coroutine takes the same lock:
  `RaftPeerTransport.cpp` 529-553, `RaftPeerServer.cpp` 501-512 (the vector copy),
  `FrameEndpoint.cpp` 543-556 (`CloseOverdue`), 589-612 (`CloseExpiredDeferrals`), 615-630 (`CloseAll`).
- *Counted before the close*: `FrameEndpoint.cpp` 546-553, which is still right when the observer runs later.
- *Close then reset*: `RaftPeerTransport.cpp` 282/293/314/334. The sender is the socket's only
  user, and it is running, not parked, at each of them.

**The tests found by a close-then-assert scan all turn the loop first.** `Settle`, `DrainUntil`,
`DrainSession` or `syncRun` runs between the close and the assertion, or the assertion reads a
value captured before the close. The sites: `LiveEventSource_test.cpp:879`,
`LiveSession_test.cpp:456/1226/1254/1274`, `LiveSourceRig.hpp:593`,
`TerminalEventStream_test.cpp:509/545/852`, `FrameEndpoint_test.cpp:1355/1828`.
`FrameEndpoint_test.cpp:2644` and `RaftPeerServer_test.cpp:1083` describe inline resumption in
their reasoning without depending on it.

**What depends on the loop picking up a closed waiter**, rather than on inline completion, and
should be re-run first on v0.2.1:
- `ReactorServerLoop.cpp`'s teardown: `Shutdown()` then a single `runUntilIdle()` per reactor.
  This relies on `runUntilIdle` not stopping at a turn that queued closed waiters, which is
  v0.2.0's idle-turn fix.
- The doc comments that say "`Close` resumes INLINE on epoll and kqueue"
  (`RaftPeerTransport.cpp`/`.hpp`, `RaftPeerServer.cpp`, `FrameEndpoint.cpp`,
  `RaftPeerServer_test.cpp`). They become false and should be reworded with the re-pin.

## The v0.4.0 round

The pin moved to `GIT_TAG v0.4.0` / `VERSION 0.4.0`, folded into commit 1. Commits 1 and 3 now say v0.4.0.

Commit 12 is `9452d2a8`, "cli: the live-stats stop watch waits on the reactor, whatever thread started it". `RunStopWatch` now runs `co_await ResumeOn { *parts.reactor }` before `Stopped()`, with a one-line comment. That is the hardening the 0.4.0 review asked for.

**Breaking and known-issue notes:**
- **AsyncQueue resume target:** no behaviour change, because every queue is built over the reactor and every pop parks there.
- **`resumeSoonOn`:** not called in fastcached.
- **Known issue core-cpp#51, `DetachedTask` under clang-cl at `-O0`:** documented in the PR, not worked around. Three detached tasks hop from another thread onto the reactor and finish without suspending again:
  - `FrameServer::Shutdown`'s close task;
  - `RaftPeerServer`'s shutdown task;
  - `RaftPeerTransport::CloseSockets`.

**Gates:**

| Gate | Result |
|---|---|
| local-gate clang-debug / gcc-release | 5138/5138 and 5137/5137 |
| hygiene | 243/243 |
| asan | exit 0; the log was lost to the WSL failure |
| TSan | clean on all four targets; re-run on a fresh tree after WSL was restored |
| clangcl-debug / cl-debug / clangcl-release | 5061, 5061, 5060, all passing |
| CI, Build 36153416003 | 26 passed, 1 skipped |

FrameEndpoint sampling on v0.4.0: Linux-clang-release passed 11 of 11 (jobs 108142201229 to 108259641761).

## The v0.3.0 round

The pin moved to `GIT_TAG v0.3.0` / `VERSION 0.3.0` and was folded into commit 1; commits 1 and 3 now say v0.3.0 in their messages. The branch was force-pushed with a lease.

Commit 11 is `093cede2`, "docs: the socket slot contract is core-cpp's, and it ends the process in every build". It changes no code:
- AGENT.md, `EndpointWriters.hpp`, `wire-and-protocol.md` and `distributed-compilation.md` said the slot guards were "Debug only".
- They also named `Net/ReadSlot.hpp`, `Net/WriteSlot.hpp` and the read- and write-slot-guard canaries, all deleted by commit 3's swap: drift from the swap itself.
- `scripts/agent-md-budget.txt` records 1633. CI's style job refused the first push, `4e662a15`, on that ratchet.

**How the CHANGELOG 0.3.0 migration notes were applied:**
- **Second park aborts in Release:** audited, no change needed.
  - The peer watch re-arms only after `SettleWatch` sees it finished.
  - The pulse is reclaimed before the reply.
  - `CompileCacheHandler`'s and `RedisResp`'s watches take the slot back with `cancelRead()`, which frees it before returning (`PosixSocket.cpp:127` at v0.3.0).
- **Callback-position ordering:** `AbandonIfPeerGone`'s single yield is right again.
  - No departure-counter test was added. The interleaving needs two readiness reports in one epoll batch in a set order, and `NodeIoLoop` has only a real backend. Ruled: if it can't be forced, don't.
- **Spawned roots:** `EventLoop::spawn` is not used here.
- **Install:** `CORE_CPP_INSTALL` defaults off under CPM, and fastcached exports no core-linked target.
- **`IHostScheduler`:** not used here.

**Gates.** Every tree was fresh and the compiler cache was off.

| Gate | Result |
|---|---|
| local-gate: gate-clang-debug, with tidy and format 22.1.8 | 5138/5138 |
| local-gate: gate-gcc-release | 5137/5137 |
| hygiene | 243/243 |
| clang-asan-ubsan | 5121/5121 |
| TSan gate | clean on all four targets |
| clangcl-debug | 5061/5061 |
| cl-debug | 5061/5061 |
| clangcl-release | 5060/5060 |
| CI, Build 36074384689 | 26 passed, 1 skipped |

The three Windows legs ran on `4e662a15`, whose code is identical to `093cede2`.

**The FrameEndpoint flake:**
- On v0.2.1: 1 failure in 8 runs. The later 6 re-runs of Linux-clang-release on `a8053d48` all passed, so no timeline was captured.
- On v0.3.0 the leading suspect, a Release double-arm silently displacing a parked operation, now aborts and names the socket.
- Linux-clang-release on `093cede2` passed 6 of 6 samples (jobs 107883656326 to 107925892551).

## The v0.2.1 round

The pin moved to `GIT_TAG v0.2.1` / `VERSION 0.2.1` (core-cpp `a6d49f2`), amended into commit 1.
The branch was pushed with `--force-with-lease`.

| # | SHA | Subject |
|---|---|---|
| 1 | `97ff9ecd` | tui: the live-stats view links core-cpp's terminal UI, and vendor/endo goes (pin v0.2.1) |
| 2 | `680514e6` | async, net: the moved API's members are spelled as core-cpp spells them |
| 3 | `6d9f4b3f` | async, net: the coroutines, the event loop and every socket are core-cpp's |
| 4 | `efac3d99` | cache: an idle expiry cycle parks once per interval, and a stop cancels it |
| 5 | `867449d0` | consensus: PeerTransportOptions::stopWakeBound goes, because nothing reads it |
| 6 | `d1db4897` | net: every listener a loop drives binds as ClientListenOptions says |
| 7 | `a4a037bf` | test(smoke): a failing sccache smoke prints what the daemon and sccache logged |
| 8 | `d2fbd3d2` | cli: live-stats ends when a read finds the terminal's input gone (`inputClosed()`; red, then green) |
| 9 | `c7106927` | net, consensus: a close resumes what it woke through the loop, and the comments say so (16 comments, no code) |
| 10 | `a8053d48` | test(node): the pipelined-watch case prints a timeline of its connection when it fails (test only) |

**Gates on `c7106927`**, each from a fresh tree with the compiler cache OFF:

| Gate | Result |
|---|---|
| local-gate: gate-clang-debug, with clang-tidy 22.1.8 and clang-format 22.1.8 | 5138/5138 |
| local-gate: gate-gcc-release | 5137/5137 |
| hygiene | 243/243 |
| clang-asan-ubsan | 5121/5121 |
| TSan gate | clean on all four targets |
| clangcl-debug | 5061/5061 |
| cl-debug | 5059/5061; two self-tests timed out under WSL load above 100 and passed alone |
| clangcl-release | 5060/5060 |
| CI on `c7106927` | 30 passed, 2 skipped by condition, after one re-run of Linux-clang-release |

**Open: `FrameEndpoint_test` "A request pipelined while a watched reply is being written is still served".**

- **The failure.** It failed once in 2 CI runs (job 107710877953) at `CHECK_FALSE(client.ReadReply().empty())`, after `Entered() == 2` had been observed.
  - The failing run took 21.27 s; the case passes in 1.27 s, and about 1.25 s of that is the sweeper tick at teardown.
  - The extra 20 s fits two stories:
    - late pickup of request 2, close to WaitFor's 15 s, followed by reply 2 stuck until the sweep closed it (5 s answer deadline plus up to 1.25 s);
    - reply 2 stuck, and then the 5 s `DrainWithin` stop ceiling, meaning a frame that close() did not wake. The EventLoop.cpp 1002-1022 NDEBUG slot displacement would look like this.
- **What did not reproduce it:**
  - Release with assertions, 0 failures in 200 runs.
  - 350 local passes.
  - A core-cpp reproduction (scratch `C3C4/repro-g2-watch-write`): a peer watch and a write parked on one socket, with the watch settling mid-write in about 80% of seeds. About 181,000 seeds ran across Release with assertions and Debug, deterministic and threaded, all cores and `taskset -c 0`, with no stall. That shape does not reach the CI failure.
- **What will tell the two stories apart.** Commit 10 makes the case print a timeline on failure:
  - each socket operation, inline or parked and when it settled;
  - each close;
  - the endpoint's log;
  - the client's steps.

  Linux-clang-release on `a8053d48` passed 6 of 6 samples (jobs 107789713652 to 107867233140). That makes 1 failure in 8 CI runs on v0.2.1, and no timeline has been captured yet.
- **A reading of the v0.2.0 to v0.2.1 diff** found no lost-wakeup candidate: readiness callbacks, computeTimeout, narrowing and widening, ParkId reuse, double queueing, and lifetime tokens.

**fastcached steps that assumed a resumption had already run** (read under v0.2.1's ordering, in which a settled I/O waiter resumes one queue hop after its readiness callback):
- **`AbandonIfPeerGone`** (`FrameEndpoint.cpp` 1416-1462) yields once (`ResumeOn`, 1445) and relies on a watcher whose wake is already queued running before that yield returns.
  - Under v0.2.1 the ordering A, C, Y, W is possible: A is the connection resuming, C the readiness callback that settles the watch, Y the yield, W the watcher. It needs A queued ahead of C in one drain, for example both readinesses in one epoll batch.
  - Y then reads `gone == false` for a peer the backend has already reported gone. The reply is written to a departed peer, and `FramePeerWatchDepartures` and `WorkerJobsAbandonedClientGone` miss it; `...Observed` still counts it.
  - The result is a counter and a wasted transfer; nothing hangs. Not changed yet: the lead decides between two yields in fastcached and an ordering statement in core-cpp.
- **Tolerant, latency only:** `AwaitWatchQuiet` / `SettleWatch` (1289, 1481), `SettlePulse` / `ReclaimFromPulse` (1574, 1661) and `ExplainIfSwept` (1721). Each polls a `finished` flag in 5 ms steps within a bound.
- **Neutral:** the sweeper (`CloseOverdue` 513, `CloseExpiredDeferrals` 598) and `Rearm` (392). The Raft and `CompileCacheHandler` G2 sites are close() sites only.

**Handed to the 0.3.0 lane:** under NDEBUG, a second read or write park displaces the slot silently (`EventLoop.cpp` 1002-1022). A waiter displaced that way is never completed, not even by `close()`.

## Housekeeping

- Worktrees `core-cpp-percommit` and `core-cpp-bench-base` still exist. Removing a worktree writes
  to `D:\fastcached\.git`, which this lane is not allowed to touch; they are for the lead or the
  user to remove.
- `D:\core-cpp-wt-fc` (core-cpp at v0.2.0, now checked out at v0.2.1 `a6d49f2`) was added with `git -C D:/core-cpp worktree add`.
