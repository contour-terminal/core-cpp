// SPDX-License-Identifier: Apache-2.0
#include <core/net/UdpSocket.hpp>
#include <core/net/testing/DatagramPayload.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using core::net::BroadcastMode;
using core::net::DatagramAddress;
using core::net::NetErrorCode;
using core::net::openUdpSocket;
using core::net::PortSharing;
using core::net::testing::datagramBytes;
using core::net::testing::datagramText;

// The real UDP socket, over loopback. `testing/InMemoryDatagram_test.cpp` is where the logic above
// this seam is tested; this is what proves the implementation behind the same seam actually sends
// and receives — an interface with only a fake behind it is an interface nobody has checked.
//
// Loopback and kernel-chosen ports throughout, so nothing here needs a fixture and nothing can
// collide with whatever else is on the machine: this suite runs in parallel.

TEST_CASE("A real UDP socket round-trips a datagram", "[net][datagram][udp]")
{
    auto receiver = openUdpSocket("127.0.0.1", 0, BroadcastMode::Off);
    REQUIRE(receiver.has_value());

    auto const bound = (*receiver)->boundAddress();
    REQUIRE_FALSE(bound.host.empty());
    REQUIRE(bound.port != 0);

    auto sender = openUdpSocket("127.0.0.1", 0, BroadcastMode::Off);
    REQUIRE(sender.has_value());

    REQUIRE((*sender)->send(datagramBytes("over the wire"), bound).has_value());

    auto const received = (*receiver)->receive(2s);
    REQUIRE(received.has_value());
    CHECK(datagramText(*received) == "over the wire");
    CHECK(received->from == (*sender)->boundAddress());
}

TEST_CASE("A datagram too large for the path is MessageTooLarge, not a short send", "[net][datagram][udp]")
{
    // **A short send on a datagram socket is not a partial write to retry.** The kernel places a
    // datagram whole or not at all, so a `sendto` that reports fewer bytes than it was given means
    // the message was too large for the path — and a caller that read it as a partial write would
    // resend the tail as a datagram of its own, which is a second message rather than the rest of
    // the first.
    //
    // 70000 bytes is past the 65507 a UDP datagram can carry over IPv4 whatever the path, so this
    // is the refusal itself rather than a fragmentation limit that differs per interface: `EMSGSIZE`
    // on POSIX, `WSAEMSGSIZE` on Winsock. Upstream reported both this and the short send as
    // `SystemError`; `MessageTooLarge` is already in this library's vocabulary and says which.
    auto sender = openUdpSocket("127.0.0.1", 0, BroadcastMode::Off);
    REQUIRE(sender.has_value());

    auto receiver = openUdpSocket("127.0.0.1", 0, BroadcastMode::Off);
    REQUIRE(receiver.has_value());

    std::vector<std::byte> const oversized(70000, std::byte { 0x5a });
    auto const result = (*sender)->send(oversized, (*receiver)->boundAddress());

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == NetErrorCode::MessageTooLarge);
}

TEST_CASE("A datagram past the old 8 KiB receive bound arrives whole or not at all", "[net][datagram][udp]")
{
    // **Never truncated**, which is the property and the reason the receive buffer is the largest a
    // datagram can be rather than a figure chosen for memory. `recvfrom` into a smaller buffer
    // truncates silently — the caller gets a full buffer and no way to tell it from a datagram that
    // happened to be exactly that long — so a bound below the maximum turns an oversized message
    // into a corrupt one. Upstream's bound was 8192.
    //
    // "Or not at all" because UDP may drop it and a test that demanded delivery would be asserting
    // something the transport does not promise. What it must never do is deliver 8192 of the 20000
    // bytes and call that the message.
    auto receiver = openUdpSocket("127.0.0.1", 0, BroadcastMode::Off);
    REQUIRE(receiver.has_value());

    auto sender = openUdpSocket("127.0.0.1", 0, BroadcastMode::Off);
    REQUIRE(sender.has_value());

    std::string const payload(20000, 'x');
    REQUIRE((*sender)->send(datagramBytes(payload), (*receiver)->boundAddress()).has_value());

    auto const received = (*receiver)->receive(2s);
    if (received.has_value())
        CHECK(received->payload.size() == payload.size());
}

TEST_CASE("Two real UDP sockets on one port: only one is handed a unicast", "[net][datagram][udp]")
{
    // The kernel behaviour the shared-port fix is premised on, asserted against a real stack rather
    // than trusted. `PortSharing::Shared` lets both of these bind — which is what a segment needs,
    // since every node listens where the others shout — and then exactly one of them is handed a
    // unicast. Which one is not portable: measured, Windows 11 picks the first-bound and Ubuntu
    // 24.04 the last, so the assertion is the count.
    //
    // It is also what proves `PortSharing::Shared` means the same thing on all three platforms.
    // SO_REUSEADDR alone would fail the second bind on macOS, and this is the case that would say
    // so.
    //
    // Unicast only. The broadcast half is what SO_REUSEADDR is *for* and is not in doubt; asserting
    // it here would mean putting a datagram on the segment from a unit test, which a CI runner may
    // refuse and a colleague's LAN should not have to see.
    auto first = openUdpSocket("127.0.0.1", 0, BroadcastMode::Off, PortSharing::Shared);
    REQUIRE(first.has_value());
    auto const shared = (*first)->boundAddress();
    REQUIRE(shared.port != 0);

    auto second = openUdpSocket("127.0.0.1", shared.port, BroadcastMode::Off, PortSharing::Shared);
    REQUIRE(second.has_value());
    CHECK((*second)->boundAddress().port == shared.port);

    auto sender = openUdpSocket("127.0.0.1", 0, BroadcastMode::Off);
    REQUIRE(sender.has_value());
    REQUIRE((*sender)->send(datagramBytes("challenge"), shared).has_value());

    // A short wait on the second of the two, because the interesting outcome — the one that breaks
    // discovery — is that it never arrives, and that answer is only ever a timeout.
    auto const atFirst = (*first)->receive(2s);
    auto const atSecond = (*second)->receive(200ms);

    CHECK(static_cast<int>(atFirst.has_value()) + static_cast<int>(atSecond.has_value()) == 1);
    if (atFirst.has_value())
        CHECK(datagramText(*atFirst) == "challenge");
    else
        CHECK(datagramText(*atSecond) == "challenge");
}

TEST_CASE("An exclusive UDP socket keeps its address to itself", "[net][datagram][udp]")
{
    // The other half of the contract, and the reason `PortSharing` is a parameter rather than
    // something every UDP socket gets. Sharing is what a beacon port needs; a socket whose whole job
    // is that the answer addressed to it arrives at IT must not share, and nothing would notice if
    // it quietly did — the datagrams would simply go to the wrong process now and then.
    auto held = openUdpSocket("127.0.0.1", 0, BroadcastMode::Off, PortSharing::Exclusive);
    REQUIRE(held.has_value());
    auto const bound = (*held)->boundAddress();
    REQUIRE(bound.port != 0);

    auto const refused = openUdpSocket("127.0.0.1", bound.port, BroadcastMode::Off, PortSharing::Exclusive);
    REQUIRE_FALSE(refused.has_value());
    // The code, not merely the refusal: a bind that failed for any other reason — no such address,
    // no permission for the port — would pass a bare `has_value()` check while saying nothing about
    // exclusivity, and this case is the only thing asserting that `Exclusive` does anything at all.
    CHECK(refused.error().code == NetErrorCode::AddressInUse);
}

TEST_CASE("A real UDP socket reports an address it cannot use", "[net][datagram][udp]")
{
    auto sender = openUdpSocket("127.0.0.1", 0, BroadcastMode::Off);
    REQUIRE(sender.has_value());

    // `.invalid` is reserved by RFC 6761 and guaranteed not to resolve, so this is the resolution
    // failure rather than a DNS round trip. It is what remains of this case now that a malformed
    // `host:port` cannot reach this layer at all: the halves arrive apart, so the only address a
    // caller can still get wrong is one that names nothing.
    auto const result =
        (*sender)->send(datagramBytes("x"), DatagramAddress { .host = "example.invalid", .port = 7000 });
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == NetErrorCode::AddressNotAvail);

    // An address with no host at all is refused before resolution, and that is not the same check:
    // `getaddrinfo("", ...)` is EAI_NONAME on glibc but SUCCEEDS on Winsock, resolving to the local
    // host. Left to the resolver, a datagram aimed at a sender this process could not render would
    // be refused on one platform and quietly sent to loopback on the other.
    auto const nowhere = (*sender)->send(datagramBytes("x"), DatagramAddress { .host = "", .port = 7000 });
    REQUIRE_FALSE(nowhere.has_value());
    CHECK(nowhere.error().code == NetErrorCode::AddressNotAvail);
}

TEST_CASE("A closed real UDP socket stops its receive loop", "[net][datagram][udp]")
{
    // The property the whole `receive(timeout)` shape exists for, on the transport it exists for:
    // POSIX does not unblock a parked `recvfrom` when another thread closes the socket, so the poll
    // timeout is the only portable way a discovery loop ever observes a shutdown. This lineage has
    // already paid for the equivalent omission on `accept()` — a `systemctl stop` that hung until
    // the supervisor escalated to SIGKILL.
    auto socket = openUdpSocket("127.0.0.1", 0, BroadcastMode::Off);
    REQUIRE(socket.has_value());

    (*socket)->close();

    auto const result = (*socket)->receive(2s);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == core::net::DatagramWait::Closed);
}
