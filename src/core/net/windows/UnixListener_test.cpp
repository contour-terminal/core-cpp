// SPDX-License-Identifier: Apache-2.0
//
// An AF_UNIX listener on Windows belongs to the loop it is made on, as `listen` and `adoptListener`
// already do: an IOCP loop -- the Windows default -- gets an `IocpListener` whose accepted sockets
// are `IocpSocket`s, and `connectUnix` dials an `IocpSocket` there too; a WFMO loop keeps
// `WindowsListener` and `WindowsSocket`. `listenUnix` used to build the WFMO listener whatever the
// loop was, so an IOCP loop served AF_UNIX through readiness and handed out `WindowsSocket`s --
// found through contour, whose daemon listens on a unix socket.
#include <core/async/Task.hpp>
#include <core/async/WhenAll.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/IListener.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/Sockets.hpp>
#include <core/net/WithTimeout.hpp>
#include <core/net/testing/BackendMatrix.hpp>
#include <core/net/windows/IocpSocket.hpp>
#include <core/net/windows/WindowsListener.hpp>
#include <core/net/windows/WindowsSocket.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>

using core::async::Task;
using core::net::EventLoop;
using core::net::IListener;
using core::net::ISocket;
using core::net::testing::BackendMatrix;

namespace
{

/// Which transport family a listener or socket belongs to.
enum class Family : std::uint8_t
{
    None,       ///< Not reached: the flow failed before it had one.
    Completion, ///< `IocpListener` / `IocpSocket`.
    Readiness,  ///< `WindowsListener` / `WindowsSocket`.
    Other,      ///< Neither.
};

[[nodiscard]] Family familyOf(IListener const* listener) noexcept
{
    if (dynamic_cast<core::net::IocpListener const*>(listener) != nullptr)
        return Family::Completion;
    if (dynamic_cast<core::net::WindowsListener const*>(listener) != nullptr)
        return Family::Readiness;
    return Family::Other;
}

[[nodiscard]] Family familyOf(ISocket const* socket) noexcept
{
    if (dynamic_cast<core::net::IocpSocket const*>(socket) != nullptr)
        return Family::Completion;
    if (dynamic_cast<core::net::WindowsSocket const*>(socket) != nullptr)
        return Family::Readiness;
    return Family::Other;
}

/// What the echo observed.
struct Echo
{
    Family accepted = Family::None;
    Family connected = Family::None;
    bool served = false;
    bool matched = false;
};

/// Accepts one connection and echoes one read.
Task<void> serve(IListener* listener, Echo* echo)
{
    auto accepted = co_await listener->accept();
    if (!accepted.has_value())
        co_return;
    auto conn = std::move(*accepted);
    echo->accepted = familyOf(conn.get());
    auto buffer = std::array<std::byte, 64> {};
    auto const got = co_await conn->read(buffer);
    if (!got.has_value() || *got == 0)
        co_return;
    auto const wrote = co_await conn->write(std::span<std::byte const> { buffer }.subspan(0, *got));
    echo->served = wrote.has_value() && *wrote == *got;
}

/// Connects, sends a request, and compares the echo. Closes the listener when it cannot connect,
/// so the accept beside it is not left parked with nothing to wake it.
Task<void> dial(EventLoop* loop, IListener* listener, std::string path, Echo* echo)
{
    auto connected = co_await core::net::connectUnix(loop, path);
    if (!connected.has_value())
    {
        listener->close();
        co_return;
    }
    auto sock = std::move(*connected);
    echo->connected = familyOf(sock.get());
    auto const request = std::string_view { "unix-iocp" };
    if (auto const wrote = co_await sock->write(std::as_bytes(std::span { request })); !wrote.has_value())
        co_return;
    auto buffer = std::array<std::byte, 32> {};
    auto const got = co_await sock->read(buffer);
    if (got.has_value())
        echo->matched = std::string_view { reinterpret_cast<char const*>(buffer.data()), *got } == request;
}

Task<void> echoOnce(EventLoop* loop, IListener* listener, std::string path, Echo* echo)
{
    co_await core::async::whenAll(serve(listener, echo), dial(loop, listener, std::move(path), echo));
}

} // namespace

TEST_CASE("listenUnix and connectUnix belong to the loop's transport family", "[net][afunix][iocp]")
{
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto const expected = loop.completionPort() != nullptr ? Family::Completion : Family::Readiness;

            auto const directory = std::filesystem::temp_directory_path()
                                   / std::format("core-cpp-unix-{}", std::random_device {}());
            auto const path = (directory / "echo.sock").string();
            auto listener = core::net::listenUnix(loop, path);
            if (!listener.has_value())
            {
                REQUIRE(listener.error().code == core::net::NetErrorCode::Unsupported);
                SKIP("AF_UNIX not supported on this platform");
            }
            auto const listenerFamily = familyOf(listener->get());

            auto echo = Echo {};
            auto const finished = loop.blockOn(core::net::withTimeout(
                &loop, echoOnce(&loop, listener->get(), path, &echo), std::chrono::seconds { 10 }));
            listener->reset();
            auto ec = std::error_code {};
            std::filesystem::remove_all(directory, ec);

            CHECK(finished); // or it waited 10s for an accept or a read that never came
            CHECK(listenerFamily == expected);
            CHECK(echo.accepted == expected);
            CHECK(echo.connected == expected);
            CHECK(echo.served);
            CHECK(echo.matched);
        }
    }
}
