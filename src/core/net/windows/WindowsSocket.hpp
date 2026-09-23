// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/net/ISocket.hpp>
#include <core/net/IoAwaitable.hpp>

// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace core::net
{

/// A reactor-driven, non-blocking Windows stream socket. Because a SOCKET is not
/// directly waitable by WaitForMultipleObjects, readiness is observed through a
/// WSAEVENT associated with the socket via WSAEventSelect (the same technique as
/// platform::SystemPipe): the loop waits on the event, while recv/send operate
/// on the socket. read/write try the syscall and, on WSAEWOULDBLOCK, park on the
/// event until ready, then retry.
///
/// One event serves BOTH directions (WSAEventSelect permits no more), so the indications are
/// consumed through `WSAEnumNetworkEvents` and latched per direction — @see latchNetworkEvents
/// for what a bare `WSAResetEvent` costs when a reader and a writer share the socket.
class WindowsSocket final: public ISocket
{
  public:
    /// Wraps a connected socket, creating and associating its readiness event.
    /// @param loop The loop whose reactor drives readiness (not owned).
    /// @param socket The connected SOCKET (ownership transferred).
    /// @param peerAddress Printable peer address, or "" if unknown.
    WindowsSocket(EventLoop& loop, SOCKET socket, std::string peerAddress = {}) noexcept;
    ~WindowsSocket() override;

    WindowsSocket(WindowsSocket const&) = delete;
    WindowsSocket& operator=(WindowsSocket const&) = delete;
    WindowsSocket(WindowsSocket&&) = delete;
    WindowsSocket& operator=(WindowsSocket&&) = delete;

    /// @copydoc ISocket::read
    ///
    /// **Coroutine-backed rather than frame-free, and deliberately so.** The frame-free shape
    /// exists to save a coroutine frame per parked operation; buying it here means rewriting the
    /// WSAEventSelect retry loop into a readiness callback, in a class that since Task B7b serves
    /// only the WFMO backend -- kept one release (core-cpp#6) -- and AF_UNIX, while `IocpSocket`
    /// carries TCP on the default one. So B6 changed the SIGNATURE -- which is the interface's,
    /// and had to change everywhere at once -- and left the body alone. The awaiting
    /// flow's stop token still reaches it, through `Task`'s own awaiter.
    /// @param buffer The destination.
    /// @return The byte count read, `0` on a clean EOF, or a @c NetError.
    [[nodiscard]] IoAwaitable read(std::span<std::byte> buffer) override;

    /// @copydoc ISocket::write
    /// @param buffer The source.
    /// @return The byte count written, or a @c NetError. @see read for why this is coroutine-backed.
    [[nodiscard]] IoAwaitable write(std::span<std::byte const> buffer) override;

    /// @copydoc ISocket::waitReadable
    ///
    /// Implemented rather than inherited, because the inherited default answers a flat `1` and this
    /// socket can tell: a `MSG_PEEK` of one byte says `0` for EOF and `>0` for pending data, exactly
    /// as on POSIX. Leaving the default in place would re-create the divergence
    /// [fastcached#677](https://github.com/LASTRADA-Software/fastcached/issues/677) was filed on —
    /// the same call reporting opposite numbers on Windows and Linux for the same event.
    /// @return `0` for EOF, `>0` for pending data, or a @c NetError.
    [[nodiscard]] IoAwaitable waitReadable() override;

    /// @copydoc ISocket::shutdownWrite
    ///
    /// Implemented rather than inherited for the reason the base states: the no-op default is for
    /// FAKES, and a transport that HAS a write half to close costs its peer the early EOF by
    /// inheriting it.
    [[nodiscard]] ResultAwaitable<void> shutdownWrite() override;

    /// @copydoc ISocket::cancelRead
    ///
    /// A parked read here -- or a parked @c waitReadable, which parks the same way -- is a
    /// coroutine awaiting the loop on this socket's event, so it is retired the way any borrowed
    /// waiter is: taken back with @c EventLoop::cancelPending, which detaches its park, then resumed
    /// INLINE to find it was retired and complete with @c NetErrorCode::Cancelled. That is
    /// `PosixSocket`'s detach-then-complete order, and like it this transport consumes nothing a
    /// retired read could lose: the bytes stay in the socket for the next read. A call with nothing
    /// parked is a no-op.
    void cancelRead() noexcept override;

    [[nodiscard]] std::string peerAddress() const override { return _peerAddress; }

    void close() noexcept override;

    /// @return True once @c close() was called, or a read observed the peer's EOF.
    ///         The second half is what makes this answer the question callers ask —
    ///         "is this connection still worth holding?" — rather than only "did I
    ///         close it myself"; @see ISocket::isClosed.
    [[nodiscard]] bool isClosed() const noexcept override { return _closed || _peerClosed; }

  private:
    /// The read body, as the coroutine it has been since contour wrote it.
    /// @param buffer The destination.
    /// @return The byte count read, `0` on a clean EOF, or a @c NetError.
    [[nodiscard]] async::Task<IoResult> readTask(std::span<std::byte> buffer);

    /// The write body, likewise.
    /// @param buffer The source.
    /// @return The byte count written, or a @c NetError.
    [[nodiscard]] async::Task<IoResult> writeTask(std::span<std::byte const> buffer);

    /// The readability probe's body.
    /// @return `0` for EOF, `>0` for pending data, or a @c NetError.
    [[nodiscard]] async::Task<IoResult> waitReadableTask();

    /// Closes the socket and its event, telling the loop first so a flow parked on
    /// the event is resumed rather than left waiting on a handle that can never
    /// signal again.
    /// @param policy How a parked flow observes the close. The public @c close()
    ///        passes @c Resume — this object is still alive, so the flow may safely
    ///        re-read @c _closed. The destructor passes @c Cancel, because by then
    ///        the flow would be reading `this` through a dangling pointer.
    void close(FdWakePolicy policy) noexcept;

    /// Which readiness a park waits for.
    enum class Ready : std::uint8_t
    {
        Read,
        Write
    };

    /// How a park on the event ended.
    enum class ParkEnd : std::uint8_t
    {
        Ready,   ///< Latched readiness for the direction asked about, or the socket closed.
        Retired, ///< @c cancelRead took the read back; the caller completes with `Cancelled`.
    };

    /// @return A BadHandle error described by @p op when the socket is closed,
    ///         else nullopt — the guard read() and write() both open with.
    [[nodiscard]] std::optional<NetError> closedError(char const* op) const noexcept;

    /// Read-and-clears the shared readiness event into the per-direction latches below.
    ///
    /// WSAEventSelect allows exactly ONE event object per socket, so both directions signal the
    /// same handle — and `WSAResetEvent` discards every indication standing on it, the other
    /// direction's included. That is precisely how a parked writer was stranded: the reader reset
    /// the event before each recv and wiped the FD_WRITE raised for the writer, which Winsock
    /// re-raises only after another send returns WSAEWOULDBLOCK. `WSAEnumNetworkEvents` resets the
    /// event and REPORTS what it cleared in one atomic step, so every indication is recorded
    /// against the direction that wants it instead of being thrown away.
    void latchNetworkEvents() noexcept;

    /// Parks the caller until the socket is ready for @p kind: the WSAEWOULDBLOCK retry path
    /// read() and write() share.
    ///
    /// Consumes an already-latched indication before parking, and latches only AFTER a wake — the
    /// order is load-bearing. Enumerating before a park would clear the event the other direction
    /// may be suspended on, and a suspended waiter cannot re-check its latch; the event is
    /// therefore never reset ahead of a park, which is also what makes an indication raised
    /// between the failing syscall and the park resolve the wait instead of being lost.
    /// @return @c ParkEnd::Retired when @c cancelRead retired a READ park, else @c ParkEnd::Ready.
    [[nodiscard]] async::Task<ParkEnd> parkUntilReady(Ready kind);

    EventLoop& _loop;
    SOCKET _socket;
    WSAEVENT _event;
    std::string _peerAddress;
    bool _closed = false;
    /// Latched by a read that observed the peer's EOF. SEPARATE from @c _closed on purpose:
    /// @c _closed gates read() and write(), and a peer that shut only its write side leaves
    /// this end perfectly able to keep writing. Only @c isClosed() consults this.
    bool _peerClosed = false;
    /// Latched readiness per direction, fed by @ref latchNetworkEvents. Sticky until the
    /// direction that wants it consumes it, so an indication raised for one direction can no
    /// longer be destroyed by the other's wait.
    bool _readReady = false;
    bool _writeReady = false;
    /// The frame of @ref parkUntilReady while it is parked for a READ, else empty: what
    /// @c cancelRead hands to @c EventLoop::cancelPending. Cleared on every way out of the park,
    /// so it can never name a frame that has gone -- a stale address could match a newer frame.
    std::coroutine_handle<> _readWaiter;
    /// Set by @c cancelRead immediately before it resumes @c _readWaiter, and consumed by it.
    bool _readRetired = false;
};

} // namespace core::net
