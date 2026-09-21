# Task B3 review — `IoBackend` replaces `EventSource`

Reviewed: `e7963de`, `0fd01f5`, `9f7eb5d`, `c4d3567`, `149659a`, `1709a3c` (tree at `1709a3c`).
A later fix-round commit, `870d12b` (`test(net): the >64-handle sweep is asked of the backend`),
landed while this review was being written; it is **outside the six** and is not covered here. It
closes the `WfmoBackend` sweep gap listed under "Not findings" and touches none of F1–F8.
Against `.superpowers/sdd/2026-09-18-core-cpp/task-B3-dispatch.md`, the design spec's §2, the
implementer's report, and `.agent/rules/{async-and-net,platform,design-principles,cpp-guidelines,testing}.md`.

## Verdicts

- **Spec compliance: PASS with three undeclared divergences** (all additive or defensible; §1).
- **Task quality: APPROVED WITH CHANGES.** Two Important findings, six Minor.

**Overall: Changes requested** — for F1 and F2 only. Neither is a defect in code that runs today;
both are gaps in what the contract *promises* versus what it *holds*, and both are cheap to close.
Everything the dispatch named as load-bearing is delivered, tested, and holds.

### Verified by running

| Preset | Tree | Result |
|---|---|---|
| `clang-debug` (WSL, clang 21) | `out/build/reviewB3-clang-debug` | configure + build clean, **zero warnings**; `ctest` **25/25 passed**, 1 skipped (`upstream-drift`, by design) |
| `cl-debug` (VS 18, x64) | `out/build/reviewB3-cl-debug` | build clean; `ctest` **26/27**, the one failure `core-cpp.vendor-selftest` is *environmental* (`git add .gitattributes` → "failed to insert into database" in the scratch repo it builds under `%TEMP%`), not a B3 defect. All four `net` binaries pass. |
| ad-hoc experiment | same Linux tree | Added a throwaway parity case asserting which callback a peer hangup reaches; **poll and epoll both route it to `onError`** (`readable=0 failed=1`). Reverted. This is the evidence behind F1. |

Both trees are private (`scratchpad/wt-b3`, a detached worktree at `1709a3c`). Nothing in the
shared checkout was touched; no formatter was run.

### Verified by reading only

- `KqueueBackend` and `WfmoBackend` behaviour (no BSD/macOS here; Windows built and ran, but the
  hangup experiment needs a peer-close on a waitable channel, which `platform::SystemPipe` does not
  offer). F1's kqueue and Wfmo halves are code-reading conclusions, argued below.
- The `>MAXIMUM_WAIT_OBJECTS` sweep (nothing drives 64 handles) — already ruled into a fix round.
- Emscripten.

---

## 1. Spec compliance — the §2 contract, declaration by declaration

`IoBackend.hpp` matches the spec's interface block on every `[[nodiscard]]`, every `noexcept`, every
parameter and every return type. Checked one by one:

| Spec | Delivered | |
|---|---|---|
| `enum class Interest : uint8_t { None=0, Read=0b01, Write=0b10 }` | `IoBackend.hpp:40-45` | ✔ |
| `enum class HandleKind : uint8_t { Fd, Socket, Waitable }` | `:70-75` | ✔ |
| `enum class BackendKind : uint8_t { Poll, Epoll, Kqueue, Iocp, Wfmo, HostDriven, Scripted, Null }` | `:196-209` | ✔ order preserved, **plus `Last`** (see D4) |
| `kind() const noexcept` `[[nodiscard]]` | `:275` | ✔ |
| `attach(ReadinessHandler&) -> expected<void,NetError>` `[[nodiscard]]` | `:298` | ✔ |
| `setInterest(ReadinessHandler&, Interest) -> expected<void,NetError>` `[[nodiscard]]` | `:321` | ✔ |
| `detach(ReadinessHandler&) noexcept` | `:340` | ✔ |
| `wait(optional<SteadyDuration>) -> WaitResult` `[[nodiscard]]` | `:350` | ✔ |
| `wake() noexcept` | `:355` | ✔ |
| `isHostDriven() const noexcept` `[[nodiscard]]`, default `false` | `:360` | ✔ |
| `armWakeAt(optional<SteadyTimePoint>) noexcept`, default `{}` | `:366` | ✔ |
| `IHostScheduler::callAfter(ms, void(*)(void*) noexcept, void*)`, `protected: ~IHostScheduler()` | `IHostScheduler.hpp:46,51` | ✔ exact |
| `HostDrivenBackend final : IoBackend`, `explicit HostDrivenBackend(IHostScheduler&)` | `HostDrivenBackend.hpp:31,39` | ✔ (D5) |
| `EmscriptenHostScheduler final : IHostScheduler` under `__EMSCRIPTEN__` | `emscripten/EmscriptenHostScheduler.hpp` | ✔ (selected by `SOURCES_EMSCRIPTEN`, not an `#ifdef` — R42, better than the spec's sketch) |
| `makeDefaultBackend()`, `makeBackend(BackendKind)` `[[nodiscard]]`, null if unavailable | `:383,391` | ✔ |
| Backends per OS: Linux Epoll+Poll; BSD/macOS Kqueue+Poll; Windows Wfmo (IOCP in B7); Emscripten HostDriven | `{linux,bsd,windows,emscripten,posix}/DefaultBackend.cpp` | ✔, and pinned by `BackendParity_test.cpp:1546-1588` and `HostDrivenBackend_test.cpp:228-249` |

**Divergences.** Five, of which **two are not in the report's §6**:

- **D1 — `ReadinessHandler::slot` absent** (report §6a, upheld by the lead). *The reasoning holds,
  and I would uphold it independently.* Nothing in B3 reads or writes a slot; the `#475` problem it
  would also have solved is solved at `detach` time, while the handler is still alive
  (`ReadyBatch::withdraw`, `ReadyBatch.hpp:148-153`), which the header argues needs no generation
  counter — correctly, because a bare pointer compared after the fact cannot survive address reuse.
  Its absence makes nothing else unimplementable: B7 needs the slot because an IOCP completion is
  *already in the kernel's queue* and cannot be withdrawn, which is a different mechanism from
  anything B3 has, and adding a field to `ReadinessHandler` is purely additive in a 0.x with no
  frozen ABI. **No action.**
- **D2 — `completionPort()` absent. Not in §6.** The spec declares
  `#if defined(_WIN32) [[nodiscard]] virtual ICompletionPort* completionPort() noexcept { return nullptr; }`
  (spec line 178). `git grep ICompletionPort` finds it only in the plan and the spec. Harmless
  today — the type does not exist and B7 adds both together — but it is a contract line the task
  was told to implement exactly, and the report does not say it was dropped. **Record it, don't
  change it**; B7 adds the type and the accessor in one commit.
- **D3 — `ReadinessHandler::kind` defaults to `DefaultHandleKind`, not `HandleKind::Fd`. Not in
  §6.** (`IoBackend.hpp:139`, `:78-82`.) On Windows the spec's `Fd` default would be wrong for every
  backend that platform has, so this is an *improvement* — but it is a silent behavioural change to
  a declared default. Worth a line in the report; no change wanted.
- **D4 — `BackendKind::Last`** (§6 silent, CHANGELOG states it). Additive sentinel; its one purpose
  is that `IoBackend_test.cpp:235-254` covers every kind without restating the list. Good. Nit: the
  doc comment says `Last` is "never compared against" while `toString` has a `case` for it.
- **D5 — `HostDrivenBackend` gains a defaulted `platform::IClock&`.** Not in §6. Additive,
  `explicit …(IHostScheduler&)` still compiles, and it is what lets `armWakeAt`'s clamp be asserted
  exactly rather than approximately. This is `design-principles.md` applied correctly.
- D6/D7 — `DefaultHandleKind` spelling and `ParkId` as a struct: both declared (§6b, §6c) and both
  forced by `.clang-tidy`. Agreed.

---

## 2. Where the dispatch said to look hardest

### 2.1 Rule 1 — structurally impossible, not merely absent. **Holds.**

I checked this the way the lead asked: not "is there a resume in a backend" but "can there be one".

`git grep` over `src/core/net` (excluding tests) finds **exactly one** call site that invokes a
`ReadinessCallback` anywhere in the module: `detail/ReadyBatch.hpp:129-132`. Every backend —
`PollBackend`, `EpollBackend`, `KqueueBackend`, `WfmoBackend` **and** `testing::ScriptedBackend` —
reaches its callbacks only through `_batch.dispatch()`; `NullBackend` and `HostDrivenBackend`
dispatch nothing at all. `dispatch()` raises `ReadinessDispatchGuard` for the whole walk
(`ReadyBatch.hpp:116`), and the loop's single resume site asserts the guard is down
(`EventLoop.cpp:273-282`). There is no second door.

The three layers the report claims in §3 are real, and I verified each:

1. one dispatch site, guarded (above);
2. `EventLoop::drainReadyQueue` asserts the negative — and the report's RED for it (`SIGABRT` with
   the rule in the message) is the right shape of evidence;
3. `BackendParity_test.cpp:1284-1313` asserts the positive **from the resumed frame's own stack**,
   on every backend this platform builds. Run: passes on poll and epoll (Linux) and wfmo (Windows).

Two honest caveats, neither a finding:

- The assertion is `assert()`, so it is gone under `NDEBUG`. That is the right trade (the
  alternative costs a thread-local read on every resume), and `clang-asan-ubsan` + `clang-tsan` +
  `clang-debug` all run with it live.
- `readinessDispatchDepth()` is thread-local (`ReadyBatch.hpp:40-44`), which is correct for
  one-loop-per-thread but means the assertion would *not* catch a resume performed on a different
  thread from inside a dispatch. That is a G3 violation rather than a Rule 1 one — see F2.

### 2.2 The three fastcached bugs

- **#475 — withdrawal from an in-flight batch.** Implemented once, in `ReadyBatch::withdraw`, and
  every backend's `detach` calls it (`PollBackend.cpp:97`, `EpollBackend.cpp:183`,
  `KqueueBackend.cpp:253`, `WfmoBackend.cpp:136`, `ScriptedBackend.hpp:137`). Two cases: the pure
  one (`IoBackend_test.cpp:130-154`) and the on-a-real-kernel one
  (`BackendParity_test.cpp:1315-1373`). Both fail when the arm is removed (report §2, verbatim RED).
  **Covered.** See F4 for a small hole in the parity form.
- **#1054/#1057 — `setInterest` reports the kernel's refusal.** The `expected` is on the interface,
  the split between `attach` and `setInterest` is argued from kqueue's mechanism
  (`IoBackend.hpp:277-298`), kqueue's `EV_RECEIPT` verdict is translated into a `NetError` carrying
  the errno (`KqueueBackend.cpp:150-186`), epoll's `dup` failure likewise
  (`EpollBackend.cpp:122-125`), and `BackendParity_test.cpp:1172-1256` drives it for real by
  squeezing `RLIMIT_NOFILE` — with a `ScopeGuard` restoring the limit on the throwing path too,
  which is the right lesson from a previous cascade. `EventLoop_test` then covers the *loop's* side
  of the refusal (`149659a`). **Covered, and well.**
- **The dead-descriptor probe (substituted, report §6d).** The dispatch's literal case
  (`EpollReactor_test`'s `AbandonParkedWork` fixpoint probe) genuinely is loop-teardown and has no
  counterpart in B3 — there is no `AbandonParkedWork` here to probe. **I accept the substitution.**
  `a dead handle in the wait set does not blind a backend to a live one`
  (`BackendParity_test.cpp:1412-1454`) is the half a readiness backend can answer, it has a real
  origin (Windows failing the whole `WaitForMultipleObjects` on one dead handle), and it is
  portable — it asserts only that the live registration still gets through, which every kernel
  agrees on. What it does **not** cover, and what B4 should know: the four kernels disagree about
  the *dead* one. poll answers `POLLNVAL` and Wfmo answers `WAIT_FAILED` → both dispatch
  `Readiness::Failed` on **every** wait until the handler is detached; epoll drops a closed
  descriptor from its set and kqueue drops its filters → both report nothing, ever. A loop that
  relied on either would be wrong on two platforms. `notifyHandleClosing` is what makes this moot,
  and it is B4's.

### 2.3 `Interest::None` parity — **correct now, and the fix is the right fix.**

`1709a3c` fixes the *case*, not a backend, and that is the correct call. The question the old case
asked ("is the second of two registrations on one descriptor dispatched to?") has no portable
answer: Linux's and FreeBSD's `poll(2)` fill every matching `pollfd`, macOS's reports the descriptor
once. The new form (`BackendParity_test.cpp:1023-1035`) detaches the control first and then asks
about a descriptor carrying **one** registration — where every multiplexer agrees. The comment
explaining why is on the spot.

**I swept the whole parity suite for the same class of mistake.** Every exact-count assertion:

| Line | Assertion | Portable? |
|---|---|---|
| 973-974, 980-981 | one registration, dispatched once, then silent after detach | ✔ one registration per handle |
| 1011-1012 | muted → 0 dispatches | ✔ |
| 1018-1020 | one muted + one watched on the same handle → exactly 1 | ✔ (only the watched one is in any wait set) |
| 1035 | `muted.readable >= 1` after the control is detached | ✔ — this is the fix |
| 1072-1073 | muted + peer hangup → 0 | ✔ (POSIX-only, correctly guarded) |
| 1115 | `first.readable > 0 \|\| second.readable > 0` | ✔ — deliberately disjunctive, with the comment |
| 1125-1126 | after detaching one, the other still fires and the detached one does not | ✔ |
| 1367-1368 | `dispatched == 1` and the sum is 1 | ✔ portable, but see **F4** |
| 1448 | `alive.readable > 0` | ✔ |
| 1186-1249 | poll succeeds under fd pressure, epoll/kqueue must refuse | ✔ keyed to the documented mechanism (poll holds no descriptor of its own), not to an accident |

**No other case asserts what one multiplexer happens to do.** The one place where four backends
genuinely disagree and nothing asserts it is F1 — which is the opposite problem: a missing case,
not a wrong one.

### 2.4 `HostDrivenBackend` on every OS — **fully delivered.**

All four properties the dispatch listed have a case in `HostDrivenBackend_test.cpp`, and the binary
that holds them (`core-cpp-net_backend-test`) is built on **every** platform including Emscripten
(`src/core/net/CMakeLists.txt:172-176` lists it in both `SOURCES` and `SOURCES_EMSCRIPTEN`):

- `attach`/`setInterest` → `NetErrorCode::Unsupported`, and `detach` harmless — `:44-65`;
- `wait()` returns at once for `nullopt`, 24h and zero, **and asks the host for nothing** — `:67-82`;
- two (three) wakes → one pump, and the coalescing is per-pump, not permanent — `:84-110`;
- `armWakeAt` clamped at 0, `nullopt` schedules nothing, a later deadline does not displace an
  earlier pump, an earlier one is scheduled *beside* the later — `:138-189`;
- `isHostDriven()` true here and false for every other backend this platform builds, plus Scripted
  and Null — `:207-226`;
- `makeDefaultBackend()`/`preferredBackendKind()` answer HostDriven under `__EMSCRIPTEN__` and
  never elsewhere — `:228-249`. This is the only test of the Emscripten factory, and it is the
  right one; `BackendParity_test`'s factory cases are native-only and would (correctly) fail there,
  which is why they are not compiled there.

The clearing-before-pump ordering has its own case (`:112-136`) and its own RED. The coalescing is
implemented against a *deadline*, not a flag (`HostDrivenBackend.cpp:48`), which is what keeps a
wake wanted now from being swallowed by a pump armed for later — a subtle thing to get right and it
is right. `ManualHostScheduler::pump()` takes the pending list out before firing any of it
(`ManualHostScheduler.hpp:60`), which is what stops a re-arming loop from never returning.

This is the strongest part of the task.

### 2.5 The deletions — **clean, with three stale comments.**

`git grep` over the whole tree for `EventSource`, `FdInterest`, `FdToken`, `WaitOutcome`,
`makeDefaultEventSource` (and every concrete `*EventSource` name):

- `FdInterest`, `FdToken`, `WaitOutcome`, `makeDefaultEventSource`, `EventSourceBackends`,
  `ScriptedEventSource`, `EpollEventSource`, `KqueueEventSource`, `DefaultEventSource`: **zero**
  hits outside `CHANGELOG.md`, `provenance.md`, the migration guide, `renames.json`, the plan and
  the spec.
- `tests/` — including `tests/consumer-shared/ConsumerSmoke.hpp` and all three consumer legs:
  **zero** hits. The `e7963de` miss is genuinely repaired.
- `src/core/tui/runtime/` and its source-list entries in `src/core/tui/CMakeLists.txt`: present and
  load-bearing, as expected until B12. `src/core/tui/TerminalQuery_test.cpp:8,369` references the
  runtime's surviving `TerminalEventSource` — a real, existing header, compiled by the default
  build. Correct.
- **Three stale doc comments inside `src/core/net/`** naming types that no longer exist — F6.

---

## 3. Findings

### Important

**F1 — `Readiness::Failed` is a promise two of four backends do not keep, and nothing asserts it.**

`src/core/net/IoBackend.hpp:95-99` documents `Readiness::Failed` as "`EPOLLERR`/`EPOLLHUP`,
`POLLERR`/`POLLHUP`/`POLLNVAL`, or a Windows wait that failed", `:145-155` documents `onError` as
"called **instead of** the two above when the kernel reports a failure", and
`.agent/rules/async-and-net.md` restates it as a rule ("`EPOLLHUP` … They go to
`ReadinessHandler::onError`"). What the backends actually do:

| Backend | A peer hangup on a registration with `onError` set | Source |
|---|---|---|
| poll | `POLLIN\|POLLHUP` → `Readable\|Failed` → **`onError`** | `posix/PollBackend.cpp:45` |
| epoll | `EPOLLIN\|EPOLLHUP` → `Readable\|Failed` → **`onError`** | `linux/EpollBackend.cpp:51` |
| kqueue | `EVFILT_READ` with `EV_EOF` → `Readable` only → **`onReadable`** | `bsd/KqueueBackend.cpp:281-289` |
| wfmo | event signalled, handle valid → the watched direction → **`onReadable`** | `windows/WfmoBackend.cpp:77-89` |

`KqueueBackend::wait` sets `Readiness::Failed` only from `event.flags & EV_ERROR`
(`KqueueBackend.cpp:286`) — but `wait()` submits a null changelist, and `EV_ERROR` in an *eventlist*
is how kqueue reports a failed **change**. So that branch is effectively unreachable: kqueue never
produces `Readiness::Failed`, and `EV_EOF` is ignored entirely. `WfmoBackend::probeHandle` returns
`Failed` only for `WAIT_FAILED`, i.e. an *invalid handle* — never for `FD_CLOSE` on a live one.

**What convinced me.** I ran it. Adding a throwaway parity case that provokes a `socketpair` peer
close on a registration whose `Probe` sets `onError` (the parity suite's `Probe` already does,
`BackendParity_test.cpp:873`) gives `readable=0 writable=0 failed=1` on **both** poll and epoll on
Linux. The kqueue and Wfmo rows are code-reading, argued above; I could not run them.

**Why no test catches it.** `grep '\.failed'` over `BackendParity_test.cpp` returns **nothing** —
no parity case ever asserts which callback a failure reaches. `IoBackend_test.cpp:55-85` tests
`selectReadinessCallback` purely, which is correct and which is not the same question.
`EventLoop::registerFdWaiter` deliberately sets `onError = nullptr` (`EventLoop.cpp:112-120`), so
production code today cannot see the divergence — which is exactly why it will land on whoever
first sets it.

**Failure scenario.** B6 merges fastcached's `PosixSocket`/`ConnectFlow`. A failed outbound connect
arrives with the error bits and, classically, *neither* direction set — the case `onError` exists
for. On Linux it reaches `onError` and the dial completes with the errno. On macOS the write filter
fires with `EV_EOF`, the backend reports `Writable`, and `onWritable` runs: the flow believes the
connect succeeded and discovers otherwise on the first write. macOS-only, and invisible to the
parity suite as it stands. On Windows the same shape applies to `FD_CONNECT`.

**Remedy** (either is acceptable, the first is what "every backend behaves identically" means):

1. Add a parity case pinning which callback a hangup reaches, and make kqueue map `EV_EOF` (and
   `fflags`, which carries the socket error) to `Readiness::Failed`, and Wfmo map `FD_CLOSE`
   (via `WSAEnumNetworkEvents`, which `windows/NetworkEvents.hpp` already wraps) to the same; or
2. State in `IoBackend.hpp` and in `.agent/rules/async-and-net.md` that `Readiness::Failed` is
   **best-effort and not portable**, that `onError` is an optimisation rather than a guarantee, and
   that every handler must therefore behave correctly when a hangup arrives on its watched
   direction — which is what `EventLoop`'s own park already assumes.

Option 2 is one paragraph and makes the layer honest; option 1 is a fix round. **Pick one before
B6 starts**, because B6 is where the assumption gets made.

---

**F2 — Rule 3's G1 and G3 are not asserted anywhere, and the report does not say so.**

The dispatch: *"Rule 3's thread-affinity guarantees are asserted **on every backend**: G1 (exactly
one thread dequeues), G2 (every resumption happens in turn step 2 of `runOnce`), G3 (helper threads
only post). G4 and G5 arrive with B6/B7."*

- **G2** is covered, and well — it is the Rule 1 machinery (§2.1).
- **G1 and G3 have no assertion and no test.** `grep` over `src/core/net` (tests excluded) for
  `thread::id`, `get_id()`, `isOnWorkerThread`, `teardownIsSerialised` or any owner-thread record
  returns **nothing**. The spec's `EventLoop` block declares `isOnWorkerThread()` and
  `teardownIsSerialisedWithDispatch()`; neither exists. `IoBackend.hpp:257-260` *states* the rule in
  prose ("every member but `wake()` must be called on the thread that calls `wait()`") and nothing
  enforces it.

That may well be the right call — an owner-thread id belongs beside `runOnce` and the inbound
queue, which are B4's — but **the report's §4 "what I left for B4" does not list it**, and §3
answers only Rule 1. §4 is the document B4 is written against; a guarantee the dispatch asked for
that appears in neither the code nor the hand-off list is how it gets lost.

**Failure scenario.** B6's `PosixSocket` calls `backend.setInterest()` from a worker thread by
mistake. Nothing fires. `PollBackend::wait`'s `static thread_local fds` (`PollBackend.cpp:107`)
makes it *worse* than a crash: the second thread builds its own `pollfd` vector from a
`_registrations` vector another thread is mutating, so the symptom is a torn read under TSan at
best and a silent wrong wait set at worst. Similarly, the Rule 1 guard is thread-local
(`ReadyBatch.hpp:42`), so a resume dispatched onto another thread from inside a dispatch would not
trip the assertion it was built for.

**Remedy.** No code change needed in B3. Either (a) add the one line to the report's §4 —
"**B4 owes G1 and G3**: an owner-thread id on `EventLoop`, `isOnWorkerThread()`, and an assertion
in every member of `IoBackend` but `wake()`" — and carry it into
`.agent/rules/async-and-net.md` so it is a rule rather than a memory; or (b) add the owner-thread
id to the backends now (one `std::thread::id` member, set on first `wait()`, asserted in
`attach`/`setInterest`/`detach`/`wait`) which is ~8 lines per backend and would have been cheap
here. **(a) is enough**, but it must be written down.

### Minor

**F3 — `EpollBackend` and `KqueueBackend` leak their kernel descriptor if `WakeupChannel` throws.**
`linux/EpollBackend.hpp:99-100` declares `int _epollFd` **before** `detail::WakeupChannel _wakeup`
(same in `bsd/KqueueBackend.hpp:119-120`). The constructor's mem-init list runs `::epoll_create1()`
first, then `WakeupChannel()` — which `throws std::runtime_error` under descriptor exhaustion
(`detail/WakeupChannel.hpp:44`). A constructor that throws from its mem-init list does not run the
class destructor, and `int` has none, so the epoll/kqueue descriptor leaks. That is precisely the
pressure under which a descriptor matters, and it makes `makeDefaultBackend()`'s documented fallback
to `PollBackend` (`IoBackend.hpp:373-382`) strictly less likely to succeed. `PollBackend` and
`WfmoBackend` are unaffected (no second resource). **Fix: swap the two member declarations** — then
`_wakeup` is constructed first and `epoll_create1` is never reached. One line each. This is
`cpp-guidelines.md`'s "RAII for every handle" in the one place it is unmet.

**F4 — the `#475` parity case can stop testing anything without failing.**
`BackendParity_test.cpp:1315-1373` writes a byte into two pipes, waits once, and asserts
`dispatched == 1` and `peers[0].dispatched + peers[1].dispatched == 1`. If a kernel reported only
*one* of the two in that batch, both assertions still pass and the withdrawal is never exercised —
the case degrades to a tautology silently. The comment ("Both readable BEFORE the wait, so ONE wait
dequeues both in a single batch") states the premise but nothing checks it, and this task has
already been bitten once by assuming how many entries one wait fills. Risk is low (these are two
*different* descriptors, which every multiplexer reports separately), but the control is three
lines: run the same two pipes **without** the withdrawal first and `REQUIRE(dispatched == 2)`. That
is what turns the premise into an assertion. `.agent/rules/testing.md`: "assert what distinguishes".

**F5 — `ScriptedBackend` is not faithful about `Interest`, and a B4 case will believe it.**
`testing/ScriptedBackend.hpp:143-160` dispatches whatever the script says regardless of the
registration's interest; it records interests (`_interests`, `interestOf()`) and never consults
them. Every real backend treats `Interest::None` as silent — which is a documented, load-bearing,
parity-tested rule — so a case that mutes a registration and scripts it readable gets a dispatch
here and silence everywhere else. Two lines in `wait()` (skip a step whose registration is muted)
close it. Related and smaller: `attach` (`:88-100`) does not refuse an already-attached handler,
where all four real backends return `BadHandle`; `_byHandler.emplace` then silently keeps the first
id while `_live` gains a second.

**F6 — three stale comments in compiled `src/core/net/` files name deleted types.** The sweep the
`ConsumerSmoke.hpp` miss earned did not reach these:
- `src/core/net/Diagnostics.hpp:33` — "reached through the `@c EventSource` interface" → `IoBackend`
  (a public header; Doxygen will render a dead `@c`);
- `src/core/net/detail/WaitChunking.hpp:14` — "`@c PollEventSource`'s Windows path" → `WfmoBackend`;
- `src/core/net/BackendParity_test.cpp:502` — "Socket_test covers this only for `PollEventSource`" →
  `PollBackend`.

Also, outside `src/`: `AGENT.md:27` still reads "`net` is contour's `EventSource` design (Task A6)",
which B3 made untrue. **That file is live in another lane's working tree — do not touch it here.**
And `tools/migrate/check_renames_test.py:149` keeps a fixture whose class docstring says "Phase B
has not landed `IoBackend` yet"; the fixture builds its own synthetic table so it still tests what
it means to, but the prose is now false.

**F7 — `makeDefaultBackend()` does not route through `makeBackend(preferredBackendKind())` on POSIX
and Windows.** `linux/` and `bsd/DefaultBackend.cpp` do, and get the `good()` check and the fallback
with it; `posix/DefaultBackend.cpp` and `windows/DefaultBackend.cpp` construct their one backend
directly. Harmless today (neither has a `good()`), but it means the two shapes drift, and B7 adds a
second Windows backend to exactly that file. One line each.

**F8 — `FdRegistrationFailed` is an exception for a recoverable condition.**
`EventLoop.hpp:55`, thrown from `await_resume` (`:554`). `design-principles.md` says fallible is
`std::expected` and exceptions are for unrecoverable conditions plus `OperationCancelled`; a kernel
refusing a registration is recoverable by the caller. The awaiter's `await_resume()` is `void` in
the spec, so there is nowhere for an `expected` to go without changing the loop's contract — which
is B4's. Raising it so **B4 settles it deliberately** rather than inheriting it, and so the
exception vocabulary of the layer stays two entries rather than three by accident.

### Not findings (confirming the lead's list, plus what I checked and cleared)

- `WfmoBackend`'s >64-handle sweep has no case — already ruled into a fix round.
- `KqueueBackend::Registration::owned` is order-dependent — I walked the eight interleavings of
  attach/arm/mute/re-arm/detach for two registrations on one descriptor, on both epoll and kqueue.
  The `owned`/`armed`/`watched` triple is *correct* in all of them; the report's §8.3 characterisation
  ("taking the dup when it is needed is the right time") is accurate. Recorded, not changed.
- A host-driven loop refusing `run()`/`blockOn()` — B4's, and §4 says so.
- `makeBackend(HostDriven)` is non-null under Emscripten while `BackendParity_test.cpp:1578` asserts
  it null. Not a contradiction: that binary is not built under Emscripten, and
  `HostDrivenBackend_test.cpp:237-248` asks the question correctly per platform.
- `ReadyBatch::dispatch()` copies each `Entry` before use (`ReadyBatch.hpp:126`) and takes its bound
  once — I looked for a use-after-free through a callback that detaches itself, detaches an already
  dispatched peer, or re-enters; none is reachable.
- Windows wakeup: `SystemPipe::read` calls `WSAResetEvent` before `recv` (`platform/SystemPipe.cpp:220`),
  so `WfmoBackend`'s re-probe after `WaitForMultipleObjects` cannot lose a wake on the
  manual-reset `WSAEVENT`. Checked because the auto-reset case would have lost it.
- Gates: `clang-format`, `clang-tidy`, `ctest -L hygiene` (12 tests, green in my Linux tree),
  `mkdocs --strict` and `check-renames.py` all reported green by the implementer; `hygiene`
  I re-ran and confirm.

---

## 4. Quality: the things worth saying out loud

**Tests that can fail.** The RED evidence in §2 of the report is arm-removal rather than
test-first, and the report says so in its own words — that is the honest framing and I would not
ask for more. Every rule in the interface does have a case that dies without it, and the two REDs
that matter most (the `SIGABRT` naming Rule 1, and the muted-registration epoll case) are the right
kind of evidence. F4 is the one case whose failure mode I would tighten.

**API hygiene.** `IoBackend` deletes copy and move (the spec did not ask); every fallible member
returns `std::expected`; `bool` appears in no signature; the only exceptions are the two documented
unrecoverable ones; every public declaration carries Doxygen that says *why*, not *what*. The
`ReadinessHandler`-address-as-identity decision is argued in the header where a reader meets it, and
the `offsetof`-is-UB justification for the `owner` back-pointer is correct.

**Documentation honesty.** `docs/modules/net.md`'s Status note says exactly what has landed and what
has not, including the Emscripten subset's real boundary. The CHANGELOG's `Breaking` section is a
nine-row migration table plus two named behavioural differences a caller can see — a consumer could
work from it without reading the diff. `provenance.md` has a row per imported file with the upstream
path, SHA and what changed. `.agent/rules/async-and-net.md` gained seven rules, each citing
fastcached#475/#1054/#1057 by full URL. This is the best-documented task in the phase so far, and
the report's §6 and §7 (what was got wrong, including the `renames.json` read-modify-write hazard)
are the reason I trust the rest of it.

**Scope discipline.** `EventLoop` is +366/−233 against 2358 lines of backend, most of it comment.
`pumpOnce` keeps the five-step shape `runOnce` will have, in order, so B4 renames rather than
restructures. That is the right answer to the dispatch's "stop and report it if the adaptation is
larger than the backends".

---

## 5. The one thing most likely to bite B4, B6 or B7

**F1 — `Readiness::Failed`, and specifically that `onError` is guaranteed on poll and epoll and
never fires on kqueue or Wfmo.**

B6 is where it lands. fastcached's `ConnectFlow` and `PosixSocket` are the first code that will set
`onError` — it is the natural place to complete a failed connect and to tear down on `POLLERR` —
and the header, the rules file and the CHANGELOG all currently tell that author the field is
reliable. It is not, on half the platforms, and:

- the parity suite cannot see it (no case asserts which callback ran for a failure);
- `EventLoop` cannot see it (it sets `onError = nullptr` on purpose);
- CI will see it only as a macOS-only socket test that reads a failed connect as a successful one,
  which is the hardest possible shape to diagnose from a red job.

Everything else in this task is structured so that a wrong answer is caught somewhere. This one is
structured so that it is not. Resolve it — by fixing the two backends or by downgrading the promise
in writing — **before B6 is dispatched**, not after.

Runner-up, for B7 specifically: the spec's `completionPort()` (D2) and `ReadinessHandler::slot` (D1)
are both absent, and B7 needs both. Neither is blocked, but B7's dispatch should say so explicitly
rather than assume the contract already carries them.

---

## Verdict

**Changes requested**, scoped to F1 and F2 (both are decisions plus a paragraph, not rewrites), with
F3 worth taking in the same round because it is two lines.

Everything the dispatch named as the point of the task — one dispatch site, a guard the loop
asserts against, the withdrawal at detach, the kernel's refusal reported, mute meaning silent
everywhere, a host-driven backend that is portable and tested natively, and a parity matrix that
enrols every backend this platform builds — is delivered, and delivered better than the spec asked.

*Review trees (private, disposable): `…/scratchpad/wt-b3` with `out/build/reviewB3-clang-debug` and
`out/build/reviewB3-cl-debug`. Remove with `git worktree remove --force <path>` from `D:\core-cpp`
when the fix round is done.*
