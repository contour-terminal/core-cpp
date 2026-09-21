# Task B1 review — `core::async` grafts

**Reviewer:** review-B1
**Scope:** `b21229d`, `f72cabc`, `7838b53`, `78bf537`. Nothing else on `master` is this task's.
**Requirements:** `task-B1-dispatch.md`, design spec §2 item 6, plan Task B1.

## Verdicts

- **Spec compliance: PASS.** Every line of spec §2 item 6 is delivered, and nothing beyond it.
- **Task quality: CHANGES REQUESTED.** One Critical and three Important findings, two of them
  demonstrated by running. The Critical one is a segfault reachable from a twelve-line program
  written entirely in this module's public vocabulary.

**Verdict: Changes requested.**

---

## Spec compliance

Spec §2 item 6 asks for contour's `Task` as the base, gaining `unownedRoot` propagation, an
awaiter that takes ownership from the rvalue `Task`, `release()`, no default-constructible-`T`
requirement, and `DetachedTask`, `syncRun`, `ParkedWork`/`detail::Parked`, `IExecutor` (with
`using IExecutor::submit;`), `ResumeOn`, `ThreadPoolExecutor`, `AsyncQueue` (stop-aware `pop`).

All present:

| Requirement | Where | Note |
|---|---|---|
| `unownedRoot` propagation | `Task.hpp:100,199,312`; `Join.hpp:115,260` | Carried further than upstream: through the join runners. That extension is real and correct in intent — and is what Finding C1 rides on. |
| Owning awaiter from the rvalue `Task` | `Task.hpp:174-219,234` | Rvalue-qualified, `_handle.release()` into a `detail::UniqueCoroHandle`. |
| `release()` | `Task.hpp:244,350`; `UniqueCoroHandle.hpp:62` | |
| No default-constructible `T` | `Task.hpp:127-135,207,258` | The `T {}` is gone from every path. |
| `DetachedTask` | `DetachedTask.hpp` (R66) | |
| `syncRun` | `SyncRun.hpp` (R66), plus `syncRunWith` | |
| `ParkedWork` / `detail::Parked` | `ParkedWork.hpp:44,101` | |
| `IExecutor` + `using IExecutor::submit;` | `IExecutor.hpp:30`; `ThreadPoolExecutor.hpp:90` | See Finding I2 on what the check proves. |
| `ResumeOn` | `ResumeOn.hpp:23` | |
| `ThreadPoolExecutor` | `ThreadPoolExecutor.hpp` (R65, header-only) | Faithful inlining — see Q5. |
| `AsyncQueue`, stop-aware `pop` | `AsyncQueue.hpp:124,248,325` | Throws `OperationCancelled`, leaves no waiter parked. |

Beyond scope: nothing. The four departures from upstream layout are each recorded and each
defensible — `unownedRootOf`/`parkedWorkFor` living in `ParkedWork.hpp`, the `CarriesUnownedRoot`
concept in place of `std::is_base_of_v<TaskPromiseBase, …>`, `syncRunWith` as a separate name, and
`Join.hpp`. The `CarriesUnownedRoot` substitution is the right call and is the only way the join
runners could carry the answer at all.

Missing: nothing from the spec. The three deferred A11 defects are all addressed (§ below).

---

## Findings

### Critical

#### C1 — A `whenAll`/`whenAny` fan-out gives the same chain root to N parks, and `detail::Parked` destroys it once per park

`src/core/async/ParkedWork.hpp:47-48` (`ParkedWork::abandon`), `:165-170` (`Parked::abandon()`),
`:144-154` (`Parked::resume()`); `src/core/async/Join.hpp:253,260`; `src/core/async/Task.hpp:199`,
`:312`.

`ParkedWork::abandon` is a *right to destroy one frame*. `Join.hpp:253` computes the chain root
once and `:260` copies it into **every** runner promise; `Task.hpp:199` then copies it on down into
every child task each runner awaits. So when two children of one `whenAll` park at the same time,
the executor holds two `ParkedWork` values naming the **same** `abandon` handle. A `detail::Parked`
container — which `ParkedWork.hpp:98-100` says is the shape an executor should use, and which this
module's own test double does use (`ParkedWork_test.cpp:121`) — then destroys that frame once per
entry.

**Verified by running.** A reproducer using only this module's public vocabulary plus
`ParkedWork_test.cpp`'s own `QueuedExecutor`:

```cpp
DetachedTask parkTwoUnderWhenAll(IExecutor* executor, FrameSentinel s, Counters* c)
{
    co_await whenAll(parkInner(executor, FrameSentinel { c }, c),   // each does
                     parkInner(executor, FrameSentinel { c }, c));  // co_await ResumeOn { *executor }
    ++c->completed;
}
```

```
    submit: resume=0x5cf3e62180d0 abandon=0x5cf3e6218020
    submit: resume=0x5cf3e6218140 abandon=0x5cf3e6218020
  parked=2 pending=2 nonEmptyAbandons=2 distinctAbandons=1
  >>> TWO PARKS NAME THE SAME abandon HANDLE <<<
  -- destroying the executor with both parks held --
Segmentation fault
```

- Plain `clang++ -std=c++23 -g` build: **SIGSEGV**.
- `-fsanitize=address,undefined`: `AddressSanitizer: heap-use-after-free`, 160-byte region, freed
  and re-read on the same teardown path.
- Identical with `whenAny` in place of `whenAll`.
- Identical on both StopToken branches (`-DCORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK`).

**Why the existing tests do not catch it.** `ParkedWork_test.cpp:315` and `:336` are exactly the
right shape — a detached root, a combinator, a park, teardown — at **arity one**
(`parkUnderWhenAll` / `parkUnderWhenAny` each take a single child, `:199-212`). One park means one
`abandon`, so they pass. The failure needs two.

**Why it is not firing today, and why that does not make it safe.** `ThreadPoolExecutor` discards
`abandon` outright (`ThreadPoolExecutor.hpp:127`, `submit(ParkedWork work) { submit(work.resume); }`),
so core-cpp's only shipped executor cannot reach it. But:

- `detail::Parked` is in the module's `FILE_SET HEADERS` (`src/core/async/CMakeLists.txt:30`), and
  `ParkedWork.hpp:98-100` documents it as the container an executor should hold its parks in;
- the module's own test double already uses it;
- B4's `EventLoop` is specified against `ParkedWork`, and the net lane's tests already write the
  failing shape — `src/core/net/BackendParity_test.cpp:147`,
  `whenAll(parkThenObserveClose(sock, …), closeAfterParked(loop, sock))`, two children that both
  park on the loop. The day `EventLoop` starts holding `detail::Parked`, this is a crash in CI.

**What the documentation currently says, which is the opposite.** `ParkedWork.hpp:41-43`:
"Destroying the root frees all of it, because ownership in a `Task` chain runs downward."
`.agent/rules/async-and-net.md:66-73` repeats it. Both are true — and both are true *once*. The
missing half of the invariant is **at most one live park per unowned root**, which held for a
linear chain and stopped holding the moment `unownedRoot` was propagated through a combinator that
fans out. That propagation is this task's own addition (the report calls it "the hole this task
found"), so this is B1-introduced, not inherited: before it, sibling parks under a combinator
answered *borrowed* and leaked whole rather than double-freeing.

I am not prescribing the fix, but the shapes available are: hand `abandon` to at most one park per
root; make the right refcounted rather than unique; or have a fan-out reset `unownedRoot` in its
children and take responsibility for freeing them itself. Whichever is chosen, the invariant needs
writing down in `ParkedWork.hpp` and a case at **arity ≥ 2** beside `ParkedWork_test.cpp:315`.

### Important

#### I2 — `JoinState::remaining` is decremented from whatever thread resumes a child, and B1 is the commit that makes a cross-thread child expressible

`src/core/async/Join.hpp:49` (`std::size_t remaining = 0;`), `:144` (`if (--join->remaining == 0 …)`),
`:266`.

The counter is a plain `std::size_t`. `FinalAwaiter::await_suspend` runs on whichever thread
resumed that child. Before B1 nothing in `core::async` could move a coroutine to another thread, so
the counter could not be touched concurrently. B1 ships `ResumeOn` and `ThreadPoolExecutor` in the
same commit, and `ResumeOn.hpp:20-22` markets precisely this move ("a blocking, seconds-long job is
moved off a loop onto a pool"). `co_await whenAll(hop(pool), hop(pool))`, where each child does
`co_await ResumeOn { pool }`, has both children reach their final suspension on two different pool
threads.

**Verified by running.** `clang++ -fsanitize=thread`, two children released simultaneously by a
gate so both finish at once:

```
WARNING: ThreadSanitizer: data race (pid=442638)
  Write of size 8 at 0x720c00000020 by thread T1:
  Previous read of size 8 at 0x720c00000020 by thread T2:
  Location is heap block of size 40 at 0x720c00000000 allocated by main thread:
```

The 40-byte block is `make_shared<WhenAllPolicy::State>` — 16 bytes of control block plus the
24-byte `JoinState`; the reported offsets land inside it. A first variant of the probe, which
polled `handle.done()`, produced the consequence rather than just the warning:

```
round 3: parent NEVER RESUMED (children done=8)
```

— every child finished and the awaiting coroutine was never resumed, i.e. a lost decrement. A plain
`-O2` build survived 500 rounds, which is the profile of a narrow race rather than evidence against
one.

Two things make this worth fixing rather than documenting away:

1. **The state is atomically refcounted while the counter it protects is not.** `shared_ptr`'s
   control block is thread-safe by construction; `remaining` next to it is not. Either the join can
   cross threads — in which case `remaining` needs to be atomic — or it cannot, in which case the
   `shared_ptr` is more than the job needs (see Q4).
2. **Nothing tests it.** `ThreadPoolExecutor_test.cpp` never mentions `whenAll` or `whenAny`; the
   `whenAll`/`whenAny` tests never mention a pool. The `clang-tsan` preset passes because the
   combination is not exercised — a gate that does not reach the code reads as passed.

Either fix works: an atomic counter, or a precondition stated in `WhenAll.hpp`/`WhenAny.hpp` ("every
child completes on the thread that awaited the join") with a case that holds the module to it.
`WhenAll.hpp:9-11` currently gestures at the single-thread assumption ("on the single UI thread
their suspensions overlap") as a description of the benefit, not as a requirement, while
`IExecutor.hpp:21-24` tells the reader the opposite ("a coroutine that has awaited its way onto one
must not assume it is still where it started").

#### I3 — The `#1041` compile-time check cannot fail on any change to this tree

`src/core/async/ParkedWork_test.cpp:221-230`; `src/core/async/ThreadPoolExecutor_test.cpp:145-147`.

This is the dispatch's question 3, and the answer is: **the control proves the instrument, not the
rule** — and I would keep it anyway, but the claim around it needs correcting.

`static_assert(!TakesOwnedWork<OnlyBorrowingSubmit>)` proves the `TakesOwnedWork` concept
discriminates: true for a type with both overloads, false for one with only the borrowing half.
That is genuinely worth having — without it, `static_assert(TakesOwnedWork<QueuedExecutor>)` would
hold for a type that hides nothing and prove no rule. The implementer's judgement that two real
diagnostics beat a reproduction that cannot compile is correct.

What it does not do is detect the defect, and this is stronger than "the control is weaker than the
defect's own shape":

**Verified by running.** Both in-tree executors declare *both* overloads themselves
(`ThreadPoolExecutor.hpp:99,127`; `ParkedWork_test.cpp:95,100`), so `using IExecutor::submit;` is a
no-op for them — name lookup already finds both in the derived class. A copy of that exact shape
with the using-declaration deleted:

```
CLANG: compiles clean -> the using-declaration is a no-op for both in-tree executors
GCC: compiles clean, no -Woverloaded-virtual diagnostic
```

So deleting every `using IExecutor::submit;` in the tree leaves all nine static_asserts green and
both compilers silent. Neither the check nor `-Woverloaded-virtual` is holding anything up today.

What actually closes the hazard is worth stating because it is better than the test: `IExecutor::
submit(ParkedWork)` is **pure virtual** (`IExecutor.hpp:56`), so no *concrete* executor can exist
having overridden only the borrowing half. The residual hazard is an *intermediate abstract* class
that re-declares one overload — fastcached#1041's actual shape — and that is caught by
`-Woverloaded-virtual` (`cmake/CoreCppToolchain.cmake:84`, `CORE_CPP_GCC_OR_CLANG`, so GCC and
clang both) and clang-tidy's `bugprone-derived-method-shadowing-base-method`.

Asked for: one sentence in `.agent/rules/async-and-net.md:96-103` and in the test comment recording
that the assert cannot regress and naming the pure-virtual overload as the structural half of the
defence. As it stands, `ThreadPoolExecutor_test.cpp:144` claims "this is the one line that holds
this executor to it", which is not what that line does.

#### I4 — `AsyncQueue::push()` is not `[[nodiscard]]`

`src/core/async/AsyncQueue.hpp:152`.

The class's own rationale says a dropped return is the failure mode it exists to make visible
(`:68-72`: "A silent drop is invisible: a protocol that recovers from loss looks healthy while
running slower than it should"). `push()` has three outcomes a caller must see — refused because
closed, refused by `DropNewest`, accepted after displacing N. Every sibling has the attribute
(`:210,:214,:225,:232,:248`), as do `Parked::take()` and `Parked::handle()`
(`ParkedWork.hpp:127,161`). The tests already write `std::ignore = queue.push(…)`
(`AsyncQueue_test.cpp:303,336,371,424,446`), which is the idiom you reach for when the attribute is
there.

Worth doing now rather than later: adding `[[nodiscard]]` after a release turns previously-clean
consumer code into an error under `-Werror`, so it becomes a Breaking entry instead of a fix.

### Minor

#### M1 — The header does not say the empty-task throw must not be caught

`src/core/async/Task.hpp:119-135`, `:249-257`, `:354-358`.

This is the dispatch's question 2, and everything else in it checks out:

- **One place:** `detail::refuseEmptyTask()` (`:127`), four call sites (`:210`, `:261`, `:323`,
  `:362`), `[[noreturn]]`, `Task<T>` and `Task<void>` refusing in the same words.
- **Empty state reachable only as documented:** default-constructed (`:221`), moved-from
  (`:225-226`), released (`:244`), and awaited-from (`:234`) — all four named in the Doxygen.
- **`done()` still answers true for it** (`:247`, `:352`), and both `result()` overloads say
  explicitly that `done()` alone is therefore not the guard.

What is missing is the last clause. `.agent/rules/async-and-net.md:84-90` states it exactly right —
"a **precondition violation reported as an exception**, never caught … a `catch` would make the
empty state a supported path". The header says none of that. It justifies the throw over an
assertion and cites `std::optional::value()`, which is a good analogy but carries no prohibition; a
consumer reads the Doxygen, not `.agent/rules/`. One sentence in `refuseEmptyTask()`'s comment
closes it.

#### M2 — `AsyncQueuePush::displaced` counts the refused item under `DropNewest`

`src/core/async/AsyncQueue.hpp:166` returns `.displaced = 1` where nothing was evicted;
`:67` documents the field as "How many items this push displaced **to make room**".
`AsyncQueue_test.cpp:278` locks the behaviour in. The behaviour is defensible as "items lost to
overflow" — which is what the cumulative `displaced()` counter (`:232`) means — but the field's
words are wrong for that row of the table. Either the doc or the name should move.

#### M3 — The rules file names the wrong test file for its last rule

`.agent/rules/async-and-net.md:62-64` says every rule is enforced by a case in
`src/core/async/{Task,ParkedWork,AsyncQueue}_test.cpp`. The last rule's only test is
`ThreadPoolExecutor_test.cpp:185`, which the brace list omits — and which is not compiled in the
single-threaded WebAssembly leg (`src/core/async/CMakeLists.txt:70-72`), so that one rule is
unenforced there.

#### M4 — `AGENT.md` was not updated

`AGENT.md:26-27` still reads "`async` has `StopToken`, `Task` and the combinators; … Phase B merges
fastcached's async and networking layer into `async` and `net`", as though the async half were still
ahead. `docs/modules/index.md:14` was updated by `78bf537`; the module table's prose was not.

#### M5 — Two documentation claims in `docs/modules/async.md`

- "The depth at which the chain overflows is the same before and after" — the reasoning is sound
  (the awaiter swapped a raw handle for a same-sized `UniqueCoroHandle`), but it is phrased as a
  measurement and there is no artifact and no test that would notice a regression.
- The recursive pre-completion teardown — new in this task, since before it the root `Task` freed
  only its own frame — is stated without the stack-depth caveat its sibling paragraph carries, and
  `Task_test.cpp:412-414` explicitly opts out of covering it ("this case never has one").

#### M6 — `whenAll`'s awaiter lost its move constructor

`Join.hpp:224-225` deletes copy and move. The pre-collapse `WhenAllAwaiter` was implicitly movable
(`WhenAll.hpp@b21229d:147-196`: vector + a by-value state). `WhenAnyAwaiter` was already immovable
(its `std::optional<StopCallback<…>>` member sees to that), so only `whenAll` changed. Guaranteed
copy elision covers `whenAll(…)`'s prvalue return and every documented use, so this is close to
theoretical; `auto a = whenAll(…); co_await std::move(a);` compiled before and does not now. Either
a line under Breaking, or a note that a `detail::` awaiter's movability was never API.

---

## The dispatch's six questions

### Q1 — Did `whenAny`'s cancellation-bridge ownership rule survive the collapse, on both branches?

**Yes.** Verified by reading the collapse line by line against `b21229d`'s `WhenAny.hpp`, and by
running.

The property has three parts and all three are intact:

1. `WhenAnyCancelBridge::operator()` still takes a `shared_ptr` copy on its own stack before
   `request_stop()` (`WhenAny.hpp:72-76`), unchanged from `b21229d:211-215`.
2. The runner's final awaiter still copies the state out of the promise **before** the latch step
   (`Join.hpp:139`, `auto const join = promise.state;`), matching `b21229d:120`, with the comment
   carried over.
3. The latch/decrement order is preserved exactly: latch first, then `--remaining`, then the tail
   transfer (`Join.hpp:140-146` against `b21229d:121-129`). This is the merge the dispatch warned
   about, and it is right — because `onChildFinished` runs *before* the winner's own decrement, the
   losers unwinding inline from within `request_stop()` cannot reach zero, so the awaiting
   coroutine is not resumed until control has returned to the winner's frame.

One substantive difference, and it is equivalent: `unhandled_exception` changed from a
`try { throw; } catch (OperationCancelled&) { cancelled = true; } catch (...) { failure = …; }`
(`b21229d:143-157`) to `escaped = std::current_exception(); cancelled = isCancellation(escaped);`
(`Join.hpp:160-164`). The old code left `failure` **null** for a cancelled child; the new code
leaves `escaped` non-null. `WhenAnyPolicy::onChildFinished` returns before reading it whenever
`outcome.cancelled` (`WhenAny.hpp:119`), so `state.exception` is assigned in exactly the same cases.
Verified equivalent by reading; verified green by running.

**Run evidence, both binaries, under ASan with `detect_leaks=1`:**

```
##### asan / core-cpp-async-test : whenAny survives children resumed from inside the cancel bridge's own callback #####
WhenAny_test.cpp:365: PASSED: REQUIRE( bCancelled )
WhenAny_test.cpp:366: PASSED: REQUIRE( parentCancelled )
WhenAny_test.cpp:367: PASSED: REQUIRE( root.done() )
All tests passed (8 assertions in 1 test case)

##### asan / core-cpp-async-fallback-test : the same case #####
All tests passed (8 assertions in 1 test case)

[whenAny] whole set: All tests passed (47 assertions in 8 test cases)   — both binaries
[whenAll] whole set: All tests passed (33 assertions in 8 test cases)   — both binaries
```

`WhenAny_test.cpp` is untouched by `f72cabc`, which is the right way to prove a refactor.

One behaviour change the collapse did introduce, checked and clean: `Policy::armParentBridge` is now
called **unconditionally** (`Join.hpp:246`), where `b21229d:245-249` armed it only inside
`if constexpr (HasStopToken<Promise>)`. It is therefore constructed on a default-constructed
`StopToken` whenever the awaiting promise carries none. Probed on both branches — direct
construct/destroy, the `optional::emplace`/`reset`/`emplace` shape, and an rvalue empty token — all
register nothing and run nothing, with a live-source control proving the callback does fire when a
stop state exists. `StopToken.hpp:441-487`'s fallback guards on `if (_state …)` throughout, and
`std::stop_callback` on an empty token is well-defined. Also probed end to end: `whenAny`/`whenAll`
awaited from a coroutine type with no `stopToken()` at all, ASan+UBSan clean on both branches. Not
a defect.

### Q2 — The null-handle decision

Executed correctly; see **M1** for the one gap. Summary: one function, four call sites, `[[noreturn]]`,
the empty state reachable only through the four documented routes, `done()` still true for it and
documented as not being the guard. What the header does not say is that the throw is not to be
caught — which `.agent/rules/async-and-net.md:84-90` says well and the Doxygen does not say at all.

### Q3 — `using IExecutor::submit;` and its negative control

See **I3**. Short form: the control is the right call and should stay; it proves the concept
discriminates. It cannot prove the rule, and — demonstrated by compiling it — deleting every
using-declaration in the tree changes nothing that any check or compiler would notice. The rule is
in fact enforced, better than the test suggests, by `submit(ParkedWork)` being pure virtual plus
`-Woverloaded-virtual` (GCC *and* clang, `CoreCppToolchain.cmake:84`) and clang-tidy. What is owed
is one honest sentence about which of those is load-bearing, replacing
`ThreadPoolExecutor_test.cpp:144`'s "the one line that holds this executor to it".

### Q4 — Is `whenAll`'s `shared_ptr` necessary or merely convenient?

**Merely convenient — and I would still accept it, with a caveat that turns into Finding I2.**

The stated justification does not apply to `whenAll`. The rule being bought is "keep a stop state
alive across its own `request_stop()`" (`Join.hpp:204-209`), and `whenAll` has no `StopSource`,
never calls `request_stop()`, and arms nothing (`WhenAll.hpp:50-58`). Nothing on `whenAll`'s path
can destroy the awaiter while a runner's `FinalAwaiter::await_suspend` is still executing: the
symmetric transfer happens *after* that function returns. A raw `WhenAllState*` was safe, which is
what `b21229d:86` shipped and tested.

What it buys is that `JoinRunner::PromiseType::state` has one type (`Join.hpp:105`). Parameterising
the *holder* as well as the policy would mean a second spelling in the runner and in
`FinalAwaiter::await_suspend` — which is the duplication the collapse existed to remove. The cost is
one `make_shared` block per join against N coroutine frames already allocated (so ~1/N in
allocations), plus roughly 2N+1 atomic refcount pairs.

I would take that trade. But it exposes the inconsistency in I2: the state is refcounted *atomically*
while `remaining`, `continuation` and `exception` next to it are plain. If a join can genuinely span
threads, `shared_ptr` is necessary and the fields need to be atomic too; if it cannot, `shared_ptr`
is more than the job needs. Right now the code asserts both. Settle that question and the answer to
this one falls out.

### Q5 — `ThreadPoolExecutor` header-only (R65)

**Nothing changed in the inlining.** Compared line by line against
`0708dd54:src/FastCache/Async/ThreadPoolExecutor.cpp`: the constructor, `stop()`, both `Submit`
overloads and `Worker()` are transcribed verbatim, comments included, with only the rename map
applied. One deliberate and correct difference: upstream holds `std::jthread`s and lets
`~vector<jthread>` join; core-cpp holds `std::thread` and joins explicitly via `joinAll()`
(`ThreadPoolExecutor.hpp:145-150`), called from both the destructor (`:82`) and the constructor's
catch handler (`:69`). That is the right translation — upstream's catch handler relies on the member
vector's destructor, which does not run for a constructor that threw a member at a time later, and
`joinAll()` makes it explicit. It also avoids `__cpp_lib_jthread`, which is exactly what
`StopToken.hpp` has a fallback for. `_threads` is declared last so it is joined before the queue it
reads is gone (`:178-179`).

The `#error` guard is real, correct and **placed before `#include <thread>`**
(`ThreadPoolExecutor.hpp:14-16`), so it fires first rather than failing inside libc++. The header is
appended to `HEADERS` only under `CORE_CPP_USE_THREADS` (`CMakeLists.txt:38-40`), and its test only
under the same condition (`:70-72`). `CORE_CPP_USE_THREADS` is off exactly under single-threaded
Emscripten (`cmake/CoreCppDependencies.cmake:143-145`). `tests/consumer-wasm/CMakeLists.txt:74-107`'s
"nothing links threads" scan is intact and unaffected, since `Threads::Threads` is attached to
`core-cpp-async` only inside the same guard (`CMakeLists.txt:14-17`).

One thing the dispatch asked about that turns out not to exist: there is **no header self-check** in
this repository. The hygiene rule set (`tests/cmake/check-cmake-hygiene.cmake:44-137`) has eighteen
rules and none of them compiles a header standalone; the only `try_compile` outside
`CoreCppTopLevel.cmake` is the StopToken probe. `.agent/rules/cpp-guidelines.md:154` requires
self-contained headers and nothing enforces it. Pre-existing, not B1's, and worth its own ticket —
but it means "excluded from the header self-check" in the dispatch was satisfied vacuously.

No async header uses a library feature newer than libc++ 17. The newest thing reached for is
`std::views::iota` (C++20, libc++ 14+), at `Join.hpp:254`, `ThreadPoolExecutor.hpp:59`,
`AsyncQueue_test.cpp:335,347` and `ThreadPoolExecutor_test.cpp:176,196`. Every other hit for
`jthread` / `stop_token` / `expected` / `flat_map` / `mdspan` / `print` / `ranges::to` /
`move_only_function` / `generator` / `atomic_ref` / `barrier` / `latch` / `semaphore` /
`to_underlying` / `unreachable` / `start_lifetime_as` is prose in a comment or lives in
`StopToken.hpp`'s already-guarded fallback.

### Q6 — `AsyncQueue`'s stop-aware `pop`

**Correct.** It throws `OperationCancelled` (`AsyncQueue.hpp:337`) — the one exception type this
codebase allows — and it leaves no waiter parked. The ordering rationale (`:318-324`) is right and
matches the rules file: data, then close, then cancellation.

The window handling is careful and I could not break it by reading:

- The stop callback is registered **before** the park is published and **outside** the queue's mutex
  (`:300-311`), so an already-stopped token runs `CancelPop` inline on this thread, finds no waiter,
  records `_cancelled`, and the re-check under the lock at `:306` returns false rather than parking.
  `AsyncQueue_test.cpp:391` is that case.
- Exactly one of `push`, `close` or `CancelPop` can obtain a given handle — each does
  `std::exchange(_waiter, {})` under the mutex (`:180`, `:203`, `:359`) — so a double resume is
  inexpressible.
- The mutex is never held across `IExecutor::submit` (`:186-187`, `:205-206`, `:361-364`), so no
  lock-order inversion with the executor's own lock is expressible.
- `_stopReg` is declared **last** so it is destroyed **first** (`:375-377`), which is what keeps
  `CancelPop`'s `_awaiter->_queue->_executor.submit(waiter)` on another thread from reading a
  destroyed awaiter: `~StopCallback` blocks for it, and the members it reads outlive that wait.

Four cancellation cases, all asserting the two things that matter — the end state and that no waiter
is left (`AsyncQueue_test.cpp:358` parked-then-cancelled, `:391` already-cancelled, `:412` item
beats cancel, `:433` close beats cancel). All four check `hasWaiter()` or `pending()`, not just the
outcome.

**TSan:** `core-cpp.async` and `core-cpp.async-fallback` both pass in a `clang-tsan` tree, with and
without `-LE no-tsan`. Incidental but worth passing on: **the `no-tsan` label is applied to no test
anywhere in the repository**, although `.agent/rules/testing.md:29` documents it and
`.github/workflows/build.yml:429` filters on it. That exclusion is currently a no-op — a gate that
excludes nothing, which reads the same as one that passed.

---

## The three deferred A11 defects

1. **Null handle answering `T {}`** — fixed, one function, see Q2 and M1. The decision (throw, not
   assert) is well argued in `Task.hpp:119-135` and I agree with it, independently of the team
   lead's confirmation: the empty state is admitted by design and reachable through the documented
   guard, and an assertion would answer with the wrong value in every Release build, which is the
   defect being removed.
2. **`makeWhenAllRunner`'s duplicated `unhandled_exception`** — fixed by the collapse. The runner
   body is `co_await std::move(task);` (`Join.hpp:195-198`) and the promise is the one place what
   escaped is recorded and classified (`:160-164`). The double wiring is gone too: the awaiter wires
   each runner once, in the start loop (`:254-262`).
3. **~200 lines of copy-paste** — collapsed into `Join.hpp`, in its own commit, after the graft was
   green. The line arithmetic in the report (548 → 592 total) is honest about the total going up;
   the duplicated *logic* is genuinely one copy. The policy split is clean and the five policy
   points are documented at `Join.hpp:18-25`.

`core-cpp#15`: the report's answer — it cannot be closed or narrowed, because the graft changed who
owns a frame and not how control is transferred — is correct. See M5 for the one sentence in
`docs/modules/async.md` that overstates it.

---

## Build and test evidence

Private trees only: `out/build/reviewB1-{clang-debug,asan,tsan}`, configured with
`cmake --preset {clang-debug,clang-asan-ubsan,clang-tsan} -B out/build/reviewB1-…`. Only the async
targets were built; the known-broken net/tui/migrate lanes were never reached. No repository file
was edited and no formatter was run.

| Tree | Build | `ctest -R 'core-cpp\.async'` |
|---|---|---|
| `reviewB1-clang-debug` | 0 warnings | 3/3 passed |
| `reviewB1-asan` (`detect_leaks=1`) | 0 warnings | 3/3 passed |
| `reviewB1-tsan` | 0 warnings | 3/3 passed, with and without `-LE no-tsan` |

`core-cpp-async-test` and `core-cpp-async-fallback-test` each report
`All tests passed (318 assertions in 78 test cases)`, identical on both StopToken branches.
`core-cpp-async-link-smoke` passes. Configure logs `[core-cpp] async: StopToken is std::stop_token`,
so the two binaries really do cover both branches.

**Verified by running:** C1 (segfault + ASan heap-use-after-free, both combinators, both branches),
I2 (TSan data race + an observed hang), I3 (the using-declaration is a no-op under both compilers),
Q1's regression case and the whole `[whenAny]`/`[whenAll]` sets under ASan on both binaries, Q1's
empty-`StopToken` probe on both branches, Q6's TSan runs.

**Verified by reading:** everything else — the collapse against `b21229d`, `ThreadPoolExecutor.hpp`
against upstream's `.cpp`, `AsyncQueue`'s window handling, the provenance and CHANGELOG rows, the
WebAssembly exclusions, the rules-file claims against the tests behind them.

## Not this task's, reported not touched

- The `no-tsan` label is consumed by `.github/workflows/build.yml:429` and applied to no test in the
  tree. Pre-existing.
- There is no header self-containment check, although `.agent/rules/cpp-guidelines.md:154` requires
  one. Pre-existing; worth a ticket.
- `provenance.md`'s `ThreadPoolExecutor.{hpp,cpp}` brace pattern — which both
  `tests/cmake/check-cmake-hygiene.cmake:385-387` and `scripts/check-upstream-drift.py` now refuse —
  shipped in `b21229d` but was **already corrected by `66dfc9a`**, a follow-up from another lane. Both
  gates pass on the current tree for every `src/core/async/` row; the eight remaining
  `cmake-hygiene` violations are all the net lane's untracked files. No action.
- `CHANGELOG.md:549-550` is missing the blank line after `### Changed` that every other section has.
  Cosmetic; nothing lints it.

## What the team lead may want to rule on

1. **C1's fix shape** — whether `abandon` becomes a single-holder right (one park per root), a
   refcounted one, or whether a fan-out takes ownership of its children back. It changes
   `ParkedWork`'s contract, which B3/B4/B5 are being written against right now, so it is worth
   deciding centrally rather than in this task.
2. **I2** — whether a join may span threads. Answering it settles Q4 as a side effect. If the answer
   is no, `WhenAll.hpp`/`WhenAny.hpp` need to say so as a precondition rather than as a description,
   and the `shared_ptr` becomes a documented convenience rather than a safety property.
3. **I4** — `[[nodiscard]]` on `push()` is cheap now and a Breaking entry after release.
