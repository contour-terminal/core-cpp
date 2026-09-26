# Review: perf branch 4a9564a..7f31670 (kept PosixSocket registration, idle fix)

Reviewer: read-only, in D:/core-cpp-wt-perf at 7f31670. Nothing was built or run here. Every
finding below comes from reading the code at the cited lines; none comes from a run.

## Verdict: APPROVE, with one should-fix

The persistent registration holds up against every lifetime and fd-reuse scenario in the brief. The
idle fix cannot hang a runUntilIdle that returned before, except with `dispatchBatch == 0`. One test
case does not distinguish on POSIX. The remaining items are nits.

## Verified sound (no finding)

1. **fd reuse / report racing close.** A backend batch exists only inside `IoBackend::wait`, and
   callbacks only enqueue, so no close can happen while a batch is live. `_watchesToNarrow` is
   keyed by fd number, but it is consumed at EventLoop.cpp:321, right after the wait and before
   any user code runs (step 5 and the next drain come later), so it cannot name a reused number.
   The watch is dropped in `notifyHandleClosing` before `::close` (PosixSocket.cpp:105), and
   `detach` withdraws it from the batch (#475). Ready entries for callback parks carry `ParkId`s,
   which are never reused, so a stale entry for the old socket's park finds nothing
   (`runDueCallback`). `releaseWatchSlot` compares slots by id (EventLoop.cpp:1170ff), so a park
   of the old fd released after a new watch exists for the same number frees nothing on it.
   Traced: close fd 7 while its parks are live, a new socket gets fd 7 and parks in the same turn,
   and a stale ready entry from the old registration is then drained. The result is correct.
2. **Watch vs socket lifetime.** `PosixSocket` is non-movable, and every path that disposes of
   its fd goes through `close()` -> `notifyHandleClosing`: the destructor, `close()`, and
   adoptSocket and adoptFd, which construct a PosixSocket that owns the fd. No path releases the
   fd without announcing it. In teardown, sockets destroyed in steps 2-5 drop their own watches,
   and the leftover detach loop runs after `_roots.clear()` while `_watches` is still alive.
   Watched parks are never `attached`, so `abandonParkedWork` does not double-detach them.
3. **Narrowing and lost wakeups.** Parks are filed in step 2, before the wait, and narrowing runs
   after it, so a slot filled this turn is always seen by the narrow. Every re-arm after a narrow
   is level-triggered on all three backends: epoll has no EPOLLET, kqueue has no EV_CLEAR, and
   poll is level-triggered. An ADD or EV_ADD after muting to None therefore reports the current
   state, and no wakeup is lost. In kqueue, narrowing Read|Write->Read issues EV_DELETE for
   EVFILT_WRITE, whose ENOENT is tolerated. Muting to None deletes both filters. The dup logic
   still covers a PerPark registration that coexists with the watch on the same fd.
4. **Writer woken on readable: no busy loop.** A report with no reader queues a narrow, which
   drops Read (keep=None) and leaves only Write armed. So a parked writer that keeps getting
   EAGAIN gets at most one spurious wake before the readable reports stop. With a reader
   parked, each readable report is consumed by that reader, so the spurious `send` is bounded by
   the rate data arrives. Callback parks persist across spurious wakes (`runDueCallback`), so
   the writer slot stays valid through this.
5. **Idle fix.** `_ready.empty()` is read at EventLoop.cpp:360, after the closed parks and
   `fireExpiredTimers` have queued their work, and nothing queues after that point. Any turn that
   leaves work behind also drains up to `dispatchBatch` of it, so the queue empties unless work
   re-queues itself. Work that re-queues itself had `drained > 0` before this change and was not
   idle then either. (See the `dispatchBatch == 0` nit.)
6. **Thread affinity.** `_watches` and `_watchesToNarrow` are touched only by
   registerPark, unregisterPark, cancelPending and notifyHandleClosing, all of which assert the
   loop thread, and by the backend callbacks, which run on the loop thread inside wait. There is
   no new cross-thread state.

## Should-fix

- **src/core/net/ClosedParkIdle_test.cpp:117 -- the socket case does not distinguish on POSIX.**
  `PosixSocket::close()` calls `settleRead` (PosixSocket.cpp:120), and that calls
  `IoAwaitable::complete`, which runs `waiter.resume()` inline (IoAwaitable.hpp:309-318).
  `readOnce` has therefore finished before the `runUntilIdle` on the next line runs. On Linux
  and macOS, `CHECK(outcome.resolved)` passes on the unfixed loop. The file header ("asks ONE
  `runUntilIdle` to finish the job") says otherwise. Only the listener case proves the fix, because
  `AcceptLoop` parks through `waitReadable`, a coroutine park, and so goes through
  `_closedParks`. Fix: either `CHECK_FALSE(outcome.resolved)` straight after `close()` and SKIP
  with a reason where the transport resolves inline, or park the read through
  `loop.waitReadable(fd)` so it takes the closed-park path. As it stands, the case counts as
  coverage on POSIX and covers nothing there. It may still distinguish on WFMO, per its own comment.

## Nits

- **EventLoop.cpp:1086 (resolveCancel), and likewise :682 and :1350 -- a cancelled writer does not
  narrow writability.** The slot's `Interest::Write` result is discarded. The later
  `unregisterPark` then gets `None` back, because `park.watched` is already false, so the
  `narrowWatch(..., Read)` at :1016 never runs. Write stays armed, and the next wait reports a
  writable socket with no writer: one spurious dispatch and one narrow, or none at all while a
  parked reader's readable reports mask it. This is bounded, not a spin, but it is the per-turn
  report the `HandleWatch` comment says is avoided. Fix: give resolveCancel the same
  `hasInterest(..., Write)` -> `narrowWatch(Read)` that unregisterPark has.
- **EventLoop.hpp:767 and async-and-net.md:614, "only while both directions are parked at once".**
  `onWatchReadable` also queues the writer when no reader is parked and Read is still armed from
  an earlier read, for example a peer that pipelines while a large reply is parked. It is one
  spurious wake before the narrow, so the claim is slightly too strong.
- **SocketRegistration_test.cpp:263 -- `REQUIRE(counting.interestChanges > 2)` does not show that
  the write parked.** The reader side alone can reach three changes: arm Read, then a report with
  no reader parked narrows it to None, then a re-arm. A more direct precondition would count
  `setInterest` calls whose mask includes Write.
- **EventLoop.hpp:142 -- `dispatchBatch = 0` now hangs `runUntilIdle`.** Before, a zero batch
  returned idle with work still queued. Now `_ready` never empties. The loop was already useless
  with a zero batch, but nothing rejects it; an assertion in the constructor would make the new
  failure mode explicit.
- **PosixSocket.cpp:96 -> :105 -- an extra kernel call on close while a write is parked.**
  `takeWrite` -> `unregisterPark` -> `narrowWatch(Read)` issues an MOD, and immediately after it
  `dropWatch` issues a DEL. This costs performance only.
- **.agent/reference/provenance.md:232 (PosixSocket.cpp) and the rows for EventLoop.cpp and
  ScriptedBackend.hpp record no post-import note for this change.** Other rows record each task's
  divergence ("Task B10: ..."), and the file warns that a row is not a licence to re-sync by
  overwriting. A re-sync from contour or fastcached would silently return to per-park
  registration. SocketRegistration_test would catch that; the table would not.
- **onWatchReadable / onWatchWritable are `noexcept` and call `push_back`.** An allocation failure
  terminates the process, the same shape as `onParkReady`'s existing enqueue. No action is needed
  beyond knowing it.

## CHANGELOG / docs

The Fixed, Changed and Added entries are present and accurate. The figures agree: 9.3-10.5 -> 5.1 us
is a 45-51% cut, as the rules text says. `docs/modules/net.md` and the rule text describe the
mechanism correctly, apart from the "both directions" nit. The provenance rows for the two new
tests are present.
