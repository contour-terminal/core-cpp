# Coroutines and lifetimes

core-cpp's coroutines are C++23 coroutines driven by an event loop. Most of the defects this
design exists to prevent are silent: a coroutine frame that is parked and never resumed or freed,
a frame freed twice, a coroutine resumed on the wrong thread, a view that outlives what it views.

!!! note "Status"
    `core::coro` and `core::net` are imported in Tasks A5 and A6 and merged with fastcached's
    design in Phase B. Task B13 completes this page with the rules as implemented.

## The rules

- **Coroutine parameters are owning values.** A reference or a `std::string_view` parameter
  names storage the caller may destroy while the coroutine is suspended.
- **A `Task` owns its coroutine**, and the awaiter that awaits it takes that ownership, so a
  temporary `Task` cannot destroy the coroutine across a suspension.
- **An event loop resumes what it parks, or frees it, and it frees only what nothing else owns.**
  Ownership travels with the park (`ParkedWork`); what is freed at teardown is the root of a chain
  that nothing else owns, never a frame somebody still holds.
- **Objects registered with a loop are destroyed before it**, on its thread or with it stopped.
- **A socket has one read and one write operation at a time**, and `close()` touches nothing of
  the socket after it completes a pending operation, because completing it can resume the
  coroutine that owns the socket and destroy it.
- **A value returned from a decoder owns its bytes**; a type that borrows says so by its name
  (`*View`).
- **A profiling zone never spans a `co_await`.**

Each rule, with the defect that made it one, is in
[`async-and-net.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/async-and-net.md).
