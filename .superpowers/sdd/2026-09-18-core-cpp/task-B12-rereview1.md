# Re-review 1 — Task B12, fix round 1

Scope: the fix diff only (`e8c6574..9924358`, one commit,
`fix(tui): ~TuiRuntime handles three states of a source flow, not two`, already on
`origin/master`). Verified in a private detached worktree at `9924358` with its own build trees;
`D:\core-cpp` was not mutated and the worktrees are removed.

**What ran, and where.** WSL Ubuntu, clang 22.1.2, `clang-debug` and `clang-asan-ubsan` presets,
built from a WSL-native worktree of `9924358`:

| Gate | Result |
|---|---|
| `core-cpp-tui-test`, `clang-debug`, whole binary | 1041 cases, 4571 assertions, all passed |
| `core-cpp-tui-test`, `clang-asan-ubsan`, `[TuiRuntime],[Modal],[teardown],[escape]` | 33 cases, 182 assertions, no sanitizer diagnostic |
| `ctest -L hygiene` (`clang-debug`) | 18/18 (`upstream-drift` skipped) |
| `python scripts/clang-format.py --all --check` | 422 files, clang-format **22.1.8** |
| `mkdocs build --strict` (Windows) | clean |

Not run by me, and named rather than assumed: **clang-tidy** (the pinned 22.1.8 is not on this WSL
and `cpp-guidelines.md` forbids running it at another version), the Windows `cl-*`/`clangcl-*` legs,
and macOS. So the macOS claim below is adjudicated by reading, not by running.

---

## Per-finding verdict

**C1 (Critical) — ADDRESSED.** All four source flows (`inputFlow`, `resizeFlow`, `interruptFlow`,
`signalFlow`, `TuiRuntime.cpp:160/197/228/261`) now open `while (!_stopping)`; `grep "while (true)"`
over `src/core/tui/` leaves only `MarkdownRenderer.cpp`, `PosixIO.hpp`, two `TerminalOutput.cpp`,
`Modal.hpp` and `SemanticBlockClient.cpp`, none of them a source flow. **Ran it:** reverting the
four guards to `while (true)` at `9924358` and rebuilding makes the new case
`A runtime destroyed before its first turn leaves the loop holding nothing` fail with
`REQUIRE(loop.parkedWaiterCount() == 0)` → `1 == 0` and then SIGSEGV, exactly as the report claims;
restored, it passes. The case therefore distinguishes.

The stronger claim — *one action is correct for all three states because of the flows' shape* — holds,
and I checked each leg rather than the summary:

- `cancelPending` really does answer true for all three: it searches `_ready`, then `_parks` by
  waiter, then `_inbound.submissions` and `_inbound.scheduled` (`EventLoop.cpp:591-650`). State 1
  is the inbound-submissions branch.
- **Resume-exactly-once is safe per state.** State 1 re-enters the body at the loop head and falls
  straight out. States 2 and 3 re-enter at `WaitHandleAwaiter::await_resume`
  (`EventLoop.hpp:~975`), which either returns (→ the post-await `if (_stopping) co_return`) or
  throws `OperationCancelled`/`FdRegistrationFailed` (→ both caught, both `co_return`). Every exit
  is a return; none loops.
- **No flow can park after `_stopping` is set, by any path.** Each flow's only suspension point is
  its own `co_await _loop.waitReadable(...)` at the loop head, and the `if (_stopping) co_return`
  sits between that await and *every* statement that could touch the loop — `armEscapeFlush`
  (`TuiRuntime.cpp:451`, the only `addTimer` a flow reaches), `routeDecoded` → `deliverInput` /
  `notifyActivity` → `handToLoop` → `resumeSoon`, and `runInterruptPolicy`. `parkOnInput` and
  `parkOnAgent` additionally assert `!_stopping`. `_stopping` is written in exactly one place, the
  destructor (`TuiRuntime.cpp:42`), and the destructor retires `_escapeFlush` *before* the source
  loop, so nothing a resumed source can arm outlives it.
- **The committed case exercises one of the four flows.** I ran a two-source probe (input *and*
  resize pipes, construct-and-destroy with no turn): green at `9924358`, `2 == 0` plus SIGSEGV with
  the guards reverted. `interruptFlow` and `signalFlow` are textually identical in shape and were
  checked by reading only.

**H1 (High) — PARTIALLY.** The code fix is correct and I verified it; the *reason given for
shipping it without a case is false*, and the case is writable. Details below — this is the most
important thing in this re-review.

**M1 (Medium) — ADDRESSED.** `AGENT.md:22`, `README.md:33` and `docs/modules/index.md` all list
`net` in the `tui` row and describe B12 in the past tense; the published Mermaid DAG has its
`tui --> net` edge (`docs/modules/index.md:49`). `mkdocs build --strict` clean.

**M2 (Medium) — ADDRESSED.** Two cases, and they are the two branches the review named: an empty
decode arms the flush and the flushed `Escape` reaches a parked `nextEvent()` (`ManualClock` crossing
50ms, `flushCount() == 1`, `pendingTimerCount() == 0`), and a non-empty decode arms nothing
(`flushCount() == 0`). Both green under ASan/UBSan as well as `clang-debug`. The three previously
unused `ScriptedInputSource` members are now used.

**L1 (Low) — ADDRESSED, and it has not become load-bearing for something else.**
`TuiRuntime.cpp:61-65` names `core-cpp#41` and calls the resume a WORKAROUND;
`.agent/rules/async-and-net.md:688-693` says explicitly that when #41 is fixed the extra resume
"stops being needed, and its owner should **delete** it". I verified the issue exists rather than
taking the number: `gh issue view 41` → OPEN, *"net: cancelPending() returns on a ready-queue hit
and leaves the readiness park attached"*. One nuance for whoever closes #41: the single `resume()`
now serves all three states, so "delete the extra resume for a QUEUED waiter" means deleting the
whole call, not editing it — which is sound (an un-started or parked frame can simply be destroyed
by `~Task`), but the rulebook sentence reads as if only one state's resume goes.

**L2 (Low) — ADDRESSED.** `TerminalInput.hpp` no longer names `TerminalEventSource`. The remaining
occurrences in the tree are historical prose (`TerminalInputSource.hpp:7`, `TuiRuntime.hpp:25`,
`docs/modules/tui.md:159`) and the plan/spec, none of them Doxygen on live public API.

**L3 (Low) — ADDRESSED.** `TuiRuntime.hpp:468-486` now argues the ordering on the rule's own terms
— the hazard is an already-stopped token running the callback inline at construction, and every
awaiter returns false on `stop_requested()` before it parks or emplaces — and points the other
three awaiters at it.

**L4 (Low) — ADDRESSED.** `ScriptedInputSource::inputHandle()` answers `platform::InvalidHandle`
unless the case asks for `HandleFor::Input`, and the four cases that need an input flow
(`TuiRuntime_test.cpp:556, 585, 608, 822`) each `REQUIRE(source.inputHandle() != InvalidHandle)`
and each use a scripted backend, which is what the enum documents. The four remaining
`ScriptedInputSource {}` uses (`:357, 542, 651, 757`) are now genuinely headless.

---

## H1 is reachable — the derivation is wrong

The report says: *"everything that could park is either already ahead of the queued waiter (and so
ran while the slot was still occupied, which the assertion forbids) or appended behind it."*

The flaw is in "already ahead ... so ran while the slot was still occupied". The queued waiter `A`
is **appended during the drain**, by the entry at the front of it. Everything sitting behind that
front entry therefore runs *after* the slot was emptied and *before* `A` resumes — it is ahead of
`A` in the queue and behind the emptying in time. `EventLoop::turn` guarantees such neighbours
exist: step 4 dispatches readiness into `_ready` and step 5 fires expired deadlines into the same
`_ready` (`EventLoop.cpp:255-322`), both drained by the next turn's step 2. A readiness resumption
and a timer resumption are routinely drained back to back.

The one extra condition is that the wake must leave the input buffer empty, or the next parker's
`await_ready` short-circuits. `notifyActivity` on a **focus report** does exactly that: the report
is consumed, nothing is buffered, and the timed waiter is handed to the loop.

**Ran it.** A probe built on the committed test doubles — `ManualClock` + `ClockAdvancingBackend`,
one flow on `nextEventFor` woken by a scripted `FocusEvent`, a sibling on `delay(50ms)` whose
deadline expires in the same turn the input handle becomes readable, driven by three `loop.runOnce()`
calls — ends with the sibling parked on input holding a 5-second deadline:

- at `9924358`: `pendingTimerCount() == 1` — pass.
- with **only** `releaseInputWaiter` reverted to its `fed98b4` form (the unconditional
  `retireTimer(_inputDeadline)`), everything else at `9924358`: `pendingTimerCount() == 0` — fail.
  The sibling's timeout was silently cancelled. Trace, per turn:
  `ready=2 parks=1 timers=1` → `ready=0 parks=1 timers=0`, against `timers=1` with the fix.

So H1 was a live defect reachable through the public API (`spawn`, `delay`, `nextEventFor`, a focus
report), not a latent one, and a regression case for it is writable with the doubles already in the
tree. The fix itself is correct and needs no change — what is owed is the case, and a correction to
the report's claim. This matters beyond the bug: the suite currently has **no** case in which two
flows interleave on the input slot, which is the shape `TuiRuntime.hpp:430-433` says to expect.

---

## Introduced by the fix

Nothing. The `std::jthread` → `std::thread` + explicit join conversion is correct: two sites
(`TuiRuntime_test.cpp:456` and `:521`), both joined **before** the assertions that follow, so a
failing `REQUIRE` cannot leave a joinable thread; no thread is detached; no cancellation is lost,
because neither worker was ever cancelled by the `jthread` stop token — each sleeps, does one thing
and returns. It matches the tree's existing convention, which I checked exists at the cited place:
`src/core/net/testing/TestLoop_test.cpp:376`. The surviving `jthread` strings are the two
explanatory comments, as intended. (`src/core/net/windows/IocpCanary.cpp:92` still uses
`std::jthread`; it is Windows-only and never compiled on AppleClang, so it is not a miss.)

One residual property, unchanged in kind from `jthread` and not worth a fix: if `blockOn` *throws*,
the join is skipped and `~std::thread` terminates instead of reporting a Catch2 failure. The tree's
convention has the same property.

## Two small observations, neither introduced here

- **There is a fourth state the enumeration does not name: RUNNING.** `~TuiRuntime` reached
  re-entrantly from inside a source flow — `interruptFlow` → `runInterruptPolicy()` →
  `_onInterrupt()`, the one place a source flow calls consumer code inline — finds
  `cancelPending` answering **false** (the loop does not hold a running frame), correctly does not
  resume, and then `~Task` destroys a frame that is currently executing. Pre-existing at `fed98b4`
  and at A7; worth a sentence in the destructor's comment, since that comment now claims three
  states exhaustively.
- The new C1 case covers the input flow only; I confirmed `resizeFlow` empirically and the other
  two by reading.

## Verdict

The fix is sound. C1 is genuinely closed and its case distinguishes. H1's code is right; its
justification is not, and it should carry the case this review demonstrated is writable.
