// SPDX-License-Identifier: Apache-2.0
#include <core/async/Cancellation.hpp>
#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/IListener.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/KeepAlive.hpp>
#include <core/net/ReadinessDial.hpp>
#include <core/net/SocketAddress.hpp>
#include <core/net/Sockets.hpp>
#include <core/net/testing/BackendMatrix.hpp>
#include <core/net/testing/CoroTestSupport.hpp>
#include <core/platform/Clock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
    #include <sys/socket.h>

    #include <unistd.h>
#endif

using core::async::Task;
using core::net::EventLoop;
using core::net::ISocket;
using core::net::KeepAlive;
using core::net::NetErrorCode;
using core::net::ResolvedEndpoint;
using core::net::SocketResult;
using core::net::detail::dialReadiness;
using core::net::testing::BackendMatrix;
using core::platform::SteadyTimePoint;

namespace
{

/// The loopback endpoint for @p port, through the production resolver.
///
/// A literal, so nothing here depends on a name server; `SystemAddressResolver` is still the
/// thing that fills the sockaddr, so a case dials exactly the bytes production would.
[[nodiscard]] ResolvedEndpoint loopbackEndpoint(std::uint16_t port)
{
    auto resolver = core::net::SystemAddressResolver {};
    auto resolved = resolver.resolve("127.0.0.1", port);
    REQUIRE(resolved.has_value());
    REQUIRE_FALSE(resolved->empty());
    return resolved->front();
}

/// The lowest file descriptor the process would be handed next.
///
/// A leak check rather than a platform branch in logic: POSIX hands out the lowest free
/// descriptor, so a dial that abandoned one makes this number GROW. There is no equivalent
/// question to ask Windows — handles are not allocated lowest-first — so this abstains there and
/// the case falls back to the park-count assertion, which is portable and which is what actually
/// catches the leak that matters (a park nothing will retire).
[[nodiscard]] std::optional<int> nextDescriptor() noexcept
{
#ifdef _WIN32
    return std::nullopt;
#else
    auto const probe = ::socket(AF_INET, SOCK_STREAM, 0);
    if (probe < 0)
        return std::nullopt;
    ::close(probe);
    return probe;
#endif
}

/// A listening port that accepts nothing, plus enough pending connections to saturate its
/// backlog — so the NEXT dial to it stays outstanding instead of completing.
///
/// Arranged rather than waited for, and it can fail to arrange: a kernel that over-accepts, or a
/// Windows stack that completes the handshake regardless of the backlog, leaves nothing pending.
/// A case that cannot arrange it SKIPs rather than passing on a dial that quietly succeeded.
struct SaturatedListener
{
    std::unique_ptr<core::net::IListener> listener;
    std::vector<std::unique_ptr<ISocket>> pending;
    std::uint16_t port = 0;

    /// True once a fill dial ran out of time rather than connecting — which is the proof that
    /// the NEXT dial will stay outstanding too, and the only thing that makes the cases below
    /// meaningful.
    bool saturated = false;
};

Task<void> saturate(EventLoop* loop, SaturatedListener* out)
{
    auto bound = core::net::listen(*loop, core::net::ListenOptions { .host = "127.0.0.1", .backlog = 1 });
    REQUIRE(bound.has_value());
    out->listener = std::move(*bound);
    out->port = out->listener->boundPort();

    // **Every fill dial is bounded, and the bound is what stops this helper from becoming the
    // hang it is arranging.** A dial with no deadline against a backlog that is already full
    // never returns — which is precisely the state being arranged, so the first one to reach it
    // would hang the case that wanted it.
    constexpr auto Fill = 32;
    constexpr auto PerDial = std::chrono::milliseconds { 300 };
    for ([[maybe_unused]] auto const attempt: std::views::iota(0, Fill))
    {
        auto dialled = co_await dialReadiness(
            loop, loopbackEndpoint(out->port), loop->clock().now() + PerDial, KeepAlive::No);
        if (!dialled.has_value())
        {
            // A timeout means the backlog no longer takes a connection. Anything else — a
            // refusal, an unreachable route — means this stack does not behave the way the
            // arrangement needs, and the cases SKIP on `saturated` staying false.
            out->saturated = dialled.error().code == NetErrorCode::Timeout;
            co_return;
        }
        out->pending.push_back(std::move(*dialled));
    }
}

/// Dials @p port and reports what came back.
Task<void> dialOnce(EventLoop* loop, std::uint16_t port, std::chrono::milliseconds budget, SocketResult* out)
{
    auto const deadline =
        budget > std::chrono::milliseconds::zero() ? loop->clock().now() + budget : SteadyTimePoint::max();
    *out = co_await dialReadiness(loop, loopbackEndpoint(port), deadline, KeepAlive::No);
}

/// Accepts one connection and reads @p expected.size() bytes from it.
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

/// Dials @p port, writes @p payload and reports whether every byte went.
Task<void> dialAndWrite(EventLoop* loop, std::uint16_t port, std::string_view payload, bool* ok)
{
    auto dialled =
        co_await dialReadiness(loop, loopbackEndpoint(port), SteadyTimePoint::max(), KeepAlive::No);
    if (!dialled.has_value())
        co_return;
    auto const bytes =
        std::span<std::byte const> { reinterpret_cast<std::byte const*>(payload.data()), payload.size() };
    auto const written = co_await (*dialled)->write(bytes);
    *ok = written.has_value() && *written == payload.size();
}

} // namespace

TEST_CASE("a readiness dial produces a connected socket on every backend", "[net]")
{
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue; // not built on this platform

        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto bound = core::net::listen(loop, core::net::ListenOptions { .host = "127.0.0.1" });
            REQUIRE(bound.has_value());
            auto listener = std::move(*bound);

            auto served = false;
            auto wrote = false;
            loop.blockOn(
                core::net::testing::allOf(acceptAndRead(listener.get(), "hello", &served),
                                          dialAndWrite(&loop, listener->boundPort(), "hello", &wrote)));
            CHECK(wrote);
            CHECK(served);
        }
    }
}

TEST_CASE("a refused connect completes with the refusal on every backend", "[net]")
{
    // **Ruling R101, in its natural habitat.** A dial checks `SO_ERROR`; it never trusts which
    // callback fired. On Linux a failed connect can arrive as an error with neither direction
    // set, while on macOS the write filter fires with `EV_EOF` and the backend reports
    // `Writable` — so a dial that believed the callback would hand its caller a socket whose
    // FIRST WRITE fails, days later, on one platform only.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;

        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };

            // A port that was bound and then given up: the kernel answers a SYN to it with a
            // reset, which is the ordinary refusal every consumer will meet.
            auto closedPort = std::uint16_t { 0 };
            {
                auto bound = core::net::listen(loop, core::net::ListenOptions { .host = "127.0.0.1" });
                REQUIRE(bound.has_value());
                closedPort = (*bound)->boundPort();
            }
            REQUIRE(closedPort != 0);

            auto answer = SocketResult {};
            loop.blockOn(dialOnce(&loop, closedPort, std::chrono::milliseconds { 5000 }, &answer));

            REQUIRE_FALSE(answer.has_value());
            INFO("dialled the just-closed port " << closedPort << " and got: " << answer.error().context);
            CHECK(answer.error().code == NetErrorCode::ConnRefused);
            CHECK(loop.parkedWaiterCount() == 0);
        }
    }
}

TEST_CASE("a dial that cannot complete is ended by its deadline and takes its descriptor with it", "[net]")
{
    auto source = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(source != nullptr);
    auto loop = EventLoop { *source };

    auto saturated = SaturatedListener {};
    loop.blockOn(saturate(&loop, &saturated));
    if (!saturated.saturated)
        SKIP("this stack completes a dial whose listener never accepts, so no dial could be left "
             "outstanding to run out of time");

    auto const before = nextDescriptor();

    auto answer = SocketResult {};
    loop.blockOn(dialOnce(&loop, saturated.port, std::chrono::milliseconds { 500 }, &answer));

    REQUIRE_FALSE(answer.has_value());
    INFO("dialled a listener with a saturated backlog and got: " << answer.error().context);
    CHECK(answer.error().code == NetErrorCode::Timeout);
    // The park is what a leak would show as: a readiness registration nothing will ever retire.
    CHECK(loop.parkedWaiterCount() == 0);
    if (before.has_value())
        CHECK(nextDescriptor() == before); // POSIX hands out the lowest free fd; see nextDescriptor
}

TEST_CASE("the flow's stop token cancels a dial in flight", "[net]")
{
    auto source = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(source != nullptr);
    auto loop = EventLoop { *source };

    auto saturated = SaturatedListener {};
    loop.blockOn(saturate(&loop, &saturated));
    if (!saturated.saturated)
        SKIP("this stack completes a dial whose listener never accepts, so no dial could be left "
             "outstanding to cancel");

    auto const before = nextDescriptor();

    // The dial has no deadline of its own: the only thing that can end it is the stop. If the
    // stop does not reach it, this case HANGS rather than failing — which is why the binary
    // carries a TIMEOUT and why the arrangement above is asserted before we get here.
    auto answer = SocketResult {};
    auto cancelled = false;
    auto dial = [](EventLoop* lp, std::uint16_t port, SocketResult* out, bool* threw) -> Task<void> {
        try
        {
            *out = co_await dialReadiness(lp, loopbackEndpoint(port), SteadyTimePoint::max(), KeepAlive::No);
        }
        catch (core::async::OperationCancelled const&)
        {
            *threw = true;
        }
    };
    auto stopAfter = [](EventLoop* lp, std::chrono::milliseconds after) -> Task<void> {
        co_await lp->delay(after);
        lp->requestStop();
    };

    loop.blockOn(core::net::testing::allOf(dial(&loop, saturated.port, &answer, &cancelled),
                                           stopAfter(&loop, std::chrono::milliseconds { 100 })));

    // A cancel from the FLOW unwinds; a cancel from the RESOURCE is a value. Either is a report,
    // and what must NOT happen is a dial that resolved into a usable socket after a stop.
    CHECK((cancelled || !answer.has_value()));
    CHECK(loop.parkedWaiterCount() == 0);
    if (before.has_value())
        CHECK(nextDescriptor() == before);
}
