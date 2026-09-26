# Task B9 review (range 8dc3361..0691aeb: b9428ad B6d shutdownWrite + 0691aeb B9)

Reviewer: review lane, read-only. I checked every finding below against the code at 0691aeb
(worktree `D:/core-cpp-wt-b9b`). I built and ran nothing, and CI has no run for either commit.

## Verdict: CHANGES

There are two blockers, and they have the same cause. Both use a C++ library facility that Apple's
libc++ does not ship, and the repository already records that fact; for one of them it already has
a seam. As a result the macOS legs (`appleclang-debug`, `appleclang-release`) would not compile
`core::net` or its test binary. Everything else is should-fix or nit. The design, the parity test
and the EPIPE change are sound.

## Blockers

### B1. `std::ranges::fold_left` in a library source bypasses the `core::ranges::FoldLeft` seam
`src/core/net/testing/InMemorySocket.cpp:381`.
`src/core/Ranges.hpp:105-128,312-318` exists because Apple's libc++ lacks `std::ranges::fold_left`
(fastcached#1392). It selects `detail::FoldLeftFallback` by `__cpp_lib_ranges_fold`. This file is
in `core::net`'s SOURCES, so on the macOS jobs the LIBRARY fails to compile, not just a test.
Fix: `core::ranges::FoldLeft(..., std::size_t { 0 }, std::plus {})` from `<core/Ranges.hpp>`.
net depends on base, so that edge already exists.

### B2. `std::jthread` in two test files
`src/core/net/BlockingSocket_test.cpp:269`, `src/core/net/HealthProbe_test.cpp:80,95`.
The tree records twice that AppleClang's libc++ has no `<stop_token>` and therefore no `jthread`
(`src/core/net/testing/TestLoop_test.cpp:376-377`, `src/core/tui/runtime/TuiRuntime_test.cpp:495`).
Those files use `std::thread` with an explicit join. Both files here are in the portable `net` test
list, so the `net` test binary does not build on macOS. Fix: use `std::thread` with an explicit
join. `ServedLoop` already joins in its destructor, so it only needs its `joinable()` guard.

## Should-fix

### S1. `MaxDatagramPayload` (65507) truncates a legal IPv6 datagram, which contradicts its own contract
`src/core/net/UdpSocket.hpp:73-86`, used by `posix/UdpSocket.cpp:47,104` and by the Windows half.
The constant's doc says "a legal datagram is never truncated", and `.agent/rules/async-and-net.md`
now states that as a rule. It holds for IPv4 only. An IPv6 UDP payload may be 65527 bytes: the
payload length field is 65535, minus the 8-byte UDP header, and the IPv6 header is not counted.
`openUdpSocket` binds whatever `getaddrinfo(AF_UNSPEC)` returns, so IPv6 sockets are supported.

Scenario: a 65520-byte datagram is sent to `::1`.
- POSIX `recvfrom` fills 65507 bytes and returns them as the whole message, because nothing checks
  `MSG_TRUNC`. That is exactly the silent corruption the constant was introduced to remove.
- Winsock fails with `WSAEMSGSIZE`, which `receive` reports as `TimedOut`.

It is a public constant, so changing it later is an API change. Fix: use 65527, or detect
truncation (`MSG_TRUNC` on POSIX, `WSAEMSGSIZE` on Windows) and drop the datagram rather than
deliver it.

### S2. The 8 KiB-bound regression case passes vacuously when nothing arrives, which on Windows is always
`src/core/net/UdpSocket_test.cpp:74-97`: `if (received.has_value()) CHECK(size == 20000)`.
The case exists to catch one mutation: the buffer going back to 8192. Under that mutation, Winsock
fails the receive with `WSAEMSGSIZE`. `receive` maps every error to `DatagramWait::TimedOut`, so
the case passes without running any assertion. The commit's mutation table ("2 cases red") can
therefore only be a Linux result. testing.md says a case that could not run SKIPs; it never passes
silently. Fix: on loopback a 20000-byte datagram is not dropped in practice, so
`REQUIRE(received.has_value())`. Or `SKIP` with a reason when it did not arrive. U1 below adds that
on macOS this case may fail at the send.

### S3. Blocking-socket cases wait without a bound
`src/core/net/BlockingSocket_test.cpp:93-111`: `connectPair` dials with `ioTimeout` 0, so there is
no `SO_RCVTIMEO`. Three waits follow from that:
- `:228-231` The half-close case reads the peer on the loop (`blockOn(readOnce(accepted))`) with no
  bound. The regression it guards against is `shutdownWrite` being a no-op, and under that
  regression the case hangs until the ctest timeout instead of failing. The timeout names nothing.
- `:216-219` `waitReadable` after `accepted->close()` blocks without a bound if the code regresses.
- `:269-276` In the zero-deadline section, the read has had its bound REMOVED by design. The `late`
  thread's write result is discarded with `std::ignore`. If that write fails, the main thread
  blocks in `recv` forever.

testing.md says every wait is bounded and says what it waited for. Fix:
- use `BlockingConnectorOptions { .ioTimeout = 5s }` in `connectPair`;
- bound the loop read with `withTimeout`, or with `anyOf` plus `sleepFor`;
- in the zero-deadline section, have the thread call `shutdownWrite` or close the accepted end
  after its write, whatever the write returned, so the read always returns.

## Nits

- **N1** `src/core/net/SocketClosedStates_test.cpp:717-732`: "the fake: the read waits" is vacuous.
  The fake has no receive deadline (its header says so), so the section never exercises
  `setReceiveDeadline(0)`, and no time passes before the write. It would pass under any
  implementation of zero. Either drop it or say in the section that it only pins the no-op.
- **N2** `SocketClosedStates_test.cpp:127,177-181`: `rowOf`'s `assert` is compiled out under
  `NDEBUG`. So the `static_assert`s that claim "checked at compile time" check nothing in any
  Release leg. A plain `if (row.key != value) throw ...;` in the constexpr function fails constant
  evaluation in every configuration.
- **N3** `src/core/net/testing/InMemorySocket.cpp:263,270,297,301`: the verb's state lives on the
  socket (`_readPending`), not in the awaitable. Consider `auto a = s->read(buf);
  auto b = s->waitReadable(); co_await a;`:
  - `a` is armed with `b`'s `Probe` kind and empty buffer.
  - If data is buffered when `a` arms, the second arm pulls into an empty span and reports `0`,
    which reads as EOF.

  This is a contract violation by the caller, but the arm-time claim catches it only when the first
  operation parked. The real sockets carry the buffer with the operation.
- **N4** `InMemorySocket.hpp:167-171`: the fake ignores the flow's stop token. A consumer test that
  cancels a parked read over it (`withTimeout`) hangs, where a real socket throws
  `OperationCancelled`. This is documented, and it produces a hang rather than a false pass. It is
  worth a follow-up ticket, because B6 made "stop -> throws" the contract.
- **N5** `src/core/net/testing/InMemoryDatagram.cpp` (`deliver`): `DatagramBus` accepts a
  70000-byte payload and an empty host. The real socket refuses both (`MessageTooLarge`,
  `AddressNotAvail`). The new rule "a test double is pinned to the real thing" applies here too; at
  least note the gap in the header.
- **N6** `posix/UdpSocket.cpp:47,104` and `windows/UdpSocket.cpp:47,115`: upstream used a per-call
  local receive buffer; this port makes it a member. That turns two concurrent `receive` calls into
  a data race. `SharedPortDatagram`'s `_ownFirst` already implies a single receiver; say so on
  `IDatagramSocket::receive`.
- **N7** `src/core/net/UdpSocket_test.cpp:181-195`: "a closed socket stops its receive loop"
  closes BEFORE receiving, so only the pre-check runs. The case the comment describes, a close while
  the receive is parked, goes through the post-`recvfrom` check, and nothing tests that.
- **N8** `src/core/net/HealthProbe.cpp` (request build): for an unbracketed IPv6 literal the probe
  sends `Host: ::1`. That is not a valid Host header (RFC 7230 wants `[::1]`), and a strict server
  answers 400.
- **N9** `src/core/net/posix/BlockingPrimitives.cpp` `waitDialled`: on EINTR, `poll` restarts with
  the full timeout. A process that receives many signals can outlast the budget.
- **N10** CHANGELOG Breaking (EPIPE): "No consumer in contour, endo or tuidu branches on
  `ConnReset`" names three of the six consumers AGENT.md lists. I checked the other three:
  fastcached, Lightweight and morph do not branch on it either (fastcached only produces it in
  fakes). Add them to the sentence.
- **N11** Process: `.superpowers/sdd/2026-09-18-core-cpp/task-B9-report.md`, named as an input,
  does not exist in either checkout. The commit message carries the report instead. So the gate runs
  that step 4 of the brief requires are not recorded anywhere I could read.

## Unverified risk (not a finding; check on the macOS leg)

- **U1** macOS caps an outgoing UDP datagram at `net.inet.udp.maxdgram`, which defaults to 9216
  bytes. If that still holds on `macos-15`, `UdpSocket_test.cpp:91`
  (`REQUIRE(send(20000 bytes).has_value())`) fails there with `MessageTooLarge`. I have not
  verified this on hardware. B2 stops the binary from building on macOS anyway, so the first macOS
  run that compiles will settle it.

## What I checked and found sound

1. **Spec compliance.** Every test the brief names is present:
   - `InMemoryDatagram_test`;
   - `SharedPortDatagram_test`;
   - `BlockingSocket_test`, without the `BlockingListener`/`AcceptRaw` cases, which were dropped
     along with those classes (stated in provenance);
   - `TcpClient_test`;
   - `HealthProbe_test`, against `core::net::serve`;
   - a new `UdpSocket_test` with `MessageTooLarge`;
   - `SocketClosedStates_test`, which the brief's corrections make mandatory.

   `SocketClosedStates_test` keeps the parity property intact. It runs one step table over
   `InMemorySocketPair` AND over a real accepted/dialled loopback pair, and asserts both at every
   step. `admits` never lets the fake answer where the real socket fails.

   Every listed import is present: `IDatagramSocket` (the plan's `Datagram`), `UdpSocket`,
   `SharedPortDatagram`, `BlockingSocket`, `TcpClient`, `HealthProbe`, and
   `testing/{InMemoryDatagram,InMemorySocket,SocketDecorator,ParkingReadableSocket,DatagramPayload}`.

   Three departures from the brief, each justified:
   - `BlockingConnector` is imported here rather than in B8. B8's report ruled that
     (task-B8-report.md:57-61, 345), because it constructs a `BlockingSocket`.
   - `IAdmissionControl` belongs to B8 and landed there.
   - The "dedupe InMemoryTransport" instruction is deliberately not carried out. The brief's own
     correction 4 is the justification: merging the fake into the real pair deletes what the parity
     test measures.
2. **EPIPE -> SystemError** is consistent everywhere:
   - `PosixSocket` drops its EPIPE row.
   - The new shared POSIX table has no EPIPE row.
   - The POSIX dial primitives never had one.
   - `WindowsSocket::fromWsa` and the shared Winsock table both leave `WSAESHUTDOWN` and
     `WSAECONNABORTED` as `SystemError`.
   - The fake answers `SystemError` both for its own half-close and for a write that drew the reset.

   The change is recorded under `### Breaking` with a migration, and it matches upstream's table,
   which had no EPIPE row.
3. **B6d `shutdownWrite`.** Every override is updated: `PosixSocket`, `WindowsSocket`,
   `SplitSocket`, `BlockingSocket`, `InMemorySocket`, `SocketDecorator`. No `void` override remains
   in `src`. `TlsSocket` inherits the default, and the docs say so. ENOTCONN and WSAENOTCONN still
   resolve as success. The change is recorded under Breaking with a migration and a
   `renames.json` note. No consumer calls it (contour, endo, tuidu, Lightweight, morph); the calls in
   fastcached are to its own `ShutdownWrite`.
4. **Lifetime in the fake.** `close` follows the safe order:
   - it detaches the wake-ups and both operations first;
   - it holds pipe references in locals;
   - it delivers the reset before the FIN;
   - it settles the parked operations last, without touching `this`.

   The destructor abandons parked operations rather than completing them. Wake-ups are read from the
   pipe when they are called, so a peer destroyed mid-sequence has already deregistered. Both slots
   (one read, one write) are claimed through `contract::claimReadSlot` and `claimWriteSlot` at the
   verb and again at the arm. The new canary watches both refuse; it is judged on a marker and exits
   77 under NDEBUG.
5. **Blocking transports.**
   - `BlockingSocket`: every awaitable it returns is already settled, so driving it with `syncRun`
     is sound. EINTR is retried in send, recv and peek. SIGPIPE is suppressed per socket
     (`MSG_NOSIGNAL`, `SO_NOSIGPIPE`), never process-wide, and a test asserts that the process
     disposition does not change.
   - `BlockingConnector`: it runs `runConnectFlow` with a null loop and arms both timeouts before
     it hands the socket over. It never passes the kernel an exhausted budget, which the kernel would
     read as "infinite". It is non-copyable, which matters because its `_clock` may bind to its own
     `_ownClock`.
   - The Windows dial clears the event selection before it restores blocking mode.
6. **Hygiene.**
   - Every new file has a provenance row, and the CMakeLists row is amended.
   - New public headers are in `FILE_SET HEADERS`; private ones (`detail/*`) are in SOURCES.
   - The source list chooses the platform halves.
   - The Imported table has the fastcached row.
   - No file in the commit has a NOLINT, a diagnostic pragma, a C-style `for` or a CRLF.
   - `renames.json` parses.
