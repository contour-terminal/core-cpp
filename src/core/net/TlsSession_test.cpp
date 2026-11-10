// SPDX-License-Identifier: Apache-2.0
//
// What a finished handshake lets a caller ask of its connection: the minimum protocol version a
// context enforces, the channel binding both ends derive, and the peer's certificate fingerprint.
//
// The refusal of an old client is held against `StrictTlsPeer`, OpenSSL driven by hand, so it is
// not a `TlsSocket` agreeing with itself; the binding is compared between two `TlsSocket`s because
// equality across the two ends is the property.
#include <core/Sha256.hpp>
#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/Tls.hpp>
#include <core/net/WithTimeout.hpp>
#include <core/net/testing/CoroTestSupport.hpp>
#include <core/net/testing/InMemoryTransport.hpp>
#include <core/net/testing/StrictTlsPeer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <utility>

using core::async::Task;
using core::net::EventLoop;
using core::net::ISocket;
using core::net::TlsOptions;
using core::net::TlsVersion;
using core::net::testing::StrictTlsPeer;

namespace
{

/// A hung handshake is a regression here, not a slow test: bound every exchange.
constexpr auto ExchangeBound = std::chrono::milliseconds { 10'000 };

/// Two `TlsSocket`s, one per end of a socket pair, handshaken.
struct Session
{
    std::unique_ptr<core::net::IoBackend> backend = core::net::makeDefaultBackend();
    EventLoop loop { *backend };
    std::unique_ptr<ISocket> server;
    std::unique_ptr<ISocket> client;
    core::net::CertKeyPem material;

    Session(TlsOptions serverOptions, bool handshake)
    {
        auto generated = core::net::generateSelfSignedCertificate();
        REQUIRE(generated.has_value());
        material = std::move(*generated);
        auto pair = core::net::testing::makeSocketPair(loop);
        REQUIRE(pair.has_value());
        auto serverContext =
            core::net::makeTlsServerContext(material.certPem, material.keyPem, serverOptions);
        REQUIRE(serverContext.has_value());
        auto clientContext = core::net::makeTlsClientContext();
        REQUIRE(clientContext.has_value());
        server = (*serverContext)->wrap(std::move(pair->first), loop);
        client = (*clientContext)->wrap(std::move(pair->second), loop);
        REQUIRE(server != nullptr);
        REQUIRE(client != nullptr);
        if (handshake)
            REQUIRE(shake(server.get(), client.get()));
    }

    /// Drives both handshakes concurrently.
    /// @return Whether both succeeded.
    bool shake(ISocket* a, ISocket* b)
    {
        auto ok = std::make_shared<std::array<bool, 2>>();
        auto const side = [](ISocket* socket, bool* out) -> Task<void> {
            *out = (co_await socket->handshakeIfNeeded()).has_value();
        };
        auto const finished = loop.blockOn(core::net::withTimeout(
            &loop, core::net::testing::allOf(side(a, &(*ok)[0]), side(b, &(*ok)[1])), ExchangeBound));
        return finished && (*ok)[0] && (*ok)[1];
    }
};

} // namespace

TEST_CASE("a TLS 1.3-only server refuses a 1.2-only client", "[net][tls][session]")
{
    auto backend = core::net::makeDefaultBackend();
    EventLoop loop { *backend };
    auto material = core::net::generateSelfSignedCertificate();
    REQUIRE(material.has_value());
    auto context = core::net::makeTlsServerContext(
        material->certPem, material->keyPem, TlsOptions { .minimumVersion = TlsVersion::Tls13 });
    REQUIRE(context.has_value());
    auto pair = core::net::testing::makeSocketPair(loop);
    REQUIRE(pair.has_value());
    auto server = (*context)->wrap(std::move(pair->first), loop);
    auto wire = std::move(pair->second);
    REQUIRE(server != nullptr);
    auto peer = StrictTlsPeer::client(TlsVersion::Tls12);
    REQUIRE(peer.has_value());

    auto serverOk = true;
    auto peerOk = true;
    auto const serverSide = [](ISocket* socket, bool* ok) -> Task<void> {
        *ok = (co_await socket->handshakeIfNeeded()).has_value();
    };
    auto const peerSide = [](StrictTlsPeer* strict, ISocket* socket, bool* ok) -> Task<void> {
        *ok = (co_await strict->handshake(socket)).has_value();
    };
    auto const finished = loop.blockOn(
        core::net::withTimeout(&loop,
                               core::net::testing::allOf(serverSide(server.get(), &serverOk),
                                                         peerSide(peer->get(), wire.get(), &peerOk)),
                               ExchangeBound));
    REQUIRE(finished);
    CHECK_FALSE(serverOk);
    CHECK_FALSE(peerOk);
}

TEST_CASE("a TLS 1.3-only server negotiates 1.3 with a modern client", "[net][tls][session]")
{
    Session session { TlsOptions { .minimumVersion = TlsVersion::Tls13 }, true };
    auto const* const server = core::net::tlsSessionOf(*session.server);
    auto const* const client = core::net::tlsSessionOf(*session.client);
    REQUIRE(server != nullptr);
    REQUIRE(client != nullptr);
    CHECK(server->negotiatedVersion() == TlsVersion::Tls13);
    CHECK(client->negotiatedVersion() == TlsVersion::Tls13);
}

TEST_CASE("both ends of one session export the same channel binding", "[net][tls][session]")
{
    Session first { {}, true };
    auto const* const server = core::net::tlsSessionOf(*first.server);
    auto const* const client = core::net::tlsSessionOf(*first.client);
    REQUIRE(server != nullptr);
    REQUIRE(client != nullptr);
    auto const serverBinding = server->channelBinding();
    auto const clientBinding = client->channelBinding();
    REQUIRE(serverBinding.has_value());
    REQUIRE(clientBinding.has_value());
    CHECK(serverBinding->size() == 32);
    CHECK(*serverBinding == *clientBinding);

    Session second { {}, true };
    auto const otherBinding = core::net::tlsSessionOf(*second.server)->channelBinding();
    REQUIRE(otherBinding.has_value());
    CHECK(*otherBinding != *serverBinding);
}

TEST_CASE("the client sees the server certificate's fingerprint", "[net][tls][session]")
{
    Session session { {}, true };
    auto const expected = core::net::certificateFingerprint(session.material.certPem);
    REQUIRE(expected.has_value());
    auto const* const client = core::net::tlsSessionOf(*session.client);
    REQUIRE(client != nullptr);
    CHECK(client->peerCertificateFingerprint() == *expected);
    CHECK(expected->size() == 64);
}

TEST_CASE("channelBinding before the handshake is an error", "[net][tls][session]")
{
    Session session { {}, false };
    auto const* const server = core::net::tlsSessionOf(*session.server);
    REQUIRE(server != nullptr);
    CHECK_FALSE(server->channelBinding().has_value());
    CHECK(server->peerCertificateFingerprint().empty());
}

TEST_CASE("tlsSessionOf is null for a socket that is not TLS", "[net][tls][session]")
{
    auto backend = core::net::makeDefaultBackend();
    EventLoop loop { *backend };
    auto pair = core::net::testing::makeSocketPair(loop);
    REQUIRE(pair.has_value());
    CHECK(core::net::tlsSessionOf(*pair->first) == nullptr);
}

TEST_CASE("sshStyleFingerprint matches ssh-keygen -lf", "[net][tls][session]")
{
    // sha256 of the empty input; ssh-keygen spells a digest as SHA256: and the base64 without padding.
    auto const digest = core::Sha256::hash(std::string_view {});
    CHECK(core::net::sshStyleFingerprint(digest) == "SHA256:47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU");
}
