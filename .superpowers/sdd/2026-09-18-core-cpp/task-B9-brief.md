# Brief for Task B9

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


### Task B9: UDP and blocking transports
- [ ] Tests first: fastcached `InMemoryDatagram_test`, `SharedPortDatagram_test`, `BlockingSocket_test`, `TcpClient_test`, `HealthProbe_test` (rewritten against core `HttpServer`), and a new `UdpSocket_test` (loopback send/recv, `MessageTooLarge`).
- [ ] Import `Datagram`, `UdpSocket`, `SharedPortDatagram`, `BlockingSocket`, `BlockingConnector`, `TcpClient`, `HealthProbe`, `IAdmissionControl`, and `testing/{InMemoryTransport,InMemoryDatagram,ParkingReadableSocket}`. Dedupe InMemoryTransport with contour's socket pair (`makeLoopbackPair`).
- [ ] Commit `net: UDP, blocking transports and in-memory test doubles`.

