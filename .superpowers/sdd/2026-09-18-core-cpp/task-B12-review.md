# Review — Task B12: the TUI runtime on the EventLoop

Reviewed: `fed98b4` (`tui: the runtime is composed on core::net::EventLoop; the second scheduler is
gone`), one commit, 26 files, against `.superpowers/sdd/2026-09-18-core-cpp/task-B12-brief.md` and
the implementer's report. Read at `origin/master`; every claim below was derived from the files at
that commit rather than from the diff's context lines.

## Verdicts

**Spec compliance: MET.** Every checklist item in the brief is delivered, and three of them are
delivered better than asked: nine files are deleted rather than the five the plan named, both
`TerminalEventSource` platform bodies are removed instead of adapted, and the `renames.json` work is
twelve `removed` rows plus eight retargeted rather than "a second set of rows". The wake-family
derivation, the "which configuration covered which file" answer, the Windows-backend answer (WFMO
default, IOCP additionally exercised by name) and the four deferred defects are all addressed. Two
shortfalls, neither of them a missed instruction: a behaviour this task *introduces*
(`TuiRuntimeOptions::escapeFlush`) ships with no case, and the module-table row was extended in
`cmake/CoreCppModules.cmake` but not in the three human-facing copies of that same table.

**Task quality: NOT MERGEABLE AS IT STANDS — one Critical, one High.** The design is the right one,
the documentation is unusually good, the gating is honest (the matrix was genuinely re-run after the
rebase; the `--clean-first` reasoning on `clangcl-release` is correct and was applied), and the two
tests the lane had to rewrite were rewritten for the right reason. But the destructor — the change
the dispatch told me to check hardest — has a second lifetime hole of exactly the shape ASan found,
in a state the object can legitimately be in and that no case reaches. It needs a fix round.

## Findings by severity

| Severity | Count |
|---|---|
| Critical | 1 |
| High | 1 |
| Medium | 2 |
| Low | 4 |

---

## C1 (Critical) — `~TuiRuntime` *starts* a source flow that never ran, it re-parks on the loop, and its frame is then destroyed underneath that park

`src/core/tui/runtime/TuiRuntime.cpp:58-64`:

```cpp
for (auto& source: _sources)
{
    if (source.done())
        continue;
    if (_loop.cancelPending(source.handle()) && !source.done())
        source.handle().resume();
}
```

The comment above it enumerates two states a retrieved source flow can be in — PARKED and QUEUED —
and both are handled correctly. There is a **third**, and it is the state every source flow is in
between `TuiRuntime`'s constructor and the first turn: *submitted and not yet started*.

The chain, each link read from the source at `fed98b4`:

1. `async::Task` is lazy — `initial_suspend()` returns `std::suspend_always`
   (`src/core/async/Task.hpp:103`). `startSourceFlow` stores the task in `_sources` and calls
   `_loop.submit(handle)` (`TuiRuntime.cpp:130-131`); it does not start it.
2. `EventLoop::submit` never resumes inline from off-thread: it pushes onto `_inbound.submissions`
   and wakes the backend (`EventLoop.cpp:550-568`). From the worker thread it `queueReady`s. Either
   way the frame is *queued at its initial suspend point*.
3. `cancelPending` searches the ready queue, the park table **and the inbound queue**
   (`EventLoop.cpp:601-650`), so it finds the un-started submission and returns `true` — an
   ownership transfer, correctly.
4. `done()` on a frame suspended at `initial_suspend` is `false`, so the destructor calls
   `resume()`. That **starts the coroutine body from the top.**
5. `inputFlow()`'s first statement is `co_await _loop.waitReadable(_inputHandle)`
   (`TuiRuntime.cpp:141`). The `_stopping` guard is at line 154, *after* the await.
   `WaitHandleAwaiter::await_suspend` sees an un-stopped token and a valid handle, calls
   `registerPark(ParkEntry::onReadiness(...))`, attaches the backend registration, arms its own
   stop callback and suspends (`EventLoop.hpp:932-964`). **The flow is now parked on the loop
   again, and nothing takes it back.**
6. The destructor body ends. `_sources` is a `std::array<async::Task<void>, 4>` member;
   `~Task` destroys the frame (`detail::UniqueCoroHandle`). The loop is left holding a park, a
   `_byWaiter` index entry and a live kernel registration naming freed coroutine storage.

**Failure scenario, concretely.** `EventLoop loop { *backend }; { TuiRuntime rt { loop, source }; }`
— construct and destroy with no turn in between. Afterwards `loop.parkedWaiterCount()` is 1 (or up
to 4, one per configured handle), each park naming a destroyed frame. `~EventLoop` then moves the
borrowed parks to the ready queue and resumes them (teardown steps 2–3), i.e. resumes a destroyed
`std::coroutine_handle`. Before that, any readiness on the terminal handle dispatches into the same
freed frame. This is the identical failure mode ASan reported for the waiter slots, one level up.

**Reachable outside a test.** Any consumer that constructs the runtime and returns on an error path
before driving the loop; any flow that constructs a `TuiRuntime` inside a turn and destroys it in
the same turn (then `submit` used `queueReady`, and the un-started handles sit in `_ready`); and,
with 64+ ready entries, a `dispatchBatch` boundary that leaves the source submissions for the next
turn while `blockOn`'s root completes in this one.

**Why six configurations and ASan were green.** `EventLoop::blockOn` always runs at least one turn
(`EventLoop.hpp:261-315`: `queueReady(root)` then `while (!task.done()) turn(...)`), and **every**
case in `TuiRuntime_test.cpp` and `Modal_test.cpp` calls `blockOn` or `runOnce` before the runtime
leaves scope. The teardown cases go out of their way to drive a turn first
(`TuiRuntime_test.cpp:843`, `:866`) — which is exactly why they cannot see this. No case constructs
a runtime and destroys it without a turn, so the suite reports coverage of a destructor whose
un-started branch is never entered.

**What a fix has to establish, not just patch.** The destructor's invariant is *"nothing the loop
holds names this object when this object returns"*. Resuming a retrieved frame is only sound where
that resume runs an `await_resume`; for an un-started frame it runs a *body*. Either the flows must
refuse to park when `_stopping` is set (the awaiter would need to be asked before the first await,
not after it), or an un-started frame must be destroyed rather than resumed — and the destructor has
to be able to tell the two apart. A regression case is the cheap half: construct, destroy, and
`REQUIRE(loop.parkedWaiterCount() == 0)` with no turn between.

---

## H1 (High) — `releaseInputWaiter` retires a deadline that belongs to a *different* waiter, so a timed wait silently loses its timeout

`src/core/tui/runtime/TuiRuntime.cpp:320-337`:

```cpp
void TuiRuntime::releaseInputWaiter(std::coroutine_handle<> waiter) noexcept
{
    if (!waiter) return;
    if (_inputWaiter == waiter) { _inputWaiter = {}; _inputWake = InputWake::EventOnly; }
    retireTimer(_inputDeadline);          // <-- unconditional
    forgetHandedToLoop(waiter);
}
```

`retireTimer(_inputDeadline)` runs whether or not the slot still belongs to `waiter`. Enumerate
every path that resumes an input waiter — `deliverInput`, `notifyActivity`, `notifyAgentReady`,
`requestCancelWaiter`, `unwindParkedWaiters` — and all five go through `takeInputWaiter()`
(`TuiRuntime.cpp:381-386`), which *already* retires `_inputDeadline`; `onInputDeadline` clears the
id itself before calling `notifyActivity`. So by the time any `await_resume` runs, the resuming
waiter's own deadline is already invalid, and this line is a no-op for its stated purpose ("a waiter
resumed by an EVENT still owns the timer its timeout armed"). The only state in which
`_inputDeadline` is *valid* here is one where it belongs to somebody else.

**Failure scenario.** The interleaving is the one `TuiRuntime.hpp:430-433` says to expect — *"a
second flow may park while the first is still queued, so it is a list rather than a slot."*

1. Flow A: `co_await rt.nextEventFor(10ms)` → `_inputWaiter = A`, `_inputDeadline = D_A`.
2. Input arrives. In turn step 2 the input flow resumes, `routeDecoded` → `deliverInput` →
   `takeInputWaiter()` (retires `D_A`, empties the slot) → `handToLoop(A)`: `A` goes on
   `_handedToLoop` and on the loop's ready queue, *behind* whatever was already queued.
3. Still in the same drain, sibling flow B (already queued ahead of A) resumes and does
   `co_await rt.nextEventFor(5s)`. The slot is empty, so `parkOnInput` accepts it:
   `_inputWaiter = B`, `_inputDeadline = D_B`.
4. The drain reaches A. `NextEventForAwaiter::await_resume` → `releaseInputWaiter(A)`. The slot
   holds B, so the slot survives — and then `retireTimer(_inputDeadline)` cancels **`D_B`**.

B is now parked on input with no deadline. Its 5-second timeout never fires; with no further input
the loop blocks indefinitely in the backend's wait and B's idle ticks — redraws, spinner frames,
status refreshes, which is what `nextEventFor`/`nextActivity` exist for — stop happening. No
assertion trips, nothing is logged, and in Release the symptom is a TUI that stops animating.

The same applies to `nextActivity`, and it is worse there: `ActivityKind::Timeout` becomes
unreachable for that flow.

No case covers two flows interleaving on the input slot, so the suite cannot see this either.

---

## M1 (Medium) — three copies of the module table still say `core::tui` does not link `core::net`, and two of them say B12 is future work

`cmake/CoreCppModules.cmake` was extended correctly (the configure would have refused the link
otherwise), and `.agent/rules/tui.md:17` was updated. The three tables a human reads were not:

- **`AGENT.md:22`** — `| tui | ... | base (leaf); + platform, async, libunicode |`, and the prose at
  `:31-33` still reads *"What is left of Phase B is ... Task B12 moving the TUI runtime onto
  `core::net::EventLoop` -- which is why the `tui` row does not list `net` yet."* This is the file
  every session in this repository loads first.
- **`README.md:33`** — same dependency list, and the status column still says *"its runtime moves
  onto `core::net::EventLoop` in B12"* in the future tense.
- **`docs/modules/index.md:16`** — same again, plus the **Mermaid dependency graph at `:44-48`**,
  which draws `tui --> base`, `tui --> tui_output`, `tui --> platform`, `tui --> async` and **no
  `tui --> net` edge**. That graph is the published module DAG on
  <https://contour-terminal.github.io/core-cpp/>, and it now disagrees with
  `cmake/CoreCppModules.cmake`.

`mkdocs build --strict` passes because nothing structural broke — which is the failure mode this
repository already paid for twice in the two commits immediately preceding this one (`fb2615f`,
"a property removed from the build survived in nine descriptions of it", and `c30f61a`). The commit
updated `docs/modules/tui.md`, `docs/modules/index.md`'s prose is untouched, `source-map.md`,
`provenance.md`, `.agent/rules/tui.md` and `.agent/rules/async-and-net.md` — so the omission is
three tables, not a general lapse.

---

## M2 (Medium) — the escape-flush behaviour is claimed as a fix in the CHANGELOG and has no case

The commit adds `TuiRuntimeOptions::escapeFlush` (50ms), `armEscapeFlush()`, `retireTimer` on a
non-empty decode, the `onEscapeFlush` loop-timer callback and `InputSource::flushPartial()`, and the
CHANGELOG records it under **Fixed** as *"A lone Escape keypress is delivered."*

`grep` over `TuiRuntime_test.cpp` and `Modal_test.cpp` for `pushFlush`, `flushCount`, `readCount` or
`escapeFlush`: **no matches.** `ScriptedInputSource::pushFlush`, `flushCount()` and `readCount()`
(`ScriptedInputSource.hpp:95-118`) are written, documented and never called. So nothing asserts:

- that an empty decode arms the flush at all;
- that a non-empty decode retires it (`TuiRuntime.cpp:164`), which is the branch that decides
  whether an ordinary keystroke leaves a 50ms timer behind on every read;
- that `armEscapeFlush`'s idempotence (`TuiRuntime.cpp:419-420`) keeps the *earlier* deadline;
- that `onEscapeFlush` delivers what `flushPartial()` returns rather than dropping it.

A regression for the defect the CHANGELOG names would be: script an empty read, advance a
`ManualClock` past 50ms, and require the flushed `Escape` to reach a parked `nextEvent()`. The three
unused `ScriptedInputSource` members are exactly the instrument for it — they were built and then
not used, which is the tell.

This is Medium rather than Low because it is a *new* timer keyed on `this` that the destructor has
to retire (`TuiRuntime.cpp:45`), i.e. it is on the lifetime path C1 is about, and nothing exercises
it.

---

## L1 (Low) — the `cancelPending` workaround is not labelled as one, and does not name core-cpp#41

`~TuiRuntime`'s comment (`TuiRuntime.cpp:50-57`) explains *why* a retrieved frame is resumed once
more — "a QUEUED one does not [come back detached] -- only `await_resume` unregisters" — and the new
bullet in `.agent/rules/async-and-net.md` states the same thing as a durable property of the loop.
Neither says that this is a defect the lane filed as **core-cpp#41**; `grep` finds no reference to
that issue anywhere in the tree. The workaround is well contained (one resume, in one place,
correct), so this is not a correctness finding — but whoever fixes #41 so that `cancelPending`
unregisters the park on a ready-queue hit will not be led here, and will leave behind a rule that
now states the opposite of the code and a resume whose justification has evaporated. One sentence
in both places naming the issue closes it.

## L2 (Low) — a deleted type survives in a public header's documentation

`src/core/tui/TerminalInput.hpp:53` still reads *"@c poll() and the runtime's @c
TerminalEventSource deliver pending events first"*. `TerminalEventSource` is deleted by this commit,
and this is a Doxygen comment on a public API, so it is rendered on the API reference site. It is
the only live-code reference to any of the nine deleted files.

## L3 (Low) — the stop callback is armed after the park is published, against a rule in this module's own rulebook

All four input awaiters call `parkOnInput`/`parkOnAgent` and *then* `_cancelReg.emplace(...)`
(`TuiRuntime.hpp:467-472`, `:521-523`, `:574-576`, `:628-630`). `.agent/rules/async-and-net.md`
states the opposite as a rule: *"A stop callback is registered before the park is published ... A
token that is already stopped runs the callback in the `StopCallback` constructor, on the
registering thread ... after the park is published it hands the handle to an executor that may
resume a coroutine whose `await_suspend` has not returned."*

The comment at `TuiRuntime.hpp:468-470` argues the order on a *different* ground — that nothing needs
unwinding if the emplace throws — and does not address the hazard the rule is about. In practice the
deviation is benign here: `requestCancelWaiter` queues through `handToLoop` → `resumeSoon` rather
than resuming inline, so an inline callback run cannot resume a coroutine mid-`await_suspend`; and a
cross-thread stop trips `requestCancelWaiter`'s own assertion
(`TuiRuntime.cpp:363-365`) rather than corrupting state. But a deviation from a written rule should
be argued on that rule's terms, in the comment, or the next reader will read the rule and the code
and conclude one of them is wrong.

## L4 (Low) — several cases depend on the test process having a usable stdin, invisibly

`ScriptedInputSource::inputHandle()` returns `platform::standardInput()` when no pipe was given
(`ScriptedInputSource.hpp:122-125`). Five cases construct `ScriptedInputSource {}` that way —
core-cpp#17, core-cpp#18, `delay(0)`, `waitReadable on an invalid handle`, `withTimeout returns the
work's value`. If the binary is run with stdin closed, `startSourceFlows` starts no input flow at
all (`TuiRuntime.cpp:112`), so core-cpp#17's `backend.pushReadable(HandlerId { 1 })` names a
registration that does not exist and the case hangs rather than failing. The file's header comment
explains the `HandlerId` numbering but not the stdin dependency it rests on. A named
`platform::InvalidHandle` sentinel for "no input channel", or an assertion that the handle is valid,
would make the premise checkable.

---

## What I checked and found sound

Credited rather than re-reported, and re-derived rather than taken from the report:

- **The ASan fix's direction is right.** Unwinding in the destructor body, with `isStopping()`
  consulted in *both* `await_suspend` and `await_resume`, is what makes the slot loop
  (`TuiRuntime.cpp:89-94`) terminate in at most two passes: a resumed body that catches
  `OperationCancelled` and awaits again gets `await_suspend() == false` and cannot re-enter a slot.
  I checked the double-resume question specifically: a waiter is in `_handedToLoop` **or** in a
  slot, never both — every `handToLoop` call site (`deliverInput`, `notifyActivity`,
  `notifyAgentReady`, `requestCancelWaiter`) takes the waiter out of its slot first, and a second
  call then finds an empty slot and returns. No waiter can be pushed onto `_handedToLoop` twice. No
  `StopCallback` is left armed: every `await_resume` begins with `_cancelReg.reset()`, and the
  destructor resumes every waiter it holds.
- **The `whenAny`-loser question, which is where a deferred cancellation usually dangles.**
  `requestCancelWaiter` queues rather than resolving inline, which `async-and-net.md` warns about
  ("a `whenAny` loser is freed the moment the winner returns"). It is safe here because
  `JoinAwaiter` resumes the parent only when `remaining` reaches zero (`Join.hpp:168`) — `whenAny`
  waits for every loser to finish, so no loser frame is freed while queued.
- **Deletions are complete.** `git grep` at `fed98b4` for `EventSource`, `PollEventSource`,
  `TerminalEventSource`, `WithTimeout`, `PollHelpers` and `MockEventSource` outside
  `provenance.md`/`CHANGELOG`/`renames.json`/the SDD records finds no live-code reference except L2
  above. `runtime/posix/` and `runtime/windows/` are gone, both `CMakeLists.txt` source lists are
  correct, and the `#ifdef`-in-logic claim holds.
- **The module table row.** `cmake/CoreCppModules.cmake:235-236` carries `net`, with a comment that
  says why the dependency is public (the loop's types are in `TuiRuntime.hpp`'s signatures) and that
  `tui_output` is unaffected. `PUBLIC_LIBS` matches. The `tui_output` target still links base alone.
- **`DefaultHandleKind` is the right default rather than a missing argument.** It is
  `HandleKind::Waitable` on Windows and `HandleKind::Fd` elsewhere (`IoBackend.hpp:80-85`), which is
  correct for all four TUI handles; the brief's `HandleKind::Waitable` is the default rather than
  something omitted.
- **The rewritten core-cpp#18 case earns its place.** Asserting `backend.waitCount() == 0` is the
  argument, not the outcome, and the old code cannot pass it by accident. The second half (the slot
  is free for the next flow) is the complement. core-cpp#17's `waitCount() == 3` with every recorded
  timeout indefinite is likewise an argument test, and the extra wait is explained rather than
  absorbed.
- **CHANGELOG and `renames.json`.** The Breaking entry is migration-grade: construction, the agent
  wakeup, the interrupt options, `withTimeout`, the `FdRegistrationFailed`-vs-`OperationCancelled`
  change (correctly called out as two facts that used to be one), the headless case and the test
  doubles. `renames.json` retargets four include rows, drops four, and adds twelve `removed` rows,
  `core::tui::runtime::`-rooted as ruling R104 requires.
- **Gating.** The `--clean-first` on `clangcl-release` after an upstream `EventLoop.hpp` change is
  the right call and the reasoning is the right reasoning; the 7-step incremental build was checked
  step by step rather than trusted; the Release skip counts are identified as pre-existing canaries
  and cross-checked against the Debug legs where they pass.
