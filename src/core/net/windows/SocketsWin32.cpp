// SPDX-License-Identifier: Apache-2.0

// winsock2.h MUST precede windows.h / ws2tcpip.h (which project headers pull in),
// so this block leads every Win32 net translation unit.
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
// clang-format on

#include <core/net/Sockets.hpp>
#include <core/net/windows/InvalidSocket.hpp>
#include <core/net/windows/IocpSocket.hpp>
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

std::expected<std::unique_ptr<IListener>, NetError> listen(EventLoop& loop, ListenOptions options)
{
    // Refused, never mapped: Windows has no load-balancing SO_REUSEPORT, and its SO_REUSEADDR
    // lets a later socket take over a port another one holds -- a hijack, not a share.
    if (options.sharing == PortSharing::Shared)
        return std::unexpected(makeNetError(
            NetErrorCode::Unsupported, 0, "port sharing: Windows has no load-balancing SO_REUSEPORT"));
    platform::ensureWinsockInitialized();
    // Which listener is the loop's question, not the platform's: a completion port completes
    // AcceptEx, and a readiness backend is told FD_ACCEPT. Both are built on Windows, and
    // `BackendKind::Wfmo` stays reachable by name for a release after IOCP became the default.
    if (loop.completionPort() != nullptr)
        return IocpListener::bind(loop, options.host, options.port, options.backlog, options.buffers)
            .transform([](std::unique_ptr<IocpListener> listener) -> std::unique_ptr<IListener> {
                return listener;
            });
    return WindowsListener::bind(loop, options.host, options.port, options.backlog, options.buffers)
        .transform(
            [](std::unique_ptr<WindowsListener> listener) -> std::unique_ptr<IListener> { return listener; });
}

std::expected<std::unique_ptr<IListener>, NetError> listen(EventLoop& loop,
                                                           std::string_view host,
                                                           std::uint16_t port,
                                                           int backlog)
{
    return listen(loop, ListenOptions { .host = host, .port = port, .backlog = backlog });
}

std::expected<std::unique_ptr<IListener>, NetError> adoptListener(EventLoop& loop,
                                                                  platform::NativeHandle handle)
{
    platform::ensureWinsockInitialized();
    if (handle == platform::InvalidHandle)
        return std::unexpected(makeNetError(NetErrorCode::BadHandle, 0, "adoptListener"));
    if (loop.completionPort() != nullptr)
        return IocpListener::adopt(loop, reinterpret_cast<SOCKET>(handle))
            .transform([](std::unique_ptr<IocpListener> listener) -> std::unique_ptr<IListener> {
                return listener;
            });
    return WindowsListener::adopt(loop, reinterpret_cast<SOCKET>(handle))
        .transform(
            [](std::unique_ptr<WindowsListener> listener) -> std::unique_ptr<IListener> { return listener; });
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
    if (sock == detail::InvalidSocket)
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
