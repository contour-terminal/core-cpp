// SPDX-License-Identifier: Apache-2.0

// winsock2.h MUST precede windows.h / ws2tcpip.h (which project headers pull in), so this block
// leads every Win32 net translation unit.
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
// clang-format on

#include <core/net/detail/DialPrimitives.hpp>

#include <core/net/EventLoop.hpp>
#include <core/net/detail/SocketErrors.hpp>
#include <core/net/windows/WindowsSocket.hpp>
#include <core/net/windows/WinsockError.hpp>
#include <core/platform/WinsockInit.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace core::net::detail
{

namespace
{
    /// Arms TCP keepalive with @p settings. Best-effort by contract; see the header.
    ///
    /// One ioctl sets the flag and both intervals together, so there is no partially-armed state
    /// to unwind. The probe COUNT is absent on purpose: Windows fixes it at 10 and offers no way
    /// to set it — @c KeepAliveSettings states what that does to the detection time rather than
    /// pretending the parameter was applied.
    /// @param socket The connected socket.
    /// @param settings The intervals to apply.
    /// @return True when the ioctl succeeded.
    [[nodiscard]] bool armKeepAlive(SOCKET socket, KeepAliveSettings const& settings) noexcept
    {
        std::ignore = settings.count;

        auto request = tcp_keepalive {};
        request.onoff = 1;
        // Milliseconds here, unlike every other platform.
        request.keepalivetime = static_cast<ULONG>(settings.idle.count());
        request.keepaliveinterval = static_cast<ULONG>(settings.interval.count());

        DWORD returned = 0;
        return ::WSAIoctl(socket,
                          SIO_KEEPALIVE_VALS,
                          &request,
                          sizeof(request),
                          nullptr,
                          0,
                          &returned,
                          nullptr,
                          nullptr)
               == 0;
    }

} // namespace

NetError fromWinsockError(int error, std::string context)
{
    // The module's Winsock table (`windows/SocketErrors.cpp`) answers every `WSAE*` code, so a
    // reset, an abort after a FIN and a refused connect mean the same here as on every other
    // Windows transport. Only two rows are the completion's and the dial's own: an ABORT is
    // `Cancelled`, because on a completion port that is what a `close()`, a `cancelRead()` or a stop
    // looks like from the operation's side -- the kernel answers `ERROR_OPERATION_ABORTED`, which
    // is not a `WSAE*` code at all -- and a family or protocol the host lacks is `Unsupported`.
    auto code = NetErrorCode::SystemError;
    switch (error)
    {
        case ERROR_OPERATION_ABORTED: code = NetErrorCode::Cancelled; break;
        case WSAEAFNOSUPPORT:
        case WSAEPROTONOSUPPORT: code = NetErrorCode::Unsupported; break;
        default: code = classifySocketError(error); break;
    }
    return makeNetError(code, error, std::move(context));
}

std::expected<DialHandles, NetError> openDialSocket(ResolvedEndpoint const& endpoint)
{
    platform::ensureWinsockInitialized();

    auto const socket = ::socket(endpoint.family, SOCK_STREAM, endpoint.protocol);
    if (socket == INVALID_SOCKET)
        return std::unexpected(fromWinsockError(WSAGetLastError(), "socket"));

    // **The socket is not what the loop watches here.** Winsock reports readiness for a socket
    // through a `WSAEVENT` associated with it, which is what the WFMO backend waits on — so a
    // dial holds two handles where POSIX holds one. `WSAEventSelect` also puts the socket into
    // non-blocking mode, which is what the connect below relies on.
    auto const event = ::WSACreateEvent();
    if (event == WSA_INVALID_EVENT
        || ::WSAEventSelect(socket, event, FD_CONNECT | FD_WRITE | FD_CLOSE) == SOCKET_ERROR)
    {
        auto const err = WSAGetLastError();
        if (event != WSA_INVALID_EVENT)
            ::WSACloseEvent(event);
        ::closesocket(socket);
        return std::unexpected(fromWinsockError(err, "WSAEventSelect"));
    }

    // A dialled socket is created by a plain `::socket`, so it is inheritable unless told
    // otherwise — and a process that dials and also spawns children would hand every child an
    // open peer connection. Best-effort: a failure here costs inheritance hygiene, not the dial.
    std::ignore = ::SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, 0);

    return DialHandles { .socket = reinterpret_cast<platform::NativeHandle>(socket),
                         .readiness = static_cast<platform::NativeHandle>(event),
                         .kind = DefaultHandleKind };
}

void closeDialSocket(EventLoop* loop, DialHandles& handles) noexcept
{
    if (!handles.valid())
        return;
    if (loop != nullptr)
        loop->notifyHandleClosing(handles.readiness, FdWakePolicy::Cancel);
    ::WSACloseEvent(static_cast<WSAEVENT>(handles.readiness));
    ::closesocket(reinterpret_cast<SOCKET>(handles.socket));
    handles = DialHandles {};
}

std::expected<ConnectProgress, NetError> beginConnect(DialHandles const& handles,
                                                      ResolvedEndpoint const& endpoint)
{
    auto const* const address = reinterpret_cast<sockaddr const*>(endpoint.storage.data());
    if (::connect(reinterpret_cast<SOCKET>(handles.socket), address, static_cast<int>(endpoint.length)) == 0)
        return ConnectProgress::Completed;

    auto const err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK || err == WSAEALREADY || err == WSAEINPROGRESS)
        return ConnectProgress::Pending;
    return std::unexpected(fromWinsockError(err, "connect"));
}

std::expected<void, NetError> pendingSocketError(DialHandles const& handles)
{
    auto pending = 0;
    auto length = static_cast<int>(sizeof(pending));
    if (::getsockopt(reinterpret_cast<SOCKET>(handles.socket),
                     SOL_SOCKET,
                     SO_ERROR,
                     reinterpret_cast<char*>(&pending),
                     &length)
        != 0)
        return std::unexpected(fromWinsockError(WSAGetLastError(), "getsockopt(SO_ERROR)"));
    if (pending != 0)
        return std::unexpected(fromWinsockError(pending, "connect"));
    return {};
}

void applyDialledSocketOptions(DialHandles const& handles, KeepAlive keepAlive) noexcept
{
    auto const socket = reinterpret_cast<SOCKET>(handles.socket);

    int const one = 1;
    std::ignore =
        ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char const*>(&one), sizeof(one));

    if (keepAlive == KeepAlive::Yes)
        std::ignore = armKeepAlive(socket, KeepAliveSettings {});
}

std::unique_ptr<ISocket> adoptDialled(EventLoop& loop, DialHandles& handles, std::string peer)
{
    auto const socket = reinterpret_cast<SOCKET>(handles.socket);

    // The dial's own event goes with the dial: `WindowsSocket` creates and associates one of its
    // own, and `WSAEventSelect` allows exactly ONE event object per socket — so leaving this one
    // attached would take the slot the socket needs. The loop is told before it closes, for the
    // reason `closeDialSocket` states.
    loop.notifyHandleClosing(handles.readiness, FdWakePolicy::Cancel);
    ::WSACloseEvent(static_cast<WSAEVENT>(handles.readiness));
    handles = DialHandles {};

    return std::unique_ptr<ISocket> { new WindowsSocket(loop, socket, std::move(peer)) };
}

} // namespace core::net::detail
