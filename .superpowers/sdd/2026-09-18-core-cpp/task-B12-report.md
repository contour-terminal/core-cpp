# Task B12 — the TUI runtime on the EventLoop

Worktree `D:/core-cpp-wt-b12`, one commit: **`fed98b4`**
(`tui: the runtime is composed on core::net::EventLoop; the second scheduler is gone`), rebased onto
`origin/master` = **`2ea0614`**. 26 files, +2213 / −1997. Nothing in `D:\core-cpp`'s working tree or
its `master` ref was touched; this report is the only file written there, and it is untracked.

**Two things happened to this task that the numbers below reflect.** It was amended once, after
AddressSanitizer found a real lifetime defect in the first version's teardown — see "What ASan
found". And it was gated twice: the first matrix ran at `5d7a5ae`, the base moved six commits, and
**every configuration was re-run after the rebase** rather than reported from the stale base. The
rebase was clean; one of my findings was fixed upstream in the meantime and is reported as history
rather than as current, below.

The base moved three times while this task ran: `5d7a5ae` -> `c30f61a` (six commits, not the four
I was told to expect) -> `2ea0614` (two more, doc-only). The full matrix was re-run after the first
rebase. The second brought no source change, so the compiled results stand; the gates that read the
tree rather than the binaries — `mkdocs --strict`, `check-renames`, `clang-format --check` and
`ctest -L hygiene` (18/18, 0 skipped) — were re-run after it, because those are the ones two
documentation commits can break.

## What was actually built

`TuiRuntime` keeps exactly one thing: **input semantics** — the decoded-event buffer, the
`nextEvent` / `nextEventFor` / `nextActivity` / `nextAgentReady` vocabulary, and the interrupt
policy. Every scheduling member forwards to `core::net::EventLoop`: `blockOn`, `spawn`, `delay`,
`sleepUntil`, `waitReadable`, `waitWritable`, `clock()`, `rootStopSource()`.

Its sources are **four parked flows, one per handle** — terminal input, resize, the interrupt
wakeup, the POSIX signal fd — each on `loop.waitReadable()`. The agent wakeup is not one of them
and has no handle at all: a worker calls `loop.post([&]{ runtime.notifyAgentReady(); })`.

Nine files deleted (see "what the brief still gets wrong"), three added:
`runtime/InputSource.hpp` (the seam: handles in, decoded events out, no `wait()`),
`runtime/TerminalInputSource.hpp` (the production adapter, header-only, **no platform body**), and
`runtime/testing/ScriptedInputSource.hpp` (the decoding double).

## RED, verbatim

Both cases were written against the runtime **as it stood** and run in `clang-debug`, so the
defects were observed failing before anything was rewritten. I said beforehand that #18 would trip
the input-waiter assertion and #16 would need ASan; **both predictions were wrong**, and the first
one was wrong in a way that mattered — see below.

### core-cpp#16 — exit 139

```
RED core-cpp#16: a cancelled delay leaves nothing behind for the pump to touch
-------------------------------------------------------------------------------
B12Red_test.cpp:130: FAILED:
due to a fatal error condition:
  SIGSEGV - Segmentation violation signal

test cases: 1 | 1 failed
assertions: 1 | 1 failed
```

**Stronger than the issue claims.** core-cpp#16 says "if that reads as not-done, the handle is
pushed onto `_ready` and resumed" — a conditional. It is not conditional: a plain `clang-debug`
build segfaults, no sanitizer needed.

### core-cpp#18 — and the first version of this case was wrong

```
RED core-cpp#18: a whenAny loser parked on input unwinds without waiting for input
-------------------------------------------------------------------------------
B12Red_test.cpp:126: FAILED:
  REQUIRE( source.waitCount() == 0 )
with expansion:
  1 == 0
```

My first version of this case raced a loser parked on input, then awaited input again, expecting
`assert(!_inputWaiter)` to trip. **It passed.** The wake that unwinds the cancelled loser is the
same wake that fills the input buffer, so the second `co_await` found a buffered event, never
parked, and never reached the slot. An outcome test could not see the defect at all.

The defect's real signature is that the loser cannot unwind *without* input arriving — so the
assertion is on the ARGUMENT: this race must need **no wait whatsoever**. That is what fails
above, and it is a case the old code cannot pass by accident.

## GREEN

`core-cpp-tui-test`, filters `[TuiRuntime],[Modal],[TerminalQuery]`: **47 cases, 230 assertions,
all passed.** Per defect:

| Defect | The assertion that distinguishes it |
|---|---|
| #16 | the race's loser unwinds cancelled, and after outliving its deadline `pendingTimerCount()` **and** `pendingTimerSlotCount()` are both 0 |
| #17 | `waitCount() == 3` for two scripted steps, and **every** recorded timeout is indefinite — no zero-timeout spin anywhere |
| #18 | the race resolves with `waitCount() == 0`, the loser records that it unwound cancelled, and a second case proves the slot is free for the next flow |
| #19 | 70 concurrent handle waits (past `MAXIMUM_WAIT_OBJECTS`) all resolve |

Plus two defects found on the way, each with a case: a focus change used to resume a `nextEvent()`
waiter, which can only yield an event or throw — so it threw `OperationCancelled` and `runModal`
closed the modal; and a lone ESC was never delivered, because the parser's timeout hook only ran
when a wait timed out, which for an untimed `nextEvent()` never happened.

On #17's count of three: the extra wait is **not slack**. The runtime is constructed outside a
turn, so its `submit` of the input flow goes through the inbound queue and wakes the backend, and
`ScriptedBackend`'s first wait consumes that wake without spending a script step. I chased that
number down rather than loosening the assertion.

## What ASan found, and why it is the most important thing in this task

The first version passed `clang-format`, `clang-tidy`, `clang-debug`, `gcc-release`, `cl-debug`
and `clangcl-release` — six green configurations — and was **wrong**. `clang-asan-ubsan` reported:

```
==6419==ERROR: AddressSanitizer: stack-use-after-scope
READ of size 8 at 0x7932893b16b0 thread T0
...
    [1712, 1992) 'runtime' (line 885) <== Memory access at offset 1712 is inside this variable
17/32 Test #17: core-cpp.tui ................................***Failed   11.32 sec
```

**The defect.** A flow parked in the runtime's own waiter slot — not a loop park — holds an
awaiter whose `_cancelReg` is a `StopCallback` registered on a token the **loop** owns, whose
lambda names the **runtime**. My first `~TuiRuntime` cleared the slots and left the flow parked.
`~EventLoop` then ran its `request_stop()`, which fired that callback into a `TuiRuntime` that had
been destroyed one scope earlier.

**Why no other gate saw it.** The read lands on stack storage that is out of scope but not yet
reused, so it is well-defined-looking garbage in every non-instrumented build. The case that
exercised it — a spawned flow parked on input across `~TuiRuntime` — *passed* everywhere else.

**The fix, and it changed the design rather than patching the symptom.** Anything the runtime
parked is unwound in `~TuiRuntime`, while every member it is about to read is still alive:

- waiters the loop is already holding are taken back with `cancelPending` and resumed (they are
  tracked in `_handedToLoop` precisely because both of the things that happen to a queued waiter
  next — `await_resume` and the cancellation callback — read this runtime);
- waiters still in a slot are resumed directly;
- `isStopping()` is consulted by all four awaiters in **both** directions: `await_suspend`
  declines to park and `await_resume` throws. That pair is what makes the unwind converge in at
  most two passes whatever a resumed body does — a flow that catches the cancellation and awaits
  again cannot get back into a slot that is going away.

**The test that held it had to change, and the change is a strengthening.** It asserted
`REQUIRE_FALSE(destroyed)` after `~TuiRuntime`, encoding the old ordering. The guard must now run
*at* `~TuiRuntime` — waiting for `~EventLoop` **is** the defect, so "it unwound eventually" cannot
tell the two apart. The case now says so in as many words, and additionally requires the loop to
be holding nothing of the runtime afterwards.

`ctest --preset clang-asan-ubsan` then passes the tui suite (`17/32 core-cpp.tui ... Passed`).

## What the brief still gets wrong

**1. `ParkEntry::onReady` does not exist, and neither does a frameless readiness park.**
The brief I was dispatched with said *"B6 owns `ISocket` and a frameless readiness park
(`ParkEntry::onReady`); your input pump is a consumer of that mechanism, not a second one."*
Derived from `src/core/net/detail/ParkTable.hpp`, the factories are `onDeadline`, `onCallback` and
`onReadiness`. So there IS a readiness park and there IS a frameless park, and there is **no
frameless readiness park**: `onReadiness` takes a coroutine handle, and `grep -rn onReady src/`
finds only two test-local callbacks. Still true at `c30f61a`.

This is the single correction that changed the design. With no frameless readiness park, each
source is a coroutine whose frame names the runtime — which is the whole reason the destructor has
to take those frames back off the loop, and the reason the frames are owned by `TuiRuntime` via
`submit` rather than handed to the loop via `spawn` (the loop offers no way to give a spawned frame
back). That obligation is now written into `.agent/rules/async-and-net.md`.

**Already actioned upstream**: the brief at `c30f61a` now records this, and says B6's approved
scope expansion adds the missing variant. I did not design toward it; it is listed as a follow-up
below.

**2. The deletion list is right as far as it goes, and it undercounts.** The brief's five
corrected rows are all correct — I re-ran `find src -iname ...` rather than trusting them. But it
describes `TerminalEventSource` as becoming an input adapter, which reads as three files surviving
in changed form. In fact **both platform bodies are deleted outright**, not adapted: everything
`TerminalInputSource` asks is already portable through `TerminalInput`, so the adapter is
header-only. Nine files are deleted, not five, and `src/core/tui/runtime/posix/` and
`src/core/tui/runtime/windows/` **cease to exist**.

**3. The wake-family paragraph was wrong at my original base, and has since been fixed upstream —
so this is history, not a live finding.** I derived it both times rather than re-reading my own
note, which is the whole point of the rule.

At **`5d7a5ae`** (the base I was dispatched against), grepping every `_backend.wake()` and
`armHostWake()` call site with its enclosing function gave: six members waking (`submit`,
`schedule`, `post`, `stop`, `spawn`, `requestCancel`), `armHostWake()` from `turn()` and
`addTimer()`, and `registerPark`, `resumeSoon` and `requestStop` calling **neither**. Against the
brief as dispatched, that made it wrong four ways — `requestStop` and `resumeSoon` named as waking
when they did not, `schedule` omitted when it did, and `registerPark` credited with an arming it
did not perform.

At **`c30f61a`** (after `29e9b24`, which landed while I was implementing), the same derivation
gives: **eight** members waking — `submit`, `schedule`, `post`, `stop`, `requestStop`, `spawn`,
`resumeSoon`, `requestCancel` — and `armHostWake()` from `turn()` and **`registerPark()`**. So
`29e9b24` moved the arming down into the primitive and added the two missing wakes, and the
paragraph is now accurate. `.agent/rules/async-and-net.md`'s "Ready work wakes; a park arms"
section matches the code, with `schedule` covered by its own bullet rather than by the list.

**Nothing of mine depended on the difference.** I consume `addTimer`, which reaches `registerPark`
either way, and the arming is a no-op on a backend that is not host-driven — which the TUI's never
is.

**4. `hasPendingWork()`** — confirmed gone at both bases, as the dispatch said. I consume only the
public API and never needed it, and nothing I wrote cites it.

**5. `renames.json` needed more than the brief's "second set of rows".** Six `include` rows and
two `symbol` rows pointed at headers this task deletes, so the drift gate failed on eight rows
before I added anything. Four include rows are retargeted to successor headers, four rows are
deleted outright (no successor exists), and **twelve** `removed` rows are added. `check-renames.py`
now reports `514 rows, 475 delivered, 1 pending, 21 removed: 0 failure(s)`.

## Which configuration covered which platform file

There is no longer a platform file in `runtime/` to cover — that is the result, not an evasion. The
platform coverage that matters is now the **backend** each configuration exercised, and the new
case *"Input arriving while the loop waits resumes the flow on the loop thread"* runs over
`core::net::testing::BackendMatrix`, skipping what a platform does not build:

| Configuration | Backends actually exercised (from `-s` output) |
|---|---|
| WSL `clang-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan` | `poll`, `epoll` |
| Windows `cl-debug`, `clangcl-release` | **`iocp`, `wfmo`** |

Re-confirmed after the rebase: `preferredBackendKind()` in `src/core/net/windows/DefaultBackend.cpp`
still returns `BackendKind::Wfmo`, and the matrix case still names both.

Other files: `posix/TerminalInput.cpp` (the SIGWINCH self-pipe, the resize handle) is covered by
the WSL legs through the resize case; `windows/TerminalInput.cpp` (the console handles) by the two
Windows legs. `TerminalInputSource` is covered by `TerminalQuery_test` on every leg.

## Which Windows backend the runtime was tested against

**Both, by name.** `makeDefaultBackend()` still returns **WFMO** on Windows — B7b has not run — so
the default path is WFMO, and that is what every other Windows case used. The matrix case
additionally constructs `makeBackend(BackendKind::Iocp)` explicitly and runs the same
input-arrives-while-waiting scenario over it, so the IOCP waitable-HANDLE bridge is exercised by
the console-handle-shaped case that is the reason it exists. Verbatim from `cl-debug`:

```
  backend=iocp
  ...PASSED
  backend=wfmo
  ...PASSED
All tests passed (6 assertions in 1 test case)
```

## core-cpp#16–19

All four are **closed**, and I checked each against the "the file is gone is only an answer for the
ones whose file is actually gone" test:

- **#16** — the file (`TuiRuntime.hpp`'s own `DelayAwaiter`) is genuinely gone; `delay()` returns
  `core::net::DelayAwaiter`, which unregisters its park on every resume. Regression case asserts
  the park counts, as the issue asked.
- **#17** — `pumpOnce` and `blockOn` are gone; `EventLoop::blockOn` waits. Note the loop's own
  documented residue: on an `IdlePolicy::Return` loop `blockOn` still polls. The TUI never
  constructs one (`IdlePolicy::Block` is the default), so this is not reachable through the TUI.
- **#18 — NOT fixed by construction, and it would have survived a careless rewrite.** The waiter
  slots are TUI state I re-implemented; a rewrite that kept them without stop-callbacks would have
  carried the defect over intact. All four input awaiters now arm one, and
  `requestCancelWaiter()` releases the slot and its deadline.
- **#19** — both files with the unchecked count are deleted, and I verified rather than assumed
  that `core::net` chunks: `WfmoBackend.cpp` sweeps in chunks of `MAXIMUM_WAIT_OBJECTS` via
  `detail/WaitChunking.hpp`, with a fair rotation. Held by the 70-handle case.

`.agent/rules/platform.md`'s rule — *an OS difference is an injected implementation, never an
`#ifdef` in logic* — now holds in `core::tui` **by construction**: `runtime/PollEventSource.cpp`
was the last `#ifdef`-in-logic file in the module, and it is deleted.

## Gates

**Every row below was re-run after the rebase onto `c30f61a`.** The first matrix ran at `5d7a5ae`
and was discarded rather than reported.

| Gate | Result at `c30f61a` |
|---|---|
| `clang-format.py --check` (8 touched files) | formatted, clang-format 22.1.8 |
| `check-renames.py` | 514 rows, 475 delivered, 1 pending, 21 removed — **0 failures** |
| `mkdocs build --strict` | clean |
| `clang-tidy` preset | **0 errors**, 56 steps, of which 11 are `core::tui`: all three new headers compiled standalone, plus `TuiRuntime.cpp`, both test files and `TerminalQuery_test.cpp`. (The first full run at the old base analysed 523 steps and found 3, all in the new test file — 2 × `bugprone-empty-catch`, 1 × `modernize-use-integer-sign-comparison` — fixed by making the cancellation observable rather than by silencing.) |
| WSL `clang-debug` | **34/34 passed, 0 skipped** |
| WSL `gcc-release` | **34/34 passed, 3 skipped** |
| WSL `clang-asan-ubsan` | **34/34 passed, 0 skipped**, **0 sanitizer diagnostics** |
| WSL `clang-tsan` | **34/34 passed, 0 skipped**, **0 race reports** |
| Windows `cl-debug` | **38/38 passed, 0 skipped** |
| Windows `clangcl-release` | **38/38 passed, 6 skipped**; **519 steps, `--clean-first`** |

`ctest -L hygiene` is inside each of those totals, including the preset-coverage gate `2224192`
added. Skip counts are inline `***Skipped` markers only — the trailing "did not run" list repeats
the same tests, and counting both doubles them.

The counts moved (32→34 on Linux, 36→38 on Windows) because `2224192` and `a48e727` added tests
upstream; none is in `core::tui`.

**The 3 skips in `gcc-release` and the 6 in `clangcl-release` are all canaries, and all
pre-existing.** `hostdriven-canary.{run,blockOn,spawnOffThread}`, `iocp-canary.{g1,g4}` and
`windows-dialog-canary.assert` exist to trip an `assert`, which a Release build compiles out. The
same canaries **pass** in `clang-debug` and `cl-debug`, which is the tell that the skip is
configuration and not breakage. None is in `core::tui`.

**On `clangcl-release`'s step count, which is the one number that can lie here.** The launcher is
**fastcache-cc** (read out of `build.ninja`, not `CMakeCache.txt`). The rebase brought in an
upstream change to `EventLoop.hpp`, and an incremental clang-cl build over somebody else's header
change is exactly the case where a cache serves stale objects and the suite passes on them — so
this leg was rebuilt with `--clean-first`, and all 519 steps ran. The earlier Windows run in this
worktree was a fresh configure, for the same reason. I also checked the one incremental clang-cl
build I did take (7 steps, after a header edit of my own) step by step rather than trusting the
number: both header self-check TUs for the changed header, `TuiRuntime.cpp`, the library link,
`Modal_test.cpp`, `TuiRuntime_test.cpp`, the executable link — the right seven, not an inherited
seven.

## What I am leaving for B13, named explicitly

1. **The consumer migrations are not done here and are not mine.** endo and tuidu must:
   construct `TuiRuntime(loop, terminal)`; move the agent wakeup to
   `loop.post([&]{ runtime.notifyAgentReady(); })`; pass the interrupt `Wakeup` and the POSIX
   signal fd through `TuiRuntimeOptions`; replace `runtime::withTimeout(&runtime, …)` with
   `core::net::withTimeout(&runtime.loop(), …)`; and catch `core::net::FdRegistrationFailed`
   wherever they relied on a refused handle surfacing as `OperationCancelled`. `renames.json`
   carries a row for every removed name. contour, fastcached and Lightweight's `dbtool` are
   unaffected — none links `core::tui`'s runtime, and `core::tui_output` still links `base` alone.

2. **`TuiRuntimeOptions::escapeFlush` is new behaviour with no consumer-tuned value.** 50ms is
   endo's documented figure for `VtParser::timeout()`. Nobody has exercised it against a real
   terminal over ssh, where the inter-byte gap is larger.

3. **The `windows-dialog-canary.assert`, `iocp-canary` and `hostdriven-canary` skips in Release
   configurations are load-bearing gaps that nothing currently reports as gaps.** A canary that
   skips silently in exactly the configuration CI ships is worth a decision before v0.1.0; it is
   pre-existing and outside B12's scope.

4. **B6's frameless readiness park is the follow-up that would simplify the pump.** B6 is adding a
   `ReadyCallback onReady` field to `ParkEntry` — the variant whose absence forced each source to
   be a coroutine, and with it the whole `cancelPending`-and-resume teardown. I deliberately did
   **not** design toward it. Once it lands, the four source flows could become four frameless
   readiness parks, the `_sources` array and `unwindParkedWaiters`'s second half would go, and
   `~TuiRuntime` would be `unregisterPark` per source. The input semantics would not change.

5. **Not attempted, deliberately:** `EventLoop`'s `IdlePolicy::Return` + `blockOn` spin (the
   loop's own documented residue of core-cpp#17) is `core::net`'s to decide, not the TUI's.

6. **One defect in another lane's file that I did NOT fix** — see Concerns below. I changed no
   file under `src/core/net/`.

## Concerns

- **None blocking.** I needed no change to `EventLoop` and made none; `git show --stat` confirms
  the commit touches no file under `src/core/net/`.
- **One observation about `EventLoop`, for whoever owns it.** `cancelPending()` checks the ready
  queue *first* and returns on a hit, leaving the park itself in `_parks` with its backend
  registration still attached — only `await_resume` unregisters a readiness park, and a frame
  taken from the ready queue has not run it. So `cancelPending` on a **queued** readiness waiter
  transfers ownership of the frame but leaks the park and its kernel registration for the loop's
  lifetime. It is not a crash and it is not reachable from `net` itself today (nothing there calls
  `cancelPending` on a readiness waiter). `TuiRuntime`'s destructor works around it by resuming the
  frame once after retrieving it, so `await_resume` runs and unregisters — which the destructor's
  comment states. Reporting, not fixing: `EventLoop.cpp` is B5's right now.
- **RESOLVED upstream.** I reported that `CHANGELOG.md:534` still described the IOCP canaries as
  `WILL_FAIL` after `a48e727` removed the property. `fb2615f` fixed it and found thirteen more
  instances, four of them live. Recorded here because the shape generalises: I found one by
  reading the line my own rebase happened to touch, and enumerating the tree found the rest — the
  same difference between noticing and auditing that this task's other findings turn on.
- **`.agent/rules/async-and-net.md` was edited in my worktree** (one new bullet under "Socket and
  coroutine lifetime", stating the obligation a loop-parking object carries). The rebase onto `c30f61a` applied
  cleanly over that file's upstream rewrite, and both survive: my bullet sits under "Socket and
  coroutine lifetime", theirs is the new "Ready work wakes; a park arms" section.
