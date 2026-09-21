# Brief for Task B7

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


### Task B7: IOCP backend (Windows default)
**Files:** Create `backend/IocpBackend.{hpp,cpp}`, `windows/{IocpSocket,IocpListener,CompletionStatus,WindowsLoopback}.{hpp,cpp}`.

**Sources:** fastcached `Async/IocpReactor.{hpp,cpp}`, `Net/{IocpSocket,IocpListener}.*` (#465 Impl held by the op, #884 retire semantics, zero-byte `WSARecv` at `IocpSocket.cpp:555-561`, `MSG_PEEK` classification at `:327-334`).

- [ ] Tests first:
  - `BackendParity` on IOCP: a waitable HANDLE (Wakeup event, console input where available, serialised by a named mutex and SKIP (77) without a console), socket read readiness, socket write readiness via WSAEventSelect + threadpool wait.
  - `IocpBackend_test`: detach with a packet in flight (no use-after-free under clang-cl ASan); a G1 assert when a second thread calls `wait`.
  - fastcached `IocpSocket_test` (13 cases), `IocpReactor_test` → `IocpBackend_test`.
  - A teardown gate.
- [ ] Implement the threadpool-wait bridge, the refcounted `ReadinessSlot` with generation, and dispatch keys `KeyWake`/`KeyReadiness`/`KeyCompletion`. The `NtAssociateWaitCompletionPacket` path is used only after a startup probe (`GetProcAddress`, no ntdll link). `makeDefaultBackend()` returns IOCP on Windows; Wfmo stays available via `makeBackend(BackendKind::Wfmo)`.
- [ ] Verify locally: `ctest --preset cl-debug` and `clangcl-debug` with `-DCORE_CPP_SANITIZERS=address` (a top-level build).
- [ ] Commit `net: IOCP is the Windows backend and can park on waitable handles`.

