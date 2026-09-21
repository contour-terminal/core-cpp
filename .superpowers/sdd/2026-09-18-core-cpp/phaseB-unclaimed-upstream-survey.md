# Phase B: what is in fastcached's `Net/` at the pin that no task claims

Method: `git ls-tree -r --name-only 0708dd54 src/FastCache/Net/` and `.../Async/`, over the whole
directories, then every path matched against the plan's Task B6-B11 file lists and each B
dispatch's Sources section. **Listing the directory rather than checking the named files is the
point** — checking a list can only find the named file that is missing; it cannot find the file
nobody named, which is the failure that has cost this project more.

Findings below are ruled, not merely recorded. Each ruling says what it costs if wrong.

---

## 1. `Net/ReactorDial.hpp` — B8's central deliverable, unnamed

**Fixed in `task-B8-dispatch.md` before B8 was dispatched.** `ReadinessDial` "non-template, over
`IoBackend`" is a de-templatising of this file, which is templated on a platform `Traits` triple and
shared by `EpollConnector` (Linux) and `KqueueConnector` (`__APPLE__`-only, and also unnamed). The
dispatch would have sent B8 to `EpollConnector_test.cpp` to re-derive it — **from the Linux wrapper
alone**, which is how R101's macOS divergence gets written straight back in.

`Net/IocpDial.hpp` was named in that dispatch's "what to build" list but not its Sources. Also fixed.

## 2. `ParkingReadableSocket` is a class, not a file — the sixth instance of this shape

The plan's Task B9 imports `testing/{InMemoryTransport,InMemoryDatagram,ParkingReadableSocket}`.

```
git ls-tree -r --name-only 0708dd54 | grep -i parking   ->  (nothing)
git grep -l ParkingReadableSocket 0708dd54              ->  src/tests/SocketDecorator.hpp  (+ 4 test/docs files)
```

**It is a class inside `src/tests/SocketDecorator.hpp`, which is under `src/tests/`, not
`src/FastCache/Net/testing/`.** Exactly B6's `IoAwaitable` shape — a plan file list describing the
*destination* layout while reading as though it described the source.

**Ruling:** B9's brief is corrected to name `src/tests/SocketDecorator.hpp` as the source and to say
the plan's three-name list is the destination. Cost if wrong: B9 spends ten minutes discovering it,
which is what the last five lanes each spent.

## 3. `Net/AcceptedHalfClose_test.cpp` — B6's, and B6 does not know

Single case: *"An accepted socket's `ShutdownWrite` reaches its peer as EOF, and its own writes then
fail."* `shutdownWrite` is in B6's `ISocket` API list. The case pins the half-close semantics B6 is
implementing, from the accepted side.

**Ruling: it goes to B6.** Cost if wrong: one test in the wrong binary, moved later at no risk.

## 4. `Net/SocketClosedStates_test.cpp` — B9's, and it is worth more than its name suggests

Single case: *"`InMemorySocket` answers every closed state the way a loopback TCP socket does."*

**This is a parity test between a shipped test double and a real socket**, and core-cpp ships
`testing/InMemoryTransport` as a DI fake for consumers. **A fake whose closed-state behaviour
diverges from a real socket does not fail — it manufactures passing tests in every consumer that
uses it.** That is a worse failure than a missing test, because it is invisible and it compounds.

**Ruling: it goes to B9, and it is not optional there.** Cost if wrong: nothing; it is a test.

## 5. `Net/LingeringClose.{hpp,cpp,_test.cpp}` — in no task, and the plan may be wrong about that

Server-side graceful close, bounded three ways (total time, max bytes, max reads), handling both a
suspending reactor socket and a blocking one. Seven cases. The last one states the problem it
exists for:

> *"A refusal written over an unread request reaches a real client intact only when the close
> lingers."*

This is the classic defect: a server that refuses a request with an unread body and closes sends a
RST that destroys the response it just wrote. **It is generic HTTP/TCP behaviour, not a fastcached
concept** — and core-cpp ships `HttpServer`.

Measured, so this is not speculation: `src/core/net/HttpServer.cpp` (334 lines) contains **zero**
occurrences of `shutdownWrite`, `close(`, `linger`, `SO_LINGER` or `drain`. It closes through
destructors. So core-cpp's HTTP server has the latent form of the defect today.

**Ruling: NOT folded into Phase B.** The plan is approved, v0.1.0's scope is the merge, and
importing an eighth component into B9 to fix a latent defect in B10's file is how a release slips.
**Recorded instead as [core-cpp#35](https://github.com/contour-terminal/core-cpp/issues/35) with this evidence**, and B10 — which adapts
`HttpServer` — is told the finding so its author can decide whether the adaptation should at least
not make it worse.

Cost if wrong: core-cpp 0.1.0 ships an HTTP server that can lose a refusal response under an unread
request body, which is a defect that reproduces against real clients and not against loopback tests
— the reason it is written down here rather than left to be rediscovered.

## 6. Named and accounted for, no action

- `Net/IocpStatus.hpp` — B7's `CompletionStatus`. B7a told.
- `Net/TlsWrap.hpp` — B11's `wrapTls`.
- `Net/PlatformListener.hpp` — B8's `listen()`/`adoptListener`.
- `Net/IDatagramSocket.hpp` — the plan's Task B9 calls this `Datagram`.
- `Net/{ReadSlot,WriteSlot}.hpp` — B6's, already corrected in its dispatch.

## 7. Two tasks both claim the same two files

`Net/BlockingConnector.*` and `Net/IAdmissionControl.hpp` appear in **both** B8's dispatch Sources
and the plan's Task B9 import list.

**Ruling: both belong to B8.** `BlockingConnector` is a connector and `IAdmissionControl` is a dial
concern; B9's subject is datagrams and blocking *transports*. B9's brief is corrected to consume
them rather than import them. Cost if wrong: a merge conflict in `CMakeLists.txt` and one file
moved between two commits — cheap, and much cheaper than two lanes importing the same file twice
and each discovering the other's copy at review.
