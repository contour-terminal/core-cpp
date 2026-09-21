# Task B5 — fix round 2, against `task-B5-rereview1.md`

Scope: the re-review's one Important, its three Minors, and the three "no case" rows it ruled on.
Base `7bdd132`, rebased onto B4's follow-up before commit. Seven files:
`src/core/net/EventLoop.{hpp,cpp}`, `HostDrivenLoop_test.cpp`, `Timers_test.cpp`,
`DeadlineTimer_test.cpp`, `CHANGELOG.md`, `.agent/rules/async-and-net.md`.

**Every finding is addressed. Two of the re-review's own claims did not survive checking, and both
are recorded here rather than quietly worked around.**

---

## 1. Important — C1's family, and the comment that claimed it was closed

### What the fix is

The arming **moved into `registerPark`** and `addTimer` **dropped its copy**. That is the whole
change in one sentence, and the placement is the point: `registerPark` is the primitive, and six
call sites in this module reach it —

```
EventLoop.cpp:339   runInbound            (on-turn; skipped by the predicate)
EventLoop.cpp:528   schedule
EventLoop.cpp:672   addTimer
EventLoop.hpp:811   DelayAwaiter::await_suspend
EventLoop.hpp:877   WaitHandleAwaiter::await_suspend
InterruptibleSleep.cpp:90  TokenDelayAwaiter::await_suspend
```

— plus whatever a consumer files through the public overload. **The round-1 fix sat one level above
the primitive and covered one of them.** Leaving it in `addTimer` as well would have left two
members computing the same answer from the same deadline heap, which my own dispatch ruled out.

`resumeSoon` and `requestStop` gained the same two lines.

### Which primitive, and the measurement that settled it

**Ready work wakes; a park arms.** `resumeSoon` and `requestStop` use `_backend.wake()`, joining
`post`, `submit`, `spawn` and `stop`; `registerPark` uses `armHostWake()`.

I first proposed `armHostWake()` everywhere and was ruled against on thread safety — `wake()` is the
only member of the backend contract declared thread-safe. **I measured the ruling rather than
arguing it**, and it costs the deadline:

```
HostDrivenLoop_test.cpp:196: CHECK( soonestDelayMs(host) == 50 )   expansion: 0 == 50
HostDrivenLoop_test.cpp:236: CHECK( soonestDelayMs(host) == 50 )   expansion: 0 == 50
```

`HostDrivenBackend::wake()` is `scheduleAt(_clock.now())`, so a park due in fifty milliseconds asks
the host to pump at once, spends a turn finding nothing due and re-arms from the same heap — the
behaviour the re-review named as the reason `armHostWake()` was correct. So the split is not two
idioms; it is **one distinction**, and the two primitives exist because the distinction does.

The thread-safety concern is sound in general and does not bite at `registerPark`: that function
already does `_parks.add(...)` with no synchronisation, so two threads calling it concurrently race
**with or without the arming** — which is what its `teardownIsSerialisedWithDispatch()` assertion
forbids. The arming cannot make a single-threaded-by-contract function less safe. The comment in the
code says so, because it is the first question the next reader has.

Native off-turn callers lose nothing either way: `!isOnWorkerThread()` plus that assertion means the
loop is not running, so there is no blocking wait to break.

### The claim I had to withdraw: `notifyHandleClosing` is a fourth sibling that cannot fire

I enumerated every member mutating `_ready` or `_parks` myself rather than correcting a bad
enumeration with another unaudited one, and found `notifyHandleClosing` (`:896`, public): it files
into `_closedParks`, which only a turn consumes (`std::exchange` at `:257`), and asks for nothing.
I reported it as the worst of the four.

**It is unreachable, and I should have checked the precondition before reporting it.**

```cpp
// HostDrivenBackend.hpp:44 — "Refuses: this backend has no readiness at all."
attach(ReadinessHandler&) -> std::unexpected{ NetErrorCode::Unsupported }
```

`registerPark` returns `ParkId::invalid()` on a refused attach, so **no park on a host-driven loop
can be on a handle**; `parksOn(handle)` is always empty and `_closedParks` never grows. The same
argument kills the companion finding that `armHostWake`'s condition omits `_closedParks` while
`hasPendingWork()` counts it: `armHostWake` early-returns for non-host-driven backends, and
host-driven ones can hold no closed park.

**So neither got a branch.** Counting `_closedParks` in `armHostWake`'s condition would handle a
closed park **silently**, so the day the premise stops holding is the day nothing says so — which is
the opposite of failing loudly, and M7's finding (*an assertion that cannot come out the other way*)
applied to a fix instead of an assertion.

**What they got instead is the loud instrument: `assert(_closedParks.empty())` in the host-driven
branch**, plus the comment naming the refusal and saying what must change first. An assertion has no
behaviour to inherit; it is the invariant the comment describes, made to fail. It is empty there for
two independent reasons, both written down: no host-driven park can be on a handle, and the turn
takes `_closedParks` with `std::exchange` before the wait with nothing between that and this call
able to refill it.

**It has no case, for the reason every assertion in this file has none** — observing one from inside
a Catch case aborts the binary, and a third `WILL_FAIL` canary mode for an invariant unreachable
today is more than it is worth. Recorded as a decision, not an omission.

**But "unreachable" and "dead" are different claims, and only one of them is acceptable here**, so
the line was proved to EXECUTE rather than merely to hold: inverting it to
`assert(!_closedParks.empty())` aborts the host-driven cases —

```
core-cpp-net_backend-test: EventLoop.cpp:499: void core::net::EventLoop::armHostWake():
Assertion `!_closedParks.empty() && "armHostWake with a closed park pending: …"' failed.
SIGABRT - Abort (abnormal termination) signal          runner_rc=134
```

— and restoring it returns `All tests passed (81 assertions in 17 test cases)`. An assertion that
never runs is indistinguishable from one that holds; this one runs.

**I also checked a bigger version of the claim and it is false.** I thought a close performed
*during* a turn would strand the park, because `armHostWake()` runs last at `:314`. It does not:
`drainReadyQueue` is `:243` and the `std::exchange` is `:257`, so a close by a coroutine this turn
resumed is picked up by *this* turn, and nothing after `:257` runs user code. The on-turn path is
correct.

### The fix that matters most is in the rulebook

`.agent/rules/async-and-net.md` carried the bad enumeration in its own words:

> *"`post`, `submit`, `schedule`, `spawn`, `requestCancel` and `stop` all wake the backend.
> `addTimer` was the sixth member of that family and **the only one that did not**."*

It was not the only one. **The rule written to record the defect reproduced the defect, inside the
file whose job is to prevent it** — which is why re-reading the rulebook could never have found it.
The comment in `EventLoop.cpp` and the comment in `HostDrivenLoop_test.cpp` both said the same
thing, so the claim existed in three places and was wrong in all three.

Corrected, with the procedure attached, because a rule without one is agreed with rather than
executed:

- **Derive the family from the code every time, including from this page.** A list in a comment or
  a rule is the record of an audit somebody once did. It is not an audit, and it reads exactly like
  one.
- **Put the behaviour in the primitive, not in the caller that exposed the gap.**
- **A member that legitimately does not join the family says so where the reader is**, because no
  test can say it for them.

---

## 2. The three new cases — RED, and one prediction that was wrong

Predicted in writing before the run. **Three RED, one green — and I named the green one in advance**
rather than discovering it: `requestCancel(timer.park)` covers a guard that already exists, so
calling it RED would have been a false claim.

### The prediction that was wrong, and the correction it produced

My first `requestStop` case spawned a flow, parked it on `delay(1000ms)` and asserted the host was
re-armed to 0 ms. **It passed.** Not because the case was weak — because `requestStop` is not
broken on that path:

```
spawn            -> promise.setStopToken(_rootStop.get_token())   EventLoop.cpp:644
DelayAwaiter     -> _cancelReg.emplace(_token, ... requestCancel) EventLoop.hpp:812
requestStop      -> _rootStop.request_stop()  ->  the callback    ->  requestCancel  ->  wake
```

The wake was coming from the cancellation, **before `unparkEverything()` ran at all**. What has no
wake is a park filed through the public `registerPark` with nothing watching the token. The case was
rewritten to that shape and isolated with `host.clear()`, so it measures `requestStop`'s own
obligation rather than inheriting `registerPark`'s. The case name says "a park nothing cancelled"
because the narrowness *is* the finding.

### RED, verbatim

```
HostDrivenLoop_test.cpp:230: FAILED: REQUIRE( host.pendingCount() == 1 )   with expansion: 0 == 1
HostDrivenLoop_test.cpp:263: FAILED: REQUIRE( host.pendingCount() == 1 )   with expansion: 0 == 1
HostDrivenLoop_test.cpp:305: FAILED: REQUIRE( host.pendingCount() == 1 )   with expansion: 0 == 1
test cases:  4 | 1 passed | 3 failed          runner_rc=1        matched_nothing=0
```

`matched_nothing=0` is checked because an earlier run of this filter reported *"No test cases
matched"* after I renamed a case — a filter that matches nothing looks exactly like a filter that
passes.

### A red that became a crash, which is worth recording

The first run of the `resumeSoon` case SIGSEGV'd in teardown and **took the two cases after it with
it**. My bug, not the code's: `flow` was declared after `loop`, so the borrowed frame died while the
loop still held its handle. `testing.md` says a `REQUIRE` above a stop turns a red into a hang; here
it turned a red into a crash that silenced later cases. Both counters are now declared before the
loop, with a comment saying why.

### GREEN and the mutation controls

Each fix removed on its own, predicted first:

| Mutation | Predicted | Actual |
|---|---|---|
| `registerPark` loses the arming | the `DetachedTask` case **and** `addTimer`'s, since `addTimer` now inherits it | **3 failed** — those two and `requestStop`'s |
| `resumeSoon` loses its wake | its case only | 1 failed |
| `requestStop` loses its wake | its case only | 1 failed |

Baseline and restored: `All tests passed (2271 assertions in 64 test cases)`, from 2254 in 60.

**The M-A prediction was wrong and the reason is my own rewrite.** The `requestStop` case isolates
itself by pumping, so it needs `registerPark` to have armed something for that pump to consume. The
coupling is a true statement about the system rather than a flaw — M-C still isolates `requestStop`'s
own guard, reddening only its case — but I reused the old number instead of re-deriving it from the
case I had just changed. Same shape as the claims table: restating is not re-deriving.

### A case that was measuring its own fixture

The `requestStop` case first isolated itself with `host.clear()`. That drops the host's queue while
leaving the **backend** believing a pump is outstanding, so its coalescing swallowed the next
request and the case failed for a reason that had nothing to do with `requestStop`
(`pendingCount == 1` expanding to `0`). It now isolates by **pumping**, which is what a real host
does and which clears both sides. A fixture that desyncs two halves of the thing under test will
fail or pass for reasons the case never names.

---

## 3. Minor — the symmetric half of I1

`Timers_test.cpp` gains *requestCancel handed a timer's park leaves the timer armed*. `TimerId::park`
is a public member of an aggregate, so `requestCancel(timer.park)` compiles and **no compile-time
check can refuse it**; the three `static_assert`s are correct about conversion and silent about this.

**The case does not red on any single mutation, and it says so in its own comment rather than
leaving a reader to discover it.** The behaviour is defended twice — `resolveCancel` returns early
on `!entry->parked`, and `queueParkedWaiter` short-circuits on the empty waiter a callback park has
— so removing either alone leaves it green. It is there because nothing exercised the path at all,
so a change removing both, or giving a callback park a waiter, would have had nothing to answer to.

---

## 4. The "no case" rows — of three, one was honest

Relabelled in `task-B5-fixround1.md`, and the pattern is worth more than the rows.

- **Lazy pruning's counterfactuals — honest.** Unchanged.
- **The loop-thread-only assertions — `declined`, not "no case".** The tree owns a `WILL_FAIL`
  canary (`HostDrivenCanary.cpp`), already wired into CMake with the `canary` label, and B4 uses it.
  Testable and not paid for is a different claim from untestable, and the column heading is what a
  later reader acts on. The cost of declining **rose** this round: I2 added a second such assertion,
  both share one predicate, and nothing reds if it inverts.
- **`DeadlineTimer` allocates nothing — now checked.** The row was wrong twice: it said "four
  members" where the header declares **five**, a miscount committed inside the table built to catch
  miscounts; and my stated obstacle was false in the way the re-review names — `sizeof` is fragile
  as an *equality*, not as an *upper bound*.

The check is a bound against a struct declaring the same five members, not a number:

```cpp
static_assert(sizeof(DeadlineTimer) <= sizeof(DeadlineTimerShape), …);
```

Both types get the same layout rules on every target, so padding, member reordering and 32- versus
64-bit cannot break it while a sixth member cannot slip past. **Mutation run rather than assumed:**
removing one member from the shape reds the build with the assertion's own message; restoring it
greens. `!std::is_polymorphic_v` sits beside it.

**The pattern: of the three rows that survived round 1, two were wrong, and the lead had already
asked.** I was asked whether the id-type move was available for the remaining three and answered no
for all three, minutes after finding it for the fourth. Writing a row down is not re-deriving it.

---

## 5. Minor — the CHANGELOG narrated a rename from a name that never shipped

`RunOnceResult` has never been released, so a reader was told about `resumed` → `drained` from a
name they were never given. The entry now says `RunOnceResult::drained` and keeps only the reason
the name is what it is. A tree-wide grep for the old field returns nothing.

The `HostDrivenBackend` entry also gains the behavioural guarantee this round creates: **filing work
from outside a turn is safe on a host-driven loop**, which is the position a DOM handler, a frame
callback or a TUI input path is in — and it names the members that inherit it.

---

## 6. What the gates did, including what they got wrong

At `7bdd132`: clang-debug, gcc-release, ASan/UBSan, TSan **31/31**; emscripten **26/26**; hygiene
**16/16**; clang-tidy **509 steps from a deleted tree, zero findings**; `cl-debug` and
`clangcl-release` **33/33**; `mkdocs --strict` clean; clang-format at the pinned 22.1.8 clean.

Three instruments misreported, all mine:

- **`cl-debug` failed with `LNK1163: invalid selection for COMDAT section`** on `EventLoop.cpp.obj`
  and `SleepUntil_test.cpp.obj`. `--clean-first` gives 517 steps and 33/33.

  **Measured:** `cl-debug`'s `build.ninja` names `fastcache-cc` as its launcher, so it goes through
  the same launcher as `clangcl-release` and the rulebook's *"`cl` … unaffected"* is too strong;
  an `LNK1163` occurred; a clean rebuild cleared it.

  **Inferred, and marked as such:** that the `LNK1163` came *from a stale cached object*.
  **`--clean-first` cures every stale-tree problem, so it cannot discriminate between causes** —
  it is a remedy that works for disk corruption, a compiler upgrade and a CMake regeneration
  equally well. This is one step short of the standard applied everywhere else in this round, and
  it is recorded as an inference rather than a result.

  The experiment that would discriminate: rebuild `cl-debug` at the base commit, apply the header
  change alone, build incrementally to reproduce the `LNK1163`, then delete **only** the two named
  objects and build incrementally again. If it links, those objects being stale is the cause; if it
  still fails, it is not. Not run — it costs a full rebuild and the rulebook line it would settle is
  B4's to amend.

  The framing holds either way and is the keeper: **clang-cl fails silently as an inherited pass,
  MSVC fails loudly as a link error — same launcher, opposite symptom, and the loud one is the
  lucky one.**
- **clang-tidy caught a real defect in this round's new code**, `cppcoreguidelines-pro-type-member-init`
  on the reference struct. Fixed with initialisers, not a `NOLINT`.
- **My mutation script drew a verdict from a mutation it never applied.** The member gained a
  default initialiser, the pattern stopped matching, and the script printed
  `mutation applied: 0 line(s)` and then concluded *"the assertion is decorative"*. It printed the
  number that contradicted its own verdict and did not act on it. A zero count is now fatal there.

---

## 7. Folded in from outside the re-review: `notifyHandleClosing` asserted nothing

Reported by the lead, verified independently, and **taken into this round rather than deferred** —
stated plainly because it widens the diff past what the re-review approved, into a function this
round does not otherwise touch.

I classified every function in `EventLoop.cpp` by whether it touches `_parks`, `_ready`,
`_closedParks`, `_abandoned` or `_roots`, whether it asserts `teardownIsSerialisedWithDispatch()`,
and whether it takes `_inboundMutex`. Among **public** members that mutate loop-owned state,
`notifyHandleClosing` was the only one with neither. (`hasPendingWork` also has neither and is
`const`; the remaining bare ones are private helpers reached from inside a turn or from an asserting
public member.) **Eleven siblings assert it, not ten** — the report that raised this omitted `run`.

Its requirement lived only in prose, in a header whose governing rules section is headed *"Thread
affinity, asserted rather than documented"*.

**Taken because adding the assertion is itself a measurement**, and the measurement costs only a
gate run this round is doing from scratch anyway: if it fires under ASan, TSan or the Windows legs
where real sockets close, that is a live defect found.

It held. Then, to the same standard as the `_closedParks` assertion, it was proved to **execute**:

```
core-cpp-net_backend-test   runner_rc=0    All tests passed (2271 assertions in 64 test cases)
core-cpp-net-test           runner_rc=134  EventLoop.cpp:981: Assertion `!teardownIsSerialised…'
                                           failed.  SIGABRT
```

**It is live, and only the other binary reaches it.** The backend test never calls
`notifyHandleClosing`; the socket test does, through real closes — so every current caller is
demonstrably on the loop thread, which is the severity claim confirmed by measurement rather than
by argument.

**Had the probe run against one binary it would have reported the guard as dead.** That is
`testing.md`'s *"a module has as many test binaries as it has things to link"* arriving from the
other direction: not *is a second binary owed* but **which binary reaches the line being asked
about**. The abort-or-not mechanism looks conclusive either way, which is what makes the
single-binary version of it dangerous.

**The message deliberately does not match its siblings.** Theirs say `post()` a call to it instead;
here that advice would be a rule that cannot be followed, because this call cannot be separated from
the `close()` that must follow it — the handle has to still be open when the backend drops its
registration, or the removal lands on a descriptor number the kernel may have reassigned. It says
the whole close moves to the loop thread.

## 8. The rebase onto `5d7a5ae`, which was not a formality

**The shared checkout was not fast-forwarded, and that was the right call.** Its HEAD was `320a9ab`
— one commit behind the branch — and a fast-forward would have had to overwrite four dirty files,
two of them the controller's uncommitted rulebook edits. The rebase was done in the build worktree
instead, by extracting my six files as a patch and `git apply --3way`-ing them onto `5d7a5ae`.

Four of six applied cleanly. **Both conflicts were B4's `5d7a5ae`, and one of them is worth
recording.**

### B4 added the same assertion, concurrently, with the wording that is wrong for the site

`git log -S` places it: `5d7a5ae` added an `assert(teardownIsSerialisedWithDispatch())` to
`notifyHandleClosing` — the same finding, landed in parallel — carrying the siblings' message,
*"post() a call to it instead"*.

**That advice cannot be followed at this site.** The call is inseparable from the `close()` that
must follow it: the handle has to still be open when the backend drops its registration, or the
removal lands on a descriptor number the kernel may already have reassigned. Resolved by combining
— B4's sentence on *why* it is worth asserting, which is better than anything I wrote, plus the
explanation of why the advice differs and the message that can actually be acted on. **A diagnostic
copied from a sibling that tells its reader to do something impossible is a defect, not
consistency.**

The second conflict, `resumeSoon`'s doc, was a pure merge: B4 removed a misleading caller list, I
added the off-turn arming sentence, and both belong.

### The dangling citation, and a claim of the controller's that does not hold

`hasPendingWork()` is deleted by `5d7a5ae`. My `armHostWake` comment cited it, and so did two
sentences of the rulebook — **both written against a shared tree that was a commit behind the
branch**, which is the same base problem in a second costume. My comment now says what is true:
`armHostWake` is the sole definition of what counts as work for a host-driven loop, which makes the
assertion carry more rather than less.

**Re-derived on the new base rather than trusting that the line survived**, and it refutes a claim
made about it. The suggestion was that a `notifyHandleClosing` from a step-5 timer callback lands
*after* the `std::exchange`, degrading the assertion's second reason to an argument. It does not:
`fireExpiredTimers` (`:320`) only QUEUES a due callback, and `runDueCallback` runs it from
`drainReadyQueue` (`:392`, reached at `:255`) — **step 2 of the next turn, ahead of that turn's own
exchange at `:269`.** A close performed by a timer callback is caught by the turn that ran it. The
second reason stands as an independent reason, and that is now written into the comment with the
line numbers, because it is exactly the path a reader would doubt.

### One more instrument that reported nothing and was read as a verdict

Checking for conflict markers, the one-liner ended `... && git status --porcelain && grep -rln
'<<<<<<<' src/ ; echo "(empty = none)"`. The `git apply` before it exited non-zero, so the `&&`
chain stopped — **and the `;` printed the label anyway.** "(empty = none)" appeared for a grep that
never ran, over two files that did contain markers. A label separated from its check by a `;`
survives the check's failure, which is the shell's form of *a gate that does not report reads as
passed*.

## 9. What this leaves for others

- **`WaitHandleAwaiter::await_suspend` is the third instance of M8's shape and is B4's**, named here
  only so it is not lost: a leaked readiness park carries a backend attach, so it is a use-after-free
  where the two guarded awaiters only leak a table slot.
- **For B12:** `addTimer`, `co_await loop->delay()`, `interruptibleSleepUntil()`, `schedule`,
  `resumeSoon` and `requestStop` are now all safe from a host callback on a host-driven loop. What is
  still not, and cannot be until a host-driven backend has readiness, is anything parked on a
  descriptor — see the comment at `armHostWake`.
