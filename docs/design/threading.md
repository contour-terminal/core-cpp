# Threading

!!! note "Status"
    This is the design of the merged event loop, Part I §2 of the
    [design spec](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md).
    It is implemented in Phase B; Task B13 completes this page.

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

## One turn of the loop

1. Swap the inbound queue; run the posted functions; resolve cancellation requests.
2. Drain the ready queue: this is where coroutines resume.
3. Refresh the clock and compute how long to wait.
4. Wait on the backend.
5. Refresh the clock and fire the expired timers in order.

## Teardown

`~EventLoop` asserts that it runs on the loop's thread or with the loop stopped, requests a stop
from its root stop source, drains in bounded passes, frees the work nothing else owns, destroys
the tasks it spawned, and unregisters its wakeup. Objects registered with a loop must be
destroyed before it.

## Without threads

Under single-threaded WebAssembly there is no thread to block: a host-driven backend never
waits, and the browser's event loop pumps the loop through an `IHostScheduler`. `run()` and
`blockOn()` are not available there. See [Portability](portability.md).
