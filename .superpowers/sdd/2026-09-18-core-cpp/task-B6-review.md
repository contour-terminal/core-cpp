# Task B6 review — one frame-free, stop-aware socket contract, and one POSIX socket

Commit `770f2dc` on `9924358`, 33 files, +4787/−206, on `origin/master`.

**Spec compliance: PASS WITH CHANGES REQUIRED.** Every deliverable the brief and dispatch name
exists and works. The three cancellation rules are implemented as specified and each is
mutation-proved. `asTask` is added. The bookkeeping (CHANGELOG under Breaking with migrations,
18 `renames.json` rows, 13 provenance rows, twelve rules in `.agent/rules/async-and-net.md` each
citing a full URL) is complete. **But the one rule the brief singled out — "verify the slot guards
enforce it rather than document it" — is enforced at the wrong moment, and on the write side the
worst branch skips the guard entirely.** Findings 1 and 2 are both in that one place.

**Task quality: PASS, strongly.** The best-instrumented report of this run. Predictions written
before every mutation run; the one wrong prediction (M2) found, diagnosed and used to replace a
vacuous case; each canary proved able to die *and* proved tied to its own guard rather than the
binary's; the analyser proved to have run by a planted violation on an already-green tree; three
misread measurements recorded as method failures rather than quietly fixed. One deduction, in
"Gates and evidence" below: the record cites no CI leg at all, so **kqueue is witnessed by nothing
in it**.

---

## Findings, most severe first

### 1. `write` / `writeVectored` destroy a parked write before the guard, and skip the guard on the branch where that is fatal — HIGH

`PosixSocket::write` (`src/core/net/posix/PosixSocket.cpp:646`) and `writeVectored` (`:684`) assign
`_write.remaining`, `_write.segments`, `_write.segmentIndex`, `_write.segmentOffset`,
`_write.written` and `_write.keepAlive`, then call `trySend(_write)`, and only *after* that reach
`contract::claimWriteSlot(_write.awaitable)`. On the inline-completion branch they execute
`_write = {}` and return without ever reaching the guard.

**Scenario.** A response streamer holds a parked `write` of a 4 MiB body: `_write.awaitable = &awA`,
`_write.park = PA`, `_write.written = 1.5 MiB`, `_write.remaining` = the tail. The loop reports the
fd writable, `pumpWrite` sends another 400 KiB and stays parked because the send window filled
again. A keepalive coroutine on the same connection now calls `sock->write(pingFrame)`. The verb
overwrites `_write.remaining` with `pingFrame` and `_write.written` with `0`; the send window has
room for 8 bytes, so `trySend` succeeds; `_write = {}` clears `awaitable` and `park`.

**Result.** `awA` is never completed and never abandoned — the streaming coroutine is suspended for
the life of the process. `PA` is never unregistered, so it stays in the park table, attached to the
fd. **No assertion fires, in Debug or in Release**, because `claimWriteSlot` is on the other branch.
The keepalive caller is told the write succeeded.

The `write-slot` canary drives the *parked → parked* path, where `trySend` returns `nullopt` and the
guard is reached — which is why it passes. The parked → inline path is the one with no tripwire on
it, and it is the likelier of the two in production, because a write only parks when the window is
full and only unparks when it drains.

In Release the parking branch is no better: the assert is gone, the cursor is already clobbered, and
the arm hook then overwrites `_write.awaitable` and `_write.park` (see finding 2).

**Fix:** claim the slot as the first statement of the verb, before `_write` is touched at all, and
on a temporary `WriteOperation` that is installed only once the operation is known to park. The read
verbs already have the safe shape — `tryRead`/`tryProbe` do not touch `_read` — which is why the
read side does not have this defect.

### 2. The slot guard is checked in the verb but the slot is claimed in the arm hook; the gap ends in a use-after-free — HIGH

`contract::claimReadSlot(_read.awaitable)` / `claimWriteSlot(_write.awaitable)` run when the *verb*
is called. `_read.awaitable = &self` and `_read.park = …` are written by the **arm hook**, inside
`await_suspend` (`PosixSocket.cpp:312`, `:339`, `:365`, `:666`, `:705`). Nothing guards the arm
site. `SocketContractCanary.cpp:193` states the gap in its own comment — *"the slot is claimed when
the operation arms, which is inside `await_suspend`. A canary that only called the verb twice would
trip nothing"* — and then does not close it. `core::async::asTask` exists specifically so a caller
*can* hold an operation between the verb and the await, so the gap is not hypothetical; it is the
documented escape hatch.

**Scenario.**

```cpp
auto a = core::async::asTask(sock->read(bufA));
auto b = core::async::asTask(sock->read(bufB));
co_await core::async::whenAll(std::move(a), std::move(b));
```

Both verbs see `_read.awaitable == nullptr` and pass the guard. Arm A files park `PA`; arm B
overwrites `_read.awaitable` with `&awB` and `_read.park` with `PB`. **`PA` is never unregistered.**

- Flow `a` is never resumed (silent hang), exactly the `fastcached#663` leak the guard exists for.
- `PA` stays registered and attached; every readiness on the fd dispatches `onReadWake` for it,
  which returns on the null-slot check.
- Then the connection ends. `PosixSocket::close` takes only `_read.park` (= `PB`) out, then calls
  `_loop.notifyHandleClosing(_fd, Cancel)`, which walks `_parks.parksOn(_fd)`, **finds `PA`, leaves
  the park in the table**, pushes it to `_closedParks` and marks it abandoned
  (`EventLoop.cpp:1074-1085`). `~PosixSocket` returns and the socket's storage is freed.
- The next turn's step 5 runs `queueParkedWaiter(PA)`, which takes the readiness branch, reads
  `ParkWake::Abandoned` and queues a `ReadyEntry`. `drainReadyQueue` → `runDueCallback(PA,
  Abandoned)` → `_parks.find(PA)` is still non-null → `onReady(state, Abandoned)` where `state` is
  the freed `PosixSocket`. **`PosixSocket::onReadWake` dereferences it.**

The generation check that protects every other path does not help here, because the park was never
taken out of the table — only its id was dropped from the socket.

**Fix:** put the assertion where the claim is. `assert(socket->_read.awaitable == nullptr)` at the
top of each arm hook, and the same on the write side, is the check that matches the invariant. The
verb-site call can stay as the early, friendlier diagnostic; it is not the enforcing one.

### 3. `ResultAwaitable` can outlive its owner, and the header asks for less than it needs — MEDIUM

`IoAwaitable.hpp:91` documents the owner pointer as *"Must outlive the await."* The destructor
(`:151`) calls `retireNow()` whenever `!_settled`, which calls `_retire(_owner, this)` — so the real
requirement is that the owner outlive **the awaitable**, awaited or not.

**Scenario.** A connection handler builds `auto pending = async::asTask(sock->read(buf));`, stores
the task in a per-connection struct, and a later branch tears the connection down without ever
awaiting it: the `unique_ptr<ISocket>` is reset first (an explicit `.reset()`, or simply a member
declared after the task). `~PosixSocket` never learns about an operation that was created and never
awaited — `takeRead()` finds `_read.awaitable == nullptr` and `abandonRead` returns. The task frame
is then destroyed, `~ResultAwaitable` sees `_settled == false`, and `retireRead(deadSocket, this)`
reads `socket->_read.awaitable` through a dangling pointer.

`ReactorSocket_test`'s "created and never awaited" case covers the socket's safety in this state
(the defect §7 of the report found); it does not cover the awaitable's safety in the reverse
destruction order.

**Fix:** state the requirement correctly in the header — this is cheap and is the part that must
land before other lanes copy the pattern. Neutralising unawaited operations from `close()` is not
possible as built (the socket has no handle on them), so the contract has to carry it.

### 4. `setReceiveDeadline(0ms)` cannot clear a deadline, and the authority it cites says the opposite — MEDIUM, signature-level

`ISocket.hpp:223` and `PosixSocket.cpp:159`: *"Non-positive leaves the current setting alone,
matching `SO_RCVTIMEO`'s own reading of zero."* `SO_RCVTIMEO` with a zero timeval **disables** the
timeout; it does not mean "ignore me". So the justification is wrong, and the chosen behaviour
leaves the interface with **no way to remove a receive deadline once one is set**.

**Scenario.** A connection applies a 5 s idle bound during protocol negotiation, then upgrades to a
long-poll or a server-sent-event stream and calls `setReceiveDeadline(0ms)` to lift it. The call is
a no-op; every subsequent read still arms a 5 s timer and resolves
`NetErrorCode::Timeout`. The consumer's only remedy is to rebuild the socket.

Also note the verb's own doc says "**Re-arms** how long a single read may wait", but the
implementation only stores the value — an already-parked read keeps the timer it armed. That is
defensible; the word is not.

This is a signature-class item: changing it later is a silent behaviour change for every caller that
has learned to pass zero.

### 5. `waitReadable` is wrong in both states on an adopted PTY or pipe fd — MEDIUM

`tryProbe` (`PosixSocket.cpp:278`) is the only `try*` function with **no `ENOTSOCK` branch**;
`tryRead` (`:181`) and `trySendFlat` (`:566`) both have one. Its `const`-ness is justified with
*"a descriptor that cannot be peeked was already found to be one by the read that preceded it"* —
an assumption that a read precedes the probe, which is the opposite of what `waitReadable` is for.

**Scenario A, before any read.** contour adopts a PTY master through `net::adoptFd`. A caller that
does the documented thing — `co_await sock->waitReadable()` before its first `read` — reaches
`::recv(fd, …, MSG_PEEK)` on a non-socket, gets `ENOTSOCK`, which is neither `EWOULDBLOCK` nor
`EINTR`, and the operation resolves `NetErrorCode::SystemError` **without ever waiting**. The caller
reads that as a dead connection on a PTY that is perfectly healthy.

**Scenario B, after `_plainFd` is set.** `tryProbe` returns a hard `1` with no readiness check at
all, so `waitReadable()` never suspends. A watchdog loop of the shape `ISocket::cancelRead`'s own
documentation motivates — hold a parked `waitReadable`, retire it when the iteration ends — becomes
a turn-free spin on a PTY.

### 6. Three shipped transports do not keep the rules `ISocket`'s docs attach to them; the report names only one — MEDIUM

Report concern 1 names `WindowsSocket::cancelRead`, which is correct, scoped and well argued.
`TlsSocket` (`Tls.cpp`) has three more, and none is mentioned:

- **`shutdownWrite` is not overridden.** The base default is a no-op that `ISocket.hpp:202` says is
  *"for FAKES"*. `TlsSocket` is not a fake and its inner socket implements the verb. Scenario: a
  response whose framing is "ends at EOF" over TLS — the server calls `shutdownWrite()`, nothing
  happens, and the client waits for a FIN that only arrives at the eventual full `close()`.
  `AcceptedHalfClose_test` covers `PosixSocket` and `WindowsSocket`, not this.
- **`waitReadable` is not overridden**, so a TLS socket answers the flat `1` — and `ISocket.hpp:151`
  calls out the TLS case by name as the reason the verb's "consumes nothing" clause is worded the
  way it is. Same spin as finding 5, scenario B.
- **`handshakeIfNeeded` is not overridden by anything in the tree.** `ISocket.hpp:127` says *"a TLS
  decorator overrides it to drive the handshake. An accept loop awaits this once before protocol
  autodetection"*. `TlsSocket` inherits the immediate-success default. The handshake still happens
  lazily inside `readPlain`/`writePlain`, so no bytes are lost today — but B8 is about to write the
  accept loop this sentence describes, against a verb that currently does nothing for the one
  transport that has a handshake.

B11 owns `Tls.cpp`'s merge and the lane says so. The *interface doc* is B6's, and it promises three
things the shipped decorator does not do.

### 7. "It inherits … the teardown" is claimed in three places and is not true — LOW/MEDIUM

CHANGELOG and report §3 both say the readiness park inherits `~EventLoop`'s teardown. It does not:
step 2 skips every `_ready` entry with `callbackPark` set (`EventLoop.cpp:81`), and
`unparkEverything` skips callback parks by the `!entry->parked` test, saying so in its own comment.
A socket operation parked when its loop is destroyed is neither completed nor abandoned — the
awaiting coroutine is never resumed and never unwinds.

Report concern 4 states the residual risk but misdescribes why it is tolerable: *"a socket that
outlives its loop keeps a `ParkId` naming a park the table has dropped; `unregisterPark` on a stale
id is a no-op"*. `~PosixSocket` would be calling `cancelTimer`, `unregisterPark` and
`notifyHandleClosing` **on a destroyed `EventLoop` object**, which is not a stale-id no-op. The
conclusion (do not outlive the loop) is right; the reason recorded for it is not, and a later lane
will read the reason.

### 8. The turn's step-5 idempotence comment is now false for the new park kind — LOW

`EventLoop.cpp:317` still reads *"a park the backend also reported is queued exactly once — the
first queueing takes its waiter, and a park with no waiter left is skipped."* A readiness park has
no waiter to take: `queueParkedWaiter` pushes a `ReadyEntry` unconditionally, so a park reported by
both the backend wait and `_closedParks` is dispatched twice. Harmless today — `onReadWake`
re-checks the slot and the stale id resolves to nothing — but the invariant the comment asserts no
longer holds, and the next lane to reason from it will be reasoning from a false premise.

### 9. `ISocket.hpp` now drags `EventLoop.hpp` into every consumer — LOW

`ISocket.hpp` → `IoAwaitable.hpp` → `<core/net/EventLoop.hpp>` (needed by `onStop()`'s
`_loop->requestCancel`). `SocketContract.hpp:33` goes to the trouble of forward-declaring
`EventLoop` and puts `assertTeardownIsSerialisedWithDispatch` out of line *"so this header stays
free of `<core/net/EventLoop.hpp>`, which a consumer that only wants `requireReadBuffer` should not
have to compile"* — and then the module's main header includes it anyway. A one-line out-of-line
`requestCancelOn(EventLoop&, ParkId) noexcept` would restore the forward declaration.

---

## What is sound, and worth saying plainly

- **The three cancellation rules are right and are proved right.** `await_resume` tests
  `_abandoned`, then the value, then the token, in that order; `abandon()` is a distinct verb from
  `complete()` and the reasoning for it (a destructor cannot hand the flow a socket to look at) is
  correct. M2/M2′ is the single best piece of work in the task: the lane predicted 1 case, measured
  0, and rather than adjusting the prediction found that its own case was vacuous, replaced it with
  the realistic ordering, and wrote the measurement into both the case and the rulebook.
- **The frameless park's design case holds up.** I checked the two costs §3 claims for the rejected
  alternative. The step-4 predicate `driven && (_parks.size() != 0 || …)` is real: a registration
  the table cannot see contributes to none of its terms, and `runOnce()` sets neither `_inRun` nor
  `until`, so `idleWait` is false there unconditionally. The honest retraction of the `hasPendingWork()`
  argument, and the refusal to strengthen the host-wake claim once it turned out to be untestable,
  are the right instinct.
- **Teardown under the three orderings the brief asked about.** Socket destroyed while the park is
  filed: correct — `takeRead`/`takeWrite` unregister before anything settles, and `close()` takes
  *both* into locals before settling either, so the second settle cannot read a `this` the first
  freed. Stop between filing and arming: correct — `requestCancel` from the loop thread resolves by
  *queueing* a `ReadyEntry`, so the `_cancelReg.emplace` in `await_suspend` cannot resume the
  awaiting coroutine re-entrantly even when the token is already stopped. Loop destroyed first: not
  handled, finding 7.
- **`asTask` is correct where it matters most.** By value, so the temporary is moved into the frame;
  the stop token reaches the operation through `Task`'s own awaiter; the `void` specialisation is
  handled; and the move constructor it needs is guarded in Debug against the post-await case.
- `close()` touches no member after completing. Verified by reading, not by the comment.
- Mechanical rules: no `NOLINT`, no diagnostic pragma, no C-style index `for`, no raw owning
  pointer, SPDX first in every new file, coroutine parameters by value throughout.

---

## Gates and evidence

**Is one `clang-debug` build sufficient re-verification for the `f277331 → 770f2dc` delta? Yes, and
the report understates how safely so.** `git diff f277331 770f2dc` touches **no file under
`src/core/net/`**: the delta is `src/core/tui/**`, `CHANGELOG.md`, `AGENT.md`, `README.md`,
`docs/modules/index.md` and nine lines of `.agent/rules/async-and-net.md`. B6's own surface is
byte-identical to the tree the full matrix ran on. The only new question the rebase raises is
whether the `EventLoop`/`ParkTable` changes still compose with B12's new TUI consumer, and a full
`clang-debug` build plus `ctest` (37/37, 0 skipped, 99 steps) answers exactly that question on the
platform where both are built. Saying so instead of restating the table as if measured twice was
the right call.

**The record cites no CI leg, and that is the one gap in an otherwise exemplary evidence trail.**
§6 is entirely local presets. Nothing in it witnesses **kqueue**, and `ReactorSocket_test`,
`WaitReadable_test`, `CancelRead_test` and `SocketDecorator_test` are the suites that would —
`MSG_CMSG_CLOEXEC`'s fallback and the `fcntl(FD_CLOEXEC)` branch in `tryReadWithFd` compile on no
leg §6 lists. The dispatch said explicitly: *"You cannot run macOS or BSD here… put a hypothesis in
the commit message and let CI refute it."*

The evidence does exist; it is just not in the report, and the report was filed while the run was
still going. Build run **35596646379** on `770f2dc`: `macos (appleclang)`, `macos
(appleclang-debug)`, `macos (llvm-22)`, all four `windows` legs, all seven `linux` legs, both
`emscripten` legs, both sanitizer legs, `style`, `coverage`, `consumer-smoke` ×3 and `unbuilt
commits` **green**; `clang-tidy` and `windows (cl-release)` still `in_progress` at the time of this
review. So kqueue is in fact covered — by a run the report does not mention and could not yet have
read. FreeBSD (`portability.yml`) has not been dispatched.

The `clangcl-release` row does honour the stale-cache rule: `--clean-first`, 537 ninja steps, with
the step count named as the tell rather than the green.

---

## Before four lanes build on `ISocket` — the signature-class items

1. **Where the slot guard lives** (findings 1, 2). This is the one a transport author copies. It
   should read: claim at the top of the verb, before any per-operation state is written, **and**
   assert at the arm site, which is where the slot is actually taken. Both `contract::claimReadSlot`
   and `claimWriteSlot` are public API, so their intended call site is part of the contract.
2. **`setReceiveDeadline(0ms)`** (finding 4). Decide now whether zero clears or is ignored. Changing
   it after B7/B8/B11 have callers is a silent behaviour break, and the current doc justifies the
   behaviour with a claim about `SO_RCVTIMEO` that is the reverse of what `SO_RCVTIMEO` does.
3. **`ResultAwaitable`'s owner-lifetime clause** (finding 3). "Must outlive the await" → "must
   outlive the awaitable". One sentence, and it is the sentence a B7 or B11 transport author will
   design their own retire hook against.
4. **`handshakeIfNeeded`** (finding 6). Either a transport overrides it or the doc stops saying one
   does, before B8 writes an accept loop that awaits it.
