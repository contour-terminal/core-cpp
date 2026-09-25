# async

The C++23 coroutine vocabulary. Namespace `core::async`, directory `src/core/async/`, target
`core::async` (header-only).

!!! note "Status"
    `StopToken`, and contour's `Task`, cancellation and combinators (`src/coro` at
    `6777ff05`, Task A5 of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md))
    exist. `Generator` moved to [base](base.md#generator) in Task A5b: `core::async::Generator`
    would read as an *asynchronous*, `co_await`-able stream, and it is a synchronous one, needing
    only the standard library. Task B1 merged fastcached's executors and ownership rules into the
    rest (`src/FastCache/Async` at `0708dd54`): `ParkedWork`, `DetachedTask`, `syncRun`,
    `IExecutor`, `ResumeOn`, `ThreadPoolExecutor` and `AsyncQueue`, and an awaiter that owns the
    task it awaits. 0.4.0 added `Strand`, `KeyedStrands`, the current-executor context and
    `testing::ManualExecutor`.

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
  task is awaited, or driven through `handle()`, once. **The awaiter owns the task it awaits**:
  `operator co_await` is rvalue-qualified and moves the frame out of the `Task` value, so a named
  local awaited with `std::move` is empty afterwards and the awaiter destroys the frame at the end
  of the `co_await` expression. Ownership therefore runs downward through a chain, which is what
  lets an executor free an abandoned one from its root. `release()` hands the frame to the caller
  instead. The promise holds a `StopToken`, inherited from the awaiting coroutine when a task is
  awaited, and a `unownedRoot` (below). `result()` of a root task requires both `done()` and an
  owned frame — `done()` is also true for a default-constructed, moved-from or released task — and
  a task owning no frame is refused with a `std::logic_error` rather than answered with a
  default-constructed `T`, so `T` need not be default-constructible. That throw reports a
  **precondition violation, not a recoverable error**: it is an assertion that survives a Release
  build, where `assert` would hand back a silently wrong value, and it is not to be caught — a
  `catch` around `result()` would make the empty state a supported path rather than a call to fix.
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
  [core-cpp#15](https://github.com/contour-terminal/core-cpp/issues/15). Task B1 does not close or
  narrow it: whether the transfer is a tail call is a property of the compiler's sibling-call
  optimisation and of WebAssembly's tail-call support, and the ownership graft changed who owns a
  frame, not how `final_suspend` transfers control. Nothing in this repository measures the depth at
  which the chain overflows, so treat "unchanged" as an argument from what was edited rather than
  as a number anybody took: the measurements above are the ones that exist, and the deep-chain case
  is a pass/fail at 100000 awaits, not a bisection of the limit.

  The teardown of a *completed* chain is not a tail call and does not need to be: each level's
  awaiter destroys the child it owns at the end of its own `co_await` expression, by which time
  that child's awaiter has already destroyed its own, so the chain is released one frame at a time,
  at no depth. A chain destroyed *before* it completes is different, and is new in Task B1 — before
  it, a root `Task` freed only its own frame. It is torn down by **plain recursion**, one stack
  frame per level, with no tail call to collapse it and no compiler flag that changes that. So the
  same 100000-deep chain that survives a completion at `-O2` would be freed recursively if it were
  abandoned instead, and the depth at which *that* overflows is likewise unmeasured.
  `Task_test.cpp` covers the property at ordinary depth and says so explicitly — its abandonment
  case "never has one" deep — so nothing here would notice a regression in the depth itself.
- `detail::UniqueCoroHandle<Promise>` (`<core/async/UniqueCoroHandle.hpp>`) is the move-only owner
  of a coroutine handle that `Task` and the combinators' child runners share.
- `<core/async/Cancellation.hpp>` has `OperationCancelled`, which a cancelled frame throws to
  unwind through ordinary RAII, and `thisCoroStopToken()`, an awaitable yielding the awaiting
  coroutine's token without suspending it (a default token where the promise has none). contour's
  copy also aliased `std::stop_token` and friends, which are now `<core/async/StopToken.hpp>`.
- `<core/async/Awaitable.hpp>` has the concepts `Awaiter` (`await_ready`, `await_suspend`,
  `await_resume`), `HasStopToken` (a promise whose `stopToken()` yields a `StopToken`) and
  `CarriesUnownedRoot` (a promise that carries the root of an await chain nobody owns). Every
  templated `await_suspend` in the module reads the awaiting promise through them, so what a
  promise must offer is stated in one place; `Awaitable_test.cpp` asserts all three over the
  module's own types and over the near misses.
- `whenAll(tasks...)` (`<core/async/WhenAll.hpp>`) starts every `Task<void>` and resumes the
  awaiting coroutine once all have finished. Each child inherits the awaiting coroutine's token.
  It does not cancel siblings when one throws: the first escape — a cancellation included — is
  rethrown once every child has finished.
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
- Both are written over one runner, one join state and one awaiter (`<core/async/Join.hpp>`, all of
  it `core::async::detail`, and a public header for the same reason `UniqueCoroHandle.hpp` is). A
  *policy* supplies the one step that differs — what a child finishing does to the shared state —
  together with the token each child observes and the bridge, if any, from the awaiting flow's own
  token. `whenAll` latches nothing and takes the parent's token; `whenAny` latches the first child
  to **complete**, requests stop on its own child source and arms the parent bridge. What escaped
  a child's task is recorded once, in the runner promise, which is also what tells a cancelled
  child from a failed one.

Changes from contour's copy, besides the namespace:

- No `NOLINT`: the coroutine hooks are exempt through `.clang-tidy`'s `IgnoredRegexp`.
- Two locals of `whenAll`'s and `whenAny`'s final awaiters are renamed, because they shadowed a
  member of the enclosing promise (`-Wshadow`, part of core-cpp's warning set);
  `WhenAny_test.cpp`'s `ManualEvent` initialises its pointer member
  (`cppcoreguidelines-pro-type-member-init`), and its two helpers that only a case compiled off
  Windows uses are compiled off Windows too (`-Wunused-function` on clang-cl). `Task_test.cpp`
  skips its deep-chain case under Emscripten without `-mtail-call` and on GCC below `-O2` (above).
- Task B1 collapsed `whenAll`'s and `whenAny`'s ~200 lines of near-identical runner, state and
  awaiter into `Join.hpp`, and with them `makeWhenAllRunner`'s try/catch, which recorded what
  escaped a child a second time after the promise already had.
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

`AsyncQueue_test.cpp`, `Awaitable_test.cpp`, `ParkedWork_test.cpp`, `StopToken_test.cpp`,
`Task_test.cpp`, `WhenAll_test.cpp` and `WhenAny_test.cpp` run in `core-cpp.async` and again, over
the `StopToken` fallback, in `core-cpp.async-fallback`; `ThreadPoolExecutor_test.cpp` joins them
wherever the build has threads. Every case that counts a coroutine frame does so with a sentinel
rather than leaving it to LeakSanitizer, because a leak only a sanitizer reports is a red once in
N runs and reads as a flake.
`core-cpp.async-link-smoke` is a third binary, `StopTokenLinkSmoke.cpp`, which links `core::async`
alone with the fallback forced: the link a consumer makes, which the other two hide by linking
`core::testing_main`. `Task_test.cpp` and
`WhenAny_test.cpp` do not compile their cases that propagate an exception out of a coroutine
frame on Windows, where contour found that throwing through a coroutine frame crashes the Catch2
harness (an MSVC coroutine-unwind interaction that also affects `std::generator`).
`WhenAll_test.cpp`'s exception cases have no such guard, and pass with `cl` and `clang-cl`.

## Ownership, executors and queues

From fastcached's `src/FastCache/Async` at `0708dd54`, with `FastCache::` renamed `core::async::`
and `Detail::` `detail::`:

- `DetachedTask` (`<core/async/DetachedTask.hpp>`) is a coroutine started for its effects: its
  body runs to its first suspension on construction and its frame frees itself at the end, so
  nobody holds a handle. An exception escaping it terminates the process, because there is no
  caller to hand it to. Its promise answers `stopToken()` with a never-stopped token, so a `Task`
  awaited from it takes the ordinary inheritance path. It is the one coroutine shape in the module
  that nothing owns, which is what makes the next entry answerable.
- `ParkedWork` (`<core/async/ParkedWork.hpp>`) is what a coroutine hands an executor: the handle
  to `resume`, and — only where this chain belongs to nobody — the chain root to `abandon` if it
  is never resumed. The two are different questions, and the second has a safe default:
  `IExecutor::submit(std::coroutine_handle<>)` borrows, so an executor may not free what it
  merely holds. `detail::parkedWorkFor` derives the answer from the parking coroutine's own
  promise (`detail::unownedRootOf`), and `detail::Parked` is the container entry that owns
  `abandon` for as long as it holds it: `resume()` disowns and resumes in one expression, and a
  handle it declines to resume is freed rather than dropped. It is the ROOT and never the parked
  frame, because ownership in a `Task` chain runs downward. Origin:
  [fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025).
- `syncRun(task)` and `syncRunWith(task, retrieve)` (`<core/async/SyncRun.hpp>`) drive a task to
  completion on the calling thread. The task must be self-driving; one still suspended after its
  resume has no result to read, and destroying its frame there tears down storage whatever parked
  it still points into, so `syncRun` throws `std::logic_error` instead. `syncRunWith` takes the
  park back first, while the frame is alive, and throws afterwards — a task its retriever did not
  wake has its frame deliberately leaked rather than freed under something that points into it.
- `IExecutor` (`<core/async/IExecutor.hpp>`) is somewhere a suspended coroutine can be handed to
  be resumed: `submit(std::coroutine_handle<>)` and `submit(ParkedWork)`, both callable from any
  thread. Every class deriving from it says `using IExecutor::submit;`, because a derived class
  that re-declares one overload of a name hides every other overload of it
  ([fastcached#1041](https://github.com/LASTRADA-Software/fastcached/issues/1041)). Both halves
  are pure: an executor that queues work has to state what it does about work it never runs.
- `co_await ResumeOn { executor }` (`<core/async/ResumeOn.hpp>`) continues the awaiting coroutine
  wherever that executor runs things.
- `ThreadPoolExecutor` (`<core/async/ThreadPoolExecutor.hpp>`) is an `IExecutor` over a fixed set
  of threads, for work that blocks — a loop multiplexes coroutines that suspend, and is the wrong
  answer for a job that occupies its thread for seconds. It does not bound admission. It never
  abandons work: its queue is drained even while stopping, and a handle submitted after `stop()`
  is resumed inline on the calling thread rather than dropped, because an unresumed coroutine
  never frees its frame. fastcached's 92-line `.cpp` is inlined here: `core::async` is an
  INTERFACE target, and a compiled body would change what every consumer links.
- `AsyncQueue<T>` (`<core/async/AsyncQueue.hpp>`) is a queue one coroutine parks on and any thread
  pushes to, replacing a mutex, a condition variable and a deque at the boundary between a
  producing thread and a consuming coroutine. `push()` and `close()` never resume the consumer
  inline; they hand its handle to the executor, because a producer commonly pushes while holding a
  lock of its own. `AsyncQueueOptions` bounds it and says which end overflow sacrifices
  (`DropOldest`, `DropNewest`); `push()` reports whether the item was admitted and how many it
  displaced. `co_await queue.pop()` resolves to `std::optional<T>` — a value, or `std::nullopt`
  meaning the queue closed — and is stop-aware: a cancel from the awaiting flow's own token throws
  `OperationCancelled`, while an item already queued and a `close()` both answer first. The queue
  owns no coroutine frame and cannot, so an owner observes its consumer finishing before
  destroying it; `~AsyncQueue` asserts that no waiter is left, and `hasWaiter()` lets a test assert
  it in a release build too. A parked consumer is resumed on the executor it was running on when
  it parked -- the current executor, below -- and on the queue's own executor only where none was
  current. Until 0.4.0 it was always the queue's.

`unownedRoot` is what ties these together. It is a member of every promise in the module, set at
each `await_suspend` from the awaiting coroutine's own, and non-empty exactly where the chain
bottoms out in a `DetachedTask`. `whenAll`'s and `whenAny`'s runners carry it too: a runner is a
coroutine type of its own between a detached root and the task that parks, and one that did not
carry the answer would make every park underneath a combinator read as *somebody owns this*.

## Strands and the current executor

Written for 0.4.0 after morph's `StrandExecutor` (morph PR #806); see
[Strands and the resume context](../design/strands.md) for the design and what it rejected.

- `Strand` (`<core/async/Strand.hpp>`) is an `IExecutor` over any `IExecutor` that runs what it is
  given one task at a time, in the order given. A *task* is one resumption: from the submit until
  the coroutine next suspends. `co_await ResumeOn { strand }` hops onto it, and `runningHere()`
  answers whether the calling thread is inside one of its tasks; `currentExecutor()` there is the
  strand's shared state, which outlives the `Strand` object, not the object's address. `StrandOptions::batch` (32) bounds
  how many tasks one turn on the base runs before the strand hands the base back and queues itself
  again. A task that throws out of `resume()` -- no coroutine type of this module does -- propagates
  to whoever resumed the strand on its base, and the tasks behind it still run. Destroying a strand
  drops what is queued (a chain rooted in a `DetachedTask` is freed, a coroutine a `Task` owns is
  left to it) and, where threads exist, waits for a task running on another thread.
- `KeyedStrands<Key, Hash, KeyEqual>` (`<core/async/KeyedStrands.hpp>`) is one strand per key over
  a shared base: work for one key is serial and ordered, work for different keys runs concurrently.
  A key's strand is made when it gets work and reclaimed when it runs out, so `size()` counts busy
  keys, not keys ever seen. `submit(key, ...)`, `co_await strands.resumeOn(key)`,
  `runningHere(key)` and `runningAnyHere()` are its members; `waitIdle()` blocks until no key has
  work, asserts when called from one of its own tasks, and is declared only where threads exist.
- Both take **callables** as well as coroutines: `post(fn)` and `post(key, fn)` run `fn` as one
  task, held by value in one allocation, freed once it has run. **`tryPost` and `trySubmit`**
  return false once the strand is closed -- `close()` is public and idempotent -- and leave the
  work with the caller, for work that must run somewhere even after its strand's owner is gone.
  `post` and `submit` drop it instead, as destruction drops what is queued.
- An **around-task hook** installs ambient context for exactly the length of each task:
  `StrandOptions::aroundTask`, an `AroundTask` built with `AroundTask::of(hook)` where `hook(run)`
  calls `run()` once; `KeyedStrands` takes a `KeyedAroundTask<Key>`, whose hook is given the key as
  well. It runs around every resumption, including a coroutine that parked on another executor and
  came back through the strand, which is what makes it the place for a request's session: `Task`
  itself carries no context, because that would cost every `co_await` for every consumer. The hook
  is a reference fixed at construction, costs a branch per task when unset, and must outlive the
  strand.
- `idle()` answers whether nothing is queued or running. On the single-threaded WebAssembly build,
  which has no `waitIdle()`, a host that wants queued work to run pumps its base until `idle()`
  before destroying the strands; destroying or closing them drops what is queued without waiting,
  since nothing else can be running.
- `ExecutorScope` (`<core/async/ExecutorContext.hpp>`) marks the calling thread as running a task
  of an executor, for as long as it lives; scopes nest and restore on every exit. `EventLoop` holds
  one per turn, `ThreadPoolExecutor` one per worker thread, `Strand` one per batch, and
  `testing::ManualExecutor` one per resumption. It is never held across a `co_await`.
- `currentExecutor()` answers the innermost scope's executor, or null. It is valid for the
  synchronous part of a task; across a suspension a coroutine holds a `ResumeTarget` instead, which
  `ResumeTarget::currentOr(fallback)` takes in `await_suspend` and which keeps a `KeyedStrands`
  key's strand alive until it is used.
- **Which awaitables come back to the current executor:** `AsyncQueue::pop`, on a push, a close and
  a stop alike. `ResumeOn` goes where it is told. The whole of `core::net` -- socket operations,
  timers, `delay`, `sleepUntil` -- resumes on its `EventLoop` and nowhere else, because guarantee
  G2 puts every resumption in step 2 of that loop's turn and the socket, its slots and its parks
  belong to that loop's thread. A strand-bound coroutine that awaits a socket hops back with
  `co_await ResumeOn { strand }`.
- `testing::ManualExecutor` (`<core/async/testing/ManualExecutor.hpp>`) is an executor a test
  drains by hand (`runOne`, `drain`, `pending`), stating itself as the current executor while it
  does. The module's first public test double.

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

Depends on no other core-cpp module, and on Threads: the `StopToken` fallback synchronises its
stop state with a `std::mutex`, a `std::condition_variable` and `std::this_thread::get_id()`, so
`target_link_libraries(app PRIVATE core::async)` has to carry pthread wherever that is a library of
its own. `core-cpp.async-link-smoke` is that link, made with the fallback forced and nothing else
on the line. Under single-threaded Emscripten the fallback keeps plain state and the module links
nothing. Under WebAssembly everything builds except
`ThreadPoolExecutor.hpp`, which refuses to compile without threads and is in no `FILE_SET` there,
and `KeyedStrands::waitIdle`, which is not declared there;
where libc++ has no
`<stop_token>` without its experimental library, `StopToken` is the fallback. See
[Coroutines and lifetimes](../design/coroutines-and-lifetimes.md).
