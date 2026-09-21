# Task B7a — IOCP is a Windows backend, and it can park on a waitable handle

Commit: **`320a9ab`** — `7bdd132..320a9ab`, one commit, 18 files, +3028/-32. Built and verified in
`D:\core-cpp-wt-b7a` (left in place for review); landed on `master` in the shared checkout.

Status: **complete**, on the scoping ruling as written. `IocpBackend` is built, reachable by name,
in the parity matrix, asserted for G1 and G4 with a canary each, and **not** the Windows default —
`preferredBackendKind()` still answers `Wfmo`, which is B7b's line to change.

## The two facts the dispatch asked me to verify myself

**1. `IoBackend.hpp` at `HEAD` has no `completionPort` member — confirmed.** `git show
HEAD:src/core/net/IoBackend.hpp` has exactly one `#ifdef _WIN32`, around `DefaultHandleKind`'s
value, and no member behind it. `ReadinessHandler` has five fields and no `slot`. The dispatch's
own correction was right and its earlier draft was wrong. I added both.

**2. Ruling R101's `selectReadinessCallback` ordering has landed — confirmed, in the function
itself rather than from the commit.** `7cd86fc` ("fix(net): a watched direction beats onError, and
a hangup keeps its bytes") exists and `git merge-base --is-ancestor 7cd86fc HEAD` succeeds, but
what I actually checked is the body: `selectReadinessCallback` tries `onReadable`, then
`onWritable`, and only then — for a failure with no watched direction — `onError`, with a final
fallback to whichever callback exists so a level-triggered failure still wakes somebody. That is
R101's order. I followed it in the one place this backend synthesises a failure: a bridge that
cannot be armed inside `wait()` is reported as `Failed | <the watched directions>`, so the
direction wins and the caller learns the rest from its own `recv`/`send`.

## What B3, B4 and B5 actually left me (enumerated, not taken on trust)

`git ls-tree -r HEAD src/core/net/` at `fe48143`. What I built on:

- **B3**: `IoBackend.hpp` (the interface, `Readiness`/`Interest`/`HandleKind`,
  `selectReadinessCallback`, `BackendKind` with `Iocp` already named and `makeBackend` documented as
  answering null for it), `detail/ReadyBatch.hpp` (which I use unchanged — collect, then one
  `dispatch()`, with `withdraw` for `detach`), `detail/WaitTimeout.hpp`, `detail/WakeupChannel.hpp`
  (which I do **not** use; see below), `testing/BackendMatrix.hpp` (which already lists
  `BackendKind::Iocp`, so making `makeBackend` answer non-null enrolled IOCP in all 29 parity cases
  with no edit to any of them).
- **B4**: `EventLoop`, `detail/WorkerIdentity.hpp` (which I reuse for the port's own G1 claim), the
  park table.
- **B5**: the timers. Nothing of mine touches them.
- **Windows, from A6/A12**: `windows/WfmoBackend.{hpp,cpp}`, `windows/NetworkEvents.hpp`
  (`consumeNetworkEvents`, which my write bridge uses rather than reaching for `WSAResetEvent`),
  `windows/WindowsLoopback.hpp` (`makeLoopbackPair`), `windows/DefaultBackend.cpp`.

Nothing the dispatch named was missing this time.

## What I built

| File | What |
|---|---|
| `src/core/net/windows/IocpBackend.{hpp,cpp}` | the backend, the three bridges, the slot, the port |
| `src/core/net/windows/WaitCompletionPacket.{hpp,cpp}` | the `NtAssociateWaitCompletionPacket` probe and its RAII packet |
| `src/core/net/windows/IocpBackend_test.cpp` | 13 cases, the ones only a completion-based backend has |
| `src/core/net/windows/IocpCanary.cpp` | `g1` and `g4`, `WILL_FAIL`, one process each |
| `src/core/net/ICompletionPort.hpp` | the seam, and the one place a handle is associated |
| `src/core/net/detail/ReadinessSlot.hpp` | `ReadinessSlot` + `ReadinessSlotRef` |
| `src/core/net/IoBackend.hpp` | `+ ReadinessHandler::slot`, `+ IoBackend::completionPort()` |
| `src/core/net/BackendParity_test.cpp` | the expected-backends row, the null-kind row, and a console-input case |

Two deliberate deviations from the design spec's sketch, both because a rulebook rule post-dates it:

- **`completionPort()` is declared on every platform**, not behind `#if defined(_WIN32)`.
  `.agent/rules/platform.md` says a member only one platform uses is declared on all of them,
  because a public header that changes shape per platform is one a consumer's build can disagree
  with this one about — and this member sits in a vtable.
- **`IocpBackend` *is* its own `ICompletionPort`** rather than owning a nested `Port` object. That
  started as a separate class and had to be folded: see "What clang-tidy cost" below.

## `slot` — what it means, and who enforces it

The rule, written on the field in `IoBackend.hpp` and again on the type in `detail/ReadinessSlot.hpp`:

> The slot belongs to the **backend**. The handler holds one refcounted share of it, taken in
> `attach` and given back in `detach`, and it never points into the handler.

A completion-based backend hands the kernel a pointer and gets it back on a later turn — after a
cancel, after a detach, after the handler's owner has been freed. Three things are checked when a
packet is dequeued, and they answer three different questions:

- the **reference count** answers *may this storage be freed* (atomic: an operation's share can be
  released off the loop's thread when a post fails);
- **`ReadinessSlot::retired()`** answers *is the handler still there* — a retired slot's `handler`
  pointer is never read;
- **the arm's own `retired` flag** answers *does this packet belong to a watch that still exists*.

**Enforced by**: the backend, entirely. No owner reads or writes `slot`, and a readiness backend
leaves it empty. The one place `slot.handler` is dereferenced is guarded by both retirement checks,
and the guard has been measured (below).

**One honest correction to the dispatch's wording.** It says "refcount it, generation-check it". I
did both, but the generation does less than that phrasing implies and I would rather say so than
let a reviewer discover it: because each **arm** gets a fresh `Operation` node that is stood down
rather than reused, a stale packet and a live one are never the same pointer, so the node's
`retired` flag is what actually decides. The generation is recorded per arm, asserted at dequeue
(`node->generation <= slot.generation()`), and is what names *which* arm a late completion came
from in a diagnostic — which a recycled pointer cannot tell you. It is not load-bearing for
correctness, and pretending otherwise would be the kind of claim this rulebook exists to prevent.

## fastcached's three scars

| Scar | What I did |
|---|---|
| **[#465](https://github.com/LASTRADA-Software/fastcached/issues/465)** — the operation holds the `Impl`, not the socket | **Kept, generalised one layer down.** This IS the slot: the completion names a refcounted object the backend owns, never the thing that might be freed. Cited in `IocpBackend.hpp`, in `ReadinessSlot.hpp`, in the rulebook and in the provenance row. |
| **[#884](https://github.com/LASTRADA-Software/fastcached/issues/884)** / **[#710](https://github.com/LASTRADA-Software/fastcached/issues/710)** — retire semantics; a retracted `OVERLAPPED` is stood down, not reused | **Kept verbatim in shape.** One `Operation` node per arm; a cancel marks it retired and it lives until its packet is reconciled; the next arm allocates a fresh one. `CancelIoEx` does not take an operation back, it asks for it back, and the code says so where it calls it. |
| **`IocpSocket.cpp:327-334`** — the `MSG_PEEK` that tells a zero-byte completion from EOF | **Deliberately not reimplemented, and I think this is right.** At the readiness layer, data, EOF and error are all `Readiness::Readable` — that is what the enum's own doc says ("Data, EOF, or an accepted connection is waiting"), and the reader is woken so its own `recv` can say which. Peeking here would consume a distinction nobody at this layer asked for and would answer a question the `Readiness` vocabulary does not ask. It belongs to `IocpSocket`, which is B7b. The reasoning is written at the call site rather than left implicit. |

**A fourth upstream file the dispatch does not name, read after the fact on the lead's prompt:
`Net/IocpStatus.hpp`.** It is *not* the `MSG_PEEK`/EOF discrimination — those are two different
questions in the same code path, and conflating them would send B7b to the wrong place.
`Detail::WsaErrorOf(socket, completion, status)` converts an `OVERLAPPED::Internal` NTSTATUS into
the `WSAE*`/`ERROR_*` numbering the error taxonomy is written in, via `WSAGetOverlappedResult`;
`MSG_PEEK` separately tells a zero-byte completion carrying data from one carrying EOF. Three facts
in it that B7b needs and that no other file states:

- **a closed socket's pending operation is reported `ERROR_OPERATION_ABORTED` by judgement, not by
  lookup** — `closesocket` is what makes the operation complete at all, and it also leaves
  `WSAGetOverlappedResult` no socket to ask;
- **a `WSARecv` aborted by `closesocket` reports `STATUS_LOCAL_DISCONNECT` (0xC0000241), not
  `STATUS_CANCELLED` (0xC0000120)** — `CancelIoEx` produces the latter, and neither has a `WSAE*`
  row, so the two arrive by different routes;
- **not every non-zero NTSTATUS is a failure** — the warning-severity codes are successes carrying
  a note, and treating one as an abort throws away bytes that were actually transferred.

**It changes nothing in this half, and I checked rather than assumed that.** This backend never
reads a completion's status: a zero-byte `WSARecv` reports `Readiness::Readable` whatever it
completed with, because R101 says a watched direction wins and the reader learns the rest from its
own `recv`. The conversion needs the SOCKET and belongs where the operation was issued, which is
B7b. What it *does* confirm is the `forget()` obligation from the other side — upstream's `Close`
closes the socket and forgets the handle, in that order, for the same reason.

`NtAssociateWaitCompletionPacket` is behind a `GetProcAddress` probe on the already-mapped `ntdll`
(`GetModuleHandleW`, never `LoadLibrary`, never a link against `ntdll.lib` — core-cpp is a library
inside other people's builds and may not add an undocumented import to their executables). All
three entry points or none. On this machine the probe answers **yes**, so the `WaitBridge::Auto`
sections genuinely exercise the kernel path and the `WaitBridge::Threadpool` sections genuinely
exercise the fallback — which is why the bridge is an injected constructor parameter rather than a
probe at the point of use: without it, the path the design calls *the* path would be compiled by CI
and run by nobody.

## RED / GREEN, per test

I wrote the backend and its cases together — a backend that does not exist has no red to capture —
and then proved each case can fail by neutering the mechanism it pins, which is what
`.agent/rules/testing.md` actually asks for. **In every round below I committed to the number and
the identity of the expected failures before building.** Two of the four predictions were wrong,
and both wrong answers were worth more than the right ones.

| # | Neutered | Predicted | Observed |
|---|---|---|---|
| 1 | `node->retired` dropped from `consumeCompletion` | 2 failures, both in *a muted registration is silent* | **0 failures.** Prediction wrong. |
| 1b | same, after adding *a muted socket registration drops the completion already in flight* | 1 failure, `probe.total() == 0` | **1 failure**, `1 == 0`, that case only |
| 2 | zero-byte `WSARecv` → one-byte receive | 1 failure, the byte count | **1 failure**, `3 == 4` |
| 3 | `rearmAll()` removed from `wait()` | "1–4 failures in the parity socket cases" | **0 failures, out of 166.** Prediction wrong. |
| 3b | same, after adding *a registration still ready is reported on the next wait too* | 2 failures (one per bridge) | **2 failures**, that case only |
| 4 | `windows/DefaultBackend.cpp` reverted to `HEAD` | 2 failures in `BackendParity_test` | **2 failures**, and 215 assertions vanished with the IOCP sections |
| 5 | both retirement checks dropped, under ASan | UAF in *a completion that arrives after its handler is gone* | **0 reports.** Prediction wrong for the waitable form. |
| 5b | same, after adding the **socket** form | `heap-use-after-free` | **exactly that** (verbatim below) |
| 6 | `drainOutstanding()` removed from `~IocpBackend` | ref count 2 instead of 1 | **1 failure**, `2 == 1` |

**The two wrong predictions are the findings.**

- **Round 1 and 5**: a *waitable-handle* mute-or-detach cannot be made to have a packet in flight on
  demand. The thread-pool callback may not have started, and on the wait-completion-packet path
  `NtCancelWaitCompletionPacket(h, TRUE)` **removes the queued packet**, so by design there is never
  one. So the two cases I first wrote for the slot's retirement pinned the *behaviour* and could not
  reach the *mechanism*. The deterministic form is a **socket**: `CancelIoEx` removes nothing, so a
  completion — the real one or its abort — is always still coming. Both waitable cases now say in
  their own comments what they can and cannot establish, and each has a socket sibling that
  discriminates.
- **Round 3**: nothing in the whole 166-case suite needed a registration to be reported **twice**.
  Every other registration is armed by `setInterest` and dispatched once, so the level-triggered
  re-arm — which on a completion port has to be *built*, because an overlapped operation is one-shot
  — was implemented, necessary, and exercised by nothing. That is now
  *an IocpBackend registration still ready is reported on the next wait too*, which also pins the
  other half (it stops when the handle does), and it is red with the re-arm gone.

The round-5b RED, verbatim:

```
==64600==ERROR: AddressSanitizer: heap-use-after-free on address 0x125c61ba4ef8 ...
READ of size 8 at 0x125c61ba4ef8 thread T0
    #0 core::net::selectReadinessCallback(ReadinessHandler const&, Readiness) IoBackend.hpp:242
    #1 core::net::detail::ReadyBatch::dispatch(void)                          ReadyBatch.hpp:129
    #2 core::net::IocpBackend::wait(...)                                      IocpBackend.cpp:832
...
freed by thread T0 here:
    #1 `anonymous namespace'::Probe::`scalar deleting destructor'
```

That is precisely the failure the slot exists to prevent, reading the freed `ReadinessHandler`
through the batch. With both checks restored: clean.

## How I proved the G1 canary can die

`core-cpp.iocp-canary.g1` starts a `std::jthread` in `wait(std::nullopt)` — nothing is registered
and nothing posts, so only the `wake()` at the end could ever release it — then **spins on
`backend.dequeuerRunning()`** rather than sleeping, and calls `wait(0ms)` from the main thread.
`dequeuerRunning()` exists for exactly this: a sleep long enough to be reliable on a cold two-core
runner is one nobody wants in a program that runs on every build, and a short one is a canary that
passes for the wrong reason.

Run directly, it prints

```
Assertion failed: (!_dequeuer.running() || _dequeuer.isOnWorkerThread()) && "a second thread
entered IocpBackend::wait() while another is dequeuing this port (G1: exactly one thread dequeues
a loop or a completion port)", file ...\IocpBackend.cpp, line 800
```

and **exits 1** (the `SIGABRT` handler converts the abort, because ctest reads a signal as an
exception and `WILL_FAIL` inverts only a return code). `WILL_FAIL` therefore scores it as passed.
If the assertion did not fire, the program wakes the worker, prints why, and returns 0 — which
`WILL_FAIL` scores as the failure it is, rather than hanging in `~jthread`.

**The G4 canary caught its own first version.** I first wrote it with a `CreateEventW` handle,
reasoning that the backend's own record is what is being provoked. It exited **77**: an event is not
an overlapped-I/O object, so `CreateIoCompletionPort` refuses the *first* association and the canary
skipped — a canary reporting that it could not ask the question as though the answer had been fine.
It now uses a loopback socket and exits 1 with the G4 assertion. Both canaries skip (77) under
`NDEBUG`.

## Tests, and what covered what

| Configuration | Result |
|---|---|
| `cl-debug` (Windows, MSVC) | **35/35 ctest**, including both IOCP canaries; net binary 168 cases / 2070 assertions, 5 skipped |
| `clangcl-release` (Windows, clang-cl) | **35/35 ctest**, after `--clean-first`: **524 build steps** |
| `clangcl-debug` (Windows, clang-cl) | **35/35 ctest**, after `--clean-first`: **506 build steps** |
| MSVC + AddressSanitizer (see the caveat below) | net binary 168 cases / 2070 assertions, **no sanitizer report** |
| `clang-debug` (WSL) | **31/31 ctest** |
| `gcc-release` (WSL) | **31/31 ctest** |
| `ctest -L hygiene` | **16/16**, including the provenance rule over the seven new files |
| `python scripts/clang-format.py --all --check` | 428 files formatted, pinned 22.1.8 |
| `mkdocs build --strict` | clean |
| `clang-tidy` preset (WSL), tree deleted first | **388 steps, 0 findings**, and the instrument proved (below) |
| clang-tidy 22.1.8 over the four new Windows sources | clean (see below) |

The IOCP-specific cases are 13 (113 assertions); the parity suite runs its whole matrix against
IOCP on Windows, which is where the other 215 assertions the round-4 neuter removed come from.

### The clang-tidy gate, with the evidence a clean result needs

A null result and a non-result print the same string, so three things are recorded rather than the
verdict alone.

**Which analyser ran**, agreeing three ways: `/home/christianparpart/.local/bin/clang-tidy`,
`LLVM version 22.1.8`, and `.clang-tidy-version`'s pin `22.1.8`. Configure prints
`[core-cpp] clang-tidy 22.1.8 (/home/christianparpart/.local/bin/clang-tidy)`, and `build.ninja`
carries **201 `CODE_CHECK` statements** with `--tidy=` on them.

> **A trap for whoever greps this next:** the same configure also prints `-- [clang-tidy] Disabled.`
> and `-- Enable clang-tidy: OFF ()`. Both are **libunicode's**, from its own summary block, and
> neither says anything about core-cpp. A check that grepped the configure log for "clang-tidy" and
> "Disabled" would conclude the gate was off while it was running over 201 statements.

**That the surface was analysed rather than inherited**: the tree was **deleted** before configuring
(core-cpp#36 — `.clang-tidy` is not an input of any object, so an up-to-date object makes Ninja skip
the whole statement and the `CODE_CHECK` inside it), and the build ran **388 steps** for 0 findings.

**That findings are fatal and reach MY code**: a `readability-identifier-naming` violation fed into
`src/core/net/detail/ReadinessSlot.hpp` — a file this commit adds, portable, included by every net
translation unit — reported as

```
.../src/core/net/detail/ReadinessSlot.hpp:85:20: error: invalid case style for constant 'Bad_Name'
    [readability-identifier-naming,-warnings-as-errors]
```

and the build **exited 1**. Reverted, rebuilt: 60 steps, 0 findings, exit 0. A fed violation and a
step count are two different proofs and neither substitutes for the other — the violation reports
from one file and says nothing about the other 500; the step count proves statements ran and
nothing about whether any carried `--tidy`.

**The clang-cl step count is reported because the rule that asks for it landed in my rebase**
(`80a8fe7`), and it applies here for the reason that commit gives rather than as a formality: this
machine's launcher is `fastcache-cc 0.2.0-739-gd4451c3b`, and `git merge-base --is-ancestor
ca8dfc32 d4451c3b` in the fastcached checkout **fails** — so the depfile fix is absent, the header
edge does not exist in the ninja graph, and an incremental `clangcl-*` "pass" would have been an
answer from an incomplete graph rather than a check. 524 steps is a real rebuild.

**The console case runs for real on this machine** — it is not a skip. `CONIN$` opens, a key record
is written, and **both** Windows backends report it:

```
BackendParity_test.cpp(1848): PASSED: CHECK( probe.readable >= 1 ) ... 1 >= 1
All tests passed (18 assertions in 1 test case)      [9 per backend × wfmo, iocp]
```

That is the merge demonstrated: one wait serving a console handle, and the same wait serving
sockets in the 29 cases around it.

## Three things that did not work as the dispatch assumed

1. **`clangcl-debug` with `-DCORE_CPP_SANITIZERS=address` does not build in this tree**, and this is
   not about my change. clang-cl's ASan refuses `/MDd` (`invalid argument '-MDd' not allowed with
   '-fsanitize=address'`), and once that is worked around, `lld-link` refuses the link:
   `mismatch detected for 'annotate_string'`. The cause is the library-hygiene rule working as
   intended — `CORE_CPP_SANITIZERS` is PRIVATE to core-cpp's targets, so Catch2 in the same tree is
   built *without* the sanitizer and MSVC's STL annotates `std::string`, `std::vector` and
   `std::optional` differently on the two sides. There is no Windows sanitizer preset and no
   Windows sanitizer CI leg, so nothing had exercised this. **I ran ASan through MSVC's own
   `cl /fsanitize=address` in a throwaway tree with the flag in the GLOBAL `CMAKE_CXX_FLAGS`** (so
   Catch2 gets it too) and the release CRT (`NDEBUG` is not implied, so every assertion survives).
   Worth a ticket; I did not add a preset, because a preset is a build-contract change and not mine
   to make in this lane.
2. **There is no LeakSanitizer on Windows**, so the teardown drain cannot be proven by a leak
   detector here. That is why *an IocpBackend destroyed with a packet in flight gives every share
   back* measures the slot's own `referenceCount()` through a second share the case keeps: after the
   backend is gone the only share left must be that one. Removing `drainOutstanding()` makes it 2.
   Nothing else in the suite distinguishes those two numbers.
3. **The `clang-tidy` preset is a Unix preset, so it lints none of this.** Every file I added under
   `windows/` is invisible to it. I ran the pinned clang-tidy 22.1.8 on Windows against
   `clangcl-release`'s compile database instead, and it found **eleven real things** in my code
   (below). This is a standing gap, not a one-off: the Windows arms of `core::net`, `core::platform`
   and `core::tui` are linted by nothing.

## What clang-tidy cost, and one finding that is not mine

Fixed in my code: a `void*`-laundered `GetProcAddress` cast (`bugprone-casting-through-void`), three
signed/unsigned comparisons against `INVALID_SOCKET` and one against `recv`'s result
(`modernize-use-integer-sign-comparison`, fixed with a typed constant and `std::cmp_equal`), a
nested conditional operator, an unused `using`, a missing self-assignment guard
(`bugprone-unhandled-self-assignment` — the operator was already self-assignment safe by ordering,
and now says so), and an analyzer-visible null function pointer.

One of them changed the design, and I think for the better. `cppcoreguidelines-virtual-class-destructor`
refuses **every** spelling of a destructor for a nested `Port` class: public-and-virtual is refused
on a `final` class (`clang-diagnostic-unnecessary-virtual-specifier`), protected-and-non-virtual is
refused by the `unique_ptr` that must hold it, and private is refused outright. So `IocpBackend`
now implements `ICompletionPort` itself — which is better anyway: the association record and the
thing that dequeues the port are one object, and `completionPort()` is `return this`. `NOLINT` is
banned by the guidelines and I did not reach for it.

**Not mine, reported rather than fixed** (`.agent/rules/` — report a defect in another lane's file):
`src/core/platform/Types.hpp:33`, the Windows arm, trips two checks —

```
misc-misplaced-const:            'InvalidHandle' declared with a const-qualified type alias;
                                 results in 'void *const' instead of 'const void *'
readability-identifier-naming:   invalid case style for constant 'InvalidHandle' (wants invalidHandle)
```

Both are pre-existing at `HEAD`, both are inside `#ifdef _WIN32`, and both are invisible to the
Unix `clang-tidy` preset — which is why they have survived. The naming one looks like a false
positive against this project's own convention (constants are `CamelCase` with no prefix), so the
`.clang-tidy` config may simply lack a `ClassConstantCase`/`GlobalConstantCase` row that the
POSIX arm never needed. I have not touched either.

## Rulebook, docs and the tables

- **`.agent/rules/async-and-net.md`** gains G1-for-the-port, G3-for-thread-pool-callbacks, G4 with
  the `forget()` obligation, the slot ownership rule, the one-node-per-arm rule, and the
  level-triggering-is-built rule. Each cites its origin as a full URL (fastcached#668, #465, #710,
  #884, and the design spec).
- **`.agent/reference/provenance.md`**: seven new rows; the `IoBackend.hpp` row records `slot` and
  `completionPort()` as B7's additions, as the dispatch asked; the `windows/DefaultBackend.cpp` row
  no longer claims B7 makes IOCP the default.
- **`CHANGELOG.md`**, under **Added** only. There is no **Changed** entry, and that is the point:
  nothing a consumer sees changes, because the Windows default has not moved. The entry says
  "**available by name, not yet the default**" in those words, so the release note is true at this
  commit rather than only after B7b.
- **`docs/`**: `design/portability.md` said Windows used IOCP with event-select as a fallback, which
  was aspirational and is now accurate; `design/threading.md`'s status note says G4 is implemented;
  `modules/net.md` lists the backend and `completionPort()`.
- **`tools/migrate/renames.json`: no rows added, deliberately.** fastcached's concrete reactors have
  *no* rows — there is no `EpollReactor`, `KqueueReactor` or `PollReactor` row in the table, because
  the concrete backends are private and a consumer reaches one through `makeBackend`. Adding an
  `IocpReactor` row would be the only one of its kind. The rows a fastcached migration will actually
  need are `IocpSocket`/`IocpListener`, and those types arrive with B7b.

## What B7b inherits

0. **`HandleKind::Socket` has no production caller, and that is the load-bearing caveat on the
   whole split.** The scoping ruling held — nothing in the readiness bridge had to guess at
   `ISocket`'s shape — but the *reason* it held is worth stating, because it is also the risk it
   created. On Windows every registration that exists today is a **waitable HANDLE**:
   `DefaultHandleKind` is `Waitable`, and `WindowsSocket` parks on a `WSAEVENT` rather than on the
   SOCKET. So IOCP serves every socket in the tree through the *waitable* bridge, with no
   socket-layer knowledge at all — which is what made jobs 1 and 2 separable from B6.

   The consequence: **the zero-byte `WSARecv` and the `WSAEventSelect` write bridge are correct
   against the only caller that has ever exercised them, which is the test I wrote to exercise
   them.** An interface with no production user is one nothing has yet contradicted. B7b passes
   `HandleKind::Socket` from production code for the first time and may find the bridge wants
   something the tests never asked for — the arm/cancel accounting, the interaction between an
   `IocpSocket`'s own `WSARecv` and the backend's zero-byte one on the same socket, or which of the
   two owns `WSAEventSelect`'s single event. **That is a design question arriving on schedule, not a
   defect**, and it is much cheaper to expect it than to meet it.

1. **`makeDefaultBackend()` / `preferredBackendKind()`** — one line each in
   `windows/DefaultBackend.cpp`, plus the `BackendParity_test` case that asserts the default IS the
   preferred kind (it already passes either way).
2. **Routing a completion this backend did not issue.** `consumeCompletion` now decides ownership by
   looking the `lpOverlapped` up in a set of pointers this backend handed over, and **drops**
   anything else with a diagnostic. That is correct today and wrong the moment `IocpSocket` issues
   its own operations on the same port: an owner's completion dropped there is an awaitable that
   never resolves. The fix is fastcached's `IocpCompletion` — an overlapped header carrying a
   dispatch pointer — and the seam is marked in the code. **I found this reviewing my own diff**: the
   first version cast every non-wake packet to its own `Operation`, which would have read an
   arbitrary struct as one of its own.
3. **`ICompletionPort::forget()` is an obligation, not a convenience.** An association ends when the
   handle is closed, the kernel says nothing about it, and Windows reuses handle values — so a
   socket's `close()` must call it or the next socket handed that value looks already associated,
   is associated with nothing, and every operation on it completes nowhere. Stated on the method and
   in the rulebook.
4. **The `MSG_PEEK` classification** (`IocpSocket.cpp:327-334`) and fastcached's 13
   `IocpSocket_test` cases, none of which have a home until `ISocket` exists.

## How it was landed, and why not with `git commit --only`

The dispatch prescribes `git commit --only -- <paths>` in the shared checkout. By the time I was
ready, master had moved six commits (B4's and B5's fixes landed) **and another lane had uncommitted
edits to two of my files** — `CHANGELOG.md` and `.agent/rules/async-and-net.md`, B5's fix round in
progress. `--only` commits the *working-tree* state of the paths it is given, so on those two files
it would have committed their work and dropped mine; and a `merge --ff-only` would have refused, or
clobbered them.

So: the commit was made and rebased onto `7bdd132` in my own worktree, where it applied cleanly.
Landing then moved the branch ref and rebuilt only my own files' working state
(`git update-ref` → `git reset` (index only) → `git checkout --` my sixteen uncontested paths),
and for the two contested files wrote a **three-way merge computed offline** with `git merge-file`
— so **their working copies were never reverted, not even momentarily**, which stashing would have
done. Both merges were clean (their hunks are in the timer and host-driven sections; mine are in
the thread-affinity and backend sections, and a new block before `### Deprecated`).

Checked afterwards, not assumed:

- `git status --porcelain` before and after is **byte-identical** — no lane's file gained or lost a
  status line.
- The other lane's pending delta is still **38 added lines** across those two files, the same
  number it had against the old base; their content is present in the working tree; and
  `diff mine→merged` is exactly their hunks and nothing else.
- `git show --stat HEAD` is the eighteen files above and nothing more.

## A false-pass path in my own canary registration, measured

`core-cpp.iocp-canary.{g1,g4}` is registered `WILL_FAIL`, which inverts **any** non-zero exit. The
lead's sweep proposed closing the gap with
`FAIL_REGULAR_EXPRESSION "unknown mode;usage:;offered no completion port;could not associate"`.
Checked against the binary's own strings, **those four are right and complete for the paths that
print**:

| Path | Prints | Exit |
|---|---|---|
| no argument / wrong count | `usage: core-cpp-iocp-canary <g1\|g4>` | 2 |
| unrecognised mode | `iocp-canary: unknown mode` | 2 |
| `completionPort()` answered null | `iocp-canary: the IOCP backend offered no completion port` | 2 |
| first `associate` refused | `iocp-canary: could not associate a loopback socket with the port at all` | 2 |
| no loopback pair | `iocp-canary: SKIPPED -- no loopback socket pair on this machine` | 77 (scored skipped) |
| `NDEBUG` | `iocp-canary: SKIPPED -- assertions are compiled out in this configuration` | 77 (scored skipped) |
| **refusal did NOT fire** (the regression) | `... and nothing refused it` | **0** → correctly scored failed |

**There is a fifth path, it prints nothing, and no `FAIL_REGULAR_EXPRESSION` can ever close it.**
`IocpBackend`'s constructor throws `std::runtime_error` when `CreateIoCompletionPort` fails, and
nothing catches it. Measured rather than reasoned — a `probe` mode was added temporarily, built and
run, then reverted (the worktree is clean and `320a9ab` never moved):

```
core-cpp-iocp-canary.exe probe   ->  exit 1, stderr COMPLETELY EMPTY
```

Exit **1** is byte-identical to the assertion firing, and stderr is empty because the abort path is
suppressed by `core::testing_dialogs` — the very thing that stops a Debug assert opening a modal
dialog also removes the CRT's message. So the throw reaches `std::terminate` → `abort()` → the
canary's own `SIGABRT` handler → `_Exit(1)`.

**The handler that makes the canary work is what makes this indistinguishable.** It converts *every*
abort into exit 1, and it cannot tell the assertion's abort from any other.

**A negative match cannot fix it, because there is no text to match.** The fix has to be positive —
either `PASS_REGULAR_EXPRESSION` on the guarantee's own assertion text (so the canary passes only
when the words `G1: exactly one thread dequeues` or `G4: a SOCKET is associated` actually appear),
or a distinct marker printed before any path that can abort without the assertion. Both are B4's
call in its sweep; **naming it is mine, and not amending `320a9ab` to do it is deliberate.**

Realistic? Handle exhaustion only. But a canary that reports *passed* on a machine under handle
pressure is exactly the false green the canary exists to rule out.

### The fix's semantics, measured rather than read off the documentation

B4 proposed `PASS_REGULAR_EXPRESSION` on a printed marker plus `FAIL_REGULAR_EXPRESSION` on the
continue paths, with no `WILL_FAIL`. CMake documents `PASS_REGULAR_EXPRESSION` as *"the process
exit code is ignored"*, which raises one question the docs do not answer: **does that also swallow
`SKIP_RETURN_CODE`?** If it did, the canary would FAIL on every Release leg — where it abstains
with 77 and prints no marker — rather than abstaining. A throwaway ctest project with four
registrations answers it:

| Case | Output / exit | ctest verdict |
|---|---|---|
| abstains (`NDEBUG`) | skip message, **77** | **Skipped** — `SKIP_RETURN_CODE` still works |
| refusal fires | marker, exit 1 | **Passed** |
| refusal does NOT fire | marker **and** the continue text, exit 0 | **Failed** — `FAIL_` beats `PASS_` |
| constructor threw | **no output**, exit 1 | **Failed** — *"Required regular expression not found"* |

So the shape is right, the Release legs keep abstaining, and **the fifth path is closed by the
positive match rather than by any alternation**. Two details are mine to get right and neither is
visible from B4's side: the marker must be **per guarantee** (the registration is a
`foreach(guarantee IN ITEMS g1 g4)` loop, so one G1-shaped regex applied to both would fail `g4`),
and it must go to **`stderr`**, which is unbuffered — `onAbort` calls `std::_Exit`, which flushes
nothing, so a marker written to `stdout` would be lost on exactly the path it exists to prove.

**And the canary's entire CI coverage is one leg.** `.github/workflows/build.yml` runs
`cl-release`, `clangcl-release`, `cl-debug` and `cl-release-tls` on Windows; only `cl-debug` has
assertions, so `cl-debug` is the sole job where either canary does anything. `clangcl-debug` — the
leg the dispatch asked for and I ran at 506 steps — is not in CI at all. That makes the
registration's correctness matter more, not less: there is no second job to disagree with it.

## Concerns

1. **`consumeCompletion` drops a foreign completion** — item 2 above. Harmless now, a hang later.
   The seam is marked but it is a real obligation on B7b, not a nicety.
2. **No CI leg lints or sanitises any Windows-only source.** Eleven findings in four new files, and
   two pre-existing ones in `core/platform/Types.hpp` that have survived since A6, all invisible to
   the Unix `clang-tidy` preset. A Windows tidy leg (and a working Windows ASan preset) would be
   worth a ticket.
3. **The console parity case shares a mutex name with `core::tui`'s test by string**
   (`Local\core-cpp-tui-console-input-test`), deliberately — the resource is the process's console,
   not the binary's, and two suites serialising on two different names would not serialise at all.
   But the name is now written in two modules that cannot share a constant, and renaming one breaks
   the serialisation silently. Said in a comment at both ends; there is no gate for it.
4. **The generation is not the discriminator the dispatch implies.** Stated plainly above rather
   than left for a reviewer to find.
5. **`_inFlight` is a `std::unordered_set<void*>` and every arm is a `new Operation`.** One hash
   insert and one allocation per readiness wait. Correct and simple, and measurably more than a
   readiness backend costs; if IOCP becomes the default and a server's profile shows it, the node is
   poolable — and *then* the generation stops being a diagnostic and starts being the check, which
   is why it is there.
6. **The `WSAEventSelect` write bridge puts the socket into non-blocking mode** and takes the
   socket's one allowed event object. A socket registered with `Interest::Write` here must not be
   selected by anybody else. Said in the code; B7b's `IocpSocket` is the one that has to honour it.
