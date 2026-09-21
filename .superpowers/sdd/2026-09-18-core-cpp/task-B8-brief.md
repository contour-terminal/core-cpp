# Brief for Task B8

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


### Task B8: Dial and DNS
- [ ] Tests first: fastcached `ConnectFlow_test`, `PlatformConnector_test`, `EpollConnector_test` → `ReadinessDial_test` (epoll/kqueue/poll), `IocpConnector_test`, `SocketAddress_test`, `ThreadedAddressResolver_test`, `BlockingConnector_test`. Plus: contour `connect()` does not call `getaddrinfo` on the loop thread (an injected resolver records the calling thread).
- [ ] Implement `IConnector`, `makeConnector(loop, resolver)`, `DialOptions`, `ConnectFlow`, `ReadinessDial` (non-template, over IoBackend), `IocpDial`, `KeepAlive`, `SocketDeadline`. `listen(loop, ListenOptions)`, `adoptListener`, and `connect` re-implemented on top. The flow's stop token cancels a dial.
- [ ] Commit `net: fastcached's dialer and resolvers; DNS never blocks a loop`.

