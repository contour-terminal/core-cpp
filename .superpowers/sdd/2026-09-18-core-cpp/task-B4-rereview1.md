# Task B4 re-review 1 — the fix round for `task-B4-review.md`

Reviewer: `rereview-B4-r1`. Scope: `fe48143..fb3fe97` (fix round 1) and `a02031c..27b8b43`
(the follow-up carrying the `DelayAwaiter` scope guard), read from the supplied diff at
`C:/Users/chris/AppData/Local/Temp/claude/D--core-cpp/2b2cf69c-729f-4406-a737-95ff47fa883e/scratchpad/rereview-B4.diff`
and verified against the committed tree at `7bdd132`. No build was run; the gates are stated green
at `fb3fe97` and `27b8b43`.

**Verdict: C1 is NOT ADDRESSED — the refusal repaired the deadlocked flow and left the legitimate
cross-thread one broken, in a shape that worked before Task B4 existed. The other eight findings
are 6 ADDRESSED and 2 ADDRESSED-WITH-CONCERN. Separately, the guard the follow-up added was
applied to two of the three sites that have its defect.**

**Section 2 (below) is the part of this report to read.** C1's diagnosis is settled and is not
re-derived here; what section 2 carries is the four questions it opens — every remaining path by
which `blockOn` frees an unfinished frame, where else the same false premise is built in, which of
the new asserts is the next instance of it, and what the round's instruments were derived from.

---

## Per-finding

| # | State | Evidence |
|---|---|---|
| **C1** `blockOn` reads a disengaged `optional` | **NOT ADDRESSED** | The `break` is gone and `EventLoop.hpp:277-282` throws `std::logic_error` with `<stdexcept>` included (`:67`), `Task`/`core::async` untouched, and two discriminating cases. That fixes the **deadlocked** flow. It leaves the **cross-thread** flow broken and makes the message false for it: B4's own probe on `8d7b8b2` — a root that hops to a pool, works 40ms and hops back — threw `logic_error` and then SIGSEGV'd, because `task` destructs as the exception unwinds and frees a frame the pool thread is inside. Pre-B4 that program worked. See section 2. |
| **I1** readiness park leaves `_byHandle` too early | **ADDRESSED** | `dropIndices` split into `dropWaiterIndices` (`ParkTable.hpp:1080`) and `dropHandleIndex` (`:1107` of the diff / `ParkTable.hpp` current), with only `take()` calling the second (`ParkTable.hpp:333-343`). Case distinguishes. One doc over-claim — see New 3. |
| **I2** teardown strands borrowed inbound work | **ADDRESSED-WITH-CONCERN** | Reverted to the drop, stated in `EventLoop.cpp:89-106`, `.agent/rules/async-and-net.md` and `CHANGELOG.md`, with a pinning case. The **stated reason does not discriminate the containers** — see below. |
| **I3** loop mutators unguarded | **ADDRESSED** | Six new `assert(teardownIsSerialisedWithDispatch())`: `requestStop` (`EventLoop.cpp:627`), `spawn` (`:638`), `resumeSoon` (`:723`), `registerPark` (`:742`), `unregisterPark` (`:802`), `wakeReasonOf` (`:825`). Predicate checked per member below; no in-tree caller trips one. Three small residues — New 5, New 6. |
| **M1** `blockOn` not instantiated under Emscripten | **ADDRESSED** | `HostDrivenLoop_test.cpp` now takes `&EventLoop::blockOn<void>` through an explicit cast. Taking a function template specialization's address is an odr-use, so the **definition** is instantiated — which the `requires`-expression never did. `CHECK(blockOnVoid != nullptr)` keeps it from being elided. |
| **M2** `pruneTimers` doc | **ADDRESSED** | States "Only from the ROOT, and it stops at the first live one", plus what that buys and what the alternative costs. Matches the body. |
| **M3** `registerPark` discards the `NetError` | **ADDRESSED-WITH-CONCERN** | `FdRegistrationFailed` gains `NetError reason` (`EventLoop.hpp:87`); `registerPark` writes through a `NetError*` out-parameter; `WaitHandleAwaiter` carries `_refusal` and moves it into the throw. The reason now reaches the caller. **The shape is against the house rule** — see New 7. |
| **M4** `blockOn` polls on an `IdlePolicy::Return` loop | **ADDRESSED** | `EventLoop.hpp:234-239` names the mechanism (`computeTimeout` forcing zero), names `testing::TestLoop` as the loop that forces the policy, and says what to use instead. The review offered "assert or document"; documenting was one of the two. |
| **M5** three consistency items | **ADDRESSED** (all three) | (a) `blockOn` queues through `queueReady` (`EventLoop.hpp:263`), so the "one place a `ReadyEntry` is made" comment is true again. (b) The dead branch in `TestLoop_test.cpp:455-471` is replaced by `REQUIRE(loop.parkedWaiterCount() == 1)` with the reason the branch could not run. (c) `_abandoned` is now erased in `unregisterPark` (`EventLoop.cpp:810`), `cancelPending` (`:561`) and `abandonParkedWork` (`:157`). I enumerated the other `take()` paths — `cancelTimer` (`:715`) and `runDueCallback` (`:465`) do not erase, and correctly so: `_abandoned` is only ever inserted by `notifyHandleClosing`, which walks `_byHandle`, and a callback park has no handle. |

### C1 in detail (the four questions asked)

- **Is `IdlePolicy::Return` handled?** Yes, and consistently: on such a loop a *parked* flow keeps
  `hasPendingWork()` true, so the throw cannot fire and the loop polls — which is M4's documented
  behaviour. A `Return` loop with nothing parked throws like any other. The two answers do not
  collide.
- **`Task<void>` versus `Task<int>`?** Both throw before `task.result()` is reached, so neither
  reads the disengaged optional. The report's RED note is worth keeping: on a hardened libstdc++
  the old `Task<int>` path aborted rather than returning garbage, which means the original Critical
  was louder on one standard library and silent on the ones consumers ship against.
- **Can the two new cases fail?** Yes. Restore the `break` and `CHECK_THROWS_AS(...,
  std::logic_error)` fails in both sections (`Task<void>` returns normally; `Task<int>` either
  returns garbage or aborts — either way not a `logic_error`). The surrounding hygiene is also
  sound: `popOne`'s frame is owned by `blockOn`'s by-value parameter, so `parkedWorkFor` arms no
  `abandon` claim (`ParkedWork.hpp:297-300`, `claimOn(unownedRootOf(...))` fills only for a
  `DetachedTask` root), and `AsyncQueue::close()` submits rather than resumes
  (`AsyncQueue.hpp:205-216`), so handing the stale waiter to `InertExecutor` frees nothing twice.
- **Can the throw fire where the old code was correct?** **Yes**, and the author's probe on
  `8d7b8b2` confirmed it with a SIGSEGV. Not re-derived here; section 2 takes it as settled.

### I2 in detail

The reverted state is internally coherent and the cost is documented in three places with a case
that pins it (`EventLoop_test.cpp`, *teardown drops borrowed work still waiting in the inbound
queue*; mutating teardown to resume inbound work turns `CHECK_FALSE(resumed)` red). The two
pre-existing cases that contradicted the move are real, and refusing the exculpatory stories about
the detached D: volume was the right call.

The concern is the **justification**, which the CHANGELOG and `async-and-net.md` now carry as a
rule: *"the loop cannot ask a borrowed `std::coroutine_handle<>` what it names"*. That is true —
and it is equally true of the **ready queue**, which teardown **does** resume. `submit(std::coroutine_handle<>)`
is a public `IExecutor` member taking a bare handle; called from inside a turn it lands in `_ready`
(`EventLoop.cpp:505-508`), and `~EventLoop` step 2 then resumes it. A never-started lazy `Task`
submitted that way is started by the destructor, exactly as it would have been from the inbound
queue. So the discriminator that was actually applied is *which container*, and the one that was
written down — "a turn accepted it" — does not bear on the hazard it is offered to explain. The
same `submit()` call, from the same thread, is resumed or dropped depending only on whether a turn
happened to run in between; that is the incoherence I2 named, and the revert documents it rather
than resolving it. I am not asking for the move back: the two failing cases settle that. I am
recording that the rule as written would, applied honestly, also forbid resuming the ready queue,
and that nothing in the tree covers a bare-handle submission that reaches `_ready`.

### I3 in detail — the predicate, per member

`teardownIsSerialisedWithDispatch()` is `!running() || isOnWorkerThread()` (`EventLoop.hpp:499-502`).

| Member | State it mutates | Predicate right? |
|---|---|---|
| `spawn` | `_roots` (list), `_rootByHandle` (map), `_ready` | Yes — the one the review named, and the docs now say so (`EventLoop.hpp:573-579`, `docs/design/threading.md`). |
| `requestStop` | `_rootStop` (thread-safe) + `unparkEverything()` → park table, `_ready`, backend detach | Yes. Its Doxygen already said "Must be called on the loop thread"; the assert makes the doc checkable. `~EventLoop` deliberately inlines `request_stop()` + `unparkEverything()` rather than calling `requestStop()`, so teardown does not re-assert. |
| `resumeSoon` | `_ready` | Yes for the state; see New 6 for the doc it contradicts. |
| `registerPark` | park table, backend attach/setInterest | Yes. Off-thread callers route through `_inbound.scheduled` and step 1 registers on the loop thread (`EventLoop.cpp:339`). |
| `unregisterPark` | park table, `_abandoned`, backend detach | Yes. Reached from `await_resume` (drain, loop thread), from `queueParkedWaiter` (loop thread) and from the new `DelayAwaiter` guard (loop thread). |
| `wakeReasonOf` | `_abandoned` | Yes. |

No legitimate caller trips one. I enumerated rather than grepped for the risky direction:
`~EventLoop` and its drain run with `running() == false` (no `WorkerIdentity::Scope` is claimed in
the destructor), so every `unregisterPark`/`wakeReasonOf` reached from a frame unwinding at
teardown passes. A loop destroyed from inside its own posted callback has `running() && isOnWorkerThread()`
and passes. The in-tree `requestStop` callers are `BackendParity_test.cpp:378` (inside a flow),
`:778` (between turns), `EventLoop_test.cpp:659` (inside a `post`), `InterruptibleSleep_test.cpp:184`,
`:204` and `Timers_test.cpp:391` — all on the loop thread or with nothing driving. The only
`resumeSoon` callers are `ClockRefresh_test.cpp:162` and `EventLoop_test.cpp:873`, both from inside
an `await_suspend` on the loop thread.

What the predicate still cannot catch is unchanged and is not new: `!running()` is true in the gap
between two `runOnce()` calls driven by another thread, which is the same "nobody is driving right
now is not nobody else can start" that `submit` calls out at `EventLoop.cpp:501-504`. That is the
project's accepted G5 approximation, not a fix-round defect.

### The `DelayAwaiter` scope guard (the second range)

**The unreachability claim is correct, and the guard is correct.**

- *Unreachable.* The only throw between the park and `registered = true` is
  `_cancelReg.emplace(_token, lambda)`. `core::async::StopCallback` registers an **intrusive**
  node (`StopToken.hpp:160-183`, `StopCallbackNode` with `_previous`/`_next` pointers), so it
  allocates nothing; `std::stop_callback` on the real `<stop_token>` is likewise intrusive. The
  remaining candidate is constructing the `std::function<void()>` from `[loop = _loop, park = _park]`
  — 8 bytes plus a `ParkId` (one `std::uint64_t`, `ParkTable.hpp:47-59`) = 16 bytes, trivially
  copyable. That fits the small-object buffer of libstdc++ (16 bytes), libc++ (24) and MSVC (64),
  so no allocation occurs on any shipped standard library. The defect is closed before it can be
  reached.
- *Correct.* `[&]` reads `_park` at guard-destruction time, so a throw from `emplace` unregisters
  the id `registerPark` had just returned, not a stale one; `registered = true` after the emplace
  keeps the normal path a no-op; `ScopeGuard`'s `is_nothrow_invocable_v` constraint is satisfied by
  the explicit `noexcept` and `unregisterPark` is itself `noexcept`, so the marking is honest; and
  the `[expr.await]` reasoning in the comment — a throw from `await_suspend` resumes the awaiting
  frame with the exception in place of the `co_await`, never running `await_resume` — is right.

Two residues: the guard's claim to also cover "a throw from `registerPark` itself" is weaker than
stated (New 4), and the same defect at the same shape was left open at a third site (New 2).

---

## New defects the fix round introduced or left open

### New 1 — superseded. `blockOn` refuses a flow whose continuation is in flight on another thread, and destroys the frame that thread is running in.

**This is C1, re-opened; it is recorded as NOT ADDRESSED above and is not a separate finding.** The
material below was written before the author's probe landed and is kept only for the parts the
probe does not cover: that no test in this tree could have caught it, and that the cheap correct
shape exists. Section 2 carries what is new.

`src/core/net/EventLoop.hpp:264-284`, `src/core/net/EventLoop.cpp:963-966`, `CHANGELOG.md` (the
`blockOn()` migration bullet).

`hasPendingWork()` is `!_ready.empty() || _parks.size() != 0 || !_closedParks.empty() || hasInbound()`.
It is false for a whole class of flows that are *not* deadlocked — they are running somewhere else
and will come back:

```cpp
Task<int> compute(EventLoop& loop, IExecutor& pool)
{
    co_await ResumeOn { pool };     // handle is now the pool's; nothing is on the loop
    // ... seconds of blocking work on the pool thread ...
    co_await ResumeOn { loop };     // comes back through _inbound
    co_return 42;
}
auto const v = loop.blockOn(compute(loop, pool));
```

Turn 1 drains the root; `ResumeOn::await_suspend` hands the handle to the pool and returns void
(`ResumeOn.hpp:37-41`). Back in `blockOn`: `_ready` empty, `_parks` empty, `_closedParks` empty,
`_inbound` empty. `task.done()` is false. **Throw.** The by-value `task` parameter then destructs
as the exception unwinds, destroying a coroutine frame **while the pool thread is executing inside
it**. This is not a rare interleaving: the pool will not finish within the nanoseconds of one turn,
so the throw is the normal outcome and the use-after-free is the normal consequence.

Three things make this worth acting on rather than noting:

1. **It is a regression from working code.** Before B4, `blockOn` spun (core-cpp#17) — it burned a
   core and then **completed correctly**, because the pool's `submit()` back to the loop landed in
   `_inbound` and the next turn picked it up. B4 replaced that with UB; the fix round replaced the
   UB with a throw. Relative to the UB this is an improvement; relative to the behaviour the issue
   was filed against it is a new failure of a program that worked.
2. **It is shipped as intended.** The CHANGELOG names "a `co_await ResumeOn { pool }` that is live
   on a pool thread" as an example of "cannot advance", and `ResumeOn`'s own header documents that
   move as its purpose — *"how a blocking, seconds-long job is moved off a loop onto a pool"*
   (`ResumeOn.hpp:19-22`). The release notes therefore tell contour, endo and tuidu that a
   correct use of one public API is a logic error in another.
3. **Nothing in the tree would have caught it.** I enumerated rather than trusting a grep: no test
   file under `src/core/net/` constructs a `ThreadPoolExecutor`, and `ResumeOn` appears in only two
   net tests (`EventLoop_test.cpp`, `LoopTeardown_test.cpp`), neither combining it with `blockOn`
   across a thread. `emscripten` cannot reach it at all (`ThreadPoolExecutor.hpp:15` `#error`s
   there). A green 25/25 says nothing about this path. What a grep for `blockOn` *would* miss is
   the consumer side: contour, endo, fastcached, tuidu and `dbtool` are not in this tree, and
   whether one of them blocks on a flow that offloads is the consumer-impact question this round
   did not ask.

The cheap correct shape exists and is one line of policy: `blockOn`'s turn currently never blocks,
because `idleWait` is `_inRun && idle == Block` and `_inRun` is false outside `run()`
(`EventLoop.cpp:283`). Letting `blockOn` wait on the backend — whose wake channel is exactly what
cross-thread `submit()`/`schedule()`/`post()` already ring (`:514`, `:535`, `:605`) — fixes
core-cpp#17 without the spin *and* keeps the offload pattern working, leaving the throw for a
genuine deadlock only if the wait is bounded first. I am reporting the defect, not prescribing the
fix; the point is that the round chose diagnosability over correctness without recording the trade.

### New 2 — Important. `WaitHandleAwaiter::await_suspend` has the defect the guard closes, and is the site where it hurts.

`src/core/net/EventLoop.hpp:870-886`.

```cpp
_park = _loop.registerPark(ParkEntry::onReadiness(...), &_refusal);
if (!_park) return false;
_cancelReg.emplace(_token, [&loop = _loop, park = _park] { loop.requestCancel(park); });   // <-- unguarded
return true;
```

Three sites in this module have the park-then-arm-the-stop-callback shape. `27b8b43` guarded
`DelayAwaiter`; `8d7b8b2` guarded `InterruptibleSleep.cpp:85-90`; this one was not swept. It is the
worst of the three, because a readiness park is also a **backend registration**: on the throw the
awaiting frame unwinds and is destroyed, while the park stays in `_parks` with `attached == true`,
its `ReadinessHandler` still registered and its `owner` pointing at storage that still exists but
whose `parked` names a destroyed frame. The next readiness on that descriptor reaches
`onParkReady` → `queueParkedWaiter` → `takeWaiter` returns the dead handle → `work.resume.done()`
is queried on a destroyed frame and the loop then queues it for resumption. `DelayAwaiter`'s
version leaks a heap slot; this one is a use-after-free.

By the same SBO argument as above the throw is unreachable today (the lambda is a reference plus a
`ParkId`, 16 bytes, trivially copyable). But the follow-up commit's whole argument is that the
unreachable version was worth closing, and it closed two of three. The rulebook entry the same
range added says it best: *"When a function joins a family that all do X, 'why does this one not do
X' is answered out loud or it is not answered."*

### New 3 — Minor. `readinessCount()` / `parkedWaiterCount()` now over-reports where it used to under-report, and the new documentation states the invariant it breaks.

`src/core/net/detail/ParkTable.hpp` (`readinessCount`), `src/core/net/EventLoop.cpp:896-922`,
`:859-876`, `:924-955`.

The new doc reads *"One per registration the backend has on this table's account"*. Three paths
detach a park and set `attached = false` while leaving it in `_parks` and therefore in `_byHandle`:
`notifyHandleClosing` (`:911-915`), `resolveCancel` (`:870-874`) and `unparkEverything`
(`:948-952`). In each window the count exceeds the backend's registrations. The fix round's own
case demonstrates it and stops one line short of saying so: after
`loop.notifyHandleClosing(...)` it asserts `source.attachedCount() == 0` but does not assert
`parkedWaiterCount()`, which reads 1 there.

The direction is the safe one — the six `REQUIRE(loop.parkedWaiterCount() == 0)` "no registration
leaked" assertions now fail loudly instead of passing while a registration is live, which was I1's
complaint — so this is a documentation defect, not a behaviour one. The honest sentence is "one per
park that still holds a handle key", and the attachment is `Park::attached`.

### New 4 — Minor. `ParkTable::add` is not exception-safe, so the guard's "covers a throw from `registerPark` itself" holds only for throws before `add` mutates.

`src/core/net/detail/ParkTable.hpp:255-276`, `src/core/net/EventLoop.hpp:795-813`.

`add` emplaces into `_byWaiter`, then `_byHandle`, then pushes the timer slot and `++_liveTimers`,
and only then `_parks.emplace(id, std::move(park))`. A `bad_alloc` from that last emplace leaves
the reverse indices and the heap slot naming an id that is not in `_parks`, and `_liveTimers`
permanently over-counted — `timerCount()`/`pendingTimerCount()` never recovers for the life of the
loop. The `DelayAwaiter` guard cannot undo it, because `_park` was never assigned and
`unregisterPark(ParkId::invalid())` is the documented no-op. `pruneTimers` does drop the orphaned
heap slot (the id is not in `_parks`), and a stale `_byWaiter` key resolves to a `take()` that
returns null, so nothing here is a crash — but the comment at `EventLoop.hpp:800-801` claims a
coverage it does not have.

### New 5 — Minor. Six new asserts, no CHANGELOG entry and no case that proves any of them fires.

The `Breaking` section documents the `blockOn` throw, `FdRegistrationFailed::reason`, the inbound
drop and the readiness-park change, but says nothing about `requestStop`, `spawn`, `resumeSoon`,
`registerPark`, `unregisterPark` and `wakeReasonOf` becoming precondition violations. A consumer
that calls `requestStop()` from a watchdog thread has been racing silently and now aborts in Debug
— which is the point, and is exactly the kind of behaviour change the workflow checklist's step 5
asks for a line about. (The generic "the thread-affinity guarantees, all asserted" in the entry's
first paragraph predates this round and does not name these members.)

Separately: this project owns the instrument for proving an assert fires — `core-cpp.hostdriven-canary`,
a `WILL_FAIL` process per mode — and none of the six has one. B5's `addTimer`/`cancelTimer` asserts
are in the same position. Six asserts nobody has seen fire are six claims, and a gate that does not
report reads as passed.

### New 6 — Minor. `resumeSoon`'s Doxygen names a caller the new assert forbids.

`src/core/net/EventLoop.hpp:508-511`: *"What every backend, completion, stop and thread-pool
callback reaches, and it ENQUEUES — it never resumes. Loop thread only."* A thread-pool callback is
by definition not on the loop thread, and a stop callback runs wherever `request_stop()` was called
— which is why the neighbouring `requestCancel` is documented **"Safe from any thread"**
(`:537-539`) and routes through the inbound queue. The sentence now describes a call that aborts.
No in-tree caller does it; the doc is what will mislead the next one.

### New 7 — Minor. M3's out-parameter is a public signature that the house rules say should be an `expected`, and it is an unrecorded deviation from the spec's interface block.

`src/core/net/EventLoop.hpp:513-523`. `registerPark` is in the public *Awaiter-facing scheduler
primitives* block, so `ParkId registerPark(ParkEntry, NetError* refusal = nullptr)` is public API.
`.agent/rules/design-principles.md`'s rule is that a fallible API is an `std::expected`; this is a
nullable raw out-parameter whose stated justification — *"so that the callers who only want a
`ParkId` ... are unchanged"* — is convenience, not design. `std::expected<ParkId, NetError>` costs
the four existing call sites a `.value_or(ParkId::invalid())` each. Also: the review recorded that
the `EventLoop` declaration matched the spec's interface block "member for member", and this
changes one of those members; B4's report flags `IoBackend::setPump` as a deliberate spec deviation
but nothing flags this one.

---

---

# Section 2 — what C1 opens

C1's diagnosis is taken as settled. What follows is the four questions it raises, answered by
reading the committed tree at `7bdd132`.

## 2.1 Every path by which `blockOn` can return with the task unfinished

The ruled fix — wait on the backend instead of throwing — makes the *normal* loop safe, because
the loop then exits only on `task.done()`. It does not make `blockOn` safe, because
`blockOn` frees the frame on **every** exit, and three of the exits do not check `done()`.

`blockOn`'s parameter is `async::Task<T> task`, taken **by value**, so the frame is destroyed by
the parameter's destructor on *any* return or throw (`EventLoop.hpp:251-286`).

| # | Exit | Reached by | `done()`? | What still names the frame |
|---|---|---|---|---|
| 1 | `return task.result()` | the loop condition | yes | nothing — safe |
| 2 | `throw std::logic_error` | `!hasPendingWork()` | **no** | C1. Third-party waiter lists, another executor |
| 3 | **an exception escaping `turn()`** | see below | **no** | **the loop's own `_ready`, `_parks` and `_byWaiter`** |
| 4 | `task.result()` rethrows the body's exception | a flow that threw | yes | nothing — safe |
| 5 | `queueReady` / `turn`'s `push_back` throwing `bad_alloc` | allocation failure | **no** | same as 3 |

**Exit 3 is the one that survives the ruled fix, and it is not cross-thread — it is single-threaded
and in-process.** `turn()` is not `noexcept` and has three ways to let an exception out:

- **A timer callback.** `runDueCallback` calls `entry->onExpired(entry->callbackState)`
  (`EventLoop.cpp:468`), and `TimerCallback` is documented **"Not `noexcept`, because the coroutine
  resumptions it is queued beside are not either: an exception leaving one propagates out of the
  turn and out of `run()`"** (`ParkTable.hpp:87-97`). Out of `run()` — and out of `blockOn`.
- **A posted callback.** `runInbound` calls `callback()` for each `_inbound.posts` entry
  (`EventLoop.cpp:334-335`); `post` takes an arbitrary `std::function<void()>`.
- **A resumed bare handle.** `entry.parked.resume()` (`:391`) resumes whatever `submit(std::coroutine_handle<>)`
  was given. A `Task` promise swallows into `promise.exception`; a promise that is not a `Task`'s
  need not.

Concretely, and with nothing exotic: `blockOn` a root that `co_await loop.delay(1s)`; arm an
`addTimer` whose callback throws. Turn N step 5 fires the timer; turn N+1 step 2 runs the callback;
it throws; the exception leaves `drainReadyQueue`, leaves `turn`, leaves `blockOn`'s `while`;
`task` destructs and frees the root frame — **while `_parks` still holds a `Park` whose `parked`
names that handle and `_byWaiter` is keyed on its address**. The loop survives (it is the caller's
object). Its next turn fires the deadline, `queueParkedWaiter` calls `work.resume.done()` on
destroyed storage and queues it for resumption. `cancelPending(handle)` from anywhere in between
reads the same freed frame through `_ready`.

The same hole exists for a root sitting in `_ready` (submitted or spawned-adjacent work queued but
not yet drained) and for `_inbound.submissions`.

**This is pre-existing** — the `break` version had it, and so did pre-B4 — but it is the class C1
belongs to, it is reachable without a second thread, and the ruled fix does not touch it. The shape
that closes exits 2, 3 and 5 at once is one statement, because `cancelPending` already searches all
three containers (`EventLoop.cpp:538-593`) and already disarms rather than releases:

```cpp
queueReady(async::ParkedWork { .resume = task.handle() });
// Runs on every exit, including the throw and an exception out of a turn: the frame is about to
// be destroyed with the parameter, and the loop must not still name it.
auto const detach = detail::ScopeGuard { [this, &task]() noexcept {
    std::ignore = cancelPending(task.handle());
} };
```

On the normal path it returns `false` and costs three lookups. It cannot close the *third-party*
case (an `AsyncQueue` waiter slot, a pool's queue), which is C1 proper and needs the loop to stop
refusing rather than to clean up better.

Two further exits that are not UAFs but are worth having on the list:

- **`IdlePolicy::Return` has no exit at all**, and that is the sharper half of C1. `computeTimeout`
  forces a zero timeout on such a loop (`EventLoop.cpp:430-431`), so the ruled fix — wait on the
  backend — **does not work there**: the wait returns instantly and `blockOn` spins. And because a
  parked flow keeps `hasPendingWork()` true, the throw never fires either. `testing::TestLoop`
  forces `Return`. So `testLoop.blockOn(flowParkedOnADeadlineNobodyAdvances())` burns a core until
  ctest's backstop — core-cpp#17, unrepaired, in the loop type every B5/B12 test author will reach
  for. See 2.4: the round answered this as M4, by documenting it, in the same header and eight
  lines above the throw that exists to prevent exactly it.
- **`stop()` does not end `blockOn`.** The loop reads `task.done()` and never `stopRequested()`, so
  `stop()` affects `run()` only. Its Doxygen says as much (`EventLoop.hpp:288`), so this is
  consistent, not a defect — but it means a caller's shutdown path cannot get a `blockOn` back.
  `requestStop()` does, and safely: the root's stop token is the root source, so the flow unwinds
  through `OperationCancelled`, `done()` goes true and `result()` rethrows it (exit 4).
- **An empty task is UB on line one.** `blockOn(Task<T> {})` reaches
  `task.handle().promise().setStopToken(...)` with a null handle before any guard. `Task::done()`
  answers `true` for an empty task (`Task.hpp:259`), and `Task::result()` has an explicit
  `refuseEmptyTask()` for exactly this state — `blockOn` dereferences first and asks later. One
  `if (!task.handle()) return task.result();` (which then refuses by name) closes it. Minor,
  pre-existing.

## 2.2 Where else the same false premise is built in

The premise is *"if this loop has nothing to do, nothing can be happening to this flow."* I
enumerated every member that draws a conclusion from the loop's own containers being empty.

**`~EventLoop` — Important, and the public documentation understates it.** The CHANGELOG states
the cost of the I2 drop as *"a cross-thread `ResumeOn { loop }` whose loop dies before the next turn
leaves its awaiting flow suspended forever."* That is the benign half. The other half is that the
pool thread has not submitted **yet**: when it does, `ResumeOn::await_suspend` calls
`target.submit(...)` → `EventLoop::submit` reads `_worker`, locks `_inboundMutex`, pushes to
`_inbound` and calls `_backend.wake()` (`EventLoop.cpp:497-515`) — **all on a destroyed loop**.
That is a use-after-free of the loop object, not a stranded flow, and it is the *more likely*
ordering, because a loop is usually destroyed while the offloaded work is still running.

G5 does not cover it: `teardownIsSerialisedWithDispatch()` is `!running() || isOnWorkerThread()`,
and a pool thread that merely *holds a handle it will submit back* is not driving, so the assert
passes and the destructor proceeds. The guarantee is about concurrent **dispatch**; it says nothing
about pending **hand-off**. The same false premise, one layer down: the loop concludes from its own
emptiness that nothing else is mid-flight.

`abandonParkedWork`'s fixpoint does not help — it swaps `_inbound` under the lock until three
containers come back empty (`:152-183`), which is a fixpoint over work **already handed over**, not
over work that will be.

**`runUntilIdle` and `RunOnceResult::idle` — clean.** `idle` is `!hadInbound && drained == 0 &&
dispatched == 0 && fired == 0` (`:310`), and the comment beside it already refuses to over-claim:
*"It deliberately says nothing about whether work is still PARKED."* Neither member tells a caller
the flow is finished, so neither acts on the premise. `runUntilIdle`'s Doxygen is likewise about
turns, not about completion. No change owed; worth noting that these are the shape `blockOn` should
have had.

**`hasPendingWork()` itself — Minor, and it is where the premise is spelled.** The name says "is
there pending work", the body answers "are any of *my four containers* non-empty"
(`EventLoop.cpp:963-966`), and `blockOn` reads it as "can this task advance". It is used in exactly
one place (I grepped the whole tree: `EventLoop.hpp:277` is the only call site; `:667` is the
declaration and `EventLoop_test.cpp:1192` is a comment). Renaming it to what it answers —
`hasWorkOfItsOwn()` — would have made C1 visible at the call site, because
`if (!hasWorkOfItsOwn()) throw "the task can no longer be advanced"` does not read as a valid
inference.

**`blockOn`'s `until` short-circuit — clean but worth knowing.** `turn` skips step 4 once
`until.done()` (`:288`), which is correct and is not this premise.

## 2.3 Are any of the six asserted members legitimately callable off-thread?

**Today, no — none of the six is reached off-thread anywhere in this tree, so no assert is a live
defect.** I enumerated the callers rather than trusting the grep's shape: `requestStop`
(`BackendParity_test.cpp:378`, `:778`, `EventLoop_test.cpp:659` via `post`,
`InterruptibleSleep_test.cpp:184`, `:204`, `Timers_test.cpp:391`), `resumeSoon`
(`ClockRefresh_test.cpp:162`, `EventLoop_test.cpp:873`, both inside an `await_suspend` on the loop
thread), and `registerPark`/`unregisterPark`/`wakeReasonOf` (reached only from the two awaiters'
`await_suspend`/`await_resume`, which run in the drain). `spawn` is loop-thread-only and correctly
asserted. What a grep would miss, and what I therefore checked by hand, is the *stop-callback*
direction: both awaiters arm `loop.requestCancel(park)` (`EventLoop.hpp:812`, `:884`), and
`requestCancel` is **not** one of the six — it is thread-safe by construction and routes through
`_inbound.cancels` (`EventLoop.cpp:833-857`). That is the right split, and it is the reason the six
are currently safe.

Two things to act on anyway:

**(a) `resumeSoon`'s documented contract is the next instance, and B6/B7 will reach it.** Its
Doxygen reads *"What every backend, completion, stop and thread-pool callback reaches … Loop thread
only"* (`EventLoop.hpp:508-511`). A thread-pool completion is off-thread by definition; a stop
callback runs wherever `request_stop()` was called. The assert now makes that documented call an
abort. And the loop already has the member that does it safely — `submit(async::ParkedWork)` is
`resumeSoon` plus the off-thread hand-off (`EventLoop.cpp:497-515`). Two public members performing
the same operation with different thread rules, where the stricter one's documentation advertises
the laxer one's callers, is how the next `EventLoop::spawn` gets written. Either `resumeSoon`
routes through `_inbound` off-thread like its sibling, or its first sentence loses the callers it
cannot serve.

**(b) The sweep took the review's list instead of the review's criterion, and missed the member
with the widest destructor surface.** The review named five (`resumeSoon`, `registerPark`,
`unregisterPark`, `wakeReasonOf`, `requestStop`) as *examples* — "worth the same sweep over" — and
the round asserted exactly those five plus `spawn`. Two documented loop-thread-only mutators were
left unasserted:

- **`notifyHandleClosing`** — *"Call this BEFORE the `close()` syscall, **on the loop thread**"*
  (`EventLoop.hpp:459-461`). It writes `_closedParks`, inserts into `_abandoned` and calls
  `_backend.detach` (`EventLoop.cpp:896-922`). Its callers are **every socket and listener `close()`
  and destructor**: `PosixSocket.cpp:83`, `PosixListener.cpp:49`, `UnixListener.cpp:134`,
  `SocketsPosix.cpp:39`, `WindowsSocket.cpp:61`, `WindowsListener.cpp:118`, `SocketsWin32.cpp:40`.
  That is the exact argument B5 wrote for asserting `cancelTimer` — *"this is the half that needs
  it more: `addTimer` is called where the author of the timer chose, while this is reached from
  `~DeadlineTimer` — wherever the object owning the timer happens to be destroyed"*
  (`EventLoop.cpp:695-698`). A socket is destroyed in more places than a `DeadlineTimer` is.
- **`cancelPending`** — documented loop-thread-only, mutates `_ready` and the park table
  (`EventLoop.cpp:538-593`).

Neither is a bug today; both are the same claim as the six, unasserted, in the members a consumer
touches most.

## 2.4 Where the round's own findings contradict each other

**M4 and C1 are the two halves of one question and got opposite answers, eight lines apart in the
same header.** C1's `@throws` says a flow that cannot advance must be refused *"because answering
with a value it never produced would hide it; a loop that kept turning would spin at full CPU
instead, which is what this replaces"* (`EventLoop.hpp:244-250`). M4's paragraph, immediately above
at `:234-239`, says that on an `IdlePolicy::Return` loop `blockOn` **does** spin at full CPU, and
resolves it by telling the reader not to do that. A `TestLoop` flow parked on a `ManualClock`
deadline nobody advances is exactly as unadvanceable as the `AsyncQueue` in C1's cases — the only
difference is that one leaves a park behind and the other does not, which is a fact about the loop's
bookkeeping, not about the flow. So the round shipped: *unadvanceable with nothing parked → throw;
unadvanceable with something parked → spin forever*. Neither the review nor the round noticed,
because C1 and M4 were filed as a Critical and a Minor rather than as one question.

The rulebook entry `27b8b43` itself added, three files away, is the diagnosis: *"A rule written
beside the code is not a rule the code applies, and adjacency makes that harder to notice rather
than easier — a reader who has just read the rule carries it into the lines below and supplies it
from memory."* It was written about the `@throws` clause sitting above a `break`. It applies again
to the same clause sitting below M4's paragraph.

**I2's worked example is C1's program.** The review's I2 used
`co_await ResumeOn { pool }; … co_await ResumeOn { loop };` (review lines 236-243) and concluded
the flow is *stranded* at teardown. Under C1's fix that program never reaches teardown — `blockOn`
throws the moment the pool takes it. One example, two findings, two unrelated remedies, and the
second makes the first unreachable. Neither document notes the other.

**And the third instance of the same example is in the tree already**, unconnected to either:
`ResumeOn.hpp:19-22` documents the pool hop as one of the two things `ResumeOn` is for.

## 2.5 What each of the nine findings' instruments was derived from

The lead's question — *are its tests derived from the review's example, or from the claim?* — is
the right one, and applied across the round it is worth more than the findings.

| Finding | Instrument | Derived from |
|---|---|---|
| **C1** | two cases, `AsyncQueue` + `InertExecutor` | **the review's example.** Both arms are the reproducer at review lines 99-108 with the executor swapped for an inert one — i.e. made *more* unadvanceable. Every instrument in the chain (reproducer → cases → ten gates) is one check counted three times. |
| **I1** | one case, single park on a pipe | **the example.** The review supplied the recipe verbatim ("park a flow on a pipe, make it readable, run one turn, call `notifyHandleClosing` *before* the resuming turn") and the case is that, plus the second turn the author found by predicting a failure and getting a pass. **The claim was wider than the example**: the review's own text named the sharper shape — *"Sharpest with two parks on one descriptor — a reader beside a writer, which is the case `_byHandle` is a multimap for"* — and that is precisely what `dropHandleIndex`'s erase-this-park-alone loop implements. **No case covers it.** A mutation replacing that loop with `_byHandle.erase(park.handle)` stays green. |
| **I2** | one case, `handOverToLoop` on one thread | **the example, narrowed.** The review's example was a pool hop; the case is a same-thread, off-turn `submit` — which proves "off-turn submissions are dropped" but not the cross-thread claim the CHANGELOG's stated cost is about, and cannot reach the `~EventLoop` UAF in 2.2 at all. |
| **I3** | none | **neither.** Six asserts, no case, no canary — and this project owns the instrument (`core-cpp.hostdriven-canary`, `WILL_FAIL` per mode). |
| **M1** | `&EventLoop::blockOn<void>` | **the claim.** The odr-use *is* the property. The one instrument in the round that cannot agree with itself for the wrong reason. |
| **M2** | none (doc) | n/a — but `timerSlotCount()` and `Timers_test.cpp`'s lazy-pruning case, added by B5, are the measurement, and they exist. |
| **M3** | none | **neither, and it is unpinned.** I grepped every `*_test.cpp` under `src/core/net/`: nothing asserts `FdRegistrationFailed::reason`. `EventLoop_test.cpp:112` catches the type and ignores the member. A mutation deleting `if (refusal != nullptr) *refusal = …` (`EventLoop.cpp:783-784`) stays green on every platform. |
| **M4** | none (doc) | the example — and see 2.4: documenting it is what let it contradict C1. |
| **M5** | one case (b only) | (a) comment accuracy, unpinnable. (b) `REQUIRE(loop.parkedWaiterCount() == 1)` is a real case. (c) the `_abandoned` erases are unpinned — nothing observes the set's size, so all three `erase` calls could be deleted and every gate stays green. |

Three of the nine are unpinned (I3, M3, M5c), one is pinned by the example rather than the claim in
a way that leaves the multimap arm uncovered (I1), and one — C1 — had every instrument built from
the same example, which is why ten gates and two careful readers agreed.

The general form, which belongs in `.agent/rules/testing.md` beside *"assert what distinguishes"*:
**when a review hands you a reproducer, the reproducer is an existence proof, not the claim.
Writing the case from it means the case can only fail the way the reviewer already knew it would.
Read the finding's *sentence* for the quantifier — "a flow suspended on something this loop does not
drive" admits two populations, and the reproducer instantiated the harmless one.**

---

## What I did not verify

- No build, no test run, no sanitizer run. Every claim above is read from the committed tree at
  `7bdd132` and names its file and line. B4's `hopToPoolAndBack` probe was not re-run; C1 is taken
  as settled on the author's evidence.
- Section 2.1's exit 3 (an exception escaping `turn()` freeing a frame the park table still names)
  is derived by reading, not by running. The reproducer is one `addTimer` whose callback throws
  under a `blockOn` whose root is parked on a `delay`, and it should be written before it is
  believed — which is the whole point of 2.5.
- Consumer source (contour, endo, fastcached, tuidu, `dbtool`, morph) is not in this checkout, so
  New 1's and New 5's consumer impact is stated as a question to ask, not as a measured fact.
- `ctest -L hygiene`, `renames.json`, `provenance.md` and `mkdocs build --strict`: read for sense
  only. The extra `FastCache/Async/IReactor.hpp` include row is well-formed and its note is
  accurate about where a caller of `addTimer`/`submit`/`blockOn` has to arrive.
- The three "known and not done" dead-stack comments from the fix-round report were in fact landed
  in `27b8b43` (`EventLoop_test.cpp:618`, `:671`, `HostDrivenLoop_test.cpp:69`); that item is
  closed.
