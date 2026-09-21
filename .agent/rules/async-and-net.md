# Coroutines, the event loop and networking

Rules for `src/core/async/` and `src/core/net/`: what the layers may depend on, how sockets and
dials behave, and the lifetime rules that keep a coroutine frame from being leaked, freed twice
or resumed on the wrong thread.

**Status.** The code these rules govern arrives in two steps: contour's `coro` and `net` were
imported as they are (Tasks A5 and A6: `core::async`, and `core::net` with its `EventSource`
API), then fastcached's async and networking layer is merged into them (Phase B). Task B1 has
landed the first half of that merge — the ownership rules under "Task ownership" below are live
code, not a forecast — and Task B3 has landed `IoBackend`, so `EventSource` is gone and the
backend rules below are live code too. The rules are
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
  firewall's `EACCES` becomes an unclassified `SystemError` no caller can match.
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

## Dialling, and the backends underneath

- **A synchronous dial spends a thread the caller does not own.** A loop thread dials through
  `makeConnector(loop, resolver)`, never `BlockingConnector`. "The caller has nothing to do until
  it connects" is true of the caller and false of the thread, which serves every other
  connection. fastcached paid for the opposite decision with a thread per peer and a shutdown
  that hung forever inside `send`.
- **DNS never runs on the loop.** `getaddrinfo` takes no timeout, so a wedged resolver blocks
  everything behind it. `ThreadedAddressResolver` runs it on a small fixed pool whose queue is
  bounded and refused rather than waited on, and a literal address never reaches the pool.
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
  They go to `ReadinessHandler::onError`, and the choice of callback is a pure function
  (`selectReadinessCallback`) so it is tested without a kernel. A handler with no `onError` has a
  failure delivered to whichever direction it *does* watch, which is what a parked read and a
  parked accept both want.
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
  it turned an abort into a spurious EOF on a healthy socket. Origin:
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
  obligation is *do something*, and for a scan when it is *say why*.
- **A wait that cannot be cancelled is a frame that cannot be freed.** `cancelPending(handle)`
  takes a parked handle back; its result is an ownership transfer, not a status: `true` means
  this call removed it and the caller alone may now destroy or resume it. It destroys rather
  than resumes, because a resume only queues on a loop that is about to stop.
- **A struct a decoder returns by value owns its bytes**; a borrowing one is named `*View`
  ([`cpp-guidelines.md`](cpp-guidelines.md)). A regression test for it stores the value, drops
  the source and churns the heap before reading, with a payload of real size: read inline,
  nothing dangles at any size.

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
