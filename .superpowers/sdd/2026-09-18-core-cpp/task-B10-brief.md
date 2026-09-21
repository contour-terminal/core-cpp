# Brief for Task B10

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


### Task B10: Helpers on the awaitable ISocket
- [ ] Tests: contour `AsyncBufferedReader_test`, `WriteQueue_test`, `HttpServer_test`, `WithTimeout` cases. Expect FAIL after B6's signature change until they are adapted.
- [ ] Adapt: `AsyncBufferedReader` calls `contract::requireReadBuffer`; `WriteQueue` writes one frame per `write`; `SplitSocket`; `withTimeout` over `loop.delay`.
- [ ] Commit `net: buffered reader, write queue, split socket and HTTP server on the new socket contract`.


---

# A finding handed to you rather than ruled into your scope

`Net/LingeringClose.{hpp,cpp,_test.cpp}` exists at the pin and **no task in the plan imports it**.
It is server-side graceful close, bounded three ways — total time, max bytes, max reads — handling
both a suspending loop socket and a blocking one, with seven cases. The last one names the defect it
exists for:

> *"A refusal written over an unread request reaches a real client intact only when the close
> lingers."*

That is the classic failure: a server that refuses a request with an unread body and closes sends a
RST that destroys the response it just wrote. It reproduces against real clients and **not against
loopback tests**, which is why it needs writing down rather than rediscovering.

**Measured, not assumed:** `src/core/net/HttpServer.cpp` (334 lines) contains **zero** occurrences
of `shutdownWrite`, `close(`, `linger`, `SO_LINGER` or `drain`. It closes through destructors. So
core-cpp's HTTP server has the latent form of this today.

**Ruling: importing `LingeringClose` is NOT in your scope**, and not in anyone's for v0.1.0 —
adding an eighth component to fix a latent defect is how a release slips, and the plan is approved.
It is recorded as [core-cpp#35](https://github.com/contour-terminal/core-cpp/issues/35).

**What is in your scope** is not making it worse. You are adapting `HttpServer` onto B6's socket
contract, which gives you `shutdownWrite` where there was nothing. **If your adaptation introduces
an explicit close where the destructor used to do it, say in your report which of the two it is** —
a destructor close and an explicit `close()` on an unread socket fail the same way, but only one of
them looks deliberate to the next reader.
