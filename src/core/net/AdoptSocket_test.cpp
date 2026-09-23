// SPDX-License-Identifier: Apache-2.0
//
// `adoptSocket`: a connected socket accepted outside core-cpp, handed to a loop of the caller's
// choosing. It is how a Windows server spreads connections over several loops -- one thread
// accepts and deals each handle out -- since Windows has no SO_REUSEPORT and a completion-port
// association is one socket to one port. The cases accept with plain sockets calls, so nothing of
// core-cpp's own accept path is involved, and adopt onto a loop that did not accept.

#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/Sockets.hpp>
#include <core/net/testing/BackendMatrix.hpp>
#include <core/platform/Types.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#ifdef _WIN32
    #include <core/platform/WinsockInit.hpp>

    #include <winsock2.h>
#else
    #include <sys/socket.h>

    #include <unistd.h>

    #include <arpa/inet.h>
    #include <netinet/in.h>
#endif

using core::async::Task;
using core::net::EventLoop;
using core::net::ISocket;
using core::net::testing::BackendMatrix;
using core::platform::NativeHandle;

namespace
{

/// A blocking TCP handle owned by the test, closed on scope exit unless released.
class RawHandle
{
  public:
    explicit RawHandle(NativeHandle handle) noexcept: _handle(handle) {}
    RawHandle(RawHandle const&) = delete;
    RawHandle(RawHandle&&) = delete;
    RawHandle& operator=(RawHandle const&) = delete;
    RawHandle& operator=(RawHandle&&) = delete;

    ~RawHandle()
    {
        if (_handle == core::platform::InvalidHandle)
            return;
#ifdef _WIN32
        ::closesocket(reinterpret_cast<SOCKET>(_handle));
#else
        ::close(_handle);
#endif
    }

    /// @return The handle, still owned here.
    [[nodiscard]] NativeHandle get() const noexcept { return _handle; }

    /// @return The handle, no longer owned here.
    [[nodiscard]] NativeHandle release() noexcept
    {
        auto const handle = _handle;
        _handle = core::platform::InvalidHandle;
        return handle;
    }

  private:
    NativeHandle _handle;
};

#ifdef _WIN32
using RawSocket = SOCKET;
using SockLen = int;
#else
using RawSocket = int;
using SockLen = socklen_t;
#endif

RawSocket rawOf(NativeHandle handle) noexcept
{
#ifdef _WIN32
    return reinterpret_cast<SOCKET>(handle);
#else
    return handle;
#endif
}

NativeHandle handleOf(RawSocket socket) noexcept
{
#ifdef _WIN32
    return reinterpret_cast<NativeHandle>(socket);
#else
    return socket;
#endif
}

/// Sends @p payload whole with a plain blocking call.
/// @return Whether all of it went.
bool rawSendAll(NativeHandle handle, std::string_view payload) noexcept
{
#ifdef _WIN32
    auto const sent = ::send(rawOf(handle), payload.data(), static_cast<int>(payload.size()), 0);
#else
    auto const sent = ::send(handle, payload.data(), payload.size(), 0);
#endif
    return sent == static_cast<decltype(sent)>(payload.size());
}

/// Receives up to @p size bytes with a plain blocking call.
/// @return How many arrived; 0 or less at EOF or on an error.
long rawReceive(NativeHandle handle, char* into, std::size_t size) noexcept
{
#ifdef _WIN32
    return ::recv(rawOf(handle), into, static_cast<int>(size), 0);
#else
    return static_cast<long>(::recv(handle, into, size, 0));
#endif
}

/// Both ends of one loopback connection, made with plain blocking socket calls.
struct RawConnection
{
    NativeHandle client = core::platform::InvalidHandle;   ///< The dialling end.
    NativeHandle accepted = core::platform::InvalidHandle; ///< The accepted end.
};

/// @return A loopback connection made without core-cpp, or two invalid handles.
RawConnection rawLoopbackConnection()
{
#ifdef _WIN32
    core::platform::ensureWinsockInitialized();
#endif
    auto const listener = RawHandle { handleOf(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) };
    auto address = sockaddr_in {};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    auto* const generic = reinterpret_cast<sockaddr*>(&address);
    auto length = static_cast<SockLen>(sizeof(address));
    if (::bind(rawOf(listener.get()), generic, length) != 0 || ::listen(rawOf(listener.get()), 1) != 0
        || ::getsockname(rawOf(listener.get()), generic, &length) != 0)
        return {};

    auto client = RawHandle { handleOf(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) };
    if (::connect(rawOf(client.get()), generic, static_cast<SockLen>(sizeof(address))) != 0)
        return {};
    auto accepted = RawHandle { handleOf(::accept(rawOf(listener.get()), nullptr, nullptr)) };
    if (accepted.get() == core::platform::InvalidHandle)
        return {};
    return RawConnection { .client = client.release(), .accepted = accepted.release() };
}

/// Reads exactly @p expected.size() bytes from @p socket into @p matched's verdict.
Task<void> readExactly(ISocket* socket, std::string_view expected, bool* matched)
{
    auto buffer = std::array<std::byte, 16> {};
    auto total = std::size_t { 0 };
    while (total < expected.size())
    {
        auto const got = co_await socket->read(std::span<std::byte> { buffer }.subspan(total));
        if (!got.has_value() || *got == 0)
            break;
        total += *got;
    }
    *matched = total == expected.size() && std::memcmp(buffer.data(), expected.data(), expected.size()) == 0;
}

/// Writes @p payload to @p socket, recording whether all of it went.
Task<void> writeAll(ISocket* socket, std::string_view payload, bool* wrote)
{
    auto const bytes =
        std::span<std::byte const> { reinterpret_cast<std::byte const*>(payload.data()), payload.size() };
    auto const sent = co_await socket->write(bytes);
    *wrote = sent.has_value() && *sent == payload.size();
}

} // namespace

TEST_CASE("A socket accepted outside core-cpp reads and writes on the loop it is adopted onto",
          "[net][adopt]")
{
    for (auto const& backend: BackendMatrix)
    {
        auto servingBackend = core::net::makeBackend(backend.kind);
        if (!servingBackend)
            continue; // not available on this platform

        DYNAMIC_SECTION("backend=" << backend.name)
        {
            // The accept happened on this thread with no loop at all, as it does on a Windows
            // server's accepting thread; the loop the socket lands on is the caller's choice.
            auto servingLoop = EventLoop { *servingBackend };

            auto const connection = rawLoopbackConnection();
            REQUIRE(connection.client != core::platform::InvalidHandle);
            REQUIRE(connection.accepted != core::platform::InvalidHandle);
            auto const client = RawHandle { connection.client };

            auto adopted = core::net::adoptSocket(servingLoop, connection.accepted, "127.0.0.1:test");
            REQUIRE(adopted.has_value());
            CHECK((*adopted)->peerAddress() == "127.0.0.1:test");

            // The peer writes with a plain blocking send; the adopted socket reads it on its loop.
            constexpr auto Ping = std::string_view { "ping" };
            REQUIRE(rawSendAll(client.get(), Ping));
            auto matched = false;
            servingLoop.blockOn(readExactly(adopted->get(), Ping, &matched));
            CHECK(matched);

            // And the other way: the adopted socket writes on its loop, the peer reads blocking.
            constexpr auto Pong = std::string_view { "pong" };
            auto wrote = false;
            servingLoop.blockOn(writeAll(adopted->get(), Pong, &wrote));
            CHECK(wrote);
            auto reply = std::array<char, 4> {};
            auto received = std::size_t { 0 };
            while (received < reply.size())
            {
                auto const got = rawReceive(client.get(), reply.data() + received, reply.size() - received);
                if (got <= 0)
                    break;
                received += static_cast<std::size_t>(got);
            }
            CHECK(std::string_view { reply.data(), received } == Pong);
        }
    }
}

TEST_CASE("Adopting an invalid handle is an error value, not an exception or a crash", "[net][adopt]")
{
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue; // not available on this platform

        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto const refused = core::net::adoptSocket(loop, core::platform::InvalidHandle, {});
            REQUIRE_FALSE(refused.has_value());
            CHECK(refused.error().code == core::net::NetErrorCode::BadHandle);
        }
    }
}
