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


---

# Corrections made before dispatch — see `phaseB-unclaimed-upstream-survey.md`

Measured at the pin with `git ls-tree -r --name-only 0708dd54 src/FastCache/Net/` over the whole
directory. **Do not trust the file list above; run that command yourself and say what it still gets
wrong.**

1. **`ParkingReadableSocket` is a class, not a file.** `grep -i parking` over the pin's entire tree
   matches no path. `git grep -l ParkingReadableSocket 0708dd54` puts it in
   **`src/tests/SocketDecorator.hpp`** — under `src/tests/`, not `Net/testing/`. The plan's
   `testing/{InMemoryTransport,InMemoryDatagram,ParkingReadableSocket}` describes the **destination**
   layout in core-cpp, not the source layout in fastcached. Sixth instance of this shape in this
   project; B6 hit the same one with `IoAwaitable`.
2. **`Datagram` is `Net/IDatagramSocket.hpp`.** There is no `Datagram.hpp` at the pin.
3. **`Net/BlockingConnector.*` and `Net/IAdmissionControl.hpp` are B8's, not yours.** Both appear in
   your import list and in B8's Sources. Ruled to B8: a connector is a dial concern, and your
   subject is datagrams and blocking *transports*. **Consume them; do not import them.** If B8 has
   not landed when you need them, say so rather than importing a second copy.
4. **`Net/SocketClosedStates_test.cpp` is yours, and it is not optional.** Single case:
   *"`InMemorySocket` answers every closed state the way a loopback TCP socket does."* It is a
   **parity test between a shipped test double and a real socket**, and core-cpp ships
   `testing/InMemoryTransport` as a DI fake for consumers to build their own suites on. **A fake
   whose closed-state behaviour diverges from a real socket does not fail — it manufactures passing
   tests in every consumer that uses it.** That is worse than a missing test, because it is
   invisible and it compounds downstream. Port it with the parity property intact: the case must
   run the same assertions against both the fake and a loopback pair, or it is not the test.
