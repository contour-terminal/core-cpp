# Threading

!!! note "Status"
    The turn, the teardown and guarantees G1 to G3 and G5 are implemented (Task B4). G4 arrives
    with the IOCP backend (Task B7), and the socket-side half of G5 with the socket contract
    (Task B6). This is Part I §2 of the
    [design spec](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md).

## One loop, one thread

An `EventLoop` runs on one thread and drives exactly one `IoBackend`. Its guarantees, asserted on
every backend:

| | Guarantee |
|---|---|
| G1 | exactly one thread dequeues a loop or a completion port |
| G2 | every coroutine resumes in step 2 of `runOnce` |
| G3 | helper threads only post |
| G4 | a socket is associated with exactly one port |
| G5 | every socket, listener and dial destructor asserts that teardown is serialised with dispatch |

**Backends dispatch, the loop resumes.** Backend, completion, stop and thread-pool callbacks only
enqueue work; nothing resumes a coroutine from inside a callback, on any operating system.

`run()`, `runOnce()` and `blockOn()` each claim the loop's worker identity for their duration, so
`running()` and `isOnWorkerThread()` answer without a loop having to remember to say so. `run()`
is deliberately not virtual: a loop that could enter its turn without claiming would answer
`teardownIsSerialisedWithDispatch()` with `true` from every thread, and every guard built on that
would stay green while checking nothing.

What another thread may do to a loop is `post()`, `submit()`, `schedule()`, `requestCancel()` and
`stop()`. Each hands work over through the inbound queue and wakes the backend; none of them
touches the loop's own containers, and none of them runs the work. Everything else is for the
loop's thread.

## One turn of the loop

1. Swap the inbound queue: run the posted functions and submissions, then resolve cancellation
   requests by live `ParkId`.
2. Drain the ready queue — **this is the one place a coroutine resumes, and the one place a timer
   callback is called**, and it is bounded by `EventLoopOptions::dispatchBatch` so work that
   re-queues itself cannot starve the rest.
3. Refresh the clock, then compute how long to wait.
4. Wait on the backend, which dispatches readiness.
5. Refresh the clock, then fire the expired deadlines, soonest first and FIFO on a tie.

Readiness dispatched in step 4 and deadlines fired in step 5 are **resumed by the next turn's step
2**. That is what makes G2 a thing the loop can state rather than a thing each backend has to be
trusted with.

**There is one deadline mechanism, not two.** `delay()` parks a coroutine and `addTimer()` parks a
callback with no frame behind it, in the same park table, on the same heap, with ids from the same
never-reused counter; step 5 fires both and step 2 runs both, so their order across the two kinds
is the heap's — soonest first, then by arming sequence. Nothing polls: an armed timer is what
bounds the wait computed in step 3, which is why `DeadlineTimer` and `interruptibleSleepUntil` cost
one wake-up each rather than one per poll interval.

Two orderings in that list are load-bearing rather than incidental:

- **Cancellations resolve in step 1, before the drain.** Resolving a cancel is what puts the
  cancelled flow into the ready queue; after the drain it would sit there while steps 3 and 4
  computed a timeout and blocked, so a cancel from another thread would take effect only when
  something unrelated woke the loop.
- **The clock is refreshed twice, and neither call is decoration.** Step 3's decides the timeout
  the backend is given, so a turn that has already spent time does not wait for it again; step
  5's decides which deadlines are due in *this* turn rather than the next. `ClockRefresh_test.cpp`
  has one case per call, because a single case passed with either of them removed.

## Teardown

`~EventLoop`, in order:

1. Assert that teardown is serialised with dispatch (G5).
2. Request a stop from the root stop source, then move every borrowed park to the ready queue.
3. Run bounded drain passes, so cancelled flows unwind and run their cleanup.
4. Abandon to a fixpoint: free the chains nothing else owns, looping because freeing a chain can
   park again.
5. Destroy the spawned roots.
6. Unregister the wake, so a host that still holds a scheduled pump finds nothing to call.

**What the loop owns is freed; what it borrows is resumed.** A chain the loop owns is a
`DetachedTask`, which carries no stop token — a detached flow has no awaiting coroutine to inherit
one from — so resuming it would not cancel it, it would run the rest of its body on a loop that is
being destroyed. A chain the loop borrows belongs to a `Task` somebody holds, and that owner set a
stop token on it, so resuming it is what makes the frame unwind.

Objects registered with a loop must be destroyed before it.

## Without threads

Under single-threaded WebAssembly there is no thread to block: a host-driven backend never waits,
and the browser's event loop pumps one turn at a time through an `IHostScheduler`. After every
turn the loop tells the backend its next deadline, and the backend asks the host to pump then.

`run()` and `blockOn()` are **compiled** there and assert at runtime, rather than being removed: a
consumer reaches them by mistake, not by design, and a symbol that is simply absent fails at link
time in somebody else's build with nothing to say why. `core-cpp.hostdriven-canary` is what proves
both refusals still fire. See [Portability](portability.md).
