# async

The C++23 coroutine vocabulary. Namespace `core::async`, directory `src/core/async/`, target
`core::async` (header-only).

!!! note "Status"
    `StopToken`, and contour's `Task`, cancellation and combinators (`src/coro` at
    `6777ff05`, Task A5 of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md))
    exist. `Generator` moved to [base](base.md#generator) in Task A5b: `core::async::Generator`
    would read as an *asynchronous*, `co_await`-able stream, and it is a synchronous one, needing
    only the standard library. Task B1 merges fastcached's executors and ownership rules into
    the rest.

## StopToken

`<core/async/StopToken.hpp>` has `core::async::StopToken`, `StopSource`, `StopCallback<F>` and
`NoStopState`, the vocabulary of cooperative cancellation.

- They are `std::stop_token`, `std::stop_source`, `std::stop_callback<F>` and `std::nostopstate`
  where the standard library defines `__cpp_lib_jthread`, and otherwise
  `core::async::detail::StopTokenFallback`, `StopSourceFallback`, `StopCallbackFallback<F>` and
  `NoStopStateFallback`. `NoStopState` is a `constexpr` object of `std::nostopstate_t` or of
  `NoStopStateFallback`. contour's copy (`src/coro/Cancellation.hpp` at `6777ff05`) aliased
  `std::` and refused to compile otherwise.
- The fallback is live wherever libc++ before 20 is used without `-fexperimental-library`, which
  gates `<stop_token>` there. That covers emsdk 3.1.56's libc++ 17, FreeBSD 15's base Clang 19,
  and AppleClang 17 (measured in CI). It runs with real threads on all of them but the first, so
  it is production code, not a WebAssembly shim. core-cpp adds no compile flag to its consumers.
  The configure log names the branch a toolchain takes, when core-cpp's tests are built:
  `[core-cpp] async: StopToken is std::stop_token`, or `... is core-cpp's fallback`.
- The fallback has the standard semantics. `request_stop()` returns true exactly once, and that
  call runs every registered callback once, on the requesting thread, before it returns. A
  callback constructed on a stopped token runs in its constructor, is never registered, and so
  its destructor waits for nothing. `~StopCallback` deregisters a registered callback, and waits
  while it runs on another thread, but not when it is called from inside that callback.
  `stop_possible()` is false for a token without a stop state, and for one whose sources are all
  gone without a request; it reads the source count before the stop flag, so a stop requested
  just before the last source goes never reads as impossible. Copies share state. Its members
  carry the standard's names (`request_stop`, `stop_requested`, `stop_possible`, `get_token`,
  `callback_type`), so code compiles against either branch.
- Under single-threaded WebAssembly the fallback keeps plain state: no atomics, no lock and no
  wait, since a running callback always runs on the calling thread there. Elsewhere a mutex
  guards its callback list, and no lock is held while a callback runs, so a callback may request
  stop again or register another callback.
- The choice is read from `<version>`, which the header includes first, so every translation unit
  makes the same one.
- `CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK` selects the fallback where the standard library has
  `<stop_token>`. It changes what every `Task` promise holds, so it must be defined the same way
  in every translation unit of a program. The test binary `core-cpp-async-fallback-test` (ctest
  `core-cpp.async-fallback`) builds the module's tests with it, so the fallback is tested on every
  platform, under ThreadSanitizer too.

## Task, cancellation and combinators

Imported from contour's `src/coro` at `6777ff05`, with `coro::` renamed `core::async::`:

- `Task<T>` (`<core/async/Task.hpp>`) is a lazy coroutine producing one value, or none for
  `Task<void>`. It starts suspended, so a `co_await` attaches its continuation before the body
  runs, and its final suspension transfers to the awaiting coroutine (symmetric transfer). A
  task is awaited, or driven through `handle()`, once. The `Task` value owns the frame and
  destroys it; the awaiter only borrows it. The promise holds a `StopToken`, inherited from the
  awaiting coroutine when a task is awaited. `result()` of a root task requires both `done()`
  and an owned frame: `done()` is also true for a default-constructed or moved-from task.
- Symmetric transfer keeps `co_await`s from growing the stack only where the compiler makes the
  transfer a tail call. Clang and MSVC do at every optimisation level. GCC does only when it
  optimises sibling calls, and WebAssembly has no tail calls without `-mtail-call`. Measured at
  100000 awaits that complete synchronously, with an 8 MiB stack:
  - GCC 14.3 and 15, a nested chain (a task awaiting a task awaiting a task ...): overflows at
    `-O0`, `-Og` and `-O1`; passes at `-O2` and `-O3`.
  - GCC 14.3 and 15, a *loop* in one coroutine awaiting tasks that complete at once: overflows at
    `-O0`; passes at `-Og` and above. Consecutive synchronous completions pile up stack until
    the coroutine really suspends, which is what a read loop over buffered data does.
  - emsdk 3.1.56 under node, the nested chain: exceeds node's call stack.

  `Task_test.cpp` skips its deep-chain case under Emscripten without `-mtail-call`, and on GCC
  unless the build says the level gives the tail call: GCC defines no macro for the level
  (`__OPTIMIZE__` is 1 at `-Og` and `-O1` too, where the chain overflows and takes the whole
  binary with it), so `src/core/async/CMakeLists.txt` reads the last `-O` off the build's own
  flags, defines `CORE_ASYNC_SYMMETRIC_TRANSFER_IS_TAIL_CALL` only at `-O2` or better, and says
  which it decided in the configure log. `gcc-release` keeps the case, `gcc-debug` and any build
  outside the presets skip it. The fix is tracked in
  [core-cpp#15](https://github.com/contour-terminal/core-cpp/issues/15), to be decided in Task B1.

  The teardown of such a chain is not a tail call and does not need to be: each level destroys
  the child it awaited at the end of its own `co_return` expression, by which time that child has
  already destroyed its own, so a completed chain is released one frame at a time. A chain
  destroyed *before* it completes is torn down by plain recursion, one `~Task` per level.
- `detail::UniqueCoroHandle<Promise>` (`<core/async/UniqueCoroHandle.hpp>`) is the move-only owner
  of a coroutine handle that `Task` and the combinators' child runners share.
- `<core/async/Cancellation.hpp>` has `OperationCancelled`, which a cancelled frame throws to
  unwind through ordinary RAII, and `thisCoroStopToken()`, an awaitable yielding the awaiting
  coroutine's token without suspending it (a default token where the promise has none). contour's
  copy also aliased `std::stop_token` and friends, which are now `<core/async/StopToken.hpp>`.
- `<core/async/Awaitable.hpp>` has the concepts `Awaiter` (`await_ready`, `await_suspend`,
  `await_resume`) and `HasStopToken` (a promise whose `stopToken()` yields a `StopToken`). Every
  templated `await_suspend` in the module reads the awaiting promise through `HasStopToken`, so
  what a promise must offer is stated in one place; `Awaitable_test.cpp` asserts both concepts
  over the module's own types and over the near misses.
- `whenAll(tasks...)` (`<core/async/WhenAll.hpp>`) starts every `Task<void>` and resumes the
  awaiting coroutine once all have finished. Each child inherits the awaiting coroutine's token.
  It does not cancel siblings when one throws: the first exception is rethrown once every child
  has finished.
- `whenAny(tasks...)` (`<core/async/WhenAny.hpp>`) resolves to
  `std::optional<std::size_t>`: the index of the first `Task<void>` to **complete**, or
  `std::nullopt` where none did (an empty input, or every child unwound cancelled). The winner
  requests stop on a child `StopSource` shared by the others, which must unwind on
  `OperationCancelled` — a child that swallows its cancellation and returns has, as far as the
  race can tell, completed. The awaiting coroutine resumes only once every child has finished, so
  the frames it owns outlive them. Cancelling the awaiting coroutine's own token cancels every
  child through a `StopCallback`, and `whenAny` then throws `OperationCancelled` — but only if no
  child completed. A cancellation that arrives after one did cannot undo it, and
  `.agent/rules/async-and-net.md` is explicit that bytes a receive already took win; the winner is reported and the flow decides.

  The race state is held by `shared_ptr`, and every call into it that can run foreign code holds
  a reference for that call's duration. Requesting stop runs the children's stop callbacks, and a
  runtime awaitable resumes its coroutine from inside one: the losers unwind there and then, the
  last transfers to the awaiting coroutine, and the awaiter — with the child source whose
  `request_stop()` is still on the stack — is destroyed before that request returns. Keeping a
  stop state alive across one's own `request_stop()` is the caller's job, and neither
  `std::stop_source` nor the fallback promises to do it.

Changes from contour's copy, besides the namespace:

- No `NOLINT`: the coroutine hooks are exempt through `.clang-tidy`'s `IgnoredRegexp`.
- Two locals of `whenAll`'s and `whenAny`'s final awaiters are renamed, because they shadowed a
  member of the enclosing promise (`-Wshadow`, part of core-cpp's warning set);
  `WhenAny_test.cpp`'s `ManualEvent` initialises its pointer member
  (`cppcoreguidelines-pro-type-member-init`), and its two helpers that only a case compiled off
  Windows uses are compiled off Windows too (`-Wunused-function` on clang-cl). `Task_test.cpp`
  skips its deep-chain case under Emscripten without `-mtail-call` and on GCC below `-O2` (above).
- The Phase A gate's third pass (Task A11): `whenAny`'s race state is reference-counted rather
  than a member of the awaiter, a child that completed beats a cancellation that follows, the
  result is a `std::optional` rather than a `detail::` `SIZE_MAX` sentinel, the variadic overloads
  require rvalues, the runner promise classifies what escaped its task instead of the body
  swallowing it, and the parent→child cancel bridge is a named functor rather than a
  `StopCallback<std::function<void()>>`.
- `Cancellation.hpp` no longer defines the stop-token aliases, nor refuses to compile without
  `__cpp_lib_jthread`.
- contour's `src/coro/test_main.cpp` is not imported: every test binary links
  `core::testing_main`. Its `src/coro/testing/SuppressWindowsDialogs.hpp` was merged into
  `core::testing` in Task A1.

`Awaitable_test.cpp`, `Task_test.cpp`, `WhenAll_test.cpp` and `WhenAny_test.cpp` run in
`core-cpp.async` and again, over the `StopToken` fallback, in `core-cpp.async-fallback`.
`core-cpp.async-link-smoke` is a third binary, `StopTokenLinkSmoke.cpp`, which links `core::async`
alone with the fallback forced: the link a consumer makes, which the other two hide by linking
`core::testing_main`. `Task_test.cpp` and
`WhenAny_test.cpp` do not compile their cases that propagate an exception out of a coroutine
frame on Windows, where contour found that throwing through a coroutine frame crashes the Catch2
harness (an MSVC coroutine-unwind interaction that also affects `std::generator`).
`WhenAll_test.cpp`'s exception cases have no such guard, and pass with `cl` and `clang-cl`.

## Conventions

From contour's `src/coro/README.md` at `6777ff05`, as far as it still holds:

- `core::async` includes nothing but the standard library (and links what that needs: Threads,
  above). `core::net` is the layer that knows
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

Depends on no other core-cpp module, and on Threads: the `StopToken` fallback synchronises its
stop state with a `std::mutex`, a `std::condition_variable` and `std::this_thread::get_id()`, so
`target_link_libraries(app PRIVATE core::async)` has to carry pthread wherever that is a library of
its own. `core-cpp.async-link-smoke` is that link, made with the fallback forced and nothing else
on the line. Under single-threaded Emscripten the fallback keeps plain state and the module links
nothing. Under WebAssembly everything builds except
`ThreadPoolExecutor.hpp` (Task B1), which refuses to compile without threads; where libc++ has no
`<stop_token>` without its experimental library, `StopToken` is the fallback. See
[Coroutines and lifetimes](../design/coroutines-and-lifetimes.md).
