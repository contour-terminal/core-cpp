// SPDX-License-Identifier: Apache-2.0
#include <core/net/detail/SocketErrors.hpp>

#include <cerrno>

namespace core::net::detail
{

NetErrorCode classifySocketError(int systemCode) noexcept
{
    // A switch rather than a table of pairs only because EAGAIN and EWOULDBLOCK may be one value,
    // which a table would have to state twice and a switch refuses to compile twice.
    switch (systemCode)
    {
        case ECONNRESET: return NetErrorCode::ConnReset;
        case ECONNREFUSED: return NetErrorCode::ConnRefused;
        // A route that does not exist and a host that does not answer are one category: both mean
        // this endpoint is unreachable from here, and neither is retryable at this layer.
        case EHOSTUNREACH:
        case ENETUNREACH: return NetErrorCode::HostUnreach;
        case EADDRINUSE: return NetErrorCode::AddressInUse;
        case EADDRNOTAVAIL: return NetErrorCode::AddressNotAvail;
        case EACCES: return NetErrorCode::PermissionDenied;
        case EBADF:
        case ENOTSOCK: return NetErrorCode::BadHandle;
        case EINTR: return NetErrorCode::Cancelled;
        case ETIMEDOUT: return NetErrorCode::Timeout;
        // `EMSGSIZE` is the whole difference between "this path cannot carry a message this big" and
        // "something went wrong": a datagram caller that cannot tell has no way to decide to split.
        case EMSGSIZE: return NetErrorCode::MessageTooLarge;
        // A receive deadline (`SO_RCVTIMEO`) expiring is EAGAIN on POSIX: `isDeadlineExpiry` is
        // what a caller asks.
        case EWOULDBLOCK:
#if EAGAIN != EWOULDBLOCK
        case EAGAIN:
#endif
            return NetErrorCode::WouldBlock;
        default: return NetErrorCode::SystemError;
    }
}

int lastSocketError() noexcept
{
    return errno;
}

} // namespace core::net::detail
