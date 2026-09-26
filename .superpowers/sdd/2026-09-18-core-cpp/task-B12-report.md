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

---

# Fix round 1

Second commit, on top of `fed98b4`. Eight findings: C1, H1, M1, M2, L1-L4. **All eight addressed**,
one of them with a case I could not write — said so below rather than papered over.

## CI caught what seven local configurations could not

`fed98b4`'s CI Build **failed**, on macOS only, in both `appleclang` and `appleclang-debug`:

```
TuiRuntime_test.cpp:451:19: error: no member named 'jthread' in namespace 'std'; did you mean 'pread'?
```

I used `std::jthread` in two cases. AppleClang's libc++ has no `<stop_token>`, so it has no
`jthread`. **The rule was already written down in this tree** — `TestLoop_test.cpp:376` says
*"`std::thread` with an explicit join rather than `std::jthread`: AppleClang's libc++ has no
`<stop_token>` ... and this file has to build there"* — and I did not read it, because nothing I
could run would fail without it. Windows and WSL are the two platforms on this machine; macOS is
reachable only through CI.

This is the same shape as C1 one level out: **a case unreachable from the suite, and a platform
unreachable from the machine.** Both were green everywhere I could look.

**Fixed with `std::thread` and an explicit join**, not by trying to make `jthread` work. core-cpp
has a fallback for `stop_token` — `core::async::StopToken` is exactly that — but none for
`jthread`, and the `-fexperimental-library` usage requirement rides on `core::async`, which a tui
test target does not necessarily inherit. `std::thread` + `join()` needs no fallback and is already
the tree's convention for this exact reason; the reason is in a comment beside each use, and the
word `jthread` survives *only* in those comments, which is the same distinction `fb2615f` drew for
`WILL_FAIL` (a comment explaining a replacement is correct and must survive).

**Two things about how this happened, worth more than the fix.**

- **Seven green configurations and none of them macOS.** That is not a process failure: the
  hypothesis was not checkable on this machine, and a local green is not evidence about what a
  local run cannot reach. CI refuted it within the hour, which is the system working.
- **Both macOS legs failed — `appleclang` as well as `appleclang-debug`.** The new Debug legs from
  `2224192` landed an hour before and it would be easy to credit them with catching this; they did
  not catch it alone. The release leg would have caught it on its own, because the defect is a
  compile error and not a Debug-only behaviour.

## C1 (Critical) — three states, not two

**RED, verbatim**, from the new case run against the previous commit:

```
A runtime destroyed before its first turn leaves the loop holding nothing
-------------------------------------------------------------------------------
TuiRuntime_test.cpp:858: FAILED:
  REQUIRE( loop.parkedWaiterCount() == 0 )
with expansion:
  1 == 0

TuiRuntime_test.cpp:858: FAILED:
due to a fatal error condition:
  SIGSEGV - Segmentation violation signal
```

Worse than the review predicted: not only the leaked park but a **segfault in the same case**, when
`~EventLoop` resumes the frame `_sources` destroyed. Verified in the source first —
`Task::initial_suspend` returns `std::suspend_always`, and its own comment says an un-started task
"can be destroyed without ever running", so resuming one runs its body from the top.

**Fixed by re-deriving the state space, not by adding a third branch.** A source flow at teardown is
in one of three states, and `cancelPending` answers true for all three: submitted-never-started
(the frame is at its initial suspend point in the inbound queue), parked (the park is taken and
detached), queued-after-readiness (the park is still filed and attached — core-cpp#41). The
destructor takes **one** action that is correct for all three, and correct because of the flows'
*shape* rather than a test on the state: resume once, and let the teardown flag end the body at its
first statement. That is what `while (!_stopping)` before the first `co_await` buys, and it is
load-bearing for state 1 alone.

Choosing a shape over a branch is deliberate: a third branch bolted onto two is how the fourth gets
missed, which is the failure this whole task has been about.

## H1 (High) — fixed, and I could not write a case for it

`releaseInputWaiter` retired `_inputDeadline` unconditionally; a waiter's deadline is retired where
it *leaves the slot*, so by the time a queued waiter's `await_resume` ran the slot could hold a
different flow's timer. Now retired only in the branch where the slot still holds the resuming
waiter.

**I could not construct a case, and I believe the defect is latent rather than reachable through
the public API today.** Derived rather than assumed: `queueReady` appends at the back,
`drainReadyQueue` takes from the front (`_ready.front()` / `pop_front()`), and `parkOnInput`
asserts the slot is empty. For the bug to bite, a second flow must park on input between a waiter
being queued and that waiter resuming — but everything that could park is either already ahead of
the queued waiter (and so ran while the slot was still occupied, which the assertion forbids) or
appended behind it. I tried the batch bound and a cross-thread submit; both append behind.

So the fix is a **narrowing that cannot regress the common path** and closes the defect before the
ordering that hides it changes — if the drain ever stops being FIFO, or a park is ever allowed
while one is queued, the old code would have lost a timeout silently. What *is* covered is the
other half of the contract: two cases require `pendingTimerCount() == 0` after a timed wait
resolves, by event and by deadline.

> **Corrected in fix round 2: the derivation above is wrong, and H1 was reachable.** It is kept
> as written so the mistake stays visible. See *Fix round 2, item 2* below.

## M2 (Medium) — the escape flush now has cases, and passed on first run

Two: an empty decode arms the flush and the flushed Escape reaches a parked `nextEvent()` (with a
`ManualClock` crossing the 50ms deadline, asserting `flushCount() == 1` and no timer left behind);
and a non-empty decode arms nothing (`flushCount() == 0`), which is the branch deciding whether an
ordinary keystroke leaves a 50ms timer on every read. **Both passed the first time they ran** — the
behaviour was right and merely unasserted, which is exactly what the review said and is the reason
the three unused `ScriptedInputSource` members were the tell.

## The rest

- **M1** — `AGENT.md`, `README.md` and `docs/modules/index.md` now list `net` in the `tui` row and
  describe B12 in the past tense, and the published Mermaid DAG gains its `tui --> net` edge.
- **L1** — `~TuiRuntime` and the rulebook bullet both name core-cpp#41 and say the resume is a
  workaround to be **deleted** when the primitive guarantees the property.
- **L2** — `TerminalInput.hpp`'s Doxygen no longer names the deleted `TerminalEventSource`.
- **L3** — the stop-callback ordering is now argued on the rule's own terms: the hazard the rule
  guards is an already-stopped token running the callback inline at construction, and every awaiter
  returns false on `stop_requested()` *before* it parks or emplaces, so that case never reaches the
  emplace; a later stop enqueues through `resumeSoon` and never resumes inline.
- **L4** — `ScriptedInputSource` no longer answers `standardInput()` by default. A source with no
  pipe reports `InvalidHandle` (the headless case) unless the case asks for `HandleFor::Input`, and
  the four cases that need an input flow assert `inputHandle() != InvalidHandle` — so the premise
  is stated and checked instead of turning a closed stdin on Windows into a hang.

## Gates, fix round

| Gate | Result |
|---|---|
| `clang-format --check` | formatted, 22.1.8 |
| `check-renames` | 0 failures |
| `mkdocs --strict` | clean |
| `clang-tidy` | **0 errors**, 21 tui steps re-analysed |
| WSL `clang-debug` | **34/34, 0 skipped**, 0 diagnostics |
| WSL `gcc-release` | **34/34, 3 skipped** |
| WSL `clang-asan-ubsan` | **34/34, 0 skipped, 0 diagnostics** |
| WSL `clang-tsan` | **34/34, 0 skipped, 0 race reports** |
| Windows `cl-debug` | **38/38, 0 skipped** |
| Windows `clangcl-release` | **38/38, 6 skipped**, 519 steps `--clean-first` |

TUI suite: **50 cases, 244 assertions**. macOS remains CI-only; that is the leg to watch on this
push, and the one that failed last time.

# Fix round 2

Third commit, on top of `a922f9e` (rebased from `c8264a0`; nothing between the two touches
`src/core/tui/`, `EventLoop`, `IoBackend` or `platform`, checked with
`git diff --name-only c8264a0 origin/master -- src/core/tui/`). Four items from re-review 1.
Resumed from `D:/core-cpp-wt-b12b` after the previous two agents were lost. The H1 case, the
four-flow C1 case and an unused `CalloutScope` were already in that worktree. I re-ran each one
rather than trusting it.

## Item 1: the H1 regression case, re-verified

`A waiter released after a sibling took the input slot leaves the sibling's timeout armed`. RED
with **only** `releaseInputWaiter` reverted to its `fed98b4` form (`retireTimer` outside the
`if`), everything else at the round-2 tree, `clang-debug`, build exit 0:

```
TuiRuntime_test.cpp:717: FAILED:  CHECK( loop.pendingTimerCount() == 1 )   with expansion: 0 == 1
TuiRuntime_test.cpp:728: FAILED:  REQUIRE( siblingTimedOut )               with expansion: false
whole binary:  test cases: 1043 | 1042 passed | 1 failed
```

It is still the only case in the binary that sees the defect. The binary now has 1043 cases, one
more than before because of item 3.

## Item 2: why round 1 derived H1 unreachable, and why that was wrong

**What was derived.** Round 1 started from three premises, and all three are true. `queueReady`
appends at the back. `drainReadyQueue` takes from the front. `parkOnInput` asserts the slot is
empty. From them it concluded: *everything that could park is either already ahead of the queued
waiter (and so ran while the slot was still occupied, which the assertion forbids) or appended
behind it.*

**Why it does not follow.** The step "ahead of it, so it ran while the slot was occupied"
treats "ahead in the queue" as if it meant "earlier in time", and they are not the same. The
queued waiter A is appended **during** the drain, by the entry at the front, the one that emptied
the slot. So every entry already behind that front entry is *ahead of A in the queue* and also runs
*after the slot was emptied*. `EventLoop::turn` produces such entries routinely: step 4 dispatches
readiness into `_ready`, step 5 fires expired deadlines into the same `_ready`, and the next turn's
step 2 drains them one after another. The case is exactly that: the input flow is followed by a
sibling whose `delay` expired in the same turn. A focus report makes the wake leave the buffer
empty, so the sibling parks and does not short-circuit.

**What would have caught it: trying to build the case.** An unreachability claim is checked by
trying to build the case, not by auditing the argument. The argument read as sound to its author,
and each premise was checked against the source. The mistake was in the step between the premises
and the conclusion, which a re-read tends to accept, because the reader already expects the
conclusion. The re-reviewer did not find the flaw by re-reading. They tried to build the case, and
it took three `runOnce` calls with doubles that were already in the tree. Round 1 said *"I tried
the batch bound and a cross-thread submit"*. Those were attempts to build a case, but only along
the paths the argument had already named, so they could only confirm it. A search that is guided
by the argument checks the argument's conclusion, not the argument itself.

## Item 3: the fourth destructor state, RUNNING. I built the case, and it is real

**Built, not argued.** A probe (not committed) creates an `std::optional<TuiRuntime>` with an
interrupt wakeup, installs `setInterruptHandler([&runtime] { runtime.reset(); })`, simulates
SIGINT, signals the wakeup and turns the loop:

- **With the new assertion removed, under `clang-asan-ubsan`** (build exit 0): `ERROR:
  AddressSanitizer: heap-use-after-free ... READ of size 8`. Frame #0 is
  `TuiRuntime::interruptFlow() [clone .resume]`. The memory was freed by
  `interruptFlow() [clone .destroy]` from `coroutine_handle<Task<void>::PromiseType>::destroy()`,
  that is, from `~Task` in `_sources`. The frame is destroyed while it runs, and the flow's body
  reads it when the handler returns. The handler's own `std::function` (`_onInterrupt`, a member)
  is destroyed while it runs as well.
- **With the assertion, `clang-debug`** (build exit 0): `Assertion '!_inCallout && "~TuiRuntime
  from inside code this runtime called ..."' failed`, SIGABRT, exit 134.

**Why it is a precondition and not a fix.** `cancelPending` correctly answers false, because the
loop is not holding a running frame. Nothing the destructor can do helps either, because what the
resumed body reads, `this` and its own frame, is gone whatever the destructor does. The right
answer is to not reach that state, so it is asserted and documented:

- `CalloutScope` (private) marks the window in which the runtime has handed control to code it does
  not own, and `callOut(...)` runs a call inside one. Every callout goes through it:
  `_onInterrupt()`, `readReady`, `readResize`, `consumeReports`, `takePending` and `flushPartial`.
  The previous agent's version cleared the flag on exit. This version **restores** the previous
  value, so an `InputSource` reached from inside the interrupt handler does not end the outer
  window early.
- `~TuiRuntime` asserts `!_inCallout`. Its enumeration now lists RUNNING as state 4, after state 3,
  instead of as "3a" in the middle. It also says the earlier comment called three states
  exhaustive, which is the same mistake round 1 fixed.
- `setInterruptHandler`'s Doxygen states the precondition and the remedy. This is a public-header
  change, so it has a CHANGELOG entry under Fixed.

**The committed case** is the remedy, because neither form of the defect can be a passing case:
`An interrupt handler that ends its runtime posts the teardown, and it leaves nothing`. The handler
`post()`s `runtime.reset()`. The case first checks that both source flows are parked
(`parkedWaiterCount() == 2`), because an idle loop also reports "nothing left". It then checks
that the handler ran once, that the runtime is gone within a bounded 10 turns, that
`parkedWaiterCount() == 0` and `readyCount() == 0`, and that the SIGINT flag was consumed. It
passes under ASan and TSan. **What it does not distinguish:** it would also pass if the assertion
were deleted, because the assertion guards a different path from the one this case takes. The
evidence for the assertion is the probe above, and I record it here rather than claim it for the
case.

## Item 4: the C1 case covers all four flows

The previous agent's extension is kept. The case configures input, resize, the interrupt wakeup and
a pipe standing in for the signal fd. It first checks that four flows really park
(`parkedWaiterCount() == 4` after one turn), then constructs and destroys the same four with no
turn. **Each guard was reverted on its own**, `while (!_stopping)` to `while (true)` in that one
flow only, `clang-debug`, build exit 0 each time:

| Guard reverted | Result |
|---|---|
| `inputFlow` | `REQUIRE( loop.parkedWaiterCount() == 0 )` → `1 == 0`, then SIGSEGV, exit 139 |
| `resizeFlow` | same, exit 139 |
| `interruptFlow` | same, exit 139 |
| `signalFlow` | same, exit 139 |

So the case checks each of the four guards directly, not by assuming the others resemble the
input flow's.

## Also fixed

- `clang-tidy` flagged `bugprone-empty-catch` on the two H1 helper coroutines, which the previous
  agent wrote. Each catch now ends in an explicit `co_return`.
- `clang-format` reflowed one line of the C1 case.

## Gates, fix round 2 (rebased tree, `a922f9e` + this commit)

| Gate | Build exit | Result |
|---|---|---|
| `clang-format --all --check` (22.1.8) | — | 461 files formatted |
| `clang-tidy` preset (22.1.8, WSL) | 0 | 0 diagnostics |
| `mkdocs build --strict` | — | exit 0 |
| WSL `clang-debug` | 0 | 38/38, 1 skipped (upstream-drift); tui 1043 cases / 4584 assertions |
| WSL `gcc-release` | 0 | 38/38, 8 skipped (7 assertion canaries, upstream-drift) |
| WSL `clang-asan-ubsan` | 0 | 38/38, 1 skipped; tui 1043 cases; 0 sanitizer lines |
| WSL `clang-tsan` | 0 | 38/38, 1 skipped; tui 1043 cases; 0 race reports |
| Windows `cl-debug`, `--clean-first` | 0 | 38/38, 0 skipped, 552/552 steps |
| Windows `clangcl-release`, `--clean-first` | 0 | 38/38, 6 skipped, 552/552 steps |

The same set was also green on the pre-rebase tree (`c8264a0` base). macOS is CI-only.

**Not run:** `/code-review`. The dispatch said not to start subagents. I reviewed the diff
myself instead.
