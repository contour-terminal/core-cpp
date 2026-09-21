# Task B4 review — `EventLoop` absorbs fastcached's reactor contract

Reviewer: `review-B4`. Reviewed against `task-B4-dispatch.md`, the spec's §2 (`docs/superpowers/specs/2026-09-18-core-cpp-design.md:157-233`), `task-B4-report.md` (DONE_WITH_CONCERNS) and the nine-commit package
`C:\Users\chris\AppData\Local\Temp\claude\D--core-cpp\2b2cf69c-729f-4406-a737-95ff47fa883e\scratchpad\task-B4-review-package.md`.
Source read at `1562018` (the committed B4 end-state) rather than from the working tree, because
`impl-B5` began editing `EventLoop.{hpp,cpp}` and `detail/ParkTable.hpp` mid-review; line numbers
below are the committed ones.

---

## Verdicts

**Spec compliance: PASS.** The `EventLoop` declaration matches the spec's interface block member for
member, in the spec's order, with the `[[nodiscard]]`s, the `noexcept`s and `using
async::IExecutor::submit;` present (`src/core/net/EventLoop.hpp:155-436`). Both orderings — the
five-step turn and the six-step teardown — are implemented in the order the dispatch states, with
the reason for each step written beside it, and each load-bearing arm has a case that dies without
it. Three deviations, all deliberate and all defensible:

- `addTimer` / `cancelTimer` / `TimerId` / `TimerCallback` and the free `sleepUntil(EventLoop*, tp)`
  are in the spec's block and are not implemented. The plan's B5 checklist names them, and B5 has
  since landed all of them plus `interruptibleSleepUntil` (untracked `SleepUntil.hpp`,
  `InterruptibleSleep.*`, `DeadlineTimer.*` in the tree). Nothing fell between the two tasks.
- `DefaultHandleKind` for the spec's `defaultHandleKind`: forced by the project's constant-naming
  rule, decided in B3, followed here.
- `IoBackend::setPump` is an **addition** to a spec block labelled "these are contracts; tasks
  implement them exactly" (`src/core/net/IoBackend.hpp:409-425`). The argument in §6a and in the
  header's own comment is right — a loop holding an `IoBackend&` cannot reach a concrete backend
  without a downcast — and the member is a defaulted no-op, so no out-of-tree backend breaks. I
  would have made the same call.

**Code quality: PASS WITH ONE CRITICAL DEFECT.** The implementation is unusually careful: the
ownership split at teardown is correct and the reasoning for it is written where it binds; the park
table's never-reused id genuinely is the generation check; the O(1) spawn unlink looks up the root
before the resume for exactly the right reason (`EventLoop.cpp:343-345`); the `WorkerIdentity` nests
correctly and publishes `_running` with release ordering; the abandon fixpoint takes all three
containers before freeing any of them. The comment density is high and the comments are load-bearing
rather than decorative. There is one defect that must be fixed before release, three that should be,
and five that are cheap.

On the lead's question — *does the diff still contain members of the six classes that reached master*
— I went looking for each. **Dead-stack writes:** every case that leaves work parked across
`~EventLoop` now declares its counter before the loop, with the reason stated; the three that declare
a counter *after* the loop (`EventLoop_test.cpp:622`, `:682`, `HostDrivenLoop_test.cpp:69`) are safe
because the work they name has completed by then, but they are one edit away from being unsafe.
**Release-only:** no remaining case depends on wall-clock progress or on an `assert` existing.
**Ungated `add_test`:** the canary is now behind `CORE_CPP_TESTING`, and it is the only bare
`add_test` in the file. **Unbounded waits:** every `runUntil` in the new tests is bounded and says
what it waited for. What I did find instead is one *coverage* member of the "invisible to every local
preset" class — Minor 1 below, `blockOn` is never instantiated under Emscripten — and the Critical,
which no preset exercises because no case asks the question.

Findings: **1 Critical, 3 Important, 5 Minor.**

**Nothing here changes the shape B5 is building on.** `ParkTable`'s public members, `ParkEntry`,
`ParkId` and every `EventLoop` declaration stay as they are; the Critical fix is one statement inside
`blockOn`, Important 1 is a two-line change inside `ParkTable::dropIndices`, Important 2 is inside
`~EventLoop`, Important 3 is one `assert`. B5 can keep building.

---

## Critical

### C1. `blockOn` on a flow that can no longer advance reads a disengaged `std::optional`

`src/core/net/EventLoop.hpp:218-236`, `src/core/async/Task.hpp:271-279`, `CHANGELOG.md` (Breaking,
the `blockOn` migration bullet).

```cpp
while (!task.done())
{
    if (!hasPendingWork())
        break;
    std::ignore = turn(std::nullopt, task.handle());
}
return task.result();
```

`hasPendingWork()` is `!_ready.empty() || _parks.size() != 0 || !_closedParks.empty() ||
hasInbound()` (`EventLoop.cpp:789-792`). It asks whether *this loop* can advance anything — not
whether the root task can. A flow suspended on something that is not the loop leaves all four false,
the `break` is taken with `task.done() == false`, and `task.result()` runs on an unfinished frame.

`Task<T>::result()` refuses only a task owning **no** frame. A task owning an unfinished one reaches
`return std::move(*promise.result);` with `promise.result` disengaged — `std::optional::operator*`
on an empty optional is undefined behaviour, not a throw. Concretely:

- **non-void `T`**: an indeterminate value is moved out of uninitialised storage and returned. For
  `Task<int>` that is a garbage int; for `Task<std::string>` it is a move-construct from a
  non-object, which in a Release build is a crash or a corrupted heap with no diagnostic.
- **`Task<void>`**: `result()` only rethrows a stored exception, so `blockOn` returns **normally**, as
  if the flow had succeeded.
- either way, `task` is a by-value parameter destroyed on return, so a frame that is still suspended
  — and that something else may still hold a handle to — is destroyed. If the flow is parked in an
  `AsyncQueue` waiter list, the next `push()` resumes freed storage.

**Reproducer**, with nothing exotic in it:

```cpp
auto q = core::async::AsyncQueue<int> {};
auto backend = core::net::makeDefaultBackend();
auto loop = core::net::EventLoop { *backend };

auto pop = [](core::async::AsyncQueue<int>& queue) -> core::async::Task<int> {
    co_return co_await queue.pop();
};
auto const v = loop.blockOn(pop(q));   // nothing ever pushes
```

`AsyncQueue::pop()` parks the coroutine in the queue's own waiter list
(`src/core/async/AsyncQueue.hpp:301`), never on the loop. Turn 1 drains the root, which suspends
there; `hasPendingWork()` is false; `v` is an indeterminate `int`. The same shape arises from
`co_await ResumeOn { threadPool }` — the flow is live on another thread and `blockOn` returns garbage
and then destroys the frame underneath it.

**What makes this Critical rather than Important is that it is shipped as the opposite.** Two
documents promise the refusal:

- `EventLoop.hpp:214-217`: *"@throws std::logic_error if @p task can no longer be advanced — nothing
  is queued, nothing is parked, and it has not finished. That is a deadlocked flow, and answering
  with a value it never produced would hide it"* — which is precisely what it does.
- `CHANGELOG.md`, the Breaking entry: *"`blockOn()` no longer spins when its flow cannot advance. It
  returns as soon as nothing is queued and nothing is parked, and **`Task::result()` then refuses the
  still-suspended task by name** instead of the loop burning a core forever."* `Task::result()` does
  no such thing, and the release notes are what contour, endo and tuidu will read.

Before B4, core-cpp#17's behaviour was a hang: loud, attributable, and impossible to mistake for
success. The replacement is silent UB. That is a worse failure mode than the bug it fixed, and it is
documented as the better one.

**Fix** — one statement, inside `blockOn`, which is the only place that has both facts:

```cpp
while (!task.done())
{
    if (!hasPendingWork())
        throw std::logic_error {
            "core::net::EventLoop::blockOn: the task can no longer be advanced -- nothing is "
            "queued, nothing is parked, and it has not finished. A flow suspended on something "
            "this loop does not drive cannot be completed by driving this loop."
        };
    std::ignore = turn(std::nullopt, task.handle());
}
return task.result();
```

and a case per arm: one `Task<int>` and one `Task<void>` parked on an `AsyncQueue`, both expecting
the throw. `<stdexcept>` joins the header's includes.

**This is the answer to the report's concern #2, and it is the same answer either way round:** see
the section at the end.

---

## Important

### I1. A readiness park leaves `_byHandle` the moment its waiter is queued, so `notifyHandleClosing` can no longer detach it

`src/core/net/detail/ParkTable.hpp:245-253` (`takeWaiter`), `:383-405` (`dropIndices`), `:305`
(`readinessCount`); `src/core/net/EventLoop.cpp:713-723` (`queueParkedWaiter`), `:725-751`
(`notifyHandleClosing`).

`takeWaiter` takes the coroutine out of a park and calls `dropIndices`, which erases the park from
`_byWaiter` **and from `_byHandle`** — while deliberately leaving the park itself in `_parks` with
`attached == true`, because the awaiter's own `unregisterPark` is what detaches it. `parksOn(handle)`
reads `_byHandle`. So between the instant a readiness park is queued and the instant its flow
actually resumes, `notifyHandleClosing` **cannot see it**, and the registration is not detached at
close time.

That window is a full turn wide by construction. Readiness is dispatched in step 4 of turn *N* and
resumed in step 2 of turn *N+1* (guarantee G2, which is the whole design). Everything in between can
close the descriptor:

1. turn *N*, step 4: the backend dispatches readiness for park P on fd F → `onParkReady` →
   `queueParkedWaiter(P)` → `takeWaiter` → P leaves `_byHandle`. P stays attached.
2. turn *N+1*, step 1: a post runs — and `post()` is exactly how another thread asks the loop to
   close a socket — calling `socket.close()`, which calls `notifyHandleClosing(F, …)` first, as the
   contract demands. `parksOn(F)` returns nothing for P. Then `close(F)`.
3. turn *N+1*, step 2: the flow resumes, `WaitHandleAwaiter::await_resume` calls `unregisterPark(P)`
   → `_backend.detach(P.handler)`.

Step 3 is the exact sequence `notifyHandleClosing`'s own documentation says it exists to prevent
(`EventLoop.hpp:354-357`): *"Deferring that to the awaiter's own detach would issue the removal
against a descriptor number the kernel may already have handed to a new socket, silently
unregistering that one instead."* Two consequences, both named in the code that was written to avoid
them:

- On epoll and kqueue the kernel-side removal names the descriptor. If any thread opened a descriptor
  in that window and got F's number, the delete hits that one. Contour and fastcached both open
  descriptors off the loop thread.
- `EventLoop.cpp:738-739`: *"Detaching here also releases the private `dup()` a duplicate registration
  holds, which would otherwise keep the peer's connection open past the close."* That dup now
  survives the close until the next turn. Sharpest with two parks on one descriptor — a reader beside
  a writer, which is the case `_byHandle` is a multimap for: whichever of the two was dispatched is
  invisible to the close, the other is detached correctly, and the pair behave differently for no
  reason a reader can see.

The `_byHandle` erase in `dropIndices` also buys nothing. `takeWaiter`'s comment justifies it as
*"…or a cancel or a closing descriptor arriving in the same turn would find a park whose waiter is
already queued and queue it twice"* — but double-queueing is already prevented one layer up:
`queueParkedWaiter` reads `if (!work.resume) return; // already taken this turn`
(`EventLoop.cpp:716-717`), and `takeWaiter` itself returns empty for a park whose `parked` is already
taken. The `_byWaiter` erase *is* needed (`cancelPending` must not hand back work already queued);
the `_byHandle` erase is not.

The same root cause makes a public query's documentation false. `parkedWaiterCount()` is documented as
*"The number of readiness parks the loop still holds — one per registration it has with the backend"*
(`EventLoop.hpp:311-314`) and returns `_byHandle.size()`. In the window above it under-reports, so an
assertion of the shape `REQUIRE(loop.parkedWaiterCount() == 0)` — used in six cases as "no
registration leaked" — can read zero while a registration is still attached to the backend. That is a
gate that reports pass while checking less than it says.

**Fix**: leave `park.handle`'s entry in `_byHandle` in `dropIndices` and erase it in `take()` only
(or split `dropIndices` into a waiter half and a park half, which is what the two call sites actually
want). Then `readinessCount()` means what its doc says. A case: park a flow on a pipe, make it
readable, run one turn, call `notifyHandleClosing` *before* the resuming turn, and assert
`backend.attachedCount() == 0` — that case fails today.

### I2. Teardown resumes borrowed work in the ready queue and the park table, and silently strands it in the inbound queue

`src/core/net/EventLoop.cpp:56-98` (`~EventLoop` steps 2-4), `:125-155` (`abandonParkedWork`),
`.agent/rules/async-and-net.md` (the teardown section).

`~EventLoop` states its rule as universal — *"What the loop OWNS is taken aside to be freed; what it
borrows stays to be resumed"* — and applies it to two containers. The inbound queue is not one of
them: step 2 partitions `_ready` and `unparkEverything()` walks `_parks`, but `_inbound.submissions`
and `_inbound.scheduled` are never moved into `_ready`. They are first touched in step 4, where
`abandonParkedWork` swaps the whole `Inbound` out and assigns `inbound = Inbound {}`. Owned chains
are freed there, correctly. **Borrowed ones are dropped: never resumed, never unwound, never told.**

Which container a piece of work is in is decided by one thing: whether a turn ran between the
`submit()` and the destructor. `EventLoop::submit` routes to `_inbound` whenever the caller is not on
the worker thread *inside a turn* (`EventLoop.cpp:440-449`), which is every cross-thread submit and
every same-thread submit between turns. So:

```cpp
core::async::Task<void> worker(core::net::EventLoop& loop, core::async::IExecutor& pool)
{
    co_await core::async::ResumeOn { pool };    // onto the pool
    // ... blocking work ...
    co_await core::async::ResumeOn { loop };    // submit() from the POOL thread -> _inbound
    // ... cleanup that must run on the loop ...
}
```

`ResumeOn`'s documented purpose is exactly this hand-off (`src/core/async/ResumeOn.hpp:19-22`). If the
loop is destroyed while that submission is in `_inbound`, the flow is never resumed; whatever awaits
it — a `syncRun`, a `whenAll`, a `Task` chain — is suspended forever and its owner's shutdown hangs.
Run the loop one more turn before destroying it and the same program is resumed-at-teardown instead.
A shutdown whose semantics depend on whether one more turn happened to run is not a contract anybody
can write against.

`LoopTeardown_test.cpp` does not reach this: `anAbandonedSubmissionIsFreedExactlyOnce` and
`aResumedSubmissionIsNotFreedTwice` both use `parkOnSubmit`, a `DetachedTask` — the *owned* case,
which step 4 handles. `workSomebodyElseOwnsIsLeftAlone` submits a borrowed handle and asserts it is
**not** touched, which is the right assertion for a never-started lazy task and says nothing about a
suspended one.

I am not asserting that resuming is obviously the right answer — resuming a `ResumeOn` continuation
at teardown runs body code on a half-destroyed loop, since `ResumeOn::await_resume()` is `noexcept`
and returns rather than throwing. The defect is that a rule stated as universal holds in two of three
containers and nothing anywhere says which. Either move borrowed inbound work into `_ready` before
step 3 and let it unwind like every other borrowed park, or state in `~EventLoop`, in
`async-and-net.md` and in the CHANGELOG that work handed over and not yet accepted by a turn is
dropped — and add the case that pins whichever you choose.

### I3. `spawn()` mutates three unguarded containers with no assert, and it is the loop-thread-only mutator a consumer is likeliest to reach from another thread

`src/core/net/EventLoop.cpp:562-581`, `src/core/net/EventLoop.hpp:296-301`.

```cpp
void EventLoop::spawn(async::Task<void> task)
{
    ...
    auto const slot = _roots.insert(_roots.end(), std::move(task));
    _rootByHandle.emplace(handle.address(), slot);
    queueReady(async::ParkedWork { .resume = handle });
    if (!isOnWorkerThread())
        _backend.wake();
}
```

`_roots` (a `std::list`), `_rootByHandle` (an `unordered_map`) and `_ready` (a `deque`) are written
with no lock and no precondition check. The contract is stated — at class level
(`EventLoop.hpp:36-37`) and in `docs/design/threading.md`, both of which name the cross-thread surface
as `post`, `submit`, `schedule`, `requestCancel`, `stop` — but:

- `spawn`'s own Doxygen says nothing about threads, while `resumeSoon` ("Loop thread only"),
  `requestStop` ("Must be called on the loop thread"), `cancelPending` ("Must be called on the loop
  thread") and `notifyHandleClosing` ("on the loop thread") all do. A reader checking the member gets
  no answer.
- the only thing `spawn` does with `isOnWorkerThread()` is decide whether to `wake()` — the same
  predicate `submit`, `schedule` and `requestCancel` use to *route through the inbound queue*. Three
  siblings treat that predicate as "I may be off-thread, so hand over"; the fourth treats it as "I may
  be off-thread, so ring the bell".
- there is no `assert`, in a class that asserts G1 in `turn()`, asserts G5 in `~EventLoop`, asserts
  the host-driven refusal in `run()` and `blockOn()`, and in which B5 has just added
  `assert(teardownIsSerialisedWithDispatch() && "EventLoop::addTimer from a second thread …")` to the
  neighbouring mutator.

The failure is a torn `std::list` splice and a concurrent `unordered_map` rehash: a crash, a lost
flow, or a `_rootByHandle` entry naming a freed list node, with no diagnostic. It is reachable by
migrating a per-connection flow from `submit` (thread-safe, does not keep the frame alive) to `spawn`
(keeps the frame alive, which is why you would move) without noticing the difference — and contour,
endo and tuidu all accept connections off the loop thread.

**Fix**: `assert(teardownIsSerialisedWithDispatch() && "EventLoop::spawn from a second thread while
another is driving this loop")` — which allows the legitimate setup call before `run()` and the
legitimate host-driven call between pumps, and catches the acceptor thread — plus one line of Doxygen.
Worth the same sweep over `resumeSoon`, `registerPark`, `unregisterPark`, `wakeReasonOf` and
`requestStop`, which are documented but unasserted.

---

## Minor

### M1. Under Emscripten, `blockOn`'s body is never instantiated, so "it compiles there" is unverified

`src/core/net/HostDrivenLoop_test.cpp:168-182`.

```cpp
STATIC_REQUIRE(requires(EventLoop& loop) { loop.blockOn(Task<void> {}); });
```

A *requires*-expression checks that the call is well-formed, which instantiates the **declaration** of
a function template with an explicit return type — never the definition. The case's comment claims it
proves "both are instantiable in a build where the default backend IS host-driven"; it proves the
declaration is viable. `run()` is a non-template member compiled into `EventLoop.cpp`, which is in the
WebAssembly `FILE_SET`, so that half is genuinely covered. `blockOn` is a header template, the canary
that calls it is `NOT EMSCRIPTEN` (`src/core/net/CMakeLists.txt:245`), and grepping the four sources
of the `net_backend` binary finds no other call. So the dispatch's *"`run()` and `blockOn()` must
compile there"* is half-unproven, and `emscripten` 25/25 does not say what the report says it says.

Fix: replace the second `STATIC_REQUIRE` with an odr-use that forces the definition, e.g.
`std::ignore = static_cast<void (EventLoop::*)(async::Task<void>)>(&EventLoop::blockOn<void>);`.

### M2. `pruneTimers`'s own documentation does not say it looks only at the root

`src/core/net/detail/ParkTable.hpp:365-378`.

Per the lead's instruction I am not re-litigating concern #4 (lazy pruning), only assessing whether it
is documented where a reader meets it. It is documented on `take()`'s `@return`
(`ParkTable.hpp:257-259`) and hinted at on `TimerSlot` (`:345-346`). It is **not** documented on
`pruneTimers` itself, whose one-line summary — *"Drops heap slots naming parks that are gone or no
longer waiting on a deadline"* — reads as though it drops every such slot, when it stops at the first
live one. A reader arriving at `pruneTimers` (which is exactly where someone extending the timer
heap arrives) gets the wrong model from the doc and the right one only from the loop body. One
sentence: *"Only from the ROOT: a stale slot deeper in the heap survives until the root reaches it,
which is what bounds the heap by deadlines ever armed rather than by live ones. Erase-and-reheap per
cancellation is O(n) and is what makes a loop with many deadlines quadratic."* B5 has since added
`timerSlotCount()` with a good comment on the same cost, so this is now the only place left where a
reader meets the mechanism without the caveat.

### M3. `registerPark` discards the `NetError` a backend refusal carries

`src/core/net/EventLoop.cpp:622-635`.

```cpp
auto const attached = _backend.attach(park->handler);
auto const armed = attached ? _backend.setInterest(park->handler, entry.interest) : attached;
if (!armed) { ... return ParkId::invalid(); }
```

`attach` and `setInterest` return `std::expected<void, NetError>` precisely so the kernel's reason is
reported (`IoBackend.hpp:333-337`, `:351-357`: *"The kernel's refusal is reported, never swallowed"*).
The loop converts both to `bool` and throws away `BadHandle` vs `Unsupported` vs `SystemError`; the
flow then receives `FdRegistrationFailed {}`, an empty struct. A consumer debugging descriptor
exhaustion against a kqueue filter refusal gets nothing to tell them apart. `FdRegistrationFailed` is
pre-existing, not B4's invention — but B4 is where the `expected` is discarded, and the project rule
is that a fallible API is an `std::expected`. Giving `FdRegistrationFailed` a `NetError` member is a
one-field change with no call-site churn (nothing constructs it with arguments).

### M4. `blockOn` on an `IdlePolicy::Return` loop still spins — and `testing::TestLoop` forces that policy

`src/core/net/EventLoop.cpp:387-388`, `src/core/net/testing/TestLoop.hpp:62-65`.

`computeTimeout` forces a zero timeout whenever `_options.idle == IdlePolicy::Return`. `blockOn` on
such a loop with a parked flow therefore has `hasPendingWork()` true forever and polls at a zero
timeout forever: core-cpp#17's exact shape, in the one configuration the fix does not cover. It is
reachable by accident, because `TestLoop` forces `Return` and inherits `blockOn`, so
`testLoop.blockOn(taskThatParksOnADeadline())` with a `ManualClock` nobody advances burns a core until
ctest's 120-second backstop. A test author writing cases against `TestLoop` for B5 or B12 is the
likely victim. Either assert the policy in `blockOn` alongside the host-driven assert, or say in
`blockOn`'s Doxygen that an `IdlePolicy::Return` loop polls.

### M5. Three small consistency items

- **`blockOn` builds a `ReadyEntry` by hand** (`EventLoop.hpp:226-228`) while `queueReady`'s comment
  claims to be *"The one place a `ReadyEntry` is made, so the flag cannot be got wrong at one site out
  of six"* (`EventLoop.hpp:501-506`). The value it writes is correct, so this is an accuracy defect in
  a comment that is load-bearing — the next person will trust it. `blockOn` is a member and can call
  `queueReady(async::ParkedWork { .resume = task.handle() })` directly.
- **A dead defensive branch that would be a use-after-free if it were not dead.**
  `src/core/net/testing/TestLoop_test.cpp:465-471` handles `loop.parkedWaiterCount() == 0` with the
  comment *"A backend with no readiness at all refuses the registration"* — but `BackendMatrix`
  explicitly excludes `Null`, `Scripted` and `HostDriven` (`testing/BackendMatrix.hpp:43-46`), and
  `NullBackend` accepts registrations, so no backend this case runs against can take it. If one ever
  could, `FdRegistrationFailed` would escape a `DetachedTask` into `unhandled_exception`, which is
  `std::terminate()` — so the branch's `handle.destroy()` could never run either. Either delete it or
  make the comment say it is unreachable and why.
- **`_abandoned` is pruned only by `wakeReasonOf`** (`EventLoop.cpp:655-660`). A park marked abandoned
  by `notifyHandleClosing(h, FdWakePolicy::Cancel)` and then taken by `cancelPending`, or freed at
  teardown, leaves its `ParkId` in the set for the loop's lifetime. Narrow — the mark is normally
  consumed one turn later — and eight bytes apiece, but the member's own doc says *"a mark left behind
  would outlive its park"*, so the path it names is open. Erasing in `unregisterPark` and `take` closes
  it.

---

## On concern #2: does B4 owe a second refusal, or does it belong to `Task`?

**B4 owes it, and it belongs in `blockOn`.** Three reasons, in order of weight.

1. **`Task::result()`'s precondition is documented as two conjuncts, and `blockOn` is the caller that
   violates the other one.** `Task.hpp:262` reads `@pre done() is true AND a frame is owned`, and
   `:266-270` goes out of its way to say that `done()` alone is not the guard. `refuseEmptyTask`'s
   comment (`Task.hpp:117-136`) names the empty state specifically and explains why *that* state earns
   an exception: it is a state the type admits by design, reachable through the documented guard.
   "Owns a frame that has not finished" is not that state — it is reachable only by calling `result()`
   without checking `done()`, which is a plain precondition violation of the kind this codebase spells
   `assert`. Pushing a check into `Task::result()` would put a branch on every `co_await`-adjacent
   result path in six consumers to catch one caller's bug.
2. **`blockOn` is the only place both facts exist.** It already computes `task.done()` and
   `hasPendingWork()` in the same loop; `Task` knows nothing about executors and cannot tell "not
   finished because it is still running" from "not finished and nothing will ever run it". The refusal
   belongs where the second fact lives.
3. **B4 has already promised it, twice.** The `@throws std::logic_error` on `blockOn`
   (`EventLoop.hpp:214-217`) and the CHANGELOG's Breaking entry both state the refusal as shipped
   behaviour. Whatever the layering argument, a header and a release note that describe a throw
   obligate the code that carries them — and today the actual behaviour is undefined for every
   non-void `T`, which is strictly worse than either the refusal or the spin it replaced.

So: one `throw std::logic_error` inside `blockOn`'s loop, `<stdexcept>` in the header, two cases
(`Task<int>` and `Task<void>` parked on an `AsyncQueue` the loop does not drive). `core::async` owes
nothing — but if `Task` ever does grow a finished-ness refusal, `blockOn`'s check is still the one
that produces a message naming the loop, which is what the person reading the crash needs.

---

## What I did not verify

- `ctest -L hygiene`, the rename gate, `provenance.md` and `mkdocs build --strict`: read for sense, not
  re-run. The lead states master is green at `1562018` across all 25 jobs, which covers the `style`
  job that owns the `tree-level` checks.
- No build or test run of my own; every claim above is from reading the committed source at `1562018`,
  and each names the file and line it comes from.
