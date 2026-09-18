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

## StopToken

`<core/coro/StopToken.hpp>` has `core::coro::StopToken`, `StopSource`, `StopCallback<F>` and
`noStopState`, the vocabulary of cooperative cancellation.

- They are `std::stop_token`, `std::stop_source`, `std::stop_callback<F>` and `std::nostopstate`
  where the standard library defines `__cpp_lib_jthread`, and otherwise
  `core::coro::detail::StopTokenFallback`, `StopSourceFallback`, `StopCallbackFallback<F>` and
  `NoStopStateFallback`. libc++ 17, which emsdk 3.1.56 ships, has `<stop_token>` only behind
  `-fexperimental-library`, and core-cpp adds no compile flag to its consumers. contour's copy
  (`src/coro/Cancellation.hpp` at `6777ff05`) aliased `std::` and refused to compile otherwise.
- The fallback has the standard semantics. `request_stop()` returns true exactly once, and that
  call runs every registered callback once, on the requesting thread, before it returns. A
  callback constructed on a stopped token runs in its constructor. `~StopCallback` deregisters,
  and waits while its callback runs on another thread, but not when it is called from inside that
  callback. `stop_possible()` is false for a token without a stop state, and for one whose
  sources are all gone without a request. Copies share state. Its members carry the standard's
  names (`request_stop`, `stop_requested`, `stop_possible`, `get_token`, `callback_type`), so code
  compiles against either branch.
- Under single-threaded WebAssembly the fallback keeps plain state: no atomics, no lock and no
  wait, since a running callback always runs on the calling thread there. Elsewhere a mutex
  guards its callback list, and no lock is held while a callback runs, so a callback may request
  stop again or register another callback.
- The choice is read from `<version>`, which the header includes first, so every translation unit
  makes the same one.
- `CORE_CORO_FORCE_STOP_TOKEN_FALLBACK` selects the fallback where the standard library has
  `<stop_token>`. It changes what every `Task` promise holds, so it must be defined the same way
  in every translation unit of a program. The test binary `core-cpp-coro-fallback-test` (ctest
  `core-cpp.coro-fallback`) builds the module's tests with it, so the fallback is tested on every
  platform, under ThreadSanitizer too.

## Planned contents

- From contour: `Task<T>` with the stop token in its promise, `UniqueCoroHandle`, cancellation,
  `whenAll`, `whenAny`.
- From fastcached: `DetachedTask`, `syncRun`, `ParkedWork`, `IExecutor`, `ResumeOn`,
  `ThreadPoolExecutor`, `AsyncQueue` with a stop-aware `pop`, and an awaiter that takes ownership
  of the task it awaits.

Depends on the standard library only. Under WebAssembly everything builds except
`ThreadPoolExecutor.hpp` (Task B1), which refuses to compile without threads; where libc++ has no
`<stop_token>` without its experimental library, `StopToken` is the fallback. See
[Coroutines and lifetimes](../design/coroutines-and-lifetimes.md).
