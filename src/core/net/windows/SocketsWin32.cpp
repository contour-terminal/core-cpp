// SPDX-License-Identifier: Apache-2.0

// winsock2.h MUST precede windows.h / ws2tcpip.h (which project headers pull in),
// so this block leads every Win32 net translation unit.
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
// clang-format on

#include <core/net/ICompletionPort.hpp>
#include <core/net/Sockets.hpp>
#include <core/net/windows/InvalidSocket.hpp>
#include <core/net/windows/IocpSocket.hpp>
#include <core/net/windows/WindowsListener.hpp>
#include <core/net/windows/WindowsSocket.hpp>
#include <core/platform/WinsockInit.hpp>

#include <cassert>
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
    // The loop's question, as `listen` and `adoptListener` ask it: an IOCP loop -- the default --
    // serves the socket through its completion port and hands out IocpSockets. This built the WFMO
    // listener whatever the loop was until 0.2.1.
    if (loop.completionPort() != nullptr)
        return IocpListener::bindUnix(loop, path, backlog)
            .transform([](std::unique_ptr<IocpListener> listener) -> std::unique_ptr<IListener> {
                return listener;
            });
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
    // Adopted, so the loop decides the transport as it does for every other socket: an
    // IocpSocket on an IOCP loop, a WindowsSocket on a WFMO one. It used to be a WindowsSocket
    // whatever the loop was. `adoptSocket` closes the socket on failure.
    co_return adoptSocket(*loop, reinterpret_cast<platform::NativeHandle>(sock), std::string {});
}

std::expected<std::unique_ptr<ISocket>, NetError> adoptSocket(EventLoop& loop,
                                                              platform::NativeHandle handle,
                                                              std::string peerAddress)
{
    assert(loop.teardownIsSerialisedWithDispatch()
           && "adoptSocket off the loop's thread: the socket would join registrations another thread "
              "is dispatching");
    auto const socket = reinterpret_cast<SOCKET>(handle);
    if (socket == detail::InvalidSocket)
        return std::unexpected(makeNetError(NetErrorCode::BadHandle, WSAENOTSOCK, "adoptSocket"));

    // The same question `listen` and the connector ask of the loop: a completion port, or readiness.
    auto* const port = loop.completionPort();
    if (port == nullptr)
        return WindowsSocket::adopt(loop, socket, std::move(peerAddress))
            .transform(
                [](std::unique_ptr<WindowsSocket> adopted) -> std::unique_ptr<ISocket> { return adopted; });

    // Associated HERE rather than by the constructor, so a refusal is this call's error value and
    // the socket is closed with it -- the constructor can only record the failure for a first read.
    if (auto associated = port->associate(handle); !associated)
    {
        ::closesocket(socket);
        return std::unexpected(std::move(associated.error()));
    }
    try
    {
        return std::unique_ptr<ISocket>(
            new IocpSocket(loop, socket, std::move(peerAddress), IocpAssociation::AlreadyAssociated));
    }
    catch (...)
    {
        // Allocating the wrapper threw: nothing owns the socket, and this call promised to close it.
        port->forget(handle);
        ::closesocket(socket);
        throw;
    }
}

std::expected<std::unique_ptr<ISocket>, NetError> adoptFd(EventLoop& /*loop*/, int /*fd*/)
{
    return std::unexpected(makeNetError(NetErrorCode::Unsupported, 0, "adoptFd on Windows"));
}

} // namespace core::net
