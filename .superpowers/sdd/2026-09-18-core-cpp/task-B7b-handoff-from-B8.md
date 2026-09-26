# Hand-off to Task B7b (the IOCP socket) from Task B8 (dialling)

Written in B8's fix round 1, from review finding L3. Until now the hand-off existed only in
`task-B8-report.md` §9, which B7b has no reason to read.

## What B8 did not build, and why

`IocpDial` (fastcached `Net/IocpDial.hpp` at `0708dd54`) is **not imported**. It issues `ConnectEx`
and settles through an `IocpCompletion` dequeued by the port. It also needs an `IocpSocket` to hand
back, and core-cpp has no `IocpSocket`: B7a landed `IocpBackend`, and B7b owns the socket.
Windows therefore dials through the readiness path (`windows/DialPrimitives.cpp`, `WSAEventSelect`),
which is what `WindowsSocket` already is. `renames.json` has `removed` rows for
`core::net::IocpConnector` and `core::net::detail::ConnectOp`. When B7b brings the dial in, those
rows go with it.

## Where it plugs in

`detail::DialStep` (`src/core/net/ConnectFlow.hpp`). Add a second step next to
`ReadinessConnector::dialStep` in `src/core/net/Connector.cpp`, and select it where the loop's
backend is `BackendKind::Iocp`. A comment at that seam states the two rules below, so they sit where
the change will be made.
`runConnectFlow` stays as it is: it owns the budget and resolution (both stop-aware since this fix
round) and the candidate loop, and it hands each candidate a deadline.

## Two things that must NOT be carried over from the readiness dial

1. **`ConnectEx` reports its outcome in the COMPLETION STATUS, not through `SO_ERROR`.** A
   successful completion must also be followed by
   `setsockopt(SO_UPDATE_CONNECT_CONTEXT)` before the socket supports `getpeername`, `shutdown` and
   the rest. R101's principle carries over: ask the operation, never the notification. Its literal
   idiom (`getsockopt(SO_ERROR)` after readiness) does not. Map the completion's error through the
   same `fromDialError` the readiness dial uses, so `WSAECONNREFUSED` is still
   `NetErrorCode::ConnRefused`.
2. **On a completion model the deadline must CANCEL the operation and let the completion report.**
   The completion is the single writer of the outcome. The readiness dial's deadline settles the
   dial itself, which is correct there because nothing else will write. Doing the same on IOCP lets
   a completion arrive later into a frame that is gone. So the deadline calls `CancelIoEx` on the
   socket, and the completion then arrives with `ERROR_OPERATION_ABORTED` and settles the dial as
   `Timeout`. A stop of the flow's token does the same thing and settles as cancelled (throws
   `OperationCancelled`, as the readiness dial does).

## Also carried over from B8's report

- `ConnectEx` needs the socket **bound** first (to `INADDR_ANY`/`in6addr_any`, port 0) and
  `ConnectEx` fetched through `WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)`. Upstream does both.
- `adoptDialled` on Windows closes the dial's own `WSAEVENT` before constructing the socket,
  because `WSAEventSelect` allows exactly one event object per socket and `WindowsSocket` makes its
  own. An IOCP socket has no such constraint, so do not carry the close over without deciding it
  again.
- **The refused-connect case must gain an IOCP leg.** "a refused connect completes with the
  refusal on every backend" (`ReadinessDial_test.cpp`) and its `Connector_test.cpp` twin run over
  `BackendMatrix`. Neither exercises an IOCP dial today, because there is none. The same case also
  reports which path a refusal took (synchronous or through readiness) and SKIPs out loud where the
  readiness path was not exercised. Give the IOCP leg the equivalent: assert that the refusal came
  through a completion.
- **Cancellation cases.** `ReadinessDial_test` has three: a root stop via `requestStop()`, a
  `whenAny` loser (the only one that reaches the dial's own stop callback, and it asserts the dial
  was parked), and a stop from another thread. Copy their shape for the IOCP dial, above all the
  `whenAny` one: `requestStop()` also unparks everything, so it proves nothing about the dial's own
  cancellation route.
