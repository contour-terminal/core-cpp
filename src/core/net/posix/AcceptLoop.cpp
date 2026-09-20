// SPDX-License-Identifier: Apache-2.0
#include <core/net/posix/AcceptLoop.hpp>

#include <core/async/Cancellation.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/detail/PeerAddress.hpp>
#include <core/net/detail/WouldBlock.hpp>
#include <core/net/posix/PosixSocket.hpp>

#include <sys/socket.h>

#include <cerrno>

#include <fcntl.h>
#include <unistd.h>

namespace core::net
{

async::Task<AcceptResult> acceptOne(EventLoop* loop, int const* fd, bool const* closed)
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
            // Portable fallback: set non-blocking + cloexec explicitly.
            if (auto const flags = ::fcntl(conn, F_GETFL, 0); flags >= 0)
                ::fcntl(conn, F_SETFL, flags | O_NONBLOCK);
            if (auto const fdFlags = ::fcntl(conn, F_GETFD, 0); fdFlags >= 0)
                ::fcntl(conn, F_SETFD, fdFlags | FD_CLOEXEC);
#endif
            co_return std::unique_ptr<ISocket>(new PosixSocket(*loop, conn, formatPeer(peer)));
        }

        auto const err = errno;
        if (net::detail::isWouldBlock(err))
        {
            // Park until the listener fd is readable (a connection is pending). A
            // cancelled wait (listener closed / stop requested) throws
            // OperationCancelled, which the accept loop turns into Cancelled.
            try
            {
                co_await loop->waitReadable(*fd);
            }
            catch (async::OperationCancelled const&)
            {
                co_return std::unexpected(makeNetError(NetErrorCode::Cancelled, 0, "accept cancelled"));
            }
            continue;
        }
        if (err == EINTR || err == ECONNABORTED)
            continue;
        co_return std::unexpected(makeNetError(NetErrorCode::SystemError, err, "accept"));
    }
}

} // namespace core::net
