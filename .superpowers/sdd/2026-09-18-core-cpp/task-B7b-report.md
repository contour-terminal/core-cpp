# Task B7b report: Windows IOCP sockets

**Status:** DONE, awaiting review.
**Worktree:** `D:/core-cpp-wt-b7b`. **Base:** `99acaa6`. **Range:** `99acaa6..c5aa85e`, unpushed and
not rebased onto master `8dc3361`, which touched `Connector.cpp`; the lead stacks lanes.

| Commit | Subject |
|---|---|
| `cb4bfe9` | net: IOCP sockets, listener and ConnectEx dial; IOCP is the Windows default |
| `c5aa85e` | fix(net): an overlapped operation never holds the caller's memory |

## cb4bfe9: the task

- **`IocpSocket` / `IocpListener`** (`src/core/net/windows/IocpSocket.{hpp,cpp}`) on
  `ISocket`/`IListener`. They issue overlapped WSARecv, WSASend and AcceptEx on the loop's port.
  - A completion reaches its owner **through the loop's turn**. It never arrives as a callback
    inside the dequeue.
    - The owner parks on its operation with the new `HandleKind::Completion`.
    - `IocpBackend` recognises the packet by the record `ICompletionPort::beginOperation` made,
      marks the operation completed and reports the park.
    - The loop resumes in turn step 2 (Rule 1, fastcached#475).
  - The dequeue hook's only job is to give back the operation's own share. An operation holds
    itself and never the socket (**fastcached#465**, cited).
  - A stop or a receive deadline calls CancelIoEx and then keeps waiting until the completion
    answers. So bytes already received win over a later stop (**fastcached#884**, cited).
  - Zero-byte WSARecv plus **MSG_PEEK classification** (cited), SO_UPDATE_ACCEPT_CONTEXT, and
    NTSTATUS conversion through WSAGetOverlappedResult are fastcached's.
  - Each read and write is tried non-blocking before an operation is issued. So turn counts match
    every other socket, which the WriteQueue case needed.
  - `cancelRead` retires a probe inline and settles a real read. `close()` resolves a parked
    operation at once, and a listener's `close()` resolves its parked accepts (BackendParity
    closehang).
- **`setReceiveDeadline` override:**
  - d>0 bounds reads started after the call.
  - d<=0 removes the bound.
  - An already-parked read keeps its timer.
- **`shutdownWrite`** is `[[nodiscard]] ResultAwaitable<void>`, with the precondition "no write
  outstanding" documented.
- **`IocpDial`** (`windows/IocpDial.cpp`, `detail::dialCompletion`) uses ConnectEx:
  - It binds to the wildcard address and associates the socket with the port first.
  - The outcome comes from the completion status plus `SO_UPDATE_CONNECT_CONTEXT`.
  - A deadline or stop cancels the operation and lets the completion report:
    - a deadline gives `Timeout`;
    - a stop throws `OperationCancelled`.
- **Default backend:** `makeDefaultBackend()` builds IOCP on Windows. If that throws a
  runtime_error, it falls back to WFMO with a diagnostic.
  - The socket factories, the connector and `testing::makeSocketPair` ask
    `EventLoop::completionPort()` which socket to build. So `makeBackend(BackendKind::Wfmo)` still
    gets `WindowsSocket`/`WindowsListener`, and WFMO stays for one release (core-cpp#6).
- **Design conditions (lead):**
  1. `HandleKind::Completion` is refused by name with Unsupported on poll, epoll, kqueue, WFMO and
     host-driven. One parity case covers them all (the non-Wfmo refusals landed in c5aa85e).
  2. `EventLoop::completionPort()` exists on every platform. It forwards to the backend, returns
     nullptr off Windows, and has no `#ifdef`.
- `detail::fromWinsockError` is shared in `windows/WinsockError.hpp`, extended with
  ERROR_OPERATION_ABORTED→Cancelled and ECONNABORTED/NETRESET→ConnReset.
- `CancelRead_test` now runs on Windows (the WFMO leg SKIPs). The read-slot and empty-read-buffer
  canaries are enrolled on Windows.
- CHANGELOG has Added entries, plus a Changed entry for the new default that says how to get WFMO
  back: `makeBackend(BackendKind::Wfmo)`. Also updated: provenance rows and notes,
  `rules/async-and-net.md`, `docs/modules/net.md`, `docs/design/portability.md`, and
  `tools/migrate/renames.json` notes.

## c5aa85e: the fix round

1. **Abandoned-read UAF** (option (a), op-owned buffers).
   - **The bug:** every abandon path ends the caller's borrow before the kernel is done. That covers
     an awaitable destroyed while parked, a socket destroyed under one, and a loop torn down.
     CancelIoEx only asks, so a receive that the peer's bytes reached first completed into freed
     memory.
   - **The fix:**
     - An overlapped receive lands in a buffer the node owns, and is copied out only when it is
       delivered to a flow still waiting for it.
     - An overlapped send is copied in when it is issued (at most 64KiB for a receive and 256KiB for
       a send, per operation).
     - The non-blocking fast paths still move bytes with no copy.
   - **RED/GREEN:** ASan cannot see a kernel write, so the new case "An abandoned read never lets
     the kernel write into the caller's buffer" watches for it directly with a buffer that outlives
     the read.
     - Old code: GREEN, as predicted, because CancelIoEx wins the race.
     - Old code with the retire path's CancelIoEx removed: **RED, `3 == 0`**.
     - New code with the same neuter: GREEN.
2. **Write-slot canaries on Windows** (ruling option (a)).
   - A Windows non-blocking send takes 8MiB at once, so `parkAWrite` keeps writing until one write
     stays pending.
   - It is bounded at 32×8MiB = 256MiB. Past that it SKIPs with exit 77 and a reason on stderr.
   - All four modes now run on Windows. They are marker-based (PASS_REGULAR_EXPRESSION /
     SKIP_RETURN_CODE 77), never WILL_FAIL.
   - `IocpSocket::shutdownWrite` no longer asserts its precondition, like every other plain socket,
     because the write-slot-inline mode breaks it on purpose to reach the guard behind it.

## Gates (at c5aa85e)

| Gate | Build exit | Steps | Tests |
|---|---|---|---|
| cl-debug `--clean-first` | 0 | 559 | ctest 42/42 |
| clangcl-release `--clean-first` | 0 | 559 | ctest 42/42 |
| WSL clang-debug (clean) | 0 | 554 | ctest 38/38 |
| WSL gcc-release (clean) | 0 | 554 | ctest 38/38 |
| clang-tidy preset (fresh tree) | 0 | 562 | 0 findings |
| MSVC `/fsanitize=address` `/Od` | 0 | n/a | 243 cases, clean |

- Also passed: `ctest -L hygiene` 18/18, `clang-format.py --all --check` (468 files), and
  `mkdocs build --strict`.
- **ASan note:** the clangcl ASan configuration fails on `-MDd`, the same as in B7a. MSVC's global
  `/fsanitize=address` works at `/Od`, and at `/O1` it hits C4737.
- **Test counts:** `IocpSocket_test` has 17 cases, and `IocpDial_test` has 4.

## Concerns

- Concerns 1, 4 and 6 were accepted by the lead. Concern 5 goes to B13.
- **Copy cost:** the owned buffers add one copy per overlapped operation, capped at 64KiB for a
  receive and 256KiB for a send. The fast paths avoid it whenever data or buffer room is already
  there.
- **Master:** the branch has not been rebased onto `8dc3361`. The `Connector.cpp` overlap is
  small, and the lead does the stacking.
