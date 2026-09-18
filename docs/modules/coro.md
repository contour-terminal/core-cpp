# coro

The C++23 coroutine vocabulary. Namespace `core::coro`, directory `src/core/coro/`, target
`core::coro` (header-only).

!!! note "Status"
    `Generator`, `StopToken`, and contour's `Task`, cancellation and combinators (`src/coro` at
    `6777ff05`, Task A5 of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md))
    exist. Task B1 merges fastcached's executors and ownership rules into them.

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
`NoStopState`, the vocabulary of cooperative cancellation.

- They are `std::stop_token`, `std::stop_source`, `std::stop_callback<F>` and `std::nostopstate`
  where the standard library defines `__cpp_lib_jthread`, and otherwise
  `core::coro::detail::StopTokenFallback`, `StopSourceFallback`, `StopCallbackFallback<F>` and
  `NoStopStateFallback`. `NoStopState` is a `constexpr` object of `std::nostopstate_t` or of
  `NoStopStateFallback`. libc++ 17, which emsdk 3.1.56 ships, has `<stop_token>` only behind
  `-fexperimental-library`, and core-cpp adds no compile flag to its consumers. contour's copy
  (`src/coro/Cancellation.hpp` at `6777ff05`) aliased `std::` and refused to compile otherwise.
- The fallback has the standard semantics. `request_stop()` returns true exactly once, and that
  call runs every registered callback once, on the requesting thread, before it returns. A
  callback constructed on a stopped token runs in its constructor, is never registered, and so
  its destructor waits for nothing. `~StopCallback` deregisters a registered callback, and waits
  while it runs on another thread, but not when it is called from inside that callback.
  `stop_possible()` is false for a token without a stop state, and for one whose sources are all
  gone without a request; it reads the source count before the stop flag, so a stop requested
  just before the last source goes never reads as impossible. Copies share state. Its members carry the standard's
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

## Task, cancellation and combinators

Imported from contour's `src/coro` at `6777ff05`, with `coro::` renamed `core::coro::`:

- `Task<T>` (`<core/coro/Task.hpp>`) is a lazy coroutine producing one value, or none for
  `Task<void>`. It starts suspended, so a `co_await` attaches its continuation before the body
  runs, and its final suspension transfers to the awaiting coroutine (symmetric transfer). A
  task is awaited, or driven through `handle()`, once. The `Task` value owns the frame and
  destroys it; the awaiter only borrows it. The promise holds a `StopToken`, inherited from the
  awaiting coroutine when a task is awaited. `result()` of a root task requires both `done()`
  and an owned frame: `done()` is also true for a default-constructed or moved-from task.
- Symmetric transfer keeps a chain of `co_await`s from growing the stack only where the compiler
  makes the transfer a tail call. Clang and MSVC do at every optimisation level. GCC does only
  when it optimises sibling calls: measured with GCC 14.3, a chain of 100000 synchronously
  completing awaits overflows an 8 MiB stack at `-O0` and passes at `-O3`. WebAssembly as emsdk
  3.1.56 builds it has no tail calls (no `-mtail-call`), and the same chain exceeds node's call
  stack. There, a long chain of awaits that complete synchronously can overflow the stack, and
  the test of that bound is skipped.
- `detail::UniqueCoroHandle<Promise>` (`<core/coro/UniqueCoroHandle.hpp>`) is the move-only owner
  of a coroutine handle that `Task` and the combinators' child runners share.
- `<core/coro/Cancellation.hpp>` has `OperationCancelled`, which a cancelled frame throws to
  unwind through ordinary RAII, and `thisCoroStopToken()`, an awaitable yielding the awaiting
  coroutine's token without suspending it (a default token where the promise has none). contour's
  copy also aliased `std::stop_token` and friends, which are now `<core/coro/StopToken.hpp>`.
- `<core/coro/Awaitable.hpp>` has the concepts `Awaiter` (`await_ready`, `await_suspend`,
  `await_resume`) and `HasStopToken` (a promise with `stopToken()`).
- `whenAll(tasks...)` (`<core/coro/WhenAll.hpp>`) starts every `Task<void>` and resumes the
  awaiting coroutine once all have finished. Each child inherits the awaiting coroutine's token.
  It does not cancel siblings when one throws: the first exception is rethrown once every child
  has finished.
- `whenAny(tasks...)` (`<core/coro/WhenAny.hpp>`) resolves to the index of the first
  `Task<void>` to finish and requests stop on a child `StopSource` shared by the others, which
  must unwind on `OperationCancelled`. The awaiting coroutine resumes only once every child has
  finished, so the frames it owns outlive them. Cancelling the awaiting coroutine's own token
  cancels every child through a `StopCallback`, and then `whenAny` throws `OperationCancelled`
  rather than report a cancelled child as the winner.

Changes from contour's copy, besides the namespace:

- No `NOLINT`: the coroutine hooks are exempt through `.clang-tidy`'s `IgnoredRegexp`.
- Two locals of `whenAll`'s and `whenAny`'s final awaiters are renamed, because they shadowed a
  member of the enclosing promise (`-Wshadow`, part of core-cpp's warning set);
  `WhenAny_test.cpp`'s `ManualEvent` initialises its pointer member
  (`cppcoreguidelines-pro-type-member-init`), and its two helpers that only a case compiled off
  Windows uses are compiled off Windows too (`-Wunused-function` on clang-cl). `Task_test.cpp`
  skips its deep-chain case under Emscripten and for GCC without optimisation (above).
- `Cancellation.hpp` no longer defines the stop-token aliases, nor refuses to compile without
  `__cpp_lib_jthread`.
- contour's `src/coro/test_main.cpp` is not imported: every test binary links
  `core::testing_main`. Its `src/coro/testing/SuppressWindowsDialogs.hpp` was merged into
  `core::testing` in Task A1.

`Task_test.cpp`, `WhenAll_test.cpp` and `WhenAny_test.cpp` run in `core-cpp.coro` and again,
over the `StopToken` fallback, in `core-cpp.coro-fallback`. `Task_test.cpp` and
`WhenAny_test.cpp` do not compile their cases that propagate an exception out of a coroutine
frame on Windows, where contour found that throwing through a coroutine frame crashes the Catch2
harness (an MSVC coroutine-unwind interaction that also affects `std::generator`).
`WhenAll_test.cpp`'s exception cases have no such guard, and pass with `cl` and `clang-cl`.

## Conventions

From contour's `src/coro/README.md` at `6777ff05`, as far as it still holds:

- `core::coro` includes nothing but the standard library. `core::net` is the layer that knows
  about sockets, and neither depends on anything above it in the
  [module table](index.md).
- Coroutine parameters are values, never references: a reference dangles once the coroutine
  suspends. A pointer is a value, and the tests' drivers take pointers to locals that outlive
  them. A coroutine lambda's closure is a temporary destroyed once the `Task` is created, so a
  body that resumes later reads its captures through a dangling `this`: write a free function.
- The awaiter and promise hooks (`await_ready`, `await_suspend`, `await_resume`,
  `initial_suspend`, `final_suspend`, `return_value`, ...) are named by the language. They stay
  non-static instance methods: a static `initial_suspend` or `final_suspend` makes the
  compiler-generated `promise.hook()` call trip `readability-static-accessed-through-instance`.
- A cancelled frame unwinds by throwing `OperationCancelled`; a runtime awaitable throws it from
  `await_resume` when its token has `stop_requested()`.
- The README's provenance note made contour's copy canonical over endo's and fastcached's. This
  copy takes that role: a fix is made here, released and re-vendored, never made in a consumer's
  copy.

## Planned contents

- From fastcached: `DetachedTask`, `syncRun`, `ParkedWork`, `IExecutor`, `ResumeOn`,
  `ThreadPoolExecutor`, `AsyncQueue` with a stop-aware `pop`, and an awaiter that takes ownership
  of the task it awaits.

Depends on the standard library only. Under WebAssembly everything builds except
`ThreadPoolExecutor.hpp` (Task B1), which refuses to compile without threads; where libc++ has no
`<stop_token>` without its experimental library, `StopToken` is the fallback. See
[Coroutines and lifetimes](../design/coroutines-and-lifetimes.md).
