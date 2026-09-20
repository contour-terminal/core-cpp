// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `ISocket` — a connected, bidirectional byte transport whose I/O is expressed as
/// `async::Task<IoResult>`, so it composes with the loop's coroutine
/// vocabulary (`whenAny`, `withTimeout`, cancellation) natively.
///
/// Implementations: @c PosixSocket / @c WindowsSocket (production, reactor-driven
/// non-blocking I/O), and @c InMemoryTransport (deterministic, in-process, for
/// tests). All are driven by an @c EventLoop: a slow read/write parks the calling
/// coroutine via `loop.waitReadable/waitWritable` rather than blocking a thread.

#include <core/async/Task.hpp>
#include <core/net/IoResult.hpp>

#include <cstddef>
#include <span>
#include <string>

namespace core::net
{

/// One read's payload plus an optionally received file descriptor
/// (SCM_RIGHTS over AF_UNIX). @c fd is -1 when none arrived; ownership of a
/// received fd transfers to the caller.
struct ReadWithFd
{
    std::size_t bytesRead = 0;
    int fd = -1;
};

/// A connected, streamed, bidirectional byte transport.
///
/// The buffer passed to @c read / @c write must stay valid until the returned task
/// completes (the operation may suspend and resume across reactor frames).
class ISocket
{
  public:
    ISocket() = default;
    virtual ~ISocket() = default;

    ISocket(ISocket const&) = delete;
    ISocket& operator=(ISocket const&) = delete;
    ISocket(ISocket&&) = delete;
    ISocket& operator=(ISocket&&) = delete;

    /// Reads up to @p buffer.size() bytes into @p buffer.
    /// @param buffer Destination span; must outlive the returned task.
    /// @return A task resolving to the byte count read, 0 on clean EOF, or a
    ///         @c NetError on failure.
    [[nodiscard]] virtual async::Task<IoResult> read(std::span<std::byte> buffer) = 0;

    /// Reads like @c read but also accepts ONE SCM_RIGHTS file descriptor
    /// when the transport supports fd passing (AF_UNIX on POSIX; the default
    /// implementation never yields one — the documented behaviour everywhere
    /// else, including Windows).
    /// @param buffer Destination span; must outlive the returned task.
    /// @return Bytes read (0 = clean EOF) plus the received fd or -1.
    [[nodiscard]] virtual async::Task<std::expected<ReadWithFd, NetError>> readWithFd(
        std::span<std::byte> buffer)
    {
        auto const n = co_await read(buffer);
        if (!n)
            co_return std::unexpected(n.error());
        co_return ReadWithFd { .bytesRead = *n, .fd = -1 };
    }

    /// Writes all of @p buffer's bytes (looping over partial writes / backpressure).
    /// @param buffer Source span; must outlive the returned task.
    /// @return A task resolving to the byte count written (== buffer.size() on
    ///         success), or a @c NetError on failure.
    [[nodiscard]] virtual async::Task<IoResult> write(std::span<std::byte const> buffer) = 0;

    /// @return The remote peer's printable address ("127.0.0.1", "::1"), or "" if
    ///         unknown (e.g. the in-memory transport).
    [[nodiscard]] virtual std::string peerAddress() const { return {}; }

    /// Closes the socket. Idempotent; a pending or subsequent read/write resolves
    /// with @c NetErrorCode::BadHandle.
    virtual void close() noexcept = 0;

    /// @return True once @c close() has been called, or a read observed the peer's
    ///         EOF. The second half is what a consumer polls this for: a connection
    ///         whose peer hung up is no longer worth holding, and a socket that
    ///         reported it only for its OWN close left such a consumer polling a dead
    ///         connection for ever.
    ///
    ///         The EOF latch is answered here and nowhere else: a peer that shut only
    ///         its write side leaves this end able to keep writing, so @c write keeps
    ///         working (and @c read keeps returning 0) after this turns true.
    [[nodiscard]] virtual bool isClosed() const noexcept = 0;
};

} // namespace core::net
