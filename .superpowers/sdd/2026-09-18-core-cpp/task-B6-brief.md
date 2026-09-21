# Brief for Task B6

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).


**Every B task follows this pattern:**
1. Port the named tests from `D:\fastcached\src\FastCache\{Async,Net}` (renamed per the Part I §2 rename map, into core names) and/or adapt the contour tests.
2. Build and confirm the new cases FAIL.
3. Implement.
4. Confirm PASS on Windows (`clangcl-debug`, `cl-debug`) and WSL (`clang-debug`, `gcc-debug`, `clang-tsan`, `clang-asan-ubsan`).
5. Push, and require CI `ci-ok` green.
6. Commit.

**Sources:** every fastcached path named in Phase B is read as `git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show origin/master:<path>`, never from the `D:\fastcached` working tree.

Implementation must preserve the lifetime rules in `.agent/rules/wire-and-protocol.md` of fastcached `origin/master` (§Sockets, §Dialing and the reactor, §Socket and coroutine lifetime) and `D:\fastcached\AGENT.md` (grep: lifetime, ParkedWork, teardown, IOCP). Each rule carried over is written to `.agent/rules/async-and-net.md` in the same task that implements it.


### Task B6: ISocket/IoAwaitable merge + PosixSocket/PosixListener
**Files:** Create `ISocket.hpp`, `IoAwaitable.hpp`, `SocketContract.hpp`, `IListener.hpp`, `posix/{PosixSocket,PosixListener,UnixListener,AcceptLoop,FdUtils}.{hpp,cpp}`, `detail/{ReadSlot,WriteSlot}.hpp`. Adapt `windows/WindowsSocket` to the new interface over Wfmo.

- [ ] Tests first:
  - fastcached `EpollSocket_test` + `KqueueSocket_test` → one `ReactorSocket_test`.
  - `WaitReadable_test` (the #677 count semantics), `CancelRead_test`, `IoAwaitable_test` plus stop-token cases (cancel → throws; `close()` → `Cancelled` value; completed bytes win over a stop), `SocketDecorator_test`.
  - contour `Socket_test` (`FdWakePolicy`, loopback pair, `peerAddress`), `UnixSocket_test`, `FdPassing_test`.
  - The read-slot, write-slot and empty-read-buffer canaries (Debug, `WILL_FAIL`, label `canary`).
- [ ] Implement per Part I §2 item 7. `ISocket::read/write` return awaitables. Add `core::coro::asTask`.
- [ ] Commit `net: one frame-free, stop-aware socket contract and one POSIX socket`.

