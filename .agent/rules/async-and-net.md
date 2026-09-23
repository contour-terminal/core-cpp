# Coroutines, the event loop and networking

Rules for `src/core/async/` and `src/core/net/`: what the layers may depend on, how sockets and
dials behave, and the lifetime rules that keep a coroutine frame from being leaked, freed twice
or resumed on the wrong thread.

**Status.** The code these rules govern arrives in two steps: contour's `coro` and `net` were
imported as they are (Tasks A5 and A6: `core::async`, and `core::net` with its `EventSource`
API), then fastcached's async and networking layer is merged into them (Phase B). Task B1 has
landed the first half of that merge — the ownership rules under "Task ownership" below are live
code, not a forecast — and Task B3 has landed `IoBackend`, so `EventSource` is gone and the
backend rules below are live code too. Task B7a landed `IocpBackend` and its readiness bridges, and
Task B7b the sockets that issue overlapped operations on its port (`IocpSocket`, `IocpListener`, the
`ConnectEx` dial) -- which is when IOCP became the Windows default; WFMO stays reachable by name for
one release. The rules are
written against the merged design's names, from the design spec,
[Part I §2](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md),
so the tasks that implement it inherit them; each Phase B task extends this file with the rules
it lands. Where a rule names a type that does not exist yet, it is the type the spec defines.

Nearly every rule here was paid for in fastcached, whose rulebook holds the measurements:
[fastcached `.agent/rules/wire-and-protocol.md`](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/rules/wire-and-protocol.md)
(cited below as "fastcached wire-and-protocol"), sections "The Net boundary", "Sockets",
"Dialing, and the reactor underneath it" and "Socket and coroutine lifetime". Its names are
translated with the spec's rename map (`IReactor` is `EventLoop`, `Schedule`/`CancelPending`
are `schedule`/`cancelPending`, `ISocket::Read` is `read`, and so on).

## Layering

- **`core::async` depends on the standard library only.** `core::net` depends on `async` and
  `platform`; `core::net_types` (`NetError`, `IoResult`) links nothing. The module table
  enforces the link edges; an include across modules must be a table edge too.
- **The layer that owns a concept owns its file.** When an include points the wrong way, the
  file moves to the layer that owns it; the table is not widened. An include graph drifts in
  silence: nothing fails, and the edge is found by whoever next tries to lift the code out.
  fastcached found ten such edges that way. Origin:
  [fastcached#100](https://github.com/LASTRADA-Software/fastcached/issues/100).
- **A header allowed across a boundary must stay a leaf.** A leaf that quietly gains one include
  drags its whole module across while every check still passes.

## The event loop contract

The contract is the spec's (Part I §2, rules 1 to 6), and it is short enough to keep in mind:

- **Backends dispatch, the loop resumes.** Backend, completion, stop and thread-pool callbacks
  only enqueue (`EventLoop::resumeSoon`); a coroutine resumes only on the loop's thread, in the
  second step of `runOnce`, on every backend. A resume from inside a backend's walk over its own
  ready list lets the resumed frame free the object whose entry the walk has not reached yet, so
  the rule is *asserted* rather than trusted: `detail::ReadyBatch::dispatch()` publishes that a
  dispatch is in flight (`detail::readinessDispatchInFlight()`) and `EventLoop::drainReadyQueue()`
  refuses to run while it is. The positive half is a parity case: a flow resumed by every backend
  records that flag from its own frame and it is false. Origin:
  [fastcached#475](https://github.com/LASTRADA-Software/fastcached/issues/475).
- **One thread dequeues a loop or a completion port**, helper threads only post, and a socket
  is associated with exactly one port.
- **Cancellation is `core::async::StopToken`**, which is `std::stop_token` where the standard
  library has it and core-cpp's fallback where it does not. A cancel from a flow's own token throws
  `core::async::OperationCancelled`; a cancel from the resource (`close()`, `cancelRead()`, a
  closed listener) returns `NetErrorCode::Cancelled` as a value. If a receive already completed
  with bytes, the data wins.

## The turn, and the orderings inside it

Task B4 landed `runOnce`, and the order of its five steps is a contract rather than an
arrangement. [`docs/design/threading.md`](../../docs/design/threading.md) states it for a reader;
what follows is what a change to it must not break.

- **One drain per turn, and it is step 2.** Readiness dispatched by the wait in step 4 and
  deadlines fired in step 5 are resumed by the NEXT turn's step 2. That is guarantee **G2** --
  every resumption happens in turn step 2 -- and it is stateable only because there is exactly one
  place that resumes. A turn that drained again at its end would be a turn where a backend's walk
  and a resumption share a stack, which is Rule 1 from the other side. Origin:
  [fastcached#475](https://github.com/LASTRADA-Software/fastcached/issues/475).
- **Cancel requests resolve in step 1, before the drain.** Resolving one is what queues the
  cancelled flow; resolved after the drain it would sit in the ready queue while steps 3 and 4
  computed a timeout and BLOCKED, so a cancel from another thread would take effect only when
  something unrelated woke the loop. `EventLoop_test.cpp`'s *a cross-thread cancel is resolved
  before the turn drains* asserts that one turn does both.
- **A cancel from the loop's own thread resolves inline; from any other thread it queues.** The
  predicate is `isOnWorkerThread()`, not "is anybody running": a park left live until the next
  turn is one whose frame its OWNER may destroy in between -- a `whenAny` loser is freed the
  moment the winner returns -- and the loop would then hold a handle into freed storage. `whenAny`
  stops its losers from inside the drain that ran the winner, so that is the common path and it is
  the inline one.
- **Both `clock.refresh()` calls are load-bearing, and they fail differently.** Step 3's decides
  the TIMEOUT the backend is given; step 5's decides which deadlines are due in this turn rather
  than the next. `ClockRefresh_test.cpp` has one case per call, because the single case that
  preceded it passed with either of them removed. A `SteadyClock` and a `ManualClock` both ignore
  `refresh()`, so nothing else in the suite notices when a turn stops making either call -- which
  is how fastcached's daemon came to serve a cache whose clock was frozen at the value
  `CachedClock` sampled in its constructor.
- **The turn is bounded (`EventLoopOptions::dispatchBatch`), and the remainder is kept.** Work
  that re-queues itself -- a flow yielding in a loop, a consumer that waits again at once -- would
  otherwise starve steps 4 and 5 entirely. Nothing is dropped: what the bound leaves is what the
  next turn takes, and step 3 answers a timeout of zero while the queue is non-empty, so a full
  ready queue is a poll rather than a block.
- **The wait is skipped when nothing could come back from it.** A loop with no park and no closed
  handle has nothing the backend can report. `run()` is the exception, and it is the whole of what
  `IdlePolicy::Block` means: a loop that owns its thread and is idle BLOCKS, because another
  thread may still `post` and the backend's wake channel is what ends that wait.
- **A loop somebody else drives never blocks inside a turn** (`IdlePolicy::Return`): the caller is
  what waits. `testing::TestLoop` is that policy plus `NullBackend`, and it is the real
  `EventLoop` rather than a second implementation -- fastcached's `TestReactor` was a reactor of
  its own, so every rule the platform reactors held had to be written twice and could differ.

## Teardown, in six steps

`~EventLoop` does six things in one order, and most of them are somebody's bug report.

1. **Assert that teardown is serialised with dispatch** (`!running() || isOnWorkerThread()`,
   guarantee G5). Origin:
   [fastcached#668](https://github.com/LASTRADA-Software/fastcached/issues/668).
2. **Request stop, THEN move the borrowed parks to the ready queue.** A waiter queued before the
   stop was requested resumes on its normal path, into an owner that is already destroyed.
3. **Bounded drain passes.** A cancelled awaitable commonly re-parks, so "drain until empty" spins
   forever on exactly the shutdown it exists to make clean.
4. **Abandon to a FIXPOINT**, over the ready queue, the park table and the inbound queue together.
   Freeing a chain re-enters the loop -- a frame holding a deadline runs its disarm into
   `cancelPending`, which reads the park table -- so a single pass leaves whatever that produced
   for MEMBER destruction, and members die in reverse declaration order: a chain freed from the
   later container then searches one whose destructor has already run. Origin:
   [fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025),
   [fastcached#1054](https://github.com/LASTRADA-Software/fastcached/issues/1054).
5. **Destroy the spawned roots**, after the abandonment and not before: a spawned flow's frame is
   owned there, so destroying it first pulls the ground from under anything still parked on it.
6. **Unregister the wake.** A host-driven backend holds a pointer to the loop and an armed host
   timer; either outliving the loop is a call into freed storage on the host's next turn.

- **What the loop OWNS is freed; what it BORROWS is resumed** -- in the ready queue and the park
  table. **The inbound queue is dropped, deliberately, and the discriminator is the CONTAINER.**
  The ready queue holds work this loop put there, so not resuming it strands flows the loop is
  responsible for; the inbound queue holds what another thread handed over and no turn took up.
  **The "cannot ask a borrowed handle what it names" hazard is NOT the reason** -- it is equally
  true of the ready queue, which teardown does resume, and a lazy `Task` submitted from inside a
  turn is started by the destructor either way. What that hazard argues is that the boundary must
  be a FIXED one rather than a judgement per item: the loop cannot inspect a handle to decide, so
  it decides by where the handle is. Resuming the inbound queue as well was tried and reverted:
  one pre-existing case failed and one segfaulted. The cost -- a cross-thread `ResumeOn { loop }`
  whose loop dies first strands its flow forever -- is paid by running one more turn before
  destroying such a loop. A chain the
  loop owns is a `DetachedTask`, which carries no stop token -- a detached flow has no awaiting
  coroutine to inherit one from -- so resuming it would not cancel it, it would run the rest of
  its body on a loop that is being destroyed. A chain the loop borrows belongs to a `Task`
  somebody holds, that owner set a stop token, and resuming it is what makes the frame unwind and
  run its cleanup. The question is answered by `ParkedWork::abandon` being non-empty, recorded
  where the work is queued rather than asked of `detail::Parked`, which offers no accessor for it.
- **A park whose waiter has been queued is still REGISTERED**, so it stays in the handle index
  until the park itself is taken. Readiness is dispatched in turn step 4 and resumed in step 2 of
  the NEXT turn, and a close landing in between must still find the park: dropping the handle
  index when the waiter was taken made that window invisible to `notifyHandleClosing`, which
  issued the kernel-side removal after the close instead of before it -- against a descriptor
  number the kernel may already have reassigned. The waiter index is a different question and
  does go then, or a cancel in the same turn hands back work that is already queued.
- **`cancelPending`'s `true` is an ownership transfer, not a status**, so the claim it takes back
  is DISARMED rather than released: releasing the last claim would free the very frame the caller
  has just been handed. It searches the ready queue, the park table and the inbound queue, because
  an answer that depended on which thread submitted is not a transfer anybody can rely on.
- **`spawn` releases a finished flow in the turn that ran it**, in O(1), through a list node the
  turn unlinks. contour swept every spawned flow at the top of each turn, which reclaimed a frame
  one turn late and cost O(n) per turn to do it; a server spawning one flow per connection pays
  that forever.

## Timers: one mechanism, and it does not poll

Task B5. Every rule here is a wake-up somebody paid for.

- **A timer does not poll; the loop already knows its next deadline.** fastcached's `DeadlineTimer`
  woke every 50ms and `InterruptibleSleepUntil` woke every `wakeBound`, and both had the same
  cause: `IReactor::Schedule` could not be taken back, so a wait that ALSO had to be woken by
  something else re-read its own condition in steps. `EventLoop` can take a deadline back by id
  (`cancelTimer`, `cancelPending`, `requestCancel`), so both park ONCE and are woken, and the armed
  deadline is what bounds the wait in turn step 3. Origin:
  [fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025), whose leak was
  the frame a poll interval left parked after the wait had already ended.
  `DeadlineTimer_test.cpp`'s *An armed DeadlineTimer bounds the turn's wait to its own deadline* is
  the case upstream could not have written.
- **A callback timer is a park, in the same table as a coroutine deadline** — same heap, same
  sequence counter, same never-reused ids. Not because sharing is tidy, but because two mechanisms
  would each answer "when is the next deadline" and the answers would drift; the symptom of that
  drift is a wait that is too long, which is a hang rather than a failure. The frameless kind is a
  `Park` whose `parked` is empty and whose `onExpired` is set, and step 5 queues both kinds into
  one ready queue, so their firing order across the two is the heap's.
- **A timer callback runs in turn step 2, where a coroutine resumes, and nowhere else.** Step 5
  could call it the moment it finds the deadline due; then user code would run at a second point in
  the turn, outside `dispatchBatch`, outside the assertion that no backend dispatch is in flight,
  and after the drain rather than in it. It also makes the window between "due" and "run"
  cancellable, which is the next rule.
- **`cancelTimer`'s `true` means THIS call prevented the callback**, including in the window
  between step 5 queueing a due timer and step 2 running it. Without that, an owner destroyed in
  that window — a `DeadlineTimer` is typically a member of the thing its callback touches — would
  have its callback run against storage that is gone, and "already fired" would mean "no longer
  cancellable and not yet harmless". It costs no scan: the ready entry names the `ParkId`, so
  taking the park out of the table is what makes it resolve to nothing.
- **A timer marks itself settled BEFORE its callback runs.** The callback is allowed to destroy the
  timer, and the destructor it triggers must find nothing left to retire. `DeadlineTimer::fire`
  reads the callback and its state into locals first, because after the call `*this` may not exist.
- **Teardown drops a queued timer callback rather than running it.** It is not work to unwind and
  not a frame to free; it is a call into an owner that is being destroyed with the loop.
- **Lazy pruning of the deadline heap is enough, and here is the measurement.** A cancelled
  deadline leaves a stale slot until the heap ROOT reaches it (Task B4's report, concern 4). The
  bound is not "deadlines ever armed": pruning walks from the root while the root is stale, so what
  accumulates is only what was armed and cancelled BEHIND the current live root — and the root is
  by definition the soonest live deadline, so the turn that fires it reclaims them all.
  `Timers_test.cpp`'s *Lazy timer pruning is bounded by the deadlines armed behind the live root*
  measures exactly that: 1000 arm/cancel pairs with no live root leave 0 slots after one turn, the
  same 1000 behind a live root leave 1001, and firing the root takes it back to 0. Eager
  erase-and-reheap would report 1 in the middle case and be O(n) per cancel, which is what made a
  loop with many deadlines quadratic upstream.
- **A WebAssembly program's exit status does not survive the host.** A loop with an armed deadline
  leaves a pending `emscripten_async_call` — that call IS how the host is asked for its next turn —
  and Emscripten takes a runtime keepalive per pending timer, so `main` returning is an IMPLICIT
  exit while the runtime is kept alive: the status is recorded, never published, and node exits 0.
  Measured: `tests/wasm/HostDrivenTimer_smoke.cpp` printed `FAIL ... did NOT fire` and exited 0.
  `-sEXIT_RUNTIME=1` does not fix it (the exit path reads the keepalive counter, not
  `noExitRuntime`), and `emscripten_force_exit()` publishes the status but zeroes that counter, so
  the pending timer's later `runtimeKeepalivePop` aborts with exit 7. **A WebAssembly test says
  what happened on its last line and ctest reads it** (`PASS_REGULAR_EXPRESSION`), which is also
  what `tests/consumer-wasm` had to do.

## Thread affinity, asserted rather than documented

**A rule written beside the code is not a rule the code applies, and adjacency makes that harder
to notice rather than easier** -- a reader who has just read the rule carries it into the lines
below and supplies it from memory. Two instances in this module, both Critical, both found by
review and not by reading:

- `EventLoop::blockOn` carried an `@throws std::logic_error` clause specifying the refusal, and
  its justification, **eight lines above a `break` that did the opposite**. A `@throws` clause is
  a checkable claim about the code beneath it and nothing in this tree checks one.
- `EventLoop::addTimer` neither woke nor armed a host-driven backend, in the function immediately
  after the one whose comment explains why a host-driven backend needs `wake()`.

The second has the more mechanical form, and it is the one to look for: **a member that files work
asks the backend for the turn that will run it, or says in the file why it does not. `addTimer` did
neither.** An invariant visible in sibling implementations and absent in one of them needs no
comment to state and can be checked by reading the family. **When a function joins a family that all
do X, "why does this one not do X" is answered out loud or it is not answered.**

**There is exactly one member that files work and does not ask, and it is named here because a
universal is worth only as much as its exceptions.** `notifyHandleClosing` appends to
`_closedParks` and neither wakes nor arms. It is sound because the turn takes `_closedParks` with
`std::exchange` *before* the wait, and a non-empty batch both forces the timeout to zero and forces
the wait to be entered at all -- the batch is a disjunct of the wait-skip predicate.

**And there is no path that strands a close behind an exchange, which is the thing a reader will
doubt.** Firing an expired deadline only *queues* a ready entry; the callback itself runs in the
drain, and **the drain precedes the exchange within the same turn.** So a close performed by a
timer callback is caught by the turn that ran it, not by a later one. An earlier version of this
paragraph claimed the opposite -- that such a close landed after the exchange and was rescued by
the next turn staying non-idle -- which is an argument where the truth is an ordering, and it was
wrong because I read the sequence of the turn's steps as the sequence in which its work executes.

**An earlier draft of this paragraph said "every member ... asks", which a reader falsifies with one
grep and then distrusts the whole section for.** A rule stated as a universal invites exactly that;
state the exception, or state the property instead of the quantifier. **The same draft cited
`hasPendingWork()` as a second, independent reason -- and that function was deleted from
`EventLoop.cpp` the same day, by the commit landing beneath it.** No line number or function name
appears in this section any more, because both are what decayed: the argument above is stated over
the *order of the steps in a turn*, which is a property a reader can re-derive from the code in
front of them rather than a coordinate that goes stale while nothing fails.

### Ready work wakes; a park arms

**Which primitive a member calls is decided by what it filed, not by which idiom the neighbours
use.** `HostDrivenBackend::wake()` is `scheduleAt(_clock.now())` and `armWakeAt(d)` is
`scheduleAt(*d)`, so the two are not interchangeable: asking for a pump *now* on behalf of a park
due in fifty milliseconds spends a turn finding nothing due and re-arms, and the deadline the
caller supplied is simply discarded.

- **Filed work that is ready now and carries no time** -- `post`, `submit`, `spawn`, `stop`,
  `requestStop`, `requestCancel`, `resumeSoon` -- calls **`_backend.wake()`**.
- **A park filed with a time** -- `registerPark`, and therefore `addTimer`, `delay`, `sleepUntil`
  and every deadline park -- calls **`armHostWake()`**, which arms at `_parks.nextDeadline()`.
- **`schedule` off the loop's thread wakes, and that is the rule rather than an exception to it.**
  What it filed is an entry on the *inbound queue*; the park does not exist yet, and the turn that
  drains the inbound queue is ready to run now. `registerPark` arms for the deadline when the drain
  reaches it. On the loop's own thread `schedule` calls `registerPark` directly and arms.

**This ruling was made the other way first and a measurement overturned it.** Told to use `wake()`
at all three sites, the lane switched two, flagged the third rather than doing it quietly, and
measured the version it had been told to write: `CHECK( soonestDelayMs(host) == 50 )` expanding to
`0 == 50`, twice. **A lane that measures an instruction instead of arguing with it settles the
question in one round**; the instruction had been reasoned from the idiom the neighbours used, and
the neighbours had not filed a deadline.

**The first version of this rule got its own enumeration wrong, and that is the rule underneath the
rule.** It said `addTimer` "was the sixth member of that family and the only one that did not". It
was not the only one -- `registerPark`, `resumeSoon` and `requestStop` did not either, and
`registerPark` is `addTimer`'s own implementation path, so the fix this rule was written to record
sat one level *above* the primitive and covered one of its six call sites. The comment in the code
made the same claim in the same words. A re-review found it by enumerating every member that
mutates `_ready` or `_parks`; re-reading this file could not have found it, because this file was
where the wrong list lived. So:

- **Derive the family from the code every time, including from this page.** Enumerate the
  definitions that touch the shared structure and answer the question for each one. A list written
  into a comment or a rule is the record of an audit somebody once did; it is not an audit, and it
  reads exactly like one.
- **Put the behaviour in the primitive, not in the caller that exposed the gap.** `addTimer`
  received the arming first; `registerPark` is where it belongs. Six call sites reach it, and a
  copy per caller is the next defect along -- two members computing the same answer from the same
  deadline heap.
- **A member that legitimately does not join the family says so where the reader is, and asserts
  what it is relying on.** `armHostWake` does not count `_closedParks`, because `HostDrivenBackend` refuses every handle and so can hold no closed park at all.
  That is unreachable rather than wrong, **no Catch case can turn it red**, and a defensive `|| ...`
  would be worse than the gap: it would let a future backend that *does* gain readiness **work**
  rather than announce itself, which is the opposite of what writing the rule down was for. The
  instrument is `assert(_closedParks.empty())` inside the host-driven branch. **An assert is not
  inert code with no case**; it is an executable statement of the invariant the comment describes,
  and it fires exactly when the premise stops holding. **Its case is a canary mode,
  `core-cpp.hostdriven-canary.closedPark`,** which builds the host-driven backend that DOES accept
  readiness and watches the assertion fire. "Unreachable today" is the argument for such a canary,
  not against it: the assertion exists for the day the premise breaks, and a canary is the only
  place that day can be rehearsed. It was declined once, priced against a whole `WILL_FAIL` process;
  with markers it is a few lines in an existing program (Task B13).
- **The family is a table, not a list in prose.** `HostDrivenLoop_test.cpp`'s "Every member that
  files work asks a quiescent host-driven loop for the turn that runs it" has one row per member
  above, with the DELAY it must ask for -- 0 for ready work, the deadline for a park -- and a row for
  `notifyHandleClosing` asserting it asks for nothing. Dropping `resumeSoon`'s wake reds that row
  alone; dropping `registerPark`'s arming reds it and the three that inherit it, and nothing else. A
  member that joins the family joins the table.

- **G1: exactly one thread dequeues a loop.** `run()`, `runOnce()` and `blockOn()` each claim the
  worker identity, and `runOnce` asserts that no OTHER thread already holds it. `run()` is not
  virtual, and that is the obligation half: a loop that could enter its turn without claiming
  would answer `teardownIsSerialisedWithDispatch()` with `true` from every thread -- the
  false-safe direction, where every guard built on it stays green while checking nothing. Origin:
  [fastcached#668](https://github.com/LASTRADA-Software/fastcached/issues/668).
- **G2: every resumption happens in turn step 2.** Asserted from the FLOW's own frame, because the
  loop asserting its own invariant would pass on a backend that never resumed anything at all:
  `EventLoop_test.cpp` has a flow that records whether the backend was inside `wait()` at the
  instant it resumed.
- **G1 again, for the completion PORT, and it needs saying twice.** A loop is a thing one thread
  drives by construction; a completion port is a thing the operating system INVITES many threads to
  drain, and doing so is its selling point everywhere but here. Nothing about the violation fails:
  the second thread simply takes half the completions, and the defect surfaces as a coroutine whose
  reads sometimes run on the wrong thread, weeks later. So `IocpBackend::wait()` claims a
  `detail::WorkerIdentity` of its OWN -- not the loop's, because a backend is drivable without one
  -- and asserts that no other thread holds it. `core-cpp.iocp-canary.g1` is a program that violates
  it and must die. Origin:
  [fastcached#668](https://github.com/LASTRADA-Software/fastcached/issues/668).
- **G3: helper threads only post.** `post`, `submit`, `schedule`, `requestCancel` and `stop` hand
  work to the inbound queue and wake the backend; step 1 is what runs it, on the loop's thread.
  The work itself records which thread it ran on. A thread-pool wait's callback is a helper thread by
  this rule: `IocpBackend`'s calls `PostQueuedCompletionStatus` and returns, and that is the whole
  body -- no resume, no member of a handler, no allocation. Where the kernel exposes
  `NtAssociateWaitCompletionPacket` the rule is satisfied by there being no helper thread at all.
- **G4: a SOCKET is associated with exactly one completion port, and `ICompletionPort::associate`
  is the only place it happens.** Not because centralising is tidy, but because the kernel's own
  refusal cannot be read: `CreateIoCompletionPort` on an already-associated handle answers
  `ERROR_INVALID_PARAMETER`, which is also what it answers for a closed handle and for half a dozen
  ordinary mistakes, so a caller taking it at face value would condemn a working connection. The
  port keeps the record itself, refuses the second association by name and asserts on it
  (`core-cpp.iocp-canary.g4`). **The other half is the owner's:** an association ends when the
  HANDLE is closed, the kernel says nothing about it, and Windows reuses handle values freely -- so
  a socket being closed calls `ICompletionPort::forget`, or the next socket handed that value looks
  already associated, is therefore associated with nothing, and every operation issued on it
  completes nowhere. That is a hang with no error and no log line.
- **A host-driven loop refuses `run()` and `blockOn()`**, and the refusal is COMPILED under
  WebAssembly rather than removed: a consumer reaches it by mistake, not by design, and a symbol
  simply absent there fails at link time in somebody else's build with nothing to say why.
  `core-cpp.hostdriven-canary` drives both. It is judged by a **marker it prints to `stderr`
  immediately before the forbidden call**, matched with `PASS_REGULAR_EXPRESSION` —
  **not `WILL_FAIL`, which was removed from every registration in this tree by `a48e727`** because
  it inverts *any* non-zero exit and so passes a canary that died before reaching its mechanism.
  Its SIGABRT handler is load-bearing rather than tidiness: both regex properties are defeated by a
  raw signal, so a cleanup deleting "unused" abort handling would turn every canary here into a
  silent pass. The marker goes to `stderr` because `_Exit` flushes nothing.
- **Every loop-thread-only member's assertion is watched firing, one process per member.** Twelve
  members assert `teardownIsSerialisedWithDispatch()` -- the destructor, the turn, `cancelPending`,
  `requestStop`, `spawn`, `addTimer`, `cancelTimer`, `resumeSoon`, `registerPark`,
  `unregisterPark`, `wakeReasonOf`, `notifyHandleClosing` -- and `core-cpp.loop-affinity-canary.<member>`
  drives a native loop on a worker thread and calls that member from another. Its PASS expression
  is the **assertion's own text naming the member**, not a marker: a death on the way there prints
  no such text, and a mode that reached a different member's guard names the other member. That is
  fastcached's `reactor-teardown-gate` (0708dd54), which kept "died" and "the guard refused" apart
  by the assertion's words for the same reason. Proved by mutation on `cl-debug`: removing
  `cancelTimer`'s assertion reds its mode on `was accepted`, and rewording `spawn`'s message reds
  its mode on `Required regular expression not found`. A member that gains the assertion gains a
  mode (Task B13).

## Task ownership

These landed with Task B1, which merged fastcached's `Task.hpp`, `ParkedWork.hpp`, `IExecutor.hpp`,
`ResumeOn.hpp`, `ThreadPoolExecutor` and `AsyncQueue` into `core::async`. They are about coroutine
frames rather than sockets, and every rule below is enforced by a case in
`src/core/async/{Task,ParkedWork,AsyncQueue,ThreadPoolExecutor}_test.cpp`. Count the last one:
it holds the only cases for the resume-or-free rule over a real pool and for a join whose children
finish on another thread, and `CMakeLists.txt` compiles it only where `CORE_CPP_USE_THREADS` is on
— so in the WebAssembly leg those rules are stated and not exercised.

- **The awaiter owns what it awaits, and ownership runs downward.** `Task::operator co_await` is
  rvalue-qualified and moves the frame into the awaiter, which lives in the awaiting coroutine's
  frame for the whole suspension and destroys it at the end of the `co_await` expression. The name
  that produced it is empty afterwards. It is what makes a chain freeable from its root: each
  frame's awaiter owns the frame it awaits, so destroying the root destroys all of it. A borrowing
  awaiter leaves the ownership sideways, and then freeing what an executor holds frees one frame
  out of the middle and leaves the rest unreachable — which is the *indirect only* LeakSanitizer
  signature. Origin:
  [fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025).
- **`unownedRoot` is set at every `await_suspend`, by every coroutine type in the module.** A
  parked coroutine has to answer *may this executor free what it is holding* without walking a
  continuation chain whose links are type-erased, so the answer is copied down one frame at a time
  and a promise states that it carries one through `core::async::CarriesUnownedRoot`. **A
  coroutine type that does not carry it silently answers "somebody owns this"**, and every park
  underneath it then leaks whole: `whenAll`'s and `whenAny`'s runners are a coroutine type of
  their own between a detached root and the task that parks, and adding them to the concept is
  what closed that hole.
- **A task that owns no frame has no result.** `done()` is true for a default-constructed,
  moved-from or released `Task` as well as for a completed one, so `if (t.done()) t.result();`
  reaches the empty case; answering it with `T {}` invents a value the coroutine never produced
  and forces every result type to be default-constructible. It is refused by name
  (`detail::refuseEmptyTask()`) — a **precondition violation reported as an exception**, never
  caught: an `assert` would answer with that wrong value in every Release build, which is the
  defect being removed, and a `catch` would make the empty state a supported path. It is also what
  `syncRun` does for a task still suspended
  after its resume: reading such a task's result is undefined, and freeing its frame tears down
  storage whatever parked it still points into. `syncRunWith(task, retrieve)` takes the park back
  first, so the refusal is the whole of the failure rather than a crash naming nothing. Origin:
  [fastcached#178](https://github.com/LASTRADA-Software/fastcached/issues/178).
- **An executor resumes what it is handed, or frees it — never neither.** `detail::Parked::resume()`
  disowns and resumes in one expression, and a handle it *declines* to resume (already done, or
  empty) has its owned chain root destroyed there rather than dropped. Taking the work out and
  then walking away is a silent leak on the one path the type exists to close.
- **A queue holding submitted work has `detail::Parked` as its element type, never
  `std::coroutine_handle<>`**, and no `submit(ParkedWork)` forwards to `submit(work.resume)`.
  Storing the handle alone drops the claim, and **`ParkedWork::abandon` is a share of ownership,
  not a note about who could free it** — so dropping the last one *frees a chain that is about to
  run*. `ThreadPoolExecutor` did exactly this, kept green by `abandon` being a raw handle nobody
  released, and turned into a use-after-free the moment the claim began refcounting: every ASan tag
  went SIGSEGV until `_queue` became `std::deque<detail::Parked>`. The tell is a `submit` overload
  whose body names `work.resume` and nothing else — it has taken half of a two-part value. The same
  applies to any queue, timer heap or completion list that holds work for later:
  `EventLoop::submit`, `resumeSoon`, `schedule(timePoint, ParkedWork)`. Origin:
  [fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025), and core-cpp's
  own Task B1 fix round (controller ruling R97, commit `ac0ff76`), which is where it was a bug.
- **Both `IExecutor::submit` overloads are pure virtual, which is what actually closes the hiding
  hazard.** A derived class that re-declares one overload of a name hides every other overload of
  it — so an executor declaring only `submit(handle)` hides `submit(ParkedWork)`, fails to override
  it, and is **abstract**: it cannot be instantiated, let alone leak. `using IExecutor::submit;` is
  therefore belt-and-braces rather than the guard, and today every one of them is a no-op, because
  each in-tree executor declares both halves itself. Say it in the class anyway, for the reader and
  for the day an overload is added. The residual shape the keyword does bear on is an
  **intermediate abstract** class declaring one half, and `-Woverloaded-virtual` — on GCC *and* on
  clang, gated by `CORE_CPP_GCC_OR_CLANG` in `cmake/CoreCppToolchain.cmake` — and clang-tidy's
  `bugprone-derived-method-shadowing-base-method` each refuse it outright, as errors here. Keep
  `ParkedWork_test.cpp`'s negative control, which is a plain non-inheriting type offering only
  `submit(handle)` — what the hiding leaves reachable — precisely because the inheriting form no
  longer compiles in this tree; it proves the concept discriminates rather than accepting
  everything. Origin:
  [fastcached#1041](https://github.com/LASTRADA-Software/fastcached/issues/1041).
- **A queue or a resource never resumes its consumer inline.** `AsyncQueue::push()` and `close()`
  hand the parked handle to `IExecutor::submit` and return. A producer commonly pushes while
  holding a lock of its own, and a queue that resumed inline would run the consumer's next step
  inside that lock, on the producer's thread, at a point where the consumer may call back into the
  producer's object. The queue's own mutex is never held across `submit`, so no lock-order
  inversion is expressible.
- **A stop callback is registered before the park is published, and never under the lock the
  callback takes.** A token that is already stopped runs the callback in the `StopCallback`
  constructor, on the registering thread: under the lock that is a deadlock, and after the park is
  published it hands the handle to an executor that may resume a coroutine whose `await_suspend`
  has not returned. Registering first means an inline run finds no waiter and only records the
  cancellation, which the re-check under the lock then reads.
- **Where a cancel and an answer arrive together, the answer wins.** `AsyncQueue::pop()` resolves
  an item first, then a `close()` (as `std::nullopt`), and only then throws `OperationCancelled`:
  an item taken out of the queue has nowhere to be put back, and `close()` is the resource saying
  *no more items, ever*, which a consumer answers by returning. Throwing over a close would make
  an ordinary shutdown unwind, and a `DetachedTask` that does not catch it ends the process.
- **A pool that is stopping drains rather than drops, and resumes inline rather than refusing.**
  An unresumed coroutine runs no destructor and frees no frame, so a queue discarded at shutdown
  leaks every job in it along with whatever it holds — a socket, a temporary directory, a slot in
  somebody's counter. `ThreadPoolExecutor` therefore has nothing for `ParkedWork::abandon` to
  answer, and says so where it ignores it.
- **An `await_ready` stays trivial -- a member read, a comparison of members, or a constant -- and
  a decision that needs a call moves into `await_suspend`, which may decline to park.** MSVC 19.44
  targeting ARM64 (cl 19.44.35228, `cl-release`, fastcached's `windows-11-arm` leg) dropped the
  enclosing `try` of a `co_await` on a TEMPORARY awaiter whose `await_ready` made a virtual call:
  the resume function's exception table declared the coroutine's own catch-all and no try block
  for the handler the source wrote, so the `OperationCancelled` that `await_resume` threw passed a
  typed `catch` and `catch (...)` alike. It was caught when `await_ready` made no call, when the
  value was read in the constructor, and when the awaiter was a named local. `EventLoop`'s
  `DelayAwaiter` had exactly that shape -- `_loop->clock().now()` -- and so every `delay` and
  `sleepUntil` in the tree, because Task B12 moved `TuiRuntime` onto it after fastcached had fixed
  the runtime's own copy. The same release made eleven more trivial: `TokenDelayAwaiter` (a clock
  read), `Task`'s two awaiters and `ResultAwaitable` (`handle.done()`, and a nested
  `await_ready`), `JoinAwaiter` and `AsyncQueue`'s `PopAwaiter` (a container's `empty()`, a lock),
  `SlotPark` and `SerialGate::Awaiter` (a lock), and `TuiRuntime`'s four input and agent awaiters
  (`hasBufferedInput()`, `agentPending()`). **Move the question, not only the call:** ask it in
  `await_suspend` BEFORE the flow's stop token is read or a callback registered, or a flow that is
  already stopped starts throwing where `await_ready`'s answer used to resume it normally --
  `SleepUntil_test.cpp`'s stopped-flow case is the one that notices. What guards it, stated as
  small as it is: `static_assert(core::async::awaitReadyIsConstantFalse<A>())` for each of those
  with a name outside its file but `ResultAwaitable` (a member read), in the module's test -- the
  helper cannot name a type with internal linkage, so `TokenDelayAwaiter`, `SlotPark` and
  `SerialGate::Awaiter` have the scan below and nothing else -- which makes a call there fail to COMPILE on GCC 14, Clang 20 and
  MSVC 19.51 or newer, and asserts nothing on the compilers that predate P2280 (MSVC 19.44 among
  them); `await_ready` stays a `const` member, not a `static` one, because clang-tidy's
  `readability-static-accessed-through-instance` reports a static one at every `co_await` in every
  caller (391 findings here when it was tried); `scripts/check-await-ready.py`, which refuses a call or a
  construction in any `await_ready` body under `src/` and `tests/` but cannot see an overloaded
  operator; and the `windows (cl-release-arm64)` leg, the only one on which the miscompile itself
  is observable. None of the C++ is wrong, so no x64 leg and no sanitizer can fail for it. Origin:
  [fastcached#1546](https://github.com/LASTRADA-Software/fastcached/issues/1546).

## Sockets

- **There is one TCP client, `TcpClient`. Do not write a second.** fastcached had three, and
  the rot was in the one no job built: it resolved no hostnames, had no bounds, no SIGPIPE
  protection, and did not compile on POSIX at all. Origin:
  [fastcached#84](https://github.com/LASTRADA-Software/fastcached/issues/84).
- **SIGPIPE is suppressed per socket, never process-wide.** An ignored disposition is inherited
  across `exec`, so a process that ignores SIGPIPE hands that to every program it launches:
  `SO_NOSIGPIPE` on macOS and the BSDs, `MSG_NOSIGNAL` per send elsewhere. Removing a
  process-wide net exposes every raw sender that leaned on it, and they are found by grep, not
  by test. Origin: fastcached wire-and-protocol, "Sockets".
- **Keepalive is armed per dial (`DialOptions`), not in the function every socket passes
  through**, or it changes when every idle connection is dropped. Bare `SO_KEEPALIVE` inherits a
  two-hour default and reads back as armed; set the intervals first and the flag last. It
  answers "is this connection dead", never "is this peer working". Origin:
  [fastcached#247](https://github.com/LASTRADA-Software/fastcached/issues/247).
- **A faster failure nobody can name is not an improvement.** Expiry closes the socket, so "this
  side gave up" and "the peer went away" reach the caller as one broken socket. The deadline
  records that it fired, and the cause is asked of the timer, never inferred from elapsed time.
- **A child process inherits this process's sockets, and neither platform stops it.** A plain
  `accept()` returns an inheritable descriptor, Windows sockets arrive inheritable, and a
  compiler holding a client's connection keeps it from closing. Close-on-exec is armed where
  every socket passes; a Windows spawn names what it hands over
  (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`) rather than marking what it does not.
- **A listening socket claims its address exclusively.** On Windows `SO_REUSEADDR` lets a second,
  unprivileged process bind a port a live socket already serves, with which one answers
  undefined; the spelling there is `SO_EXCLUSIVEADDRUSE`. Sharing a port is an explicit option
  (`SO_REUSEPORT`, POSIX). A `setsockopt` that carries a security property fails the bind
  rather than being ignored. Origin:
  [fastcached#85](https://github.com/LASTRADA-Software/fastcached/issues/85).
- **A platform socket error is classified in one table.** A second copy lacks a row, and a
  firewall's `EACCES` becomes an unclassified `SystemError` no caller can match. The table is
  `detail::classifySocketError` (`posix/SocketErrors.cpp`, `windows/SocketErrors.cpp`), and every
  transport and both platforms' dial read through it; on Windows `detail::fromWinsockError` adds
  the two things a WSAE* table cannot hold. Until Task B13 `PosixSocket`, `WindowsSocket` and the
  POSIX dial each kept a private switch -- the POSIX dial held `EPERM` and `EAFNOSUPPORT` rows the
  shared table lacked -- and `SocketErrors_test.cpp`, one per platform, now asserts every row.
- **EOF means "the peer has finished sending", not "the peer is gone".** A server answers what
  is already determined and abandons what is still pending, which is what a reference Redis
  was measured doing. `ISocket::shutdownWrite` exists so the question can be asked in
  production. Origin: [fastcached#671](https://github.com/LASTRADA-Software/fastcached/issues/671).
- **A disconnect watch reads the count:** an error is an abortive close, `0` is EOF, and EOF is
  the ordinary way a client leaves. Both arms reach one outcome, so a green suite proves
  nothing: delete an arm and see which case fails, and keep a control that must survive an open
  write side. Origin:
  [fastcached#673](https://github.com/LASTRADA-Software/fastcached/issues/673).
- **A peer that sent nothing asked nothing:** it is closed, not refused, and "sent nothing" is
  two states (idle past the deadline, gone). A deadline expiry is two error codes across
  platforms (`EAGAIN` on POSIX, `WSAETIMEDOUT` on Winsock), so it is asked through one
  predicate, `isDeadlineExpiry`. Origin:
  [fastcached#824](https://github.com/LASTRADA-Software/fastcached/issues/824).
- **A TLS peer says "finished sending" with a record.** A well-behaved peer sends `close_notify`
  and then FIN, so the raw socket sees bytes and answers `>0`. TLS `waitReadable` decrypts with
  `SSL_peek`, which removes nothing the next `read` would have returned. Origin:
  [fastcached#712](https://github.com/LASTRADA-Software/fastcached/issues/712).

## The socket contract: three rules, and the guards that hold them

Task B6 merged fastcached's `EpollSocket`/`KqueueSocket` and contour's `PosixSocket` into one
socket whose operations are frame-free `ResultAwaitable`s. These are the rules that merge had to
get right, and each one is a defect that has already happened.

- **A socket operation allocates no coroutine frame, and the retry loop is therefore NOT in the
  awaiting coroutine.** A `co_await` expression suspends exactly once, so `await_resume` cannot
  re-park — while `write` must send every byte of its buffer, and a level-triggered poller may
  report a descriptor readable whose `recv` still answers `EAGAIN`. The loop runs where the
  readiness is delivered: a frameless `core::net::ReadyCallback` park on the loop, completing the
  awaitable when the operation finally answers. Anything that needs to STORE an operation pays the
  frame explicitly through `core::async::asTask`.

- **A cancel from the FLOW throws; a cancel from the RESOURCE is a value.** `close()`,
  `cancelRead()` and a closed listener answer `NetErrorCode::Cancelled` as a
  `std::expected` value, because the flow is alive and asked a question about a socket that has
  gone away. The awaiting flow's own stop token throws `core::async::OperationCancelled`, because
  the flow is being unwound and its `co_await` has no value to hand back. **A merge that collapses
  them makes a closed socket indistinguishable from a cancelled flow**, and every caller that
  branches on the difference — a connection loop deciding whether to reconnect — takes the wrong
  arm. Design spec §2 item 5.

- **If a receive already produced a value, the value wins over a stop.** A receive that took bytes
  out of the stream cannot un-take them: they exist nowhere else. So `await_resume` tests the
  result before it tests the token, never the other way round. Origin:
  [fastcached#884](https://github.com/LASTRADA-Software/fastcached/issues/884). The case that pins
  it must request the stop BEFORE the completion lands — the obvious spelling, complete then stop,
  resumes the flow synchronously and asserts nothing at all, measured.

- **A DESTRUCTOR abandons a parked operation; `close()` resolves it.** `close()` can hand the flow
  a value because the socket is still there to look at. A destructor cannot: by the time the flow
  runs, `this` is gone, so `ResultAwaitable::abandon()` makes `await_resume` throw whatever the
  flow's own token says. Unwinding never re-enters the body, which is the only safe thing to do
  with a frame whose socket has been destroyed. This is `FdWakePolicy::Cancel` in the shape an
  awaitable can express it.

- **Detach the operation FIRST, complete it LAST, and touch no member afterwards.** Completing
  resumes the parked coroutine, and a coroutine that OWNS the socket runs to its end and destroys
  it before the completion returns. `PosixSocket::close` takes BOTH operations into locals before
  it settles either, so the second settle does not read a `this` the first one freed. The ASan
  report that established this is recorded on fastcached's `EpollSocket::Close`.

- **A socket has ONE read operation and ONE write operation, and the read verbs share theirs.**
  `read`, `readWithFd` and `waitReadable` all claim the read slot; arming any over a parked one
  drops that coroutine, which is then never resumed and never freed — no assertion, no error, no
  log, and a leak proportional to traffic. `core::net::contract::claimReadSlot` and
  `claimWriteSlot` are the tripwires; they are **public**, because a transport outside this
  library is under the same rule. Origin:
  [fastcached#663](https://github.com/LASTRADA-Software/fastcached/issues/663) and
  [fastcached#893](https://github.com/LASTRADA-Software/fastcached/issues/893).

- **The guards are Debug-only, and that is a decision rather than an omission.** In a release
  build each is what it replaced: one store, or nothing. Refusing the operation instead would turn
  today's silent leak into a broken connection on a live path, which is the worse trade — so the
  fix for a caller that trips one belongs at the caller. Each is watched refusing by
  `ctest -R socket-contract-canary`, which drives a REAL socket into the guard and must die:
  asserting the assertion would prove `assert` works and say nothing about whether a transport
  ever reaches it.

- **The verb records the operation; the AWAIT arms it, and the gap between them is reachable.**
  `[[nodiscard]]` makes dropping a socket operation a warning, not an impossibility — and a
  consumer building with different flags does not even get the warning. So `ResultAwaitable`'s
  destructor retires an operation that was never awaited, and every consumer of the slot tolerates
  an entry whose awaitable is null. Found by the read-slot canary: without this, `auto op =
  sock->read(buf);` in a scope that returns early left the slot naming freed storage, and the next
  `close()` dereferenced null — in Release, where the guard is not there to catch the spelling that
  produced it.

- **`waitReadable`'s count is the contract, not a hint.** `0` means the peer has closed its write
  side and a `read` here returns EOF; `>0` means bytes are pending. The default answers `1`,
  which is the fail-safe direction: a transport that cannot tell must not claim EOF, because a
  false `>0` costs one `read` that discovers the truth while a false `0` tells a caller its peer is
  gone. Origin:
  [fastcached#677](https://github.com/LASTRADA-Software/fastcached/issues/677).

- **`cancelRead` retires whatever is parked NOW, which is not the same as being idempotent.** A
  retirement that completes its victim inline resumes that coroutine before the call returns, so a
  flow that arms its next read there leaves a NEW operation in the slot and a second call retires
  THAT one. The declaration once claimed the stronger word. Origin:
  [fastcached#1233](https://github.com/LASTRADA-Software/fastcached/issues/1233).

- **A receive deadline is a consumer of the loop's timers, never a wake of its own.**
  `setReceiveDeadline` arms an `EventLoop::addTimer` on the one deadline heap `computeTimeout` and
  `armHostWake` already read, and the read that answers cancels it. A socket that computed a "next
  wake" for itself is the second mechanism the timer rule exists to prevent, and its symptom is a
  wait that is too long — a hang, not a failure. See *Timers: one mechanism, and it does not poll*.

- **A stop callback may run on ANY thread, so it names a loop park and never the socket.** The
  awaitable is told the park id at arm time — on the loop's thread, strictly before the callback
  can be registered, which is what makes that id safe to read without being atomic — and the
  callback does nothing but `EventLoop::requestCancel`. The loop resolves it on its own thread and
  calls the socket back with `ParkWake::Cancelled`. A stop callback that reached into the socket
  directly would be touching the loop thread's members from a watchdog's.

## Dialling, and the backends underneath

- **A synchronous dial spends a thread the caller does not own.** A loop thread dials through
  `makeConnector(loop, resolver)`, never `BlockingConnector`. "The caller has nothing to do until
  it connects" is true of the caller and false of the thread, which serves every other
  connection. fastcached paid for the opposite decision with a thread per peer and a shutdown
  that hung forever inside `send`.
- **A name is never resolved on a loop thread, and the SEAM is what enforces it rather than the
  thread.** `getaddrinfo` takes no timeout, so a wedged resolver stalls everything behind it for as
  long as the platform's resolver library feels like — on an event loop that is every coroutine on
  it, including the ones with nothing to do with the network. Every dial therefore goes through an
  injected `IAsyncAddressResolver`; `ThreadedAddressResolver` is merely the implementation that
  ships, with a fixed pool of two (one would let a single five-second SERVFAIL head-of-line-block
  every dial behind it), a bounded queue that answers `WouldBlock` rather than waiting for room,
  and a fast path that never hands a LITERAL address to a thread at all. **A null loop means
  resolve inline** — with nowhere to submit a result back to, offloading would park a coroutine
  nothing could resume — which is also what keeps an inline resolver drivable by
  `core::async::syncRun`. The seam is the deliverable because it is what makes the rule TESTABLE:
  an injected resolver records the thread it was called on, and `Connector_test` asserts that
  thread is not the loop's. Origin:
  [fastcached `Net/IAsyncAddressResolver.hpp` at 0708dd54](https://github.com/LASTRADA-Software/fastcached/blob/0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21/src/FastCache/Net/IAsyncAddressResolver.hpp).
- **A dial checks `SO_ERROR`; it never trusts which callback fired** (Ruling R101). Readiness is
  not success: a refused connect makes the socket ready too, and `getsockopt(SO_ERROR)` is the only
  thing that says which happened. This is not defensive coding, it is the portable connect idiom —
  and the failure mode is platform-specific and silent. On Linux a failed connect can reach
  `onError` with neither direction set; on macOS the kqueue write filter fires with `EV_EOF`, the
  backend reports `Writable`, and a dial that read that as success hands its caller a socket whose
  FIRST WRITE fails, days later, on one platform. A parity suite that stops at "the dial returned"
  cannot see it, which is why `ReadinessDial_test` and `Connector_test` assert `ConnRefused`
  against a closed port on every backend the platform builds. The same sentence generalises and is
  worth keeping in that form: **you never learn what went wrong from the readiness bits — you do
  the `read`, the `write` or the `getsockopt`, and let that report.** Origin:
  [fastcached `Net/ReactorDial.hpp` at 0708dd54](https://github.com/LASTRADA-Software/fastcached/blob/0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21/src/FastCache/Net/ReactorDial.hpp),
  and [core-cpp ruling R101](https://github.com/contour-terminal/core-cpp/blob/master/.superpowers/sdd/2026-09-18-core-cpp/task-B3-fixround2.md).
- **A dial's per-attempt state lives in the dialling coroutine's own frame**, not in the connector.
  Its address is stable for exactly as long as the loop can reach it and it disappears with the
  attempt, where a connector holding a slot per dial would let a dial that timed out while still in
  flight tie one up. The corollary is the ordering every settle obeys: retire the park FIRST — a
  level-triggered backend still reporting the handle would otherwise dispatch, be ignored, and
  report again on every wait, which is a busy loop rather than a leak — and resume LAST, touching
  nothing afterwards, because the resumed coroutine may run to its end and destroy the frame the
  state lives in.
- **A socket option that bounds a blocking call is inert on a socket whose reads suspend.**
  `SO_RCVTIMEO` belongs to the blocking transports only; a loop caller arms a deadline that
  closes the socket, which also bounds a peer dribbling one byte at a time.
- **A dial's budget is divided across the resolved candidates.** Giving each the full timeout
  multiplies the bound by the number of addresses; giving the first all of it defeats the
  fallback when that candidate black-holes. Windows silently drops a connect to a closed
  loopback port, which is how this was found.
- **Coroutine parameters are owning values**: a dial takes `std::string`, not
  `std::string_view`, because the frame outlives the call expression
  ([`cpp-guidelines.md`](cpp-guidelines.md)).
- **A readiness backend reports errors that were not asked for.** epoll delivers `EPOLLERR` and
  `EPOLLHUP` whether or not they were requested, and a failed connect can arrive with neither
  direction set; dropping them is a hang and a loop spinning at 100% CPU with nothing logged.
  The choice of callback is a pure function (`selectReadinessCallback`) so it is tested without a
  kernel.
- **A watched direction beats `onError`, and `Readiness::Failed` is best-effort** (Ruling R101).
  `selectReadinessCallback` returns exactly ONE callback, so the older failure-first order was data
  loss rather than a routing preference: a peer hangup on a socket with unread bytes arrives as
  `POLLIN|POLLHUP` on poll and epoll, and the moment a handler set `onError` the reader stopped
  being woken and those bytes were never read. `onError` now takes a failure only when no watched
  direction accompanies it — the `POLLERR`-only failed connect it exists for. The reason this is
  *correct* rather than merely safer: **no platform lets a caller learn what went wrong from the
  readiness bits.** You are woken, you call `read()` or `write()`, and that reports the error. A
  reader needs the wakeup so it can read 0; a dial needs it and then checks `SO_ERROR`. Neither
  consults which callback fired, so nothing above a backend may branch on `Failed` for
  correctness — it is a hint, and the backends genuinely disagree about it.
- **Do not "fix" the divergence by mapping `EV_EOF` to `Readiness::Failed`.** It is the first thing
  the next reader of this will reach for, and it is a regression: `EV_EOF` on a kqueue read filter
  means the peer called `shutdown(WR)`, which is an ordinary EOF, so macOS would begin reporting
  every normal close as a failure. The divergence is not a defect to be unified. What is portable,
  and what `BackendParity_test` now pins, is that **a peer hangup wakes the direction the handler
  watches, on every backend** — with the buffered bytes still collectable, which is the property
  every handler actually depends on.
- **Service at most one callback per registration per wait**: a readable callback may free
  the object the writable callback lives in. Level-triggering re-reports what was skipped. The
  merge is in `detail::ReadyBatch::add`, not in each backend, because only kqueue's kernel
  duplicates — it answers per (descriptor, filter) — and a rule enforced in the one backend that
  needs it is a rule the next backend does not have.
- **`detach()` withdraws the handler from the batch a wait in flight is walking.** Dropping a
  kernel registration stops FUTURE reports and does nothing about an entry the wait has already
  written; a callback that detaches another handler and frees its owner leaves a dangling entry
  the same walk reads. It is done at *detach* rather than validated at dispatch because the
  handler is still alive at that moment, so the comparison is against a live address and needs no
  generation counter — a scheme that validated a dequeued pointer after the fact would have to
  survive address reuse, which a bare pointer cannot. So `detach()` is called BEFORE the owner is
  freed, on every path. Origin:
  [fastcached#475](https://github.com/LASTRADA-Software/fastcached/issues/475).
- **`setInterest` reports the kernel's refusal; `attach` cannot.** kqueue has no "register with no
  filters" operation — a filter IS the registration — so an `attach` that claimed to have
  registered the descriptor would be telling the truth on epoll and not here. `attach` answers
  that the handler and the backend are usable together; whether the kernel accepted it is what
  `setInterest` answers, which is why it returns `std::expected<void, NetError>` carrying the
  errno. A registration the caller believes succeeded and the kernel never made parks a flow with
  nothing left to resume it: no message, no stack, just a hang. Origin:
  [fastcached#1054](https://github.com/LASTRADA-Software/fastcached/issues/1054),
  [fastcached#1057](https://github.com/LASTRADA-Software/fastcached/issues/1057).
- **Muting means silent, on every backend.** `Interest::None` keeps a registration attached and
  reports nothing for it — not even the hangup and error conditions a kernel volunteers whatever
  was asked for. poll(2) needs a negative descriptor in the `pollfd` (`events == 0` does not mute
  it), and epoll needs the descriptor out of the set entirely, because `EPOLLHUP` on a muted
  registration is level-triggered and spins the pump. Windows and kqueue are silent already.
- **A loop that does not own its thread is PUMPED, and the host is what waits.**
  `HostDrivenBackend` has no readiness at all — `attach` and `setInterest` answer
  `NetErrorCode::Unsupported`, because accepting would park a flow on a registration nothing can
  report — and its `wait()` returns at once. The timeout is not dropped but delegated, through
  `armWakeAt`. Two requests before the host gets a turn are ONE pump, or a burst of `post()`s
  queues a browser timer each and the page spends its frame budget in the scheduler; a request
  earlier than the pump already out is scheduled BESIDE it, because a host's timer cannot be
  retracted and a spurious pump costs one empty turn where a missed one is a hang. The schedule is
  cleared before a pump runs, not after, or the turn it drives cannot arm the next one. It is
  portable and tested on every platform over `testing::ManualHostScheduler`: the browser is one of
  its hosts, not its definition, and a behaviour observable only in a node run is one nobody reads.
- **The wakeup channel belongs to the backend, not to the loop.** `IoBackend::wake()` is the one
  member of that interface another thread may call, and every backend that blocks needs the same
  mechanism behind it (`detail::WakeupChannel`). A wakeup raised with no wait in flight is not
  lost: the channel stays readable and the next wait returns at once. A lost one is a wait that
  never ends, which shows as a shutdown that hangs and nowhere else, so both halves are parity
  cases.
- **A test double whose interface says "callable from any thread" must be.** `TestLoop::submit`
  is thread-safe for the same reason `EventLoop::submit` is, or every cross-thread case is forced
  onto a real loop.
- **A loopback connect usually completes inline**, which skips the whole readiness path, so a
  dial test that stops at "connected" exercises none of it. Connector tests move bytes and
  arrange the read to park; only a parked read proves the registration exists.
- **A completion port has no readiness, so a backend over one SYNTHESISES it, and each kind of
  handle needs its own source.** A waitable HANDLE -- console input, an event,
  `platform::SystemPipe`'s wakeup -- gets a thread-pool wait whose callback only posts; socket
  readability is a zero-byte `WSARecv`, which completes for data, for EOF and for an error alike and
  is `Readiness::Readable` for all three, because this layer wakes the reader and the reader's own
  `recv` is what says which; socket writability has no completion at all, so it goes through
  `WSAEventSelect` for `FD_WRITE` and then through the first bridge. **This is the whole of the
  merge:** contour could wait on a console handle and capped at 64 of them, fastcached could scale
  and express a completion and kept a second coroutine runtime because its port could not park on a
  console. `BackendParity_test` registers `CONIN$` on every Windows backend, so one wait serving a
  console and a socket is a checked property rather than a claim.
  `NtAssociateWaitCompletionPacket` is an optimisation taken only when a startup `GetProcAddress`
  probe finds it -- never a link against `ntdll`, which would put an undocumented import into every
  consumer's executable -- and the probe failing is an ordinary answer. Origin: the design spec,
  [Part I](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md),
  "IOCP readiness bridging".
- **`lpOverlapped` points at a backend-owned refcounted slot, never into the handler.** On a
  readiness backend nothing of the caller's travels into the kernel; on a completion port a pointer
  does, and it comes back on a later turn -- after a cancel, after a detach, after the handler's
  owner has been freed. So it points into a `detail::ReadinessSlot` the BACKEND owns: the handler
  holds one share for the length of its registration (`ReadinessHandler::slot`), each armed
  operation holds another, and a packet arriving after the detach finds the slot `retired()` and
  drops **without reading the handler pointer at all**. Measured: dropping the two retirement checks
  reports `heap-use-after-free` in `selectReadinessCallback`, reading the freed `ReadinessHandler`.
  The same hazard one layer up is what forced fastcached's operation to hold the socket's `Impl` by
  `shared_ptr` rather than reaching back through the socket. Origin:
  [fastcached#465](https://github.com/LASTRADA-Software/fastcached/issues/465).
- **One operation node per ARM, stood down rather than reused.** A retracted operation's
  `OVERLAPPED` belongs to the kernel until a later turn delivers its abort completion, so the next
  arm gets a fresh node and the retired one lives until its packet is reconciled. `CancelIoEx` does
  not take an operation back, it ASKS for it back. Reusing the node turned an abort into a spurious
  EOF on a healthy socket upstream, which is that ticket's other half -- bytes already received beat
  a later stop -- seen from the node's side. Origin:
  [fastcached#710](https://github.com/LASTRADA-Software/fastcached/issues/710),
  [fastcached#884](https://github.com/LASTRADA-Software/fastcached/issues/884).
- **On a completion port, level-triggering is BUILT rather than inherited.** An overlapped operation
  is one-shot: it completes once and the kernel forgets it, where poll, epoll, kqueue and
  `WaitForMultipleObjects` re-report a condition nobody consumed. So `IocpBackend` re-arms every
  watched registration at the top of every `wait()` -- before the block, never after the dispatch,
  because a callback may detach its own registration and an arm issued for that one is an operation
  nobody will ever collect. Worth knowing that this was invisible: deleting the re-arm left all 166
  cases in the suite green, because every other registration is armed by `setInterest` and
  dispatched exactly once.
- **A completion reaches its owner through the loop's turn, never from inside the port's
  dequeue.** fastcached's `IocpCompletion` carried a dispatch pointer the reactor called, and that
  call resumed the waiting coroutine -- Rule 1 broken on the one event where a socket most wants to
  resume its reader. Here the owner parks on its operation (`HandleKind::Completion`, whose handle
  is the operation's address), the port marks the operation completed and reports that park, and
  the loop runs the owner's callback in turn step 2. The one call the port still makes into the
  owner is the dequeue hook, which only lets go of the operation's own share, and is the last thing
  the port does with the pointer. A port recognises an owner's packet by the record
  `ICompletionPort::beginOperation` made BEFORE the Winsock call -- a packet can be queued the
  instant the call is issued -- and a packet nobody announced is dropped and said, because reading
  it as either kind is reading an arbitrary struct. Origin: Task B7b,
  [fastcached#475](https://github.com/LASTRADA-Software/fastcached/issues/475).
- **On a completion model, a stop, a receive deadline or a dial deadline ASKS for the operation
  back and lets the completion answer.** The kernel performs the operation, so the completion is
  the single writer of its outcome: `CancelIoEx` makes it complete with an abort or with whatever it
  had already done, and that is what the flow resumes with. Settling at the stop instead throws
  away bytes the kernel had already taken out of the stream
  ([fastcached#884](https://github.com/LASTRADA-Software/fastcached/issues/884)). The loop DETACHES the park a
  stop came through, so the owner registers a fresh one to hear the abort on, and the port marks an
  operation completed whether or not a park is listening, so a completion that landed in between is
  reported as soon as the new park exists. The two exceptions are the ones that cannot wait:
  `close()` resolves a parked operation at once (closing is what aborts it, and the operation's
  storage outlives the wait on its own share), and a loop being torn down abandons it. Origin: Task
  B7b; the hand-off from Task B8 (`task-B7b-handoff-from-B8.md`) stated it for the dial first.
- **An overlapped operation is never handed the caller's memory.** The caller's buffer is borrowed
  for as long as its operation is AWAITED, and every abandon path ends that borrow early -- an
  awaitable destroyed while parked, a socket destroyed under one, a loop torn down -- while
  `CancelIoEx` only asks for the operation back. A receive the peer's data reaches first then
  completes into freed memory, and AddressSanitizer reports nothing, because the kernel's copy is
  not an instrumented access. So `IocpSocket` receives into a buffer its operation node owns and
  copies out on delivery, and copies a send in when it issues it; only an operation that had to
  wait pays for it. `IocpSocket_test.cpp`'s *an abandoned read never lets the kernel write into
  the caller's buffer* observes the write directly, with a buffer that outlives the read: with the
  retire path's `CancelIoEx` removed and no owned buffer, it reads `3 == 0`. Origin: Task B7b's fix
  round, upstream's residual hazard, not exempted by being upstream's.
- **Windows buffers what a non-blocking send is given, far past `SO_SNDBUF`** -- measured, 8MiB taken
  at once by a socket with a 4KiB send buffer and a peer that never reads. `IocpSocket` tries each
  read and write without an operation first, as libuv does, so a write that fits returns inline and
  costs the same turns as on every other socket; the price is that "a payload big enough to park"
  does not exist here. A case that needs a PARKED write fills the send window itself, until the
  kernel answers `WSAEWOULDBLOCK`, and only then issues the write under test. Origin: Task B7b.
- **IOCP specifics:** an accept must be awaited while it is outstanding, or an early completion
  is dropped; `ConnectEx` needs a `bind` to the family's wildcard first and
  `SO_UPDATE_CONNECT_CONTEXT` after; and `OVERLAPPED::Internal` is an `NTSTATUS`, not a Winsock
  code, so it is converted with `WSAGetOverlappedResult`.
- Origin for this section: fastcached wire-and-protocol, "Dialing, and the reactor underneath
  it".

## Socket and coroutine lifetime

- **`close()` may be the last thing that runs on a socket, so it touches no member after it
  completes an awaitable.** Completing one resumes the coroutine that may own the socket, which
  can run to its end and destroy it before `complete` returns. Detach every parked awaitable
  first, complete them last. Origin: fastcached wire-and-protocol, "Socket and coroutine
  lifetime".
- **A decorator whose `close()` retires more than one operation checks it is still alive between
  them.** "Complete last" cannot be met by ordering when there are two completions, because either
  can destroy the decorator and everything it owns. `SplitSocket` holds a liveness token and returns
  once it has expired; the operations it did not reach were abandoned by the destructors that ran.
  Origin: Task B10, a SIGSEGV in `SocketDecorator_test`.
- **An object an event loop owns is destroyed on that loop's thread, or with the loop
  stopped:** `EventLoop::teardownIsSerialisedWithDispatch()`, which is
  `!running() || isOnWorkerThread()`. Every socket, listener and dial destructor asserts it
  (guarantee G5), on every backend: the defect is portable even where only one backend's timing
  exposes it. A drain that waits for the loops does not mean the loop has stopped; posting the
  teardown and waiting for it hangs the ordinary case; deferring destruction past the join is
  the third option. **Match the assertion, never the test case that happened to be running**: a
  race shared by every case's teardown fails in a different case each run. A regression test
  removes the race instead of waiting for it. Origin:
  [fastcached#668](https://github.com/LASTRADA-Software/fastcached/issues/668),
  [fastcached#737](https://github.com/LASTRADA-Software/fastcached/issues/737),
  [fastcached#840](https://github.com/LASTRADA-Software/fastcached/issues/840),
  [fastcached#885](https://github.com/LASTRADA-Software/fastcached/issues/885).
- **A loop resumes what it parks, or frees it, and it frees only what nothing else owns.**
  `schedule` and `submit(std::coroutine_handle<>)` *borrow*: a blanket destroy at teardown is a
  double free for a caller that still owns its `Task`. Ownership travels with the park as
  `ParkedWork::abandon`, non-empty exactly for a chain rooted in a `DetachedTask`, and what is
  freed is the chain's root, because ownership in a `Task` chain runs downward. Resuming at
  teardown is not an alternative: a bounded wait re-parks, so it spins. The release is folded
  into the resume (`detail::Parked::resume()` disowns and resumes in one expression), and the
  teardown loops to a fixpoint, because freeing a chain can park again. LeakSanitizer reports
  the leaked set as *indirect only*, which is the signature. Origin:
  [fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025),
  [fastcached#1054](https://github.com/LASTRADA-Software/fastcached/issues/1054).
- **An object that parks flows on a loop takes them back in its own destructor, and is therefore
  destroyed before the loop.** A long-lived flow parked on `waitReadable` holds a frame that names
  the object that started it, and the loop will resume that frame whenever its handle next becomes
  ready. So the flow's frame belongs to the object -- `submit`, which BORROWS, not `spawn`, which
  hands it over and offers nothing that gives it back -- and the destructor calls `cancelPending`
  on each one. `cancelPending`'s answer is an ownership transfer: true means the loop no longer
  has it -- nothing of it queued, parked, or registered with the backend -- **and the destructor
  destroys the frame then**, while every member it names is alive, rather than leaving it to a
  member destructor that runs after some of them are gone. The flow's loop must test the flag
  the destructor sets BEFORE its first `co_await`, so that nothing resumed during teardown parks
  again. Until [core-cpp#41](https://github.com/contour-terminal/core-cpp/issues/41) was fixed,
  a waiter QUEUED after readiness came back with its park still filed and attached -- the
  ready-queue branch of `cancelPending` returned before the branch that detaches -- and the
  owner had to resume it once so that `await_resume` would unregister it. The fix is in the
  primitive, and the resume was deleted from `~TuiRuntime` with it; the regression case is
  `TestLoop_test.cpp`'s "cancelPending on a waiter queued after readiness takes its park too".
  `core::tui::runtime::TuiRuntime` is the first such object; its four source flows are one per
  handle. Origin: Task B12.
- **A derived interface that re-declares one overload hides every other overload of that name.**
  `IReactor` re-declared `Submit(handle)` and not `Submit(ParkedWork)`, so every call through the
  derived type bound to the borrowing overload, and nothing diagnosed it. The shape is
  unexpressible here because both `async::IExecutor::submit` overloads are **pure** — see the
  executor rule above — so the hiding class stays abstract; the `using IExecutor::submit;` every
  derived class writes and the compile-time check that `submit(ParkedWork{})` selects the owning
  overload are the belt and the braces, not the trousers. Origin:
  [fastcached#1041](https://github.com/LASTRADA-Software/fastcached/issues/1041).
- **A watchdog may not write to a socket a coroutine owns**, and the reason is ownership, not
  interleaving: the socket lives in the coroutine's frame, so a write that suspends can outlive
  it. A lock does not help. The owner writes its own refusal when control returns to it.
- **An awaitable's address is taken in `await_suspend`, never in the factory that returns it.**
  The factory's awaitable is a local returned by value; the caller suspends on a different
  object. Origin: [fastcached#734](https://github.com/LASTRADA-Software/fastcached/issues/734).
- **A socket has one read operation, shared by `read` and `waitReadable`, and one write
  operation.** Arming either while the other is parked drops the parked coroutine: never
  resumed, never freed, no signal. The claim and its assertion are one expression
  (`core::net::contract::claimReadSlot`, `claimWriteSlot`), so no arm site can forget it; a
  canary double-arms a real socket and must die, and another must be seen to accept a
  sequential pair. Origin:
  [fastcached#663](https://github.com/LASTRADA-Software/fastcached/issues/663),
  [fastcached#893](https://github.com/LASTRADA-Software/fastcached/issues/893).
- **A parked read is taken back with `cancelRead()`, by the caller that armed it.** At the arm
  site a socket cannot tell a stale parked wait from a live one; the caller can. Retiring a
  watch is not a disconnect: the cancel arrives as an error, so what silences it must be the
  retirement, never the code. On IOCP the kernel owns a retracted operation's `OVERLAPPED` until
  a later turn, so the operation node is stood down and the next read gets a fresh one; reusing
  it turned an abort into a spurious EOF on a healthy socket. **Every transport the library hands
  out declares it**, because the interface's default is a no-op and a transport whose reads park
  cannot retire them with one; `core-cpp.cancel-read-declared` refuses a class that inherits it.
  `WindowsSocket`'s read is an ordinary coroutine parked on the loop, so its `cancelRead` takes that
  frame back with `EventLoop::cancelPending` and resumes it inline to complete with `Cancelled` --
  the detach-then-complete order `PosixSocket` uses. Origin:
  [fastcached#710](https://github.com/LASTRADA-Software/fastcached/issues/710),
  [fastcached#884](https://github.com/LASTRADA-Software/fastcached/issues/884).
- **`read`'s buffer must be non-empty**, because `0` already means "the peer finished sending"
  and every receive primitive answers `0` for an empty request. `contract::requireReadBuffer` is
  `read`'s first statement on every transport; it is a precondition, so an assertion.
  Origin: [fastcached#838](https://github.com/LASTRADA-Software/fastcached/issues/838).
- **A guard folded into the operation is self-enforcing; a guard called alongside one needs a
  scan.** The slot claim is folded (every arm site must clear the slot anyway); the buffer check
  is additive, so every site can omit it independently, and fastcached needed a scan that
  derives the set of `read` definitions to hold it. Reach for the type system when the
  obligation is *do something*, and for a scan when it is *say why*. Both scans are core-cpp's
  too since Task B13: `core-cpp.read-buffer-guard` (every `ISocket::read`, out of line or in a
  class body, calls the guard before its first return or delegates to another socket's `read`)
  and `core-cpp.cancel-read-declared`, each with a self-test whose every case a mutation kills.
- **A wait that cannot be cancelled is a frame that cannot be freed.** `cancelPending(handle)`
  takes a parked handle back; its result is an ownership transfer, not a status: `true` means
  this call removed it and the caller alone may now destroy or resume it. It destroys rather
  than resumes, because a resume only queues on a loop that is about to stop.
- **A struct a decoder returns by value owns its bytes**; a borrowing one is named `*View`
  ([`cpp-guidelines.md`](cpp-guidelines.md)). A regression test for it stores the value, drops
  the source and churns the heap before reading, with a payload of real size: read inline,
  nothing dangles at any size.

## Datagrams, blocking transports, and the fake a consumer tests on

Task B9. Datagrams (`IDatagramSocket`, `openUdpSocket`, `SharedPortDatagram`), the transports for
threads that may block (`BlockingSocket`, `BlockingConnector`, `TcpClient`, `HealthProbe`), and the
in-process doubles (`testing::DatagramBus`, `testing::InMemorySocket`, the parking decorators).

- **A test double is pinned to the real thing by a parity test, or it is not trusted.** A fake a
  consumer builds its suites on that answers some state MORE permissively than a real socket does
  not fail anywhere: it manufactures a passing test in every consumer that reaches that state, which
  is worse than a missing test because it is invisible and it compounds downstream.
  `SocketClosedStates_test.cpp` runs one table of steps over `testing::InMemorySocket` AND over a
  real loopback pair, and asserts both. A new closed state, a new verb or a new error mapping on the
  real sockets is a new row there. Origin:
  [fastcached#1553](https://github.com/LASTRADA-Software/fastcached/issues/1553).
- **The real pair and the fake stay two things.** `testing/InMemoryTransport.hpp` is a pair of REAL
  sockets (a socketpair, or a loopback TCP pair on Windows) and `testing/InMemorySocket.hpp` is the
  fake. Deduplicating them deletes the difference the parity test measures.
- **A fake says in its header what it does not model**, and says which failure that produces: the
  in-memory socket has no loop, so a flow's stop token does not reach a parked operation (a hang,
  not a false pass), and it has no receive deadline (the interface's weaker-bound default).
- **EPIPE is not a reset.** A reset is the peer closing over bytes it had not read (`ECONNRESET`,
  `WSAECONNRESET`, `ConnReset`); a write after this end's own half-close, or after a FIN that the
  previous write turned into a reset, is `EPIPE`/`WSAESHUTDOWN`/`WSAECONNABORTED` and maps to
  `SystemError`. `PosixSocket` once mapped EPIPE to `ConnReset`, and the parity test reddened on
  every POSIX leg until it did not.
- **A datagram socket is not an `ISocket`, and its receive is always bounded.** There is no stream,
  no partial read and no slot to share, and POSIX does not unblock a parked `recvfrom` when another
  thread closes the socket -- so `close()` sets a flag and the bounded receive is how a loop sees it.
  `SO_RCVTIMEO` of zero means block for ever, so every wait is floored at a millisecond.
- **A receive buffer below the largest datagram turns an oversized message into a corrupt one**:
  POSIX truncates silently and Winsock fails the receive. The buffer is sized by the family that
  bound (`MaxIpv4DatagramPayload`, 65507; `MaxIpv6DatagramPayload`, 65527, because the IPv6 length
  field does not count its header), allocated once per socket, and a datagram that still does not
  fit is dropped and answered `DatagramWait::MessageTooLarge` -- `MSG_TRUNC` through `recvmsg`,
  `WSAEMSGSIZE` -- never handed back cut short and never read as a timeout. A datagram send that places fewer bytes than it was given is
  `MessageTooLarge`, never a partial write to retry: the rest resent is a second message.
- **Sharing a port buys hearing a broadcast, and nothing else.** A unicast to a shared port reaches
  one socket, and which one differs by platform, so a node that shares a port to hear the segment
  sends -- and is answered -- from an address only it holds (`answerFromOwnAddress`,
  `openSharedPortUdpSocket`). The broadcast capability belongs to the private socket, which is the
  one that sends.
- **A blocking transport never parks, and so is driven by `syncRun`.** Every awaitable
  `BlockingSocket` returns is already settled, and `BlockingConnector` resolves inline and waits in a
  syscall. A caller that takes a `BlockingConnector&` rather than an `IConnector&` is stating that it
  may block. What bounds it is the socket's own `SO_RCVTIMEO`/`SO_SNDTIMEO`, armed before the
  socket is handed over; a non-positive `setReceiveDeadline` removes the bound, as it does
  everywhere.
- **One socket-error table per platform** (`detail/SocketErrors.hpp`). The datagram and blocking
  transports use it; `PosixSocket`, `WindowsSocket` and the dial primitives still carry private
  switches, which is the debt this rule names rather than a design: a private switch is the shape
  that lacked a row and turned a firewall's `EACCES` into an unclassified `SystemError` upstream.
  A new transport uses the shared table, and moving the other three onto it is owed.

## TLS

- **A decorator's half-close writes before it forwards.** A TLS `shutdownWrite` is `close_notify`,
  flushed through the inner socket, and only then the inner socket's own half-close. A FIN with no
  alert before it is what a truncation looks like, and OpenSSL 3 reading from a socket reports it
  as an error rather than an end. A decorator is tested against a peer that is NOT itself --
  `testing::StrictTlsPeer` -- because two copies of one lenient reader agree with each other.
  Origin: Task B11; [fastcached#712](https://github.com/LASTRADA-Software/fastcached/issues/712)
  for the `waitReadable` half of the same fact (a `close_notify` is a record, so a raw peek
  reads it as data).
- **A decorator's own parks live as long as their waiters, go through the loop, and hear stop.**
  `TlsSocket`'s handshake and flush gates are shared with every waiter and every holder's scope
  guard, because a socket destroyed under a parked driver unwinds that driver after the socket's
  members are gone; the destructor abandons the gates, then the inner socket, then frees the
  session. A released waiter is handed to the socket's loop, never resumed inline, and a waiter
  whose stop token fires unwinds at once rather than when the gate opens. Origin: Task B11's
  review, B1/S1/S2, each a case in `TlsLifetime_test.cpp` that crashed or hung before.
- **`cancelRead` on a decorator retires the READ direction only**, and a cancel is never a sticky
  failure: a cancelled inner read took nothing from the stream, so the next operation re-drives.
  Where a write drives the handshake, the inner read under it is the write's. Origin: Task B11's
  review, S3.
- **A TLS read answers `0` for `close_notify` and nothing else.** A transport EOF before the
  alert is `NetErrorCode::ConnReset` ("peer closed without close_notify"), because `0` tells the
  caller the stream ended whole, and a truncated stream -- an attacker's cut, or a crash -- would
  pass as a complete one. `waitReadable` answers the same way. OpenSSL 3 reading from a socket
  refuses the same thing (`SSL_R_UNEXPECTED_EOF_WHILE_READING`). Origin: Task B11's review.
- **`ERR_clear_error()` before every `SSL_*` call whose result is classified.** `SSL_get_error`
  reads the THREAD's error queue, and every connection on a loop shares that thread: an entry
  another connection left behind turns this one's `WANT_READ` into `SSL_ERROR_SSL`, and a healthy
  connection fails for a neighbour's error. Origin: fastcached `Net/TlsSocket.cpp` at
  `0708dd54`, and `TlsSocket_test`'s stale-error case, which fails without it.
- **One outbound flush at a time, and a READ never waits for one.** A socket may have a read and
  a write in flight at once, and both reach the inner socket's `write` through the flush. Two
  flushes are two writes in a slot that holds one (`contract::claimWriteSlot`) and interleaved
  ciphertext. A write waits for a flush in progress; a read skips it, because that flush drains
  the BIO to empty -- the read's bytes included -- and a read parked behind a write could not be
  retired by `cancelRead`. Origin: Task B11, found by counting writes at a gated inner socket.
- **No OpenSSL type in any header, not even a forward-declared struct tag**, and no OpenSSL
  include outside the permitted units. `core-cpp.openssl-seam` holds both; a new unit that needs
  OpenSSL is a new row there, with its reason. Origin: the design spec's "No OpenSSL type appears
  in any header", which had no gate until Task B11.

## Profiling zones never span a `co_await`

`CORE_ZONE_SCOPED` and its variants declare a thread-local, stack-shaped RAII guard. A
coroutine that suspends inside a zone resumes on a later turn, possibly on another thread, and
the guard's destructor then corrupts the profiler's per-thread zone stack. Put zones in
synchronous leaf functions or in blocks containing no `co_await`; `CORE_FRAME_MARK` is a
stackless event and is safe anywhere. See
[`../guides/profiling-tracy.md`](../guides/profiling-tracy.md). Origin:
[fastcached `.agent/guides/profiling-tracy.md`](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/guides/profiling-tracy.md).

## Open work

- **[core-cpp#6](https://github.com/contour-terminal/core-cpp/issues/6)** — remove `WfmoBackend`
  and `BackendKind::Wfmo` after one release in which the IOCP backend is the Windows default and
  green.
- **[core-cpp#9](https://github.com/contour-terminal/core-cpp/issues/9)** — resolve morph's
  follow-up from its move onto `core::async` (executors and strand, logger, `FileIoOps`, the
  DateTime clock seam, `morph::net` on Windows), and graduate morph's strand into `core::async`
  once a second consumer needs one.
