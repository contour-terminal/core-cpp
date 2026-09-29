// SPDX-License-Identifier: Apache-2.0
#include <core/net/posix/AcceptLoop.hpp>

#include <core/async/Cancellation.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/detail/PeerAddress.hpp>
#include <core/net/detail/SocketErrors.hpp>
#include <core/net/detail/StreamSocketOptions.hpp>
#include <core/net/detail/WouldBlock.hpp>
#include <core/net/posix/PosixSocket.hpp>

#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>

namespace core::net
{

namespace detail
{

    namespace
    {

        /// The failed accepts the loop takes again at once, and nothing else: `EINTR`, and the
        /// pending-connection errors Linux's accept(2) says to treat like EAGAIN that no category
        /// of `NetErrorCode` names. Each one dequeued the connection it reports on, so retrying
        /// consumes the backlog rather than spinning on it. The man page's list goes on --
        /// `EHOSTUNREACH`, `ENETUNREACH`, `EHOSTDOWN`, `ENETDOWN`, `ENONET`, and `EPERM` for a packet
        /// filter -- but those HAVE a category, so they are reported as it, and a caller that tells
        /// a per-connection failure from exhaustion can. `EOPNOTSUPP` is on that list too and is
        /// reported, as `Unsupported`, deliberately: it is also what `accept` answers on a socket
        /// that is not a stream, on every call, and retrying that would never stop.
        constexpr auto RetriedAcceptErrors = std::array { EINTR, ECONNABORTED, EPROTO, ENOPROTOOPT };

    } // namespace

    AcceptFailure acceptFailureOf(int err) noexcept
    {
        if (isWouldBlock(err))
            return AcceptFailure { .step = AcceptStep::Park, .code = NetErrorCode::Ok };
        if (std::ranges::find(RetriedAcceptErrors, err) != RetriedAcceptErrors.end())
            return AcceptFailure { .step = AcceptStep::Retry, .code = NetErrorCode::Ok };
        return AcceptFailure { .step = AcceptStep::Report, .code = classifySocketError(err) };
    }

} // namespace detail

async::Task<AcceptResult> acceptOne(EventLoop* loop,
                                    int const* fd,
                                    bool const* closed,
                                    std::weak_ptr<void const> listener)
{
    while (true)
    {
        if (*closed || *fd < 0)
            co_return std::unexpected(makeNetError(NetErrorCode::Cancelled, 0, "accept on closed listener"));

        auto peer = sockaddr_storage {};
        auto peerLen = socklen_t { sizeof(peer) };
#ifdef __linux__
        auto const conn =
            ::accept4(*fd, reinterpret_cast<sockaddr*>(&peer), &peerLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
        auto const conn = ::accept(*fd, reinterpret_cast<sockaddr*>(&peer), &peerLen);
#endif
        if (conn >= 0)
        {
#ifndef __linux__
            // Portable fallback: non-blocking explicitly; close-on-exec is the helper's below.
            if (auto const flags = ::fcntl(conn, F_GETFL, 0); flags >= 0)
                ::fcntl(conn, F_SETFL, flags | O_NONBLOCK);
#endif
            // What a dialled socket is given too -- TCP_NODELAY above all, which only the dial
            // used to set, so a server's replies waited on Nagle while its client's did not. The
            // buffer sizes are not asked for here: the socket inherited them from the listener,
            // which asked before it listened.
            detail::applyStreamSocketOptions(conn, KeepAlive::No);
            co_return std::unique_ptr<ISocket>(new PosixSocket(*loop, conn, formatPeer(peer)));
        }

        auto const err = errno;
        auto const failure = detail::acceptFailureOf(err);
        if (failure.step == detail::AcceptStep::Retry)
            continue;
        if (failure.step == detail::AcceptStep::Report)
            co_return std::unexpected(makeNetError(failure.code, err, "accept"));
        // AcceptStep::Park: wait until the listener fd is readable (a connection is pending). A
        // cancelled wait (listener closed / stop requested) throws OperationCancelled, which the
        // accept loop turns into Cancelled.
        try
        {
            co_await loop->waitReadable(*fd);
        }
        catch (async::OperationCancelled const&)
        {
            co_return std::unexpected(makeNetError(NetErrorCode::Cancelled, 0, "accept cancelled"));
        }
        // Asked before `*closed` and `*fd` are: the listener's `close()` woke this park, and
        // its owner may have destroyed it before the loop got here.
        if (listener.expired())
            co_return std::unexpected(makeNetError(NetErrorCode::Cancelled, 0, "the listener was destroyed"));
    }
}

} // namespace core::net
