# Task B1: fix round 1 (Rulings R97–R99)

Spec compliance **PASS** — every line of §2 item 6 delivered, nothing beyond it, all three deferred
A11 defects addressed, four departures from upstream layout each recorded and each defensible.
Task quality **Changes requested**: 1 Critical, 3 Important, 6 Minor.

Read `task-B1-review.md` in full. Four of the six things I asked the reviewer to attack survived
scrutiny — the `whenAny` cancellation bridge (latch-then-decrement preserved verbatim on both
branches), the null-handle execution, the `ThreadPoolExecutor` inlining against upstream's `.cpp`,
and `AsyncQueue::pop`. That is the part of this task that is done.

## Critical — C1, and it is B1-introduced

**A `whenAll`/`whenAny` fan-out gives the same chain root to N parks, and `detail::Parked`
destroys it once per park.** `Join.hpp:253,260` copies one `chainRoot` into every runner promise;
`Task.hpp:199` carries it into every child. Two children that both park hand the executor two
`ParkedWork` values naming the **same** `abandon` handle. The reviewer wrote a 12-line reproducer
from this module's own vocabulary plus `ParkedWork_test.cpp`'s `QueuedExecutor`: it prints two
identical abandon addresses and **segfaults at -O2**; ASan says heap-use-after-free. Both
combinators, both `StopToken` branches.

The missing invariant is **at most one live park per unowned root** — true of a linear chain, false
the moment `unownedRoot` propagates through a combinator, which is this task's own addition. Before
it, sibling parks leaked whole rather than double-freeing. The existing cases at
`ParkedWork_test.cpp:315,336` are the right shape at **arity one**, which is exactly why they pass.

**Ruling R97: refcount the abandon root.** I considered the three shapes the reviewer named and
this is the one that does not move the contract:

- A **single holder** needs an answer to *which* park owns it, and the parks that do not own it
  then leak their roots when the owner is the one resumed. It trades a double free for a leak
  whose trigger is scheduling order.
- The **fan-out reclaiming its children** is arguably the correct ownership — the combinator made
  the children, so it should own their teardown — but it moves the loop's abandon-to-fixpoint pass
  (design spec §2, teardown step 4) from the children to the join, and **B3, B4 and B5 are being
  written against `ParkedWork` right now.** That is a redesign to schedule deliberately, not to
  land inside a fix round.
- **Refcounting** is invisible to those tasks: `ParkedWork` still carries an `abandon`, the loop
  still frees roots of remaining parked work, and the last reference is the one that destroys.
  One atomic against a coroutine frame allocation is not a cost worth discussing.

Requirements: **state the invariant** where `ParkedWork` documents `abandon`, and **test at arity
≥ 2** — the reviewer's reproducer is the case, promoted into `ParkedWork_test.cpp` for both
combinators, under ASan. If refcounting turns out not to hold, stop and report rather than
reaching for the redesign; that is my call and B3/B4/B5 are downstream of it.

## Important

**I2 — Ruling R98: yes, a join may span threads, and the join state's counters become atomic.**
`JoinState::remaining` (`Join.hpp:49,144`) is a plain `size_t` decremented from whatever thread
resumed the child, and **this task is the one that makes a cross-thread child expressible** — it
ships `ResumeOn` and `ThreadPoolExecutor`. TSan reports a race on the join state's heap block, and
a variant hung at round 3 with every child finished.

Forbidding it would make the module internally inconsistent: `ResumeOn` and `ThreadPoolExecutor`
are this module's own vocabulary, and `IExecutor.hpp:21-24` already tells the reader executors are
cross-thread. A precondition contradicted by the header next to it is a precondition nobody keeps.
The reviewer also noted the tell: the state is refcounted **atomically** while the counter it
protects is not.

This settles **Q4** with it: the `shared_ptr` stays. It was convenient rather than necessary for
`whenAll` alone, but a join that may span threads needs shared state outliving whichever thread
finishes last. One control block per join against N frames already allocated.

Requirements: the counters become atomic with the ordering written down beside them; a case that
joins children resumed on a `ThreadPoolExecutor`, in the TSan binary, because *"`clang-tsan` passes
by not reaching it"* is the finding. `ThreadPoolExecutor_test.cpp` never mentions `whenAll` today.

**I3 — the `#1041` check cannot fail on any change to this tree, and my dispatch's premise was
wrong.** Both in-tree executors declare *both* overloads, so every `using IExecutor::submit;` is a
no-op — deleting all of them leaves nine `static_assert`s green and both compilers silent. Keep the
negative control: it proves the concept discriminates, and I still hold that a control needing a
`NOLINT` to exist is not one we can keep.

What actually closes the hazard is stronger than the test claims, and should be what the comment
says: `submit(ParkedWork)` is **pure virtual**, so no concrete executor can override only the
borrowing half; the residual intermediate-abstract shape is caught by `-Woverloaded-virtual` and by
clang-tidy. And **`CoreCppToolchain.cmake:84` is `CORE_CPP_GCC_OR_CLANG`** — so clang diagnoses it
too, and the "GCC is the only one of our three" line I put in your dispatch and repeated to two
other lanes was about **MSVC**. My error; correct it wherever it was written down.

Replace `ThreadPoolExecutor_test.cpp:144`'s *"the one line that holds this executor to it"* with
one sentence saying what genuinely holds it.

**I4 — `[[nodiscard]]` on `AsyncQueue::push()`** (`AsyncQueue.hpp:152`). Now, not later: every
sibling has it, the class's own documentation says a silent drop is the failure it exists to make
visible, and it is free before v0.1.0 and Breaking after.

## Minor

Take or argue each in a sentence: the header never says the empty-task throw must not be caught
(the rules file says it well, the Doxygen does not — and that clause was the requirement I attached
to the ruling); `displaced=1` under `DropNewest` where the field's doc says *"to make room"*; the
rules file's brace list omits `ThreadPoolExecutor_test.cpp`, which holds its last rule's only test
and is not compiled in the wasm leg; `AGENT.md:26-27` still reads as though the async merge is
future work; two overstated sentences in `docs/modules/async.md`; `whenAll`'s awaiter silently lost
movability.

## Two gaps that are not yours — do not fix, they are being filed

- **There is no header self-check in this repository at all.** Eighteen hygiene rules, none of
  which compiles a header standalone — so the dispatch's "excluded from the header self-check" was
  satisfied vacuously, and the C++ guidelines' self-contained-header rule is unenforced.
- **The `no-tsan` label is consumed by `build.yml:429` and applied to no test anywhere**, so that
  exclusion is a no-op.

## Then

- RED before each fix; the reviewer's reproducer is C1's, promoted into the suite.
- Both binaries (`core-cpp.async` and `core-cpp.async-fallback`), `clang-debug`, `gcc-release`,
  ASan, TSan, `cl-debug`, `clangcl-release`.
- Under R73 watch the newest head, and say plainly if you close with no green covering you.
  A superseded run reports `cancelled` with zero jobs.
- Append "Fix round 1" to `task-B1-report.md` with RED/GREEN for C1, I2 and I4.
