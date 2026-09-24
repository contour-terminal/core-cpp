// SPDX-License-Identifier: Apache-2.0

// winsock2.h MUST precede windows.h / ws2tcpip.h (which project headers pull in),
// so this block leads every Win32 net translation unit.
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
// clang-format on

#include <core/net/windows/WindowsListener.hpp>

#include <core/net/SocketAddress.hpp>
#include <core/net/detail/PeerAddress.hpp>
#include <core/net/detail/StreamSocketOptions.hpp>
#include <core/net/windows/InvalidSocket.hpp>
#include <core/net/windows/NetworkEvents.hpp>
#include <core/net/windows/UnixSocketPath.hpp>
#include <core/net/windows/WindowsSocket.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <afunix.h>

namespace core::net
{

WindowsListener::WindowsListener(
    EventLoop& loop, SOCKET socket, WSAEVENT event, std::uint16_t boundPort, std::string path) noexcept:
    _loop(loop), _socket(socket), _event(event), _boundPort(boundPort), _path(std::move(path))
{
}

WindowsListener::~WindowsListener()
{
    // Cancel, not Resume: the accept loop reaches _socket, _event and _closed
    // through `this`, which is about to stop existing. Unwinding via
    // OperationCancelled returns without dereferencing them again.
    close(FdWakePolicy::Cancel);
}

void WindowsListener::close() noexcept
{
    close(FdWakePolicy::Resume);
}

void WindowsListener::close(FdWakePolicy policy) noexcept
{
    if (_closed)
        return;
    _closed = true;
    if (_event != WSA_INVALID_EVENT)
    {
        // Before the close, while the handle is still valid. The event -- not the
        // socket -- is what accept() registers with the loop, so it is the handle a
        // parked accept must be woken by.
        _loop.notifyHandleClosing(_event, policy);
        WSACloseEvent(_event);
        _event = WSA_INVALID_EVENT;
    }
    if (_socket != detail::InvalidSocket)
    {
        closesocket(_socket);
        _socket = detail::InvalidSocket;
    }
    // Parity with UnixListener::close(): the socket FILE goes with the socket. Leaving it
    // behind makes the next bind's liveness probe find a path whose server is gone, and
    // contradicts the contract stated in docs/internals/vthost.md ("Daemon lifetime").
    // Empty for a TCP listener; best effort, since the path may already be gone.
    //
    // DeleteFileA rather than std::filesystem::remove: this function is noexcept, and
    // remove() would construct a path temporary from _path — an allocation that could throw
    // and terminate. It is also what bindUnix already reclaims a stale socket file with.
    if (!_path.empty())
        ::DeleteFileA(_path.c_str());
}

std::expected<std::unique_ptr<WindowsListener>, NetError> WindowsListener::bind(
    EventLoop& loop,
    std::string_view host,
    std::uint16_t port,
    int backlog,
    SocketBufferSizes acceptedBuffers)
{
    auto hints = addrinfo {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

    auto const hostStr = std::string { host };
    auto const portStr = std::to_string(port);

    addrinfo* resolved = nullptr;
    auto const rc =
        getaddrinfo(hostStr.empty() ? nullptr : hostStr.c_str(), portStr.c_str(), &hints, &resolved);
    if (rc != 0 || resolved == nullptr)
        return std::unexpected(makeNetError(NetErrorCode::AddressError, rc, "getaddrinfo"));

    auto sock = detail::InvalidSocket;
    NetError lastError = makeNetError(NetErrorCode::AddressError, 0, "no usable address");
    auto const* next = resolved;
    while (next != nullptr)
    {
        // Step to the next candidate first, so every `continue` below moves on to it.
        auto const* ai = std::exchange(next, next->ai_next);
        sock = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == detail::InvalidSocket)
        {
            lastError = makeNetError(NetErrorCode::SystemError, WSAGetLastError(), "socket");
            continue;
        }

        // Before listen, so the window scale of every connection it accepts can count the receive
        // buffer; an accepted socket inherits both sizes from its listener.
        detail::applySocketBufferSizes(reinterpret_cast<platform::NativeHandle>(sock), acceptedBuffers);

        // Exclusive, as the IOCP listener binds, and a failure to make it so fails the candidate:
        // this is a security property, not a tuning one (`.agent/rules/async-and-net.md`, "A
        // listening socket claims its address exclusively"). It was SO_REUSEADDR here, which on
        // Windows lets a second socket bind a port this one is listening on and take its
        // connections -- the default the WFMO backend handed every caller.
        BOOL const exclusive = TRUE;
        if (::setsockopt(sock,
                         SOL_SOCKET,
                         SO_EXCLUSIVEADDRUSE,
                         reinterpret_cast<char const*>(&exclusive),
                         static_cast<int>(sizeof(exclusive)))
                == 0
            && ::bind(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0
            && ::listen(sock, backlog) == 0)
            break; // success

        lastError = makeNetError(WSAGetLastError() == WSAEADDRINUSE ? NetErrorCode::AddressInUse
                                                                    : NetErrorCode::SystemError,
                                 WSAGetLastError(),
                                 "bind/listen");
        closesocket(sock);
        sock = detail::InvalidSocket;
    }
    freeaddrinfo(resolved);

    if (sock == detail::InvalidSocket)
        return std::unexpected(lastError);

    auto const event = WSACreateEvent();
    if (event == WSA_INVALID_EVENT || WSAEventSelect(sock, event, FD_ACCEPT) == SOCKET_ERROR)
    {
        if (event != WSA_INVALID_EVENT)
            WSACloseEvent(event);
        closesocket(sock);
        return std::unexpected(makeNetError(NetErrorCode::SystemError, WSAGetLastError(), "WSAEventSelect"));
    }

    auto bound = sockaddr_storage {};
    auto boundLen = int { sizeof(bound) };
    std::uint16_t actualPort = port;
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0)
    {
        if (bound.ss_family == AF_INET)
            actualPort = ntohs(reinterpret_cast<sockaddr_in const*>(&bound)->sin_port);
        else if (bound.ss_family == AF_INET6)
            actualPort = ntohs(reinterpret_cast<sockaddr_in6 const*>(&bound)->sin6_port);
    }

    // A TCP listener owns no socket file, hence the empty path.
    return std::unique_ptr<WindowsListener>(new WindowsListener(loop, sock, event, actualPort, {}));
}

std::expected<std::unique_ptr<WindowsListener>, NetError> WindowsListener::adopt(EventLoop& loop,
                                                                                 SOCKET socket)
{
    if (socket == detail::InvalidSocket)
        return std::unexpected(makeNetError(NetErrorCode::BadHandle, 0, "adoptListener"));

    // The event, not the socket, is what an accept registers with the loop — and associating it
    // also puts the socket into non-blocking mode, which a socket handed over by another process
    // will not be.
    auto const event = WSACreateEvent();
    if (event == WSA_INVALID_EVENT || WSAEventSelect(socket, event, FD_ACCEPT) == SOCKET_ERROR)
    {
        auto const err = WSAGetLastError();
        if (event != WSA_INVALID_EVENT)
            WSACloseEvent(event);
        // The socket is NOT closed: on failure the caller still owns it, as `adoptListener`
        // documents and as the POSIX adopt does. Closing it here as well would make a caller
        // that follows the documentation close it a second time.
        return std::unexpected(makeNetError(NetErrorCode::SystemError, err, "WSAEventSelect"));
    }

    // The port is asked of the KERNEL rather than taken on trust: the caller adopting a socket is
    // exactly the caller that does not know which port it is.
    auto bound = sockaddr_storage {};
    auto boundLen = int { sizeof(bound) };
    auto port = std::uint16_t { 0 };
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0)
        port = detail::portOfSockaddr(&bound, static_cast<std::uint32_t>(boundLen));

    // An adopted listener owns no socket file: this process did not create one, so removing a
    // path on close would delete somebody else's.
    return std::unique_ptr<WindowsListener>(new WindowsListener(loop, socket, event, port, {}));
}

std::expected<std::unique_ptr<WindowsListener>, NetError> WindowsListener::bindUnix(EventLoop& loop,
                                                                                    std::string_view path,
                                                                                    int backlog)
{
    // Never hijack a live daemon and never delete user files: probe the path first
    // (exactly as UnixListener::bind does) and reclaim only a stale socket file.
    auto const claimed = detail::claimUnixSocketPath(path);
    if (!claimed)
        return std::unexpected(claimed.error());
    auto const& addr = *claimed;
    auto const pathString = std::string { path };

    auto const sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == detail::InvalidSocket)
        return std::unexpected(makeNetError(NetErrorCode::Unsupported, WSAGetLastError(), "socket(AF_UNIX)"));

    if (::bind(sock, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) != 0
        || ::listen(sock, backlog) != 0)
    {
        auto const err = WSAGetLastError();
        closesocket(sock);
        return std::unexpected(
            makeNetError(err == WSAEADDRINUSE ? NetErrorCode::AddressInUse : NetErrorCode::SystemError,
                         err,
                         "bind/listen unix"));
    }

    auto const event = WSACreateEvent();
    if (event == WSA_INVALID_EVENT || WSAEventSelect(sock, event, FD_ACCEPT) == SOCKET_ERROR)
    {
        if (event != WSA_INVALID_EVENT)
            WSACloseEvent(event);
        closesocket(sock);
        return std::unexpected(makeNetError(NetErrorCode::SystemError, WSAGetLastError(), "WSAEventSelect"));
    }

    return std::unique_ptr<WindowsListener>(
        new WindowsListener(loop, sock, event, /*boundPort=*/0, pathString));
}

async::Task<AcceptResult> WindowsListener::accept()
{
    auto const lifetime = std::weak_ptr<void const> { _lifetime };
    while (true)
    {
        if (_closed || _socket == detail::InvalidSocket)
            co_return std::unexpected(makeNetError(NetErrorCode::Cancelled, 0, "accept on closed listener"));

        auto peer = sockaddr_storage {};
        auto peerLen = int { sizeof(peer) };
        auto const conn = ::accept(_socket, reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (conn != detail::InvalidSocket)
        {
            // What a dialled socket is given too -- TCP_NODELAY above all, which only the dial
            // used to set. The buffer sizes it inherited from the listener.
            detail::applyStreamSocketOptions(reinterpret_cast<platform::NativeHandle>(conn), KeepAlive::No);
            co_return std::unique_ptr<ISocket>(new WindowsSocket(_loop, conn, formatPeer(peer)));
        }

        auto const err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK)
        {
            // Consume the indication rather than reset the event. A connection that landed
            // between the ::accept above and this point has already been RECORDED, and Winsock
            // raises a recorded indication only once — so `WSAResetEvent` cleared the event
            // while the record stood, the park never woke, and the listener went silent for
            // that connection AND every later one. Enumerating clears both and says what it
            // took, so an indication from that window is served here instead of lost.
            // @see consumeNetworkEvents; WindowsSocket::latchNetworkEvents does the same for
            // the two directions that share a connected socket's event.
            if ((consumeNetworkEvents(_socket, _event) & FD_ACCEPT) != 0)
                continue; // a connection arrived in that window: take it rather than park
            try
            {
                co_await _loop.waitReadable(_event);
            }
            catch (async::OperationCancelled const&)
            {
                co_return std::unexpected(makeNetError(NetErrorCode::Cancelled, 0, "accept cancelled"));
            }
            // Asked before any member is: `close()` woke this park, and the owner may have
            // destroyed the listener before the loop got here.
            if (lifetime.expired())
                co_return std::unexpected(
                    makeNetError(NetErrorCode::Cancelled, 0, "the listener was destroyed"));
            continue;
        }
        co_return std::unexpected(makeNetError(NetErrorCode::SystemError, err, "accept"));
    }
}

} // namespace core::net
