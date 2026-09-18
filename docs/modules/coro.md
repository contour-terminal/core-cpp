# coro

The C++23 coroutine vocabulary. Namespace `core::coro`, directory `src/core/coro/`, target
`core::coro` (header-only).

!!! note "Status"
    Only `Generator` exists yet. Task A5 of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md)
    imports contour's `src/coro` at `6777ff05`, and Task B1 merges fastcached's executors and
    ownership rules into it.

## Generator

`<core/coro/Generator.hpp>` has `core::coro::Generator<T>`, a lazy, single-pass range a coroutine
fills with `co_yield`, imported from endo (`src/platform/Generator.hpp` at `f774a210`).

- It is `std::generator<T>` where the standard library has `<generator>` and is not libstdc++,
  and otherwise `core::coro::detail::GeneratorFallback<T>`, a small implementation over
  `<coroutine>`. libc++ has no `<generator>` yet (emsdk 3.1.56 ships libc++ 17), and GCC 14
  reports a null `coroutine_handle` inside libstdc++'s own `std::generator` at `-O2`
  (`-Wnull-dereference`), which the zero-warning policy makes an error. In practice MSVC uses
  `std::generator` and everything else the fallback.
- The choice is read from `<version>`, which the header includes first, so every translation unit
  makes the same one. endo's copy tested `__cpp_lib_generator` before including anything, so the
  answer depended on what a file included first; a virtual function returning a `Generator` could
  then have two return types in one program.
- `CORE_GENERATOR_FORCE_FALLBACK`, defined the same way in every translation unit, selects the
  fallback everywhere.
- The fallback is always defined and is tested on every platform. Each yielded value lives in the
  coroutine frame until the next increment, so yield owning values. Breaking out of the loop
  destroys the suspended frame.

## Planned contents

- From contour: `Task<T>` with the stop token in its promise, `UniqueCoroHandle`, cancellation,
  `whenAll`, `whenAny`.
- From fastcached: `DetachedTask`, `syncRun`, `ParkedWork`, `IExecutor`, `ResumeOn`,
  `ThreadPoolExecutor`, `AsyncQueue` with a stop-aware `pop`, and an awaiter that takes ownership
  of the task it awaits.

Depends on the standard library only. Under WebAssembly everything builds except
`ThreadPoolExecutor.hpp`, which refuses to compile without threads; `<stop_token>` comes from
libc++'s experimental library on libc++ 17 to 19. See
[Coroutines and lifetimes](../design/coroutines-and-lifetimes.md).
