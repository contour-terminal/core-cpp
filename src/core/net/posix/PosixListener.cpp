// SPDX-License-Identifier: Apache-2.0
#include <core/net/posix/PosixListener.hpp>

#include <core/net/SocketAddress.hpp>
#include <core/net/posix/AcceptLoop.hpp>
#include <core/net/posix/FdUtils.hpp>

#include <sys/socket.h>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <netdb.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>

namespace core::net
{

PosixListener::PosixListener(EventLoop& loop, int fd, std::uint16_t boundPort) noexcept:
    _loop(loop), _fd(fd), _boundPort(boundPort)
{
}

PosixListener::~PosixListener()
{
    // Cancel, not Resume: acceptOne holds `int const* fd` / `bool const* closed`
    // into this object, which is about to stop existing. Unwinding via
    // OperationCancelled returns without ever dereferencing them again.
    close(FdWakePolicy::Cancel);
}

void PosixListener::close() noexcept
{
    close(FdWakePolicy::Resume);
}

void PosixListener::close(FdWakePolicy policy) noexcept
{
    if (_closed)
        return;
    _closed = true;
    if (_fd >= 0)
    {
        // Before the close, while the descriptor is still valid: epoll and kqueue
        // cannot report a closed descriptor, so without this a parked accept would
        // never be resumed.
        _loop.notifyHandleClosing(_fd, policy);
        ::close(_fd);
        _fd = -1;
    }
}

std::expected<std::unique_ptr<PosixListener>, NetError> PosixListener::bind(EventLoop& loop,
                                                                            std::string_view host,
                                                                            std::uint16_t port,
                                                                            int backlog)
{
    auto hints = addrinfo {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

    auto const hostStr = std::string { host };
    auto const portStr = std::to_string(port);

    addrinfo* resolved = nullptr;
    auto const rc =
        ::getaddrinfo(hostStr.empty() ? nullptr : hostStr.c_str(), portStr.c_str(), &hints, &resolved);
    if (rc != 0 || resolved == nullptr)
        return std::unexpected(makeNetError(NetErrorCode::AddressError, rc, "getaddrinfo"));

    int fd = -1;
    NetError lastError = makeNetError(NetErrorCode::AddressError, 0, "no usable address");
    auto const* next = resolved;
    while (next != nullptr)
    {
        // Step to the next candidate first, so every `continue` below moves on to it.
        auto const* ai = std::exchange(next, next->ai_next);
        // makeStreamSocket, not a bare ::socket: it asks for SOCK_CLOEXEC atomically where the
        // platform offers it, closing the window in which a fork+exec from another thread
        // inherited the listening descriptor — and kept the port (or the socket file) alive
        // after this process exited. connect()/connectUnix() already create their sockets this
        // way. The hints above fix ai_socktype at SOCK_STREAM, which is what this supplies.
        fd = makeStreamSocket(ai->ai_family, ai->ai_protocol);
        if (fd < 0)
        {
            lastError = makeNetError(NetErrorCode::SystemError, errno, "socket");
            continue;
        }

        int const one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        // makeNonBlockingCloexec stays: on a platform without the atomic socket() flags
        // makeStreamSocket sets them best-effort and ignores a failure, while a listener must
        // not be handed back blocking. Re-applying flags already set is a no-op.
        if (::bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 && ::listen(fd, backlog) == 0
            && makeNonBlockingCloexec(fd))
            break; // success

        lastError = makeNetError(errno == EADDRINUSE ? NetErrorCode::AddressInUse : NetErrorCode::SystemError,
                                 errno,
                                 "bind/listen");
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(resolved);

    if (fd < 0)
        return std::unexpected(lastError);

    // Read back the actual bound port (it may have been an OS-assigned ephemeral).
    auto bound = sockaddr_storage {};
    auto boundLen = socklen_t { sizeof(bound) };
    std::uint16_t actualPort = port;
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0)
    {
        if (bound.ss_family == AF_INET)
            actualPort = ntohs(reinterpret_cast<sockaddr_in const*>(&bound)->sin_port);
        else if (bound.ss_family == AF_INET6)
            actualPort = ntohs(reinterpret_cast<sockaddr_in6 const*>(&bound)->sin6_port);
    }

    return std::unique_ptr<PosixListener>(new PosixListener(loop, fd, actualPort));
}

std::expected<std::unique_ptr<PosixListener>, NetError> PosixListener::adopt(EventLoop& loop, int fd)
{
    if (fd < 0)
        return std::unexpected(makeNetError(NetErrorCode::BadHandle, EBADF, "adoptListener"));

    // The reactor requires non-blocking I/O, and an inherited descriptor has neither flag: a
    // listener handed over by a supervisor was created for a process that blocked on `accept`.
    if (!makeNonBlockingCloexec(fd))
        return std::unexpected(makeNetError(NetErrorCode::SystemError, errno, "fcntl"));

    // The port is asked of the KERNEL rather than taken on trust: the caller adopting a
    // descriptor is exactly the caller that does not know which port it is.
    auto bound = sockaddr_storage {};
    auto boundLen = socklen_t { sizeof(bound) };
    auto port = std::uint16_t { 0 };
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0)
        port = detail::portOfSockaddr(&bound, static_cast<std::uint32_t>(boundLen));

    return std::unique_ptr<PosixListener>(new PosixListener(loop, fd, port));
}

async::Task<AcceptResult> PosixListener::accept()
{
    // The shared loop records the TCP peer's printable host via formatPeer.
    return acceptOne(&_loop, &_fd, &_closed);
}

} // namespace core::net
