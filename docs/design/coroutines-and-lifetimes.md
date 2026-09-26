# Coroutines and lifetimes

core-cpp's coroutines are C++23 coroutines driven by an event loop (`core::net::EventLoop`) or an
executor (`core::async::IExecutor`). Most of the defects this design exists to prevent are silent:
a frame that is parked and then never resumed or freed, a frame freed twice, a coroutine resumed on
the wrong thread, a socket operation that outlives the socket. Each rule below exists because it
was broken at least once. Most of them were broken in fastcached, whose issue is linked, and the
design merged here is the one that fixed them.

The canonical text, with the measurement behind every rule, is
[`.agent/rules/async-and-net.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/async-and-net.md).
This page states the rules; that file is what a change to `core::async` or `core::net` is reviewed
against.

## Frames and ownership

- **Coroutine parameters are owning values.** A reference or a `std::string_view` parameter names
  storage the caller may destroy while the coroutine is suspended, so a dial takes `std::string`
  and a flow takes pointers only to objects that outlive it by construction.
- **The awaiter owns what it awaits, and ownership runs downward.** `Task::operator co_await`
  moves the frame into the awaiter, so a temporary `Task` cannot destroy the frame it is awaiting
  across a suspension
  ([fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025)).
- **A task that owns no frame has no result.** `done()` is true for an empty `Task`, and awaiting
  one is refused rather than resumed into nothing.
- **An executor resumes what it is handed, or frees it -- never neither.** Submitted work travels
  as `core::async::ParkedWork`: the handle to resume and, where the chain belongs to nobody, the
  root to free. A queue of submitted work holds `detail::Parked`, never a bare handle
  ([fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025)).
- **Both `IExecutor::submit` overloads are pure virtual.** A derived interface that re-declares one
  overload hides the other, so a call through the derived type bound to the borrowing overload and
  leaked the owning one
  ([fastcached#1041](https://github.com/LASTRADA-Software/fastcached/issues/1041)).
- **An awaitable's address is taken in `await_suspend`, never in the factory that returns it.** The
  factory's result is moved into the coroutine frame, and the address taken earlier names the
  temporary
  ([fastcached#734](https://github.com/LASTRADA-Software/fastcached/issues/734)).

## The loop

- **Backends dispatch, the loop resumes.** A backend, completion, stop or thread-pool callback only
  enqueues. Every coroutine resumes in step 2 of the turn, so nothing resumes a frame from inside a
  walk that a resumption could invalidate
  ([fastcached#475](https://github.com/LASTRADA-Software/fastcached/issues/475)). See
  [Threading](threading.md).
- **A loop resumes what it parks, or frees it, and it frees only what nothing else owns.** What the
  loop borrows (a `Task` somebody holds, a `blockOn` root) is resumed at teardown so its stop token
  unwinds it; what the loop owns (a `DetachedTask`, which carries no stop token) is freed, looping to
  a fixpoint because freeing a chain can park again
  ([fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025),
  [fastcached#1054](https://github.com/LASTRADA-Software/fastcached/issues/1054)).
- **`cancelPending`'s `true` is an ownership transfer, not a status.** It takes the work back from
  wherever the loop holds it -- the ready queue, the park table, or the inbound queue -- and a
  second call answers `false`. A waiter queued after readiness is in the ready queue AND the park
  table until its `await_resume` runs, and `cancelPending` takes both, so nothing of it is left
  registered with the backend
  ([core-cpp#41](https://github.com/contour-terminal/core-cpp/issues/41)).
- **An object that parks flows on a loop takes them back in its own destructor, and is therefore
  destroyed before the loop.** It starts them with `submit`, which borrows, not `spawn`, which
  hands them over; its destructor calls `cancelPending` on each and destroys the frame while every
  member it names is still alive. `core::tui::runtime::TuiRuntime` is the example.
- **Ready work wakes; a park arms.** A member that files work only a turn can reach must ask for
  that turn, or on a quiescent host-driven loop the work is filed, correct and never run. Ready work
  asks with `wake()`; a park with a deadline asks with `armHostWake()` for that deadline. One
  parameterised case in `HostDrivenLoop_test.cpp` holds every member of the family to it.

## Sockets

- **A socket has one read operation and one write operation**, and `read` and `waitReadable` share
  the read one. A second concurrent operation of a kind is a precondition violation, asserted in
  Debug builds
  ([fastcached#663](https://github.com/LASTRADA-Software/fastcached/issues/663),
  [fastcached#893](https://github.com/LASTRADA-Software/fastcached/issues/893)).
- **A socket operation allocates no coroutine frame**, so its retry loop runs where the readiness
  arrives rather than in a frame that suspends once.
- **A cancel from the FLOW throws; a cancel from the RESOURCE is a value.** A stopped token unwinds
  the flow with `OperationCancelled`; `close()` and `cancelRead()` complete the operation with
  `NetErrorCode::Cancelled`, because the flow is alive and asked a question about an operation that
  was taken away from it.
- **If a receive already produced a value, the value wins over a stop.** On a completion port the
  kernel may already have taken the bytes; dropping them for a cancel loses data
  ([fastcached#884](https://github.com/LASTRADA-Software/fastcached/issues/884)).
- **`close()` touches no member after it completes a pending operation.** Completing it can resume
  the coroutine that owns the socket, which may destroy it. Detach the operation first, complete it
  last.
- **A destructor abandons a parked operation; `close()` resolves it.** A destructor that resumed the
  flow would resume it into a socket that is going away.
- **A parked read is taken back with `cancelRead()`, by the caller that armed it.** Only the caller
  knows a wait is stale; at the arm site a socket cannot tell a stale wait from a live one
  ([fastcached#710](https://github.com/LASTRADA-Software/fastcached/issues/710)). It retires what is
  parked NOW, which is not the same as idempotent
  ([fastcached#1233](https://github.com/LASTRADA-Software/fastcached/issues/1233)). Every transport
  the library hands out declares it, because the interface's default is a no-op and a transport
  whose reads park cannot retire them with it.
- **`read` refuses an empty buffer.** `0` means the peer has finished sending, and every transport's
  receive primitive answers `0` for a zero-length request
  ([fastcached#838](https://github.com/LASTRADA-Software/fastcached/issues/838)).
- **`waitReadable`'s count is the contract:** `0` is EOF, more than `0` is data pending
  ([fastcached#677](https://github.com/LASTRADA-Software/fastcached/issues/677)).
- **EOF means "the peer has finished sending", not "the peer is gone"**
  ([fastcached#671](https://github.com/LASTRADA-Software/fastcached/issues/671)), and a TLS peer says
  it with `close_notify`
  ([fastcached#712](https://github.com/LASTRADA-Software/fastcached/issues/712)).
- **On a completion port, the operation owns the memory the kernel writes.** `lpOverlapped` points
  at a backend-owned, refcounted slot, never into a handler or the caller's buffer
  ([fastcached#465](https://github.com/LASTRADA-Software/fastcached/issues/465)).

## Teardown

- **An object an event loop owns is destroyed on that loop's thread, or with the loop not running.**
  Destroying it elsewhere clears a pending awaitable while a turn may be dispatching into it. Every
  loop-thread-only member of `EventLoop` asserts it, and so does every socket, listener and dial
  destructor
  ([fastcached#668](https://github.com/LASTRADA-Software/fastcached/issues/668),
  [fastcached#737](https://github.com/LASTRADA-Software/fastcached/issues/737),
  [fastcached#840](https://github.com/LASTRADA-Software/fastcached/issues/840),
  [fastcached#885](https://github.com/LASTRADA-Software/fastcached/issues/885)).
- **Objects registered with a loop are destroyed before it.** The loop's own teardown order is in
  [Threading](threading.md#teardown).
- **A profiling zone never spans a `co_await`.** A zone is a scope on one thread's stack, and a
  suspension moves the rest of the scope to another turn, often another thread.

## What holds these rules

A rule that nothing checks is a sentence people agree with. These are the checks, and each one has
been watched refusing:

| Rule | Held by |
|---|---|
| every loop-thread-only `EventLoop` member refuses a second thread | `core-cpp.loop-affinity-canary.*`, one process per member, judged on the assertion's own text |
| a host-driven loop refuses `run()`, `blockOn()`, and a closed park it cannot report | `core-cpp.hostdriven-canary.*` |
| one read and one write operation per socket; `read` refuses an empty buffer | `core-cpp.socket-contract-canary.*` and `core-cpp.inmemory-socket-canary.*` |
| every transport declares `cancelRead` | `core-cpp.cancel-read-declared`, a scan with a self-test |
| every `ISocket::read` guards its buffer before its first return | `core-cpp.read-buffer-guard`, a scan with a self-test -- the canary aborts at the first violation and so can only watch one transport |
| every member that files work asks for the turn that runs it | `HostDrivenLoop_test.cpp`, one parameterised case |
| `cancelPending` takes a queued waiter's park with it | `TestLoop_test.cpp`, on every backend |

An assertion cannot be observed from inside a Catch case, because it aborts the binary. That is why
the first three rows are canary processes, each registered with `PASS_REGULAR_EXPRESSION` rather
than `WILL_FAIL`, so a process that died on the way to its mechanism is a failure rather than a
pass. What must appear is the assertion's own text where the canary can name one assertion -- the
loop-affinity modes and `hostdriven-canary.closedPark` -- and otherwise a marker printed to stderr
immediately before the forbidden call. They skip on Release builds, where the assertions are
compiled out.
