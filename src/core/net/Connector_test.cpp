// SPDX-License-Identifier: Apache-2.0
#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/IAsyncAddressResolver.hpp>
#include <core/net/IConnector.hpp>
#include <core/net/IListener.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/SocketAddress.hpp>
#include <core/net/Sockets.hpp>
#include <core/net/ThreadedAddressResolver.hpp>
#include <core/net/testing/BackendMatrix.hpp>
#include <core/net/testing/CoroTestSupport.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using core::async::Task;
using core::net::DialOptions;
using core::net::EventLoop;
using core::net::KeepAlive;
using core::net::NetErrorCode;
using core::net::ResolvedEndpoint;
using core::net::SocketResult;
using core::net::testing::BackendMatrix;

namespace
{

/// A blocking resolver that answers "this loopback port" for any name, and records the thread it
/// was asked on.
///
/// **The whole point of the seam.** `getaddrinfo` takes no timeout and a wedged resolver parks
/// whoever called it for as long as the platform's resolver library feels like; on an event loop
/// that is every connection on it, including the ones with nothing to do with the network. A
/// recording resolver is how that rule becomes an assertion instead of a paragraph.
class RecordingResolver final: public core::net::IAddressResolver
{
  public:
    explicit RecordingResolver(std::uint16_t port) noexcept: _port(port) {}

    [[nodiscard]] std::expected<std::vector<ResolvedEndpoint>, std::string> resolve(
        std::string_view /*host*/, std::uint16_t /*port*/) override
    {
        {
            auto const guard = std::scoped_lock { _mutex };
            _callers.push_back(std::this_thread::get_id());
        }
        auto system = core::net::SystemAddressResolver {};
        return system.resolve("127.0.0.1", _port);
    }

    /// @return The thread each lookup ran on, in call order.
    [[nodiscard]] std::vector<std::thread::id> callers() const
    {
        auto const guard = std::scoped_lock { _mutex };
        return _callers;
    }

  private:
    mutable std::mutex _mutex;
    std::vector<std::thread::id> _callers;
    std::uint16_t _port;
};

/// Accepts one connection and reads @p expected from it.
Task<void> acceptAndRead(core::net::IListener* listener, std::string_view expected, bool* ok)
{
    auto accepted = co_await listener->accept();
    if (!accepted.has_value())
        co_return;
    auto buffer = std::array<std::byte, 64> {};
    auto const read = co_await (*accepted)->read(std::span<std::byte> { buffer }.first(expected.size()));
    *ok = read.has_value() && *read == expected.size()
          && std::string_view { reinterpret_cast<char const*>(buffer.data()), expected.size() } == expected;
}

/// Dials through @p connector, writes @p payload, and records the loop thread it ran on.
Task<void> dialAndWrite(core::net::IConnector* connector,
                        std::string host,
                        std::uint16_t port,
                        std::string_view payload,
                        bool* ok,
                        std::thread::id* ranOn)
{
    *ranOn = std::this_thread::get_id();
    auto dialled = co_await connector->connect(std::move(host), port, DialOptions {});
    if (!dialled.has_value())
        co_return;
    auto const bytes =
        std::span<std::byte const> { reinterpret_cast<std::byte const*>(payload.data()), payload.size() };
    auto const written = co_await (*dialled)->write(bytes);
    *ok = written.has_value() && *written == payload.size();
}

} // namespace

TEST_CASE("connect() never resolves a name on the loop's thread", "[net]")
{
    // **The case this task exists for.** An injected resolver records the thread that called it,
    // and the dial asserts that thread is not the loop's. Written against contour's inline
    // `getaddrinfo` it could not even be expressed: there was no seam to inject through.
    auto source = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(source != nullptr);
    auto loop = EventLoop { *source };

    auto bound = core::net::listen(loop, core::net::ListenOptions { .host = "127.0.0.1" });
    REQUIRE(bound.has_value());
    auto listener = std::move(*bound);

    // A NAME, so the fast path for literals does not apply and the lookup is genuinely offloaded.
    // The inner answers from the listener's port, so no name server is involved anywhere.
    auto inner = RecordingResolver { listener->boundPort() };
    auto resolver = core::net::ThreadedAddressResolver { inner };
    auto connector = core::net::makeConnector(loop, resolver);

    auto served = false;
    auto wrote = false;
    auto loopThread = std::thread::id {};
    loop.blockOn(core::net::testing::allOf(
        acceptAndRead(listener.get(), "hello", &served),
        dialAndWrite(connector.get(), "peer.test", 80, "hello", &wrote, &loopThread)));

    CHECK(wrote);
    CHECK(served);

    auto const callers = resolver.offloaded();
    INFO("the resolver offloaded " << callers << " lookup(s)");
    CHECK(callers == 1);

    REQUIRE(inner.callers().size() == 1);
    CHECK(loopThread == std::this_thread::get_id()); // the flow really did run on the loop
    CHECK(inner.callers().front() != loopThread);    // and the lookup really did not
}

TEST_CASE("a dial to a literal costs no resolver thread at all", "[net]")
{
    // Load-bearing rather than an optimisation: every internal dial in the consuming projects is
    // to a literal, and it is also what lets the whole connect path be exercised with no thread
    // existing.
    auto source = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(source != nullptr);
    auto loop = EventLoop { *source };

    auto bound = core::net::listen(loop, core::net::ListenOptions { .host = "127.0.0.1" });
    REQUIRE(bound.has_value());
    auto listener = std::move(*bound);

    auto inner = RecordingResolver { listener->boundPort() };
    auto resolver = core::net::ThreadedAddressResolver { inner };
    auto connector = core::net::makeConnector(loop, resolver);

    auto served = false;
    auto wrote = false;
    auto loopThread = std::thread::id {};
    loop.blockOn(core::net::testing::allOf(
        acceptAndRead(listener.get(), "hello", &served),
        dialAndWrite(connector.get(), "127.0.0.1", listener->boundPort(), "hello", &wrote, &loopThread)));

    CHECK(wrote);
    CHECK(served);
    CHECK(resolver.offloaded() == 0);
    // It still went through the injected resolver — inline, on this thread — rather than around it.
    REQUIRE(inner.callers().size() == 1);
    CHECK(inner.callers().front() == std::this_thread::get_id());
}

TEST_CASE("the free connect() reaches a listener on every backend", "[net]")
{
    // contour's `connect(loop, host, port)` keeps its signature and its behaviour; what changed
    // underneath is that it is now `makeConnector` plus the process resolver.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;

        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto bound = core::net::listen(loop, core::net::ListenOptions { .host = "127.0.0.1" });
            REQUIRE(bound.has_value());
            auto listener = std::move(*bound);

            auto served = false;
            auto client = [](EventLoop* lp, std::uint16_t port, bool* ok) -> Task<void> {
                auto dialled = co_await core::net::connect(lp, "127.0.0.1", port);
                if (!dialled.has_value())
                    co_return;
                constexpr auto Payload = std::string_view { "hello" };
                auto const bytes =
                    std::span<std::byte const> { reinterpret_cast<std::byte const*>(Payload.data()),
                                                 Payload.size() };
                auto const written = co_await (*dialled)->write(bytes);
                *ok = written.has_value() && *written == Payload.size();
            };
            auto wrote = false;
            loop.blockOn(core::net::testing::allOf(acceptAndRead(listener.get(), "hello", &served),
                                                   client(&loop, listener->boundPort(), &wrote)));
            CHECK(wrote);
            CHECK(served);
        }
    }
}

TEST_CASE("the free connect() reports a refusal on every backend", "[net]")
{
    // Ruling R101 through the public surface: a dial checks `SO_ERROR` and never trusts which
    // callback fired, so a closed port produces `ConnRefused` and not a socket whose first write
    // fails.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;

        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto closedPort = std::uint16_t { 0 };
            {
                auto bound = core::net::listen(loop, core::net::ListenOptions { .host = "127.0.0.1" });
                REQUIRE(bound.has_value());
                closedPort = (*bound)->boundPort();
            }
            REQUIRE(closedPort != 0);

            auto answer = SocketResult {};
            auto client = [](EventLoop* lp, std::uint16_t port, SocketResult* out) -> Task<void> {
                *out = co_await core::net::connect(lp, "127.0.0.1", port);
            };
            loop.blockOn(client(&loop, closedPort, &answer));

            REQUIRE_FALSE(answer.has_value());
            INFO("dialled the just-closed port " << closedPort << ": " << answer.error().context);
            CHECK(answer.error().code == NetErrorCode::ConnRefused);
        }
    }
}

TEST_CASE("a connector honours the per-call budget", "[net]")
{
    // The budget is a parameter rather than a policy: the OS default connect timeout runs to
    // minutes on some systems, so a caller that wants to notice a dead peer — or simply to shut
    // down — cannot be made to wait for it.
    auto source = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(source != nullptr);
    auto loop = EventLoop { *source };

    auto resolver = core::net::InlineAddressResolver {};
    auto connector = core::net::makeConnector(loop, resolver);

    // An empty host is refused before the resolver is touched, which is the one failure this
    // case can provoke without a network in it.
    auto answer = SocketResult {};
    auto dial = [](core::net::IConnector* c, SocketResult* out) -> Task<void> {
        *out =
            co_await c->connect("", 80, DialOptions { .connectTimeout = std::chrono::milliseconds { 50 } });
    };
    loop.blockOn(dial(connector.get(), &answer));

    REQUIRE_FALSE(answer.has_value());
    CHECK(answer.error().code == NetErrorCode::AddressNotAvail);
}

TEST_CASE("a listener reports the port it actually bound", "[net]")
{
    // A bind to port 0 means "pick a free one", so the port an operator, a log line or a test
    // needs is the one the kernel chose and not the one that was asked for. `boundPort`, where
    // contour spelled it `localPort`.
    auto source = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(source != nullptr);
    auto loop = EventLoop { *source };

    auto bound = core::net::listen(loop, core::net::ListenOptions { .host = "127.0.0.1", .port = 0 });
    REQUIRE(bound.has_value());
    CHECK((*bound)->boundPort() != 0);
}
