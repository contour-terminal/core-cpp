# coro

The C++23 coroutine vocabulary. Namespace `core::coro`, directory `src/core/coro/`, target
`core::coro` (header-only).

!!! note "Status"
    Not imported yet. Task A5 of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md)
    imports contour's `src/coro` at `6777ff05`, and Task B1 merges fastcached's executors and
    ownership rules into it.

## Planned contents

- From contour: `Task<T>` with the stop token in its promise, `UniqueCoroHandle`, cancellation,
  `whenAll`, `whenAny`.
- From endo: `Generator`.
- From fastcached: `DetachedTask`, `syncRun`, `ParkedWork`, `IExecutor`, `ResumeOn`,
  `ThreadPoolExecutor`, `AsyncQueue` with a stop-aware `pop`, and an awaiter that takes ownership
  of the task it awaits.

Depends on the standard library only. Under WebAssembly everything builds except
`ThreadPoolExecutor.hpp`, which refuses to compile without threads; `<stop_token>` comes from
libc++'s experimental library on libc++ 17 to 19. See
[Coroutines and lifetimes](../design/coroutines-and-lifetimes.md).
