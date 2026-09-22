// SPDX-License-Identifier: Apache-2.0
#include <core/net/detail/DialPrimitives.hpp>

#include <core/net/EventLoop.hpp>
#include <core/net/posix/FdUtils.hpp>
#include <core/net/posix/PosixSocket.hpp>

#include <sys/socket.h>

#include <cerrno>
#include <chrono>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include <unistd.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

namespace core::net::detail
{

namespace
{
    /// Maps an errno from the dial path onto the vocabulary a caller can act on.
    ///
    /// `ConnRefused` and `HostUnreach` are the two a caller genuinely branches on — one says
    /// "nothing is listening there", the other says "this machine cannot get there" — and
    /// collapsing either into `SystemError` leaves a connector unable to do the job it exists
    /// for.
    /// @param err The captured errno.
    /// @param context What was being attempted.
    /// @return The classified error.
    [[nodiscard]] NetError fromDialErrno(int err, std::string context)
    {
        auto code = NetErrorCode::SystemError;
        switch (err)
        {
            case ECONNREFUSED: code = NetErrorCode::ConnRefused; break;
            case ECONNRESET: code = NetErrorCode::ConnReset; break;
            case ETIMEDOUT: code = NetErrorCode::Timeout; break;
            case EHOSTUNREACH:
            case ENETUNREACH: code = NetErrorCode::HostUnreach; break;
            case EADDRNOTAVAIL: code = NetErrorCode::AddressNotAvail; break;
            case EACCES:
            case EPERM: code = NetErrorCode::PermissionDenied; break;
            case EAFNOSUPPORT:
            case EPROTONOSUPPORT: code = NetErrorCode::Unsupported; break;
            case EBADF:
            case ENOTSOCK: code = NetErrorCode::BadHandle; break;
            default: break;
        }
        return makeNetError(code, err, std::move(context));
    }

    /// Whole seconds, and never zero.
    ///
    /// The keepalive options take seconds, and a zero is not "immediately" — it is rejected, or
    /// read as "keep the default", depending on the option and the platform. Rounding a
    /// sub-second request down to nothing would leave the two-hour system default in place while
    /// reporting success, which is exactly the silently-unarmed state @c KeepAliveSettings says
    /// is worth nothing.
    /// @param value The requested interval.
    /// @return At least one second.
    [[nodiscard]] int wholeSeconds(std::chrono::milliseconds value) noexcept
    {
        auto const whole = std::chrono::ceil<std::chrono::seconds>(value);
        return whole.count() > 0 ? static_cast<int>(whole.count()) : 1;
    }

    /// Arms TCP keepalive with @p settings. Best-effort by contract; see the header.
    /// @param fd The connected socket.
    /// @param settings The intervals to apply.
    /// @return True when the flag AND the intervals were all applied.
    [[nodiscard]] bool armKeepAlive(int fd, KeepAliveSettings const& settings) noexcept
    {
        // macOS spells the idle time `TCP_KEEPALIVE`; it is `TCP_KEEPIDLE` everywhere else.
#ifdef __APPLE__
        constexpr int IdleOption = TCP_KEEPALIVE;
#else
        constexpr int IdleOption = TCP_KEEPIDLE;
#endif
        auto const idle = wholeSeconds(settings.idle);
        auto const interval = wholeSeconds(settings.interval);
        auto const count = static_cast<int>(settings.count);

        // **The INTERVALS FIRST, and the flag last.** Reversed, a socket whose intervals could
        // not be applied would be left probing on the system default — two hours on Linux —
        // which is indistinguishable from no keepalive at all for every deadline this protects,
        // while reading back as armed to anything that checks the flag.
        if (::setsockopt(fd, IPPROTO_TCP, IdleOption, &idle, sizeof(idle)) != 0)
            return false;
        if (::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) != 0)
            return false;
        if (::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count)) != 0)
            return false;

        int const on = 1;
        return ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) == 0;
    }

} // namespace

std::expected<DialHandles, NetError> openDialSocket(ResolvedEndpoint const& endpoint)
{
    // Non-blocking AND close-on-exec in one call where the platform allows it; see makeStreamSocket.
    auto const fd = makeStreamSocket(endpoint.family, endpoint.protocol);
    if (fd < 0)
        return std::unexpected(fromDialErrno(errno, "socket"));

    // The descriptor IS the readiness object here: poll, epoll and kqueue all watch it directly.
    return DialHandles { .socket = fd, .readiness = fd, .kind = DefaultHandleKind };
}

void closeDialSocket(EventLoop* loop, DialHandles& handles) noexcept
{
    if (!handles.valid())
        return;
    if (loop != nullptr)
        loop->notifyHandleClosing(handles.readiness, FdWakePolicy::Cancel);
    ::close(handles.socket);
    handles = DialHandles {};
}

std::expected<ConnectProgress, NetError> beginConnect(DialHandles const& handles,
                                                      ResolvedEndpoint const& endpoint)
{
    auto const* const address = reinterpret_cast<sockaddr const*>(endpoint.storage.data());
    if (::connect(handles.socket, address, static_cast<socklen_t>(endpoint.length)) == 0)
        return ConnectProgress::Completed;

    auto const err = errno;
    // EINPROGRESS is the documented answer for a non-blocking connect; EALREADY and EAGAIN turn
    // up on a busy AF_UNIX backlog and on some stacks, and each means the same thing here: the
    // kernel took the request and readiness will say how it ended.
    if (err == EINPROGRESS || err == EALREADY || err == EAGAIN)
        return ConnectProgress::Pending;
    return std::unexpected(fromDialErrno(err, "connect"));
}

std::expected<void, NetError> pendingSocketError(DialHandles const& handles)
{
    auto pending = 0;
    auto length = static_cast<socklen_t>(sizeof(pending));
    if (::getsockopt(handles.socket, SOL_SOCKET, SO_ERROR, &pending, &length) != 0)
        return std::unexpected(fromDialErrno(errno, "getsockopt(SO_ERROR)"));
    if (pending != 0)
        return std::unexpected(fromDialErrno(pending, "connect"));
    return {};
}

void applyDialledSocketOptions(DialHandles const& handles, KeepAlive keepAlive) noexcept
{
    // TCP_NODELAY so a small request is not held back waiting for the peer's ACK of a previous
    // segment. Best-effort: an AF_UNIX or otherwise non-TCP socket simply refuses it.
    int const one = 1;
    std::ignore = ::setsockopt(handles.socket, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    if (keepAlive == KeepAlive::Yes)
        std::ignore = armKeepAlive(handles.socket, KeepAliveSettings {});
}

std::unique_ptr<ISocket> adoptDialled(EventLoop& loop, DialHandles& handles, std::string peer)
{
    auto const fd = std::exchange(handles.socket, platform::InvalidHandle);
    handles = DialHandles {};
    return std::unique_ptr<ISocket> { new PosixSocket(loop, fd, std::move(peer)) };
}

async::Task<SocketResult> dialCompletion(EventLoop* /*loop*/,
                                         ResolvedEndpoint /*endpoint*/,
                                         platform::SteadyTimePoint /*deadline*/,
                                         KeepAlive /*keepAlive*/)
{
    // No loop here lends a completion port, so no connector asks for this; the answer is still a
    // true one rather than an unresolved symbol, because the declaration is portable.
    co_return std::unexpected(
        makeNetError(NetErrorCode::Unsupported, 0, "a completion-port dial on a platform without one"));
}

} // namespace core::net::detail
