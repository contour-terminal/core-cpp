// SPDX-License-Identifier: Apache-2.0

// winsock2.h MUST precede windows.h / ws2tcpip.h (which project headers pull in),
// so this block leads every Win32 net translation unit.
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
// clang-format on

#include <core/net/Sockets.hpp>
#include <core/net/windows/WindowsListener.hpp>
#include <core/net/windows/WindowsSocket.hpp>
#include <core/platform/WinsockInit.hpp>

#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

#include <afunix.h>

namespace core::net
{

namespace
{
    /// Closes a connect-readiness event the connect path is done with, announcing it
    /// to the loop first.
    ///
    /// The EVENT, not the socket, is what a park registers with the loop, so it is
    /// the handle the loop knows. Nothing is normally parked on it by this point —
    /// the awaiter detached in its own await_resume — but the loop's contract is
    /// that a handle which may be registered is announced BEFORE it closes, and
    /// honouring that unconditionally keeps the behaviour identical to POSIX.
    /// @param loop The loop the event may have been registered with.
    /// @param event The event to close.
    void discardEvent(EventLoop* loop, WSAEVENT event) noexcept
    {
        loop->notifyHandleClosing(static_cast<HANDLE>(event), FdWakePolicy::Cancel);
        WSACloseEvent(event);
    }
} // namespace

std::expected<std::unique_ptr<IListener>, NetError> listen(EventLoop& loop,
                                                           std::string_view host,
                                                           std::uint16_t port,
                                                           int backlog)
{
    platform::ensureWinsockInitialized();
    return WindowsListener::bind(loop, host, port, backlog)
        .transform(
            [](std::unique_ptr<WindowsListener> listener) -> std::unique_ptr<IListener> { return listener; });
}

async::Task<std::expected<std::unique_ptr<ISocket>, NetError>> connect(EventLoop* loop,
                                                                       std::string_view host,
                                                                       std::uint16_t port)
{
    platform::ensureWinsockInitialized();
    auto hints = addrinfo {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    auto const hostStr = std::string { host };
    auto const portStr = std::to_string(port);

    addrinfo* resolved = nullptr;
    if (getaddrinfo(hostStr.c_str(), portStr.c_str(), &hints, &resolved) != 0 || resolved == nullptr)
        co_return std::unexpected(makeNetError(NetErrorCode::AddressError, WSAGetLastError(), "getaddrinfo"));

    NetError lastError = makeNetError(NetErrorCode::AddressError, 0, "no usable address");
    auto const* next = resolved;
    while (next != nullptr)
    {
        // Step to the next candidate first, so every `continue` below moves on to it.
        auto const* ai = std::exchange(next, next->ai_next);
        auto const sock = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == INVALID_SOCKET)
        {
            lastError = makeNetError(NetErrorCode::SystemError, WSAGetLastError(), "socket");
            continue;
        }

        // Associate a connect-readiness event before the non-blocking connect.
        auto const event = WSACreateEvent();
        if (event == WSA_INVALID_EVENT
            || WSAEventSelect(sock, event, FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR)
        {
            if (event != WSA_INVALID_EVENT)
                WSACloseEvent(event);
            closesocket(sock);
            lastError = makeNetError(NetErrorCode::SystemError, WSAGetLastError(), "WSAEventSelect");
            continue;
        }

        auto const rc = ::connect(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen));
        auto connectErr = (rc == 0) ? 0 : WSAGetLastError();
        if (rc != 0 && connectErr == WSAEWOULDBLOCK)
        {
            try
            {
                co_await loop->waitWritable(static_cast<HANDLE>(event));
            }
            catch (async::OperationCancelled const&)
            {
                discardEvent(loop, event);
                closesocket(sock);
                freeaddrinfo(resolved);
                co_return std::unexpected(makeNetError(NetErrorCode::Cancelled, 0, "connect cancelled"));
            }
            // Re-check the connect outcome.
            int soError = 0;
            auto soLen = int { sizeof(soError) };
            ::getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &soLen);
            connectErr = soError;
        }

        // The WindowsSocket re-associates the event with its own interest set.
        discardEvent(loop, event);

        if (connectErr == 0)
        {
            freeaddrinfo(resolved);
            co_return std::unique_ptr<ISocket>(new WindowsSocket(*loop, sock));
        }
        lastError = makeNetError(connectErr == WSAECONNREFUSED ? NetErrorCode::ConnRefused
                                                               : NetErrorCode::SystemError,
                                 connectErr,
                                 "connect");
        closesocket(sock);
    }
    freeaddrinfo(resolved);
    co_return std::unexpected(lastError);
}

std::expected<std::unique_ptr<IListener>, NetError> listenUnix(EventLoop& loop,
                                                               std::string_view path,
                                                               int backlog)
{
    platform::ensureWinsockInitialized();
    // The parent directory is created but NOT permission-hardened: NTFS ACLs
    // (the user's profile/temp tree) govern access, not POSIX mode bits.
    auto ec = std::error_code {};
    std::filesystem::create_directories(std::filesystem::path { std::string { path } }.parent_path(), ec);
    return WindowsListener::bindUnix(loop, path, backlog)
        .transform(
            [](std::unique_ptr<WindowsListener> listener) -> std::unique_ptr<IListener> { return listener; });
}

async::Task<std::expected<std::unique_ptr<ISocket>, NetError>> connectUnix(EventLoop* loop,
                                                                           std::string_view path)
{
    platform::ensureWinsockInitialized();
    auto addr = sockaddr_un {};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
        co_return std::unexpected(makeNetError(NetErrorCode::AddressError, 0, "unix socket path too long"));
    std::memcpy(addr.sun_path, path.data(), path.size());

    auto const sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
        co_return std::unexpected(
            makeNetError(NetErrorCode::Unsupported, WSAGetLastError(), "socket(AF_UNIX)"));

    // A same-machine AF_UNIX connect completes immediately (or fails); no
    // reactor parking is needed the way the TCP path needs it.
    if (::connect(sock, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) != 0)
    {
        auto const err = WSAGetLastError();
        closesocket(sock);
        co_return std::unexpected(
            makeNetError(err == WSAECONNREFUSED ? NetErrorCode::ConnRefused : NetErrorCode::SystemError,
                         err,
                         "connect unix"));
    }
    co_return std::unique_ptr<ISocket>(new WindowsSocket(*loop, sock));
}

std::expected<std::unique_ptr<ISocket>, NetError> adoptFd(EventLoop& /*loop*/, int /*fd*/)
{
    return std::unexpected(makeNetError(NetErrorCode::Unsupported, 0, "adoptFd on Windows"));
}

} // namespace core::net
