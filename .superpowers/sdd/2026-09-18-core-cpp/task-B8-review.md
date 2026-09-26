# Task B8 review: dialling and resolution

Reviewed: `origin/master` `c8264a0..a922f9e` (`696efe5`, `193b245`, `a922f9e`), against
`task-B8-dispatch.md` and `task-B8-report.md`. Source read at `a922f9e` with `git show`; upstream
compared at fastcached `0708dd54`. No fix made here.

## Verdicts

- **Spec compliance: PASS with gaps.** Everything on the build list is delivered except `IocpDial`,
  whose absence is justified: there is no `IocpSocket` to build it on. `BlockingConnector` goes to
  B9, and the reason given is sound. The thread-identity case, the refused-connect-on-every-backend
  case and the stop-cancels-a-dial case all exist. The resolution rule and the `SO_ERROR` rule are
  in `.agent/rules/async-and-net.md` with full-URL origins. `CHANGELOG.md` has a Breaking entry.
  `renames.json` and `provenance.md` changed in the same commit as the code. The gaps: the budget
  does not cover resolution even though `DialOptions` documents that it does (H1). Two behaviour
  changes to `connect()` are missing from the CHANGELOG (M2).
- **Task quality: NEEDS A FIX ROUND.** The readiness dial itself is sound. It is correctly
  de-templatised, it asks `SO_ERROR` rather than trusting the callback, and it closes its
  descriptor on every exit. The findings are in what surrounds it:
  - the flow cannot be bounded or cancelled while a name is resolving;
  - one new public signature breaks the coroutine-parameter rule;
  - `IAdmissionControl` has a data race and a check-then-act gap;
  - three test cases are named for a property their assertions do not distinguish.

Count: **1 High, 5 Medium, 6 Low.**

## Which configuration covered which claim

| Claim | Covered by |
|---|---|
| The lookup runs off the loop thread (the connector path) | The lane's Linux and Windows legs (reported, not re-run here) |
| R101 refusal on poll and epoll | Linux. **Measured here** on WSL2 with a C probe: a non-blocking loopback connect to a just-closed port returns `EINPROGRESS`. `poll` then reports `revents=0x1c` (POLLOUT, POLLERR and POLLHUP), and `SO_ERROR` is `ECONNREFUSED`, five out of five times. On Linux the case therefore goes through the readiness path, and a dial that trusted `Writable` would **fail** it, so the case distinguishes there. |
| R101 refusal on WFMO | Windows `cl-debug` and `clangcl-release` (reported) |
| R101 refusal on kqueue | **Nothing yet.** Build run 35774850506 for `a922f9e` was still in progress when this review was written. See M5 for why a green there may not be evidence. |
| Cross-thread stop, and the TSan claim in §7 | Linux `clang-tsan` (reported) |

## High

### H1. Name resolution is covered by neither the dial budget nor the flow's stop token, though `DialOptions` says it is

`DialOptions::connectTimeout` is documented as covering "the **whole call**, name resolution and
every candidate address included". In `runConnectFlow`, however, `co_await resolver->resolve(host,
port, loop)` runs with no deadline, and the budget is checked only **after** the lookup returns.
`ThreadedAddressResolver`'s `SlotPark` never looks at the stop token, so a stop cannot reach a
flow that is parked on a lookup. `ConnectFlow_test`'s case "the budget covers the whole call,
resolution included" passes with the defect present: its scripted resolver advances the clock
and then **returns**, so the case tests "the budget is checked after resolution", not "resolution
is bounded".

**Failure scenario.** A caller runs `connect(&loop, "db.internal", 5432, &resolver,
{ .connectTimeout = 2s })` while its nameserver drops packets. With glibc defaults (timeout 5 s,
2 attempts, up to 3 nameservers), the lookup takes up to about 30 s, and the 2 s bound is not
applied at all. Wrapping the call does not help either. `whenAny(connect(...), delay(2s))` also
waits the full 30 s, because `Join` resumes the awaiting coroutine only when the **last** child
finishes, and the resolving child cannot be cancelled. The same flow blocks shutdown:
`requestStop()`'s `unparkEverything` does not know about the parked lookup. The loop is no longer
stalled, and that part of the task is delivered. The **caller**, though, still cannot bound or
cancel a dial, which is the property the budget exists to give.

The same gap exists upstream (`ConnectFlow.cpp:50` at `0708dd54`), so it was inherited, not
introduced. That is not a reason to keep it: here it contradicts a documented contract on
the task's own subject.

**Fix shape.**

- `SlotPark` registers a `StopCallback`. Under the slot's lock, the callback takes the waiter out
  of the slot and submits it with a cancelled mark. The worker's later `settle` then finds no
  waiter and drops the result, which is already legal: the slot is shared.
- The flow races the lookup against `deadline`, or the resolver is given the deadline (see
  "Signatures").
- `ConnectFlow_test` gets a resolver that **never** answers, and the case asserts `Timeout` at the
  budget on a `ManualClock`.

## Medium

### M1. The new `connect(..., IAsyncAddressResolver*, DialOptions)` overload takes `std::string_view host` in a lazy coroutine

`.agent/rules/cpp-guidelines.md` requires coroutine parameters to be owning types when the
coroutine outlives the call expression, and it names `string_view` specifically. `Sockets.cpp`'s
comment says "The host is copied HERE rather than deeper down, because this is the frame that
outlives the call expression". The copy does not help, because `Task` is **lazy**: the body,
including the copy, does not run until the task is first awaited.

**Failure scenario.** `auto t = connect(&loop, makeHost(), 443, &r, {}); co_await t;` stores a
view of a temporary that dies at the `;`. The resolver is then handed freed bytes. ASan catches it
in tests, but in production it sends an arbitrary name to DNS. The three-argument overload has
the same shape. It was inherited from contour, but B8 re-implemented it, and changing
`string_view` to `std::string` breaks no caller: every contour caller passes a literal.

### M2. `connect()` changed behaviour in two ways that neither the CHANGELOG nor the code comment records

The comment in `Sockets.cpp` says "same signature, same result type, same errors", and the
CHANGELOG says "Nothing to change for the common call". Two things did change:

1. **Cancellation now throws.** The old body caught `OperationCancelled` and returned
   `NetErrorCode::Cancelled`. `dialReadiness` now throws. `connectUnix`, in the same header, still
   returns the value, so the two dials in one header now disagree. **Scenario:** a reconnect loop
   `while (true) { auto r = co_await connect(...); if (!r && r.error().code == Cancelled) break; }`
   used to exit cleanly on shutdown. Now the exception escapes the loop instead.
2. **Resolution failure changed code.** It was `AddressError`; it is now `AddressNotAvail`,
   through `resolveFailure`, and the empty-host guard uses the same code. `NetError.hpp` documents
   `AddressNotAvail` as "A bind failed because the address is not available locally" and
   `AddressError` as "Address resolution or parsing failed". The new mapping contradicts the
   library's own vocabulary. **Scenario:** a caller that retries a DNS failure with
   `code == AddressError` stops matching and treats every DNS failure as a local bind problem.

Either restore the old behaviour or record both changes under Breaking. `resolveFailure` is public,
so its code is part of the API either way.

### M3. `IAdmissionControl` has a data race and a check-then-act gap, and ships with no test and no caller

- `CountingAdmissionControl` is documented as "Thread-safe", but `setMax` writes a plain
  `std::size_t _max` that `allowAccept` reads from any thread. That is a data race under the
  memory model. It also contradicts the rule that configuration is fixed at construction.
- `allowAccept()` followed by `onConnectionStarted()` is check-then-act. **Scenario:** two loops
  accept on one policy with the cap at 100 and 99 connections in flight. Both calls to
  `allowAccept()` read 99, and both loops admit, reaching 101. A cap that can be exceeded under
  exactly the load it exists for is not a cap.
- `onConnectionEnded()` without a matching start wraps the counter to `SIZE_MAX`, after which every
  accept is refused.

The race is inherited from upstream `IAdmissionControl.hpp:69-75`. On whether shipping it is
acceptable for v0.1.0, see "Signatures". It should not ship in this form.

### M4. Three test cases are named for a property their assertions do not distinguish

1. **"a connector honours the per-call budget"** (`Connector_test.cpp`) asserts only that an empty
   host is refused. It passes if `connectTimeout` is ignored entirely. It also passes if
   `ReadinessConnector::connect` passed `nullptr` for the clock, which would silently disable
   every budget. No test drives the budget end to end through `makeConnector`.
   `ConnectFlow_test` calls `runConnectFlow` directly, and `ReadinessDial_test` calls
   `dialReadiness` directly, so the wiring between them is uncovered.
2. **The thread-identity case's `CHECK(loopThread == std::this_thread::get_id())`** records
   `loopThread` in `dialAndWrite` **before** the first suspension, so the check is always true.
   Report §5 says this check "stops the case passing because the *flow* moved off the loop". It
   cannot do that. Suppose `settle()` called `waiter.resume()` inline: the rest of the dial would
   run on a resolver thread, and this check would still pass. At most a Debug-only affinity
   assert catches that. Record the thread **after** `co_await connector->connect(...)`. The
   other half of the case, `inner.callers().front() != loopThread`, does distinguish.
3. **The task's named requirement, "`connect()` asserts that thread is not the loop's", is met
   for `IConnector::connect` but not for the free `connect()` contour calls.** The two
   free-`connect()` cases dial `"127.0.0.1"`, which never reaches the pool. If the
   three-argument `connect` were wired to `InlineAddressResolver`, no test would fail. A case
   that dials `"localhost"` (answered from the hosts file, no DNS) and asserts that
   `defaultAsyncResolver().offloaded()` went up by one would close this.

### M5. The kqueue leg of the R101 case may never reach the readiness path, so a macOS green may not be evidence

On Linux the case is sound (see the table above). On BSD-derived stacks, a non-blocking connect
to a closed **loopback** port often fails synchronously with `ECONNREFUSED`, because the RST is
processed inline. **Not measured here; I have no macOS host.** If that holds, the kqueue leg
leaves through `beginConnect`'s errno branch and never tests the `EV_EOF`-as-`Writable` path that
R101 exists for. the commit message's hypothesis (`696efe5` as landed) for CI would then come back green without having been
tested. Make the case say which path it took, for example by asserting or `INFO`-logging that the
dial parked (a `detail` probe, or `parkedWaiterCount()` sampled during the dial). If it did not
park, report that loudly rather than pass silently. Until then, a green `appleclang-debug` result
shows that refusal reaches the caller, not that the dial ignores `Writable`.

## Low

- **L1. Only one cancellation case exercises the dial's own `StopCallback`, and nothing asserts
  which arm it took.** "the flow's stop token cancels a dial in flight" stops through
  `requestStop()`, which also calls `unparkEverything()`, so the case passes even without the
  dial's `cancelReg`. The cross-thread case (`rootStopSource().request_stop()`) is the only one
  that goes through the callback. Its comment admits it may cover the pre-park arm, and nothing
  asserts which. No case uses a flow-scoped token, for example a `whenAny` loser, which is the
  common real use. On Windows the leak check reduces to the park count; nothing checks that the
  socket was closed.
- **L2. `armSocketDeadline` has no test.** It is about 15 lines and trivially testable today with
  `TestLoop`, `ManualClock` and `makeSocketPair`: assert that a zero ceiling arms nothing, and that
  `expired` is set before the close. It is public API with an ordering promise, and "B10 will test
  it" is a promise with no owner in B10's brief. Close this before v0.1.0. It is not blocking now.
- **L3. The `IocpDial` hand-off exists only in the B8 report.** There is no B7b brief, and
  `progress.md` does not record it. The content is mostly right: the completion is the single
  writer, and the deadline cancels the operation instead of settling it. Two things are missing:
  (a) the refused-connect case must gain an IOCP leg; (b) `ConnectEx`'s outcome is the
  completion status, followed by `SO_UPDATE_CONNECT_CONTEXT`, **not** `SO_ERROR`. R101's
  principle carries over (ask the operation, not the notification), but the literal idiom does
  not. Put it in `progress.md`, and in a comment at the `dialStep` seam in `Connector.cpp`, where
  the IOCP change will be made.
- **L4. `adoptListener` does not say who owns the handle on failure.** The doc says ownership
  transfers to the returned listener. If `makeNonBlockingCloexec` fails, the descriptor is neither
  closed nor documented as still the caller's, so a caller will either leak it or close it twice.
  Document "on failure the caller still owns @p handle".
- **L5. `ThreadedAddressResolver::Impl` is a public nested struct**, "so the `.cpp`'s worker can
  name it". A forward-declared private `Impl` with the worker as a member function would do the
  same without widening the public header.
- **L6. The report's records are inconsistent.** §1 says "Two commits" and lists three, and its
  hashes (`ce37aac`/`abe699a`/`f228143`) do not match what landed (`696efe5`/`193b245`/`a922f9e`).
  Harmless now, but a later reader cannot follow the references.

## Specific questions from the dispatch

- **Does the thread-identity case distinguish?** Yes for the defect it names: if `connect()`
  resolved inline (for example, the connector passing a null loop), then `offloaded() == 1` and
  `callers().front() != loopThread` both fail. It does not distinguish the result being handed back
  on the wrong thread, which is what its second check claims to cover (M4.2).
- **R101 and `Readiness::Failed`.** Nothing branches on `Failed`. The dial registers only
  `Interest::Write`, maps every `ParkWake::Ready` to "ask `SO_ERROR`", and never calls `onError`.
  That is correct.
- **Is the §7 comment honestly worded?** Yes. It says the write was a race on paper, that 30 timed
  TSan runs did not provoke it, and that the change is hardening, not a closed defect. The
  reasoning behind "an ordinary cross-thread stop is ordered through the inbound queue" is correct:
  `requestCancel` from a non-loop thread goes through `_inboundMutex`, and `resolveCancel` on the
  loop thread only queues the callback, so it never settles inline inside `await_suspend`.
  `await_resume` resets `cancelReg` first, which waits for a callback running on another thread,
  so the callback cannot outlive the frame.
- **`armSocketDeadline` and `IAdmissionControl` with no caller.** The first is acceptable to ship
  once it has a test (L2). The second is not acceptable in its current form (M3).

## Signatures that should change before other lanes build on them

1. **`IAsyncAddressResolver::resolve(std::string, std::uint16_t, EventLoop*)`** has nothing to
   bound it by. Either add the deadline, as a `platform::SteadyTimePoint` or a small
   `ResolveOptions` struct, or document that the resolver **must** honour the awaiting flow's stop
   token and make `ThreadedAddressResolver` do so. B9's `BlockingConnector` and every test fake
   will implement this interface, which is why deciding now is cheap. (H1)
2. **`connect(EventLoop*, std::string_view host, …)`, both overloads**: take `std::string host`.
   (M1)
3. **`IAdmissionControl`**: replace `allowAccept()` + `onConnectionStarted()` with one atomic
   `tryAdmit()` that returns a move-only lease whose destructor ends the connection, and drop
   `setMax` (or make `_max` atomic and document the reload). Alternatively, leave the header out
   of the public `FILE_SET` until an accept loop consumes it. (M3)
4. **`resolveFailure`'s code**: `AddressError`, per `NetError.hpp`'s own definitions. (M2)

`DialOptions`, `IConnector::connect(std::string, std::uint16_t, DialOptions)`, `makeConnector`,
`listen(loop, ListenOptions)`, `adoptListener` (apart from its doc, L4), `IListener::boundPort` and
`AcceptResult = SocketResult` are sound as signatures.
