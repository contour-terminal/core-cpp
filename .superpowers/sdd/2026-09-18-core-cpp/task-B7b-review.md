# Task B7b review: 99acaa6..c5aa85e (cb4bfe9 implementation, c5aa85e fix round 1)

Reviewer: review-B7b. Read-only. Source read at D:/core-cpp-wt-b7b (HEAD c5aa85e).
Implementer report `task-B7b-report.md` was **missing** at review time; reviewed without it.

## Verdict: CHANGES

No blocker. Two should-fix correctness defects, one should-fix test gap, and nits. The approved
rulings are all met (details at the end).

## Should-fix

### S1. `IocpListener::accept` leaks the accept socket and leaves its AcceptEx armed when the frame is destroyed mid-wait
`src/core/net/windows/IocpSocket.cpp:1240-1276`. `accepted` is a bare `SOCKET` local, and nothing
closes it unless the frame *resumes*: the only guard is `listed`, which delists the wait.
`~CompletionWait` (`IocpOperation.hpp:212-218`) unregisters the park but does not call `CancelIoEx`.
`CompletionWait`'s own comment names this path ("a flow destroyed mid-wait"), and
`ParkedWork`'s `release()` does destroy an abandoned chain's root (`ParkedWork.hpp:103-104`).

What goes wrong: an accept frame is destroyed while it is parked, for example as a sibling in a
chain whose root is freed, and the listener stays open. The `AcceptEx` is still outstanding on
the listening socket. The **next inbound connection is accepted into `accepted`**, a socket nobody
owns. The client sees a completed handshake and is never served, and the handle leaks. Nothing
reports it.

`dialCompletion` gets this right with its `discard` ScopeGuard (`IocpDial.cpp:128-135`); accept
has no equivalent. Fix: a ScopeGuard that closes `accepted` on every exit except the hand-off to
`IocpSocket`. Closing the accept socket is what aborts the AcceptEx. The existing `closesocket`
calls on the resume paths then go away. Add a case: start `accept()` as a lazy Task, destroy it
while it is parked, connect a client, and assert that the next `accept()` gets the connection.

### S2. Release build: a write armed over a parked write is a use-after-free on the loop thread
`IocpSocket.cpp:705-722` and `747-765`. `claimWriteSlot` compiles to nothing under NDEBUG
(`SocketContract.hpp:139-146`). `_write = std::move(node)` then drops the socket's share of a
node whose park is still registered with `state = &oldNode`.

The failing sequence: the old node survives only on the kernel's `self`. When its packet is
dequeued, `IocpBackend::consumeOwnerCompletion` calls `_batch.add` because its route is live.
Then `onDequeued` releases `self` and frees the node. After that, `_batch.dispatch()` calls
`onWriteWake(&freedNode)`, whose first act is to read `node.awaitable`.

The read side handles exactly this case (`IocpSocket.cpp:374-377`: "Release builds, where the
guard above is gone: the orphan is kept reachable"). The write side does not. `PosixSocket`
turns the same violation into a leak, and here it becomes a UAF. The synchronous `trySend` in the
verb also puts the new write's bytes on the wire ahead of the parked write's remainder. Fix:
mirror the read side and keep the orphan reachable (a settling list for writes, or `_settling`
generalised). Alternatively, refuse the write in Release (`BadHandle`, "a write is already
outstanding") before `trySend` runs.

### S3. Nothing tests the copy-in send path's correctness across operations
The fix round's new logic is `copyOwed` (256 KiB bound, spanning segments), `advanceCursor` part-way
through a segment, and re-issue after a partial completion. No case checks the bytes it
produces. The only overlapped-write case (`IocpSocket_test.cpp:467-540`) destroys the socket and
asserts ownership. The `BackendMatrix` suites almost never reach the overlapped path on Windows,
because a non-blocking send takes 8 MiB inline (measured, per the canary comment). Add a case:
fill the pipe as the mid-write case does, then `writeVectored` more than 256 KiB over at least 3
segments of odd sizes, including an empty segment. The peer drains the connection and asserts
the byte count, content and order. That is the only thing that would catch an off-by-one at the
256 KiB boundary or a wrong `segmentOffset`.

## Focus-area answers

**1. Lifetime.** The op/Impl ownership is sound (#465): every `IocpOperation` is heap-allocated
and holds `self` from issue until the port dequeues it. `beginOperation` is recorded before the
Winsock call and withdrawn on synchronous refusal. `consumeOwnerCompletion` runs `onDequeued` last,
and `retire` clears `route` together with the slot, so a late packet finds no route. A socket
destroyed with a receive, a send or an accept in flight leaves the kernel writing into live
storage, and the tests at `IocpSocket_test.cpp:409-465` cover this. Teardown order: `close()`
detaches first, forgets the association before `closesocket`, and completes last with no member
touched in between. G5's `assertTeardownIsSerialisedWithDispatch` is in `~IocpSocket` and
`~IocpListener`. Nodes are freed only in `take()` or in the dequeue, both on the loop's thread.
`~IocpBackend` drains `_issued` as well as `_inFlight`. Exceptions: S1 and S2.

**2. Cancellation.** The stop path (`onReadWake`/`onWriteWake` `Cancelled`, `CompletionWait::onWake`)
unregisters, calls `CancelIoEx`, re-parks and lets the completion answer. The value wins (#884):
`IoAwaitable::await_resume` tests the value before the token. A resource cancel (`close()`,
`cancelRead`, closed listener) is a `Cancelled` VALUE, and a flow stop throws. The dial maps
abort+deadline to `Timeout` and abort+stop to `OperationCancelled`. `IocpListener::close()`
resolves parked accepts at once through `CompletionWait::close()`, taking them into locals before
resuming. The stop of an accept (`takeBack` on an AcceptEx) is correct by reading but has no case
(N4).

**3. Copy-in/copy-out.** Partial reads are right: `received <= owned.size() <= buffer.size()`, and
the copy-out happens only on the way to `complete`. The vectored write is handled: `copyOwed`
flattens across segments from `segmentIndex`/`segmentOffset`, and `advanceCursor` moves the
cursor on the original spans. The cost is one memcpy of the payload plus one loop turn per
256 KiB once the send buffer is full, which is acceptable. **No path hands caller memory to the
kernel for an overlapped operation.** `tryReceive`/`trySend`/`tryPeek` are non-overlapped
(`lpOverlapped = nullptr`) and synchronous on the non-blocking socket. AcceptEx writes into the
op's `addresses`. ConnectEx's sockaddr is captured at issue, and it has no send buffer. The cost
worth knowing is N3.

**4. `shutdownWrite` precondition.** **It is not enforced anywhere.** `IocpSocket::shutdownWrite`
(`:685-701`) never looks at `_write`. The write-slot guard fires only on the *next* `write`, not
on the `shutdownWrite` itself. This matches `PosixSocket::shutdownWrite` and the contract text
(`ISocket.hpp`: "A plain socket claims no slot and so trips nothing"), so the removal is
consistent rather than a regression. The contract is nonetheless unguarded on every plain socket.
If the lead wants it guarded, the canary's write-slot-inline mode could half-close through the
native handle instead of through `ISocket::shutdownWrite`. The assert could then return on all
plain sockets. This is the lead's call; I rate it a nit.

**5. kqueue refusal** (`bsd/KqueueBackend.cpp:89-95`). Read line by line, it has no compile
problem. `HandleKind`, `NetErrorCode::Unsupported`, `makeNetError` and `std::unexpected { ... }`
are all already in scope and used the same way three lines above. The literal concatenation is
fine, and no `switch` over `HandleKind` exists in any non-Windows backend that could now warn
under `-Wswitch` (grep). The parity test's designated initialiser for `ReadinessHandler` follows
declaration order, and it compiles on Windows from the same source.

**6. Test quality.** Every wait is bounded (`pumpUntil` 5 s, `drainOwnerOperations` 5 s, dial
loops 10 s). `SKIP` is used and not `SUCCEED`: the WFMO leg of `CancelRead_test`, and the canary's
`noWindowToFill` exits 77 with a reason. The added lines have no `#ifdef` in logic, no NOLINT and
no pragma. See N1, N2, N4 and N5.

**7. Hygiene.** The CHANGELOG has Added and Changed entries, and the Changed entry says how to get
WFMO back. Every new file has a provenance row, and the `IoBackend.hpp`/`ICompletionPort.hpp`/
`EventLoop.hpp` rows are extended. The new headers are private (`SOURCES_WINDOWS`), so
`<winsock2.h>` in `IocpSocket.hpp` does not breach platform.md. `ICompletionPort`'s new pure
virtuals are unreleased (B7a, same `[Unreleased]`), so they need no Breaking entry.
`renames.json` notes are updated per the B8 hand-off. See N6.

## Nits

- **N1.** `BackendParity_test.cpp` "host-driven" section: `HostDrivenBackend::attach` refuses
  *every* registration with `Unsupported` (`HostDrivenBackend.cpp:15-19`), so the section cannot
  tell a Completion refusal from any other. Either say so in the comment, or drop the claim from
  the commit and CHANGELOG ("the host-driven one included").
- **N2.** `IocpSocket_test.cpp:696-738` ("An abandoned read never lets the kernel write into the
  caller's buffer") passes on the pre-fix code as shipped. The commit message admits the RED
  needed the retire path's `CancelIoEx` removed. Its second section cannot distinguish at all,
  because `closesocket` aborts the receive before the peer's send. The comment should say it
  guards the copy only against a future removal of `CancelIoEx`.
- **N3.** Receive memory: each read that has to wait allocates a fresh `Node` and
  `owned.resize(min(buffer, 64 KiB))`. The resize value-initialises, which means a memset, and
  the memory stays pinned until the completion arrives, doubling the caller's buffer for every
  idle connection. The approved ruling chose op-owned buffers. For the record, the zero-read idiom
  (libuv) would remove both the copy and the pinned memory for reads: park a zero-byte probe, then
  do a non-overlapped `recv` into the caller's buffer on wake. The probe machinery already exists,
  and it would also let a real read's `cancelRead` resolve inline like POSIX.
- **N4.** No case stops a parked `accept()` (`CompletionWait::takeBack` on AcceptEx: abort, then
  a `Cancelled` value), and none covers data-wins for accept. The dial cases exercise
  `CompletionWait`'s stop path, but not the listener's `completionError(shared->socket, ...)`
  branch.
- **N5.** Two cases rely on a 300 ms sleep rendezvous (`:661`, `:786`), and their comments say so
  honestly. `HasOverlappedIoCompleted` on the node (`Internal != STATUS_PENDING`) might make the
  rendezvous observable. Upstream measured `WSAGetOverlappedResult` and not that macro, so it is
  worth one measurement.
- **N6.** CHANGELOG says "16 cases in `windows/IocpSocket_test.cpp`". There are 17 after the fix
  round (`grep -c ^TEST_CASE`).

## Approved rulings: checked against the code

- `HandleKind::Completion` refused with `Unsupported` by poll, epoll, kqueue, WFMO (and host-driven
  by construction), and a parity case covers them: **met** (N1 on the host-driven leg).
- `EventLoop::completionPort()` on every platform, nullptr off Windows, with no `#ifdef` in logic:
  **met** (the factories and `LoopConnector::dialStep` branch on it at runtime).
- Completion cancel is `CancelIoEx`, and the completion reports: **met** (`cancelInKernel`,
  `CompletionWait::takeBack`).
- One read and one write per socket; `shutdownWrite` is `ResultAwaitable<void>` with the
  no-write precondition: **met in the signature**. The precondition is unguarded (focus 4), and a
  Release write-over-write is S2.
- An abandoned overlapped op never lets the kernel write into caller memory: **met** (focus 3).
