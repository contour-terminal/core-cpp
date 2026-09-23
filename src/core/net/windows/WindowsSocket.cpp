// SPDX-License-Identifier: Apache-2.0
#include <core/net/windows/WindowsSocket.hpp>

#include <core/net/SocketContract.hpp>
#include <core/net/windows/InvalidSocket.hpp>
#include <core/net/windows/NetworkEvents.hpp>
#include <core/net/windows/WinsockError.hpp>

#include <array>
#include <coroutine>
#include <memory>
#include <tuple>
#include <utility>

namespace core::net
{

namespace
{
    /// Records the awaiting coroutine's handle and carries on without suspending: how a coroutine
    /// learns its own handle, for a caller that has to find it again in the loop's park table.
    struct PublishSelf
    {
        std::coroutine_handle<>* out; ///< Where to record the handle.

        /// @return False: record the handle in @c await_suspend, which is the only place it is
        ///         offered.
        [[nodiscard]] bool await_ready() const noexcept { return false; }

        /// @param self The awaiting coroutine.
        /// @return False: resume it at once.
        [[nodiscard]] bool await_suspend(std::coroutine_handle<> self) const noexcept
        {
            *out = self;
            return false;
        }

        void await_resume() const noexcept {}
    };
} // namespace

WindowsSocket::WindowsSocket(EventLoop& loop, SOCKET socket, std::string peerAddress) noexcept:
    _loop(loop), _socket(socket), _event(WSACreateEvent()), _peerAddress(std::move(peerAddress))
{
    // Associate the socket's read/write/close readiness with the event so the
    // loop can wait on it. WSAEventSelect also sets the socket non-blocking.
    if (_event != WSA_INVALID_EVENT && _socket != detail::InvalidSocket)
        WSAEventSelect(_socket, _event, FD_READ | FD_WRITE | FD_CLOSE);
}

WindowsSocket::~WindowsSocket()
{
    // Cancel, not Resume: read/write and parkUntilReady reach _closed, _event and
    // _socket through `this`, which is about to stop existing. Unwinding via
    // OperationCancelled never re-enters the body.
    close(FdWakePolicy::Cancel);
}

void WindowsSocket::close() noexcept
{
    close(FdWakePolicy::Resume);
}

void WindowsSocket::close(FdWakePolicy policy) noexcept
{
    if (_closed)
        return;
    _closed = true;
    if (_event != WSA_INVALID_EVENT)
    {
        // Before the close, while the handle is still valid. The event -- not the
        // socket -- is what parkUntilReady registers with the loop, so it is the
        // handle a parked flow must be woken by.
        _loop.notifyHandleClosing(_event, policy);
        WSACloseEvent(_event);
        _event = WSA_INVALID_EVENT;
    }
    if (_socket != detail::InvalidSocket)
    {
        closesocket(_socket);
        _socket = detail::InvalidSocket;
    }
}

std::optional<NetError> WindowsSocket::closedError(char const* op) const noexcept
{
    if (_closed || _socket == detail::InvalidSocket)
        return makeNetError(NetErrorCode::BadHandle, 0, op);
    return std::nullopt;
}

void WindowsSocket::latchNetworkEvents() noexcept
{
    auto const indications = consumeNetworkEvents(_socket, _event);
    // FD_CLOSE feeds BOTH latches: a peer that hung up makes recv report EOF and send fail, and
    // whichever direction is parked has to wake up to observe it.
    if ((indications & (FD_READ | FD_CLOSE)) != 0)
        _readReady = true;
    if ((indications & (FD_WRITE | FD_CLOSE)) != 0)
        _writeReady = true;
}

void WindowsSocket::cancelRead() noexcept
{
    if (_closed || !_readWaiter)
        return;
    // Taken back first, so the park -- and its registration on the event -- is gone before the
    // frame runs again; `true` means this call now owns the resumption. `false` means the loop no
    // longer holds the waiter, so there is nothing parked here to retire.
    auto const waiter = std::exchange(_readWaiter, {});
    if (!_loop.cancelPending(waiter))
        return;
    _readRetired = true;
    // INLINE, as `PosixSocket` settles a retired read: the flow is resumed before this returns, so
    // a flow that arms its next read there leaves a NEW waiter, and a second call retires that one.
    waiter.resume();
}

async::Task<WindowsSocket::ParkEnd> WindowsSocket::parkUntilReady(Ready kind)
{
    auto& latch = kind == Ready::Read ? _readReady : _writeReady;
    // The close() guard is the loop's exit, not an optimization: close() invalidates the event, and
    // a handle that can never signal again would otherwise be parked on for ever — or, since the
    // reactor reports an invalid handle as ready, spun on. Returning hands the decision back to
    // read()/write(), whose own closedError() guard reports it as the BadHandle it is.
    while (!_closed && _event != WSA_INVALID_EVENT)
    {
        // Consume a latched indication WITHOUT touching the shared event. Enumerating here instead
        // would clear the event the OTHER direction may be parked on at this very moment, and that
        // waiter has no way to re-check its latch — it is already suspended. Reading the latch is
        // the only safe thing to do before parking.
        if (std::exchange(latch, false))
            co_return ParkEnd::Ready;

        // Nothing latched: park. An indication raised between the syscall that returned
        // WSAEWOULDBLOCK and this point has left the event SIGNALLED, so the wait resolves on the
        // next pump rather than being lost — which is why the event is never reset before a park.
        if (kind == Ready::Read)
        {
            // Published for `cancelRead` for exactly as long as this frame is parked, and cleared on
            // EVERY way out of the park, so it can never name a frame that has gone.
            //
            // **The unwinding path must not touch `this` unless the socket still exists.** The
            // destructor closes with `FdWakePolicy::Cancel`, and the `OperationCancelled` that
            // unwinds this frame arrives a TURN LATER, after the socket is freed -- the class
            // comment's "unwinding never re-enters the body" is what makes that safe, and a scope
            // guard writing `_readWaiter` would break it. A flow cancelled by its own token unwinds
            // the same way with the socket alive, and there the handle must be cleared, or a later
            // `cancelRead` could hand `cancelPending` an address a newer frame now occupies. The
            // lifetime token tells the two apart without reading freed storage.
            co_await PublishSelf { &_readWaiter };
            auto const lifetime = std::weak_ptr<void const> { _lifetime };
            try
            {
                co_await _loop.waitReadable(_event);
            }
            catch (...)
            {
                // `_readRetired` too: a `cancelRead` that resumes a flow whose own token is stopped
                // lands HERE, not on the line that consumes the flag, and a flag left set would
                // retire the socket's NEXT read the moment real data woke it.
                if (!lifetime.expired())
                {
                    _readWaiter = {};
                    _readRetired = false;
                }
                throw;
            }
            _readWaiter = {};
            if (std::exchange(_readRetired, false))
                co_return ParkEnd::Retired;
        }
        else
            co_await _loop.waitWritable(_event);

        // Woken. Both directions wake together (they share the event), so read-and-clear it into
        // the per-direction latches: whichever gets there first RECORDS the other's indication for
        // it instead of destroying it. Then loop, to consume our own if it is among them.
        latchNetworkEvents();
    }
    co_return ParkEnd::Ready;
}

IoAwaitable WindowsSocket::read(std::span<std::byte> buffer)
{
    contract::requireReadBuffer(buffer);
    return IoAwaitable { readTask(buffer) };
}

IoAwaitable WindowsSocket::write(std::span<std::byte const> buffer)
{
    return IoAwaitable { writeTask(buffer) };
}

async::Task<IoResult> WindowsSocket::readTask(std::span<std::byte> buffer)
{
    while (true)
    {
        if (auto const closed = closedError("read on closed socket"))
            co_return std::unexpected(*closed);

        // Clamp to INT_MAX: ::recv's length parameter is int; a larger buffer
        // would have its size truncated (possibly to a negative value), causing
        // WSAEFAULT and a spurious connection drop.
        auto const n = ::recv(_socket,
                              reinterpret_cast<char*>(buffer.data()),
                              static_cast<int>(std::min(buffer.size(), static_cast<size_t>(INT_MAX))),
                              0);
        if (n > 0)
            co_return static_cast<std::size_t>(n);
        if (n == 0)
        {
            _peerClosed = true;          // isClosed() now answers true, as ISocket documents
            co_return std::size_t { 0 }; // clean EOF
        }

        auto const err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK)
        {
            auto const parked = co_await parkUntilReady(Ready::Read);
            if (parked == ParkEnd::Retired)
                co_return std::unexpected(
                    makeNetError(NetErrorCode::Cancelled, 0, "the read was retired by cancelRead"));
            continue;
        }
        co_return std::unexpected(detail::fromWinsockError(err, "recv"));
    }
}

IoAwaitable WindowsSocket::waitReadable()
{
    return IoAwaitable { waitReadableTask() };
}

ResultAwaitable<void> WindowsSocket::shutdownWrite()
{
    // Completes INLINE, as on POSIX: there is nothing to flush before the FIN.
    if (_closed || _socket == detail::InvalidSocket)
        return ResultAwaitable<void> { std::expected<void, NetError> {} };
    if (::shutdown(_socket, SD_SEND) == SOCKET_ERROR)
    {
        auto const err = ::WSAGetLastError();
        // WSAENOTCONN is the state the caller asked for, not a failure to report.
        if (err != WSAENOTCONN)
            return ResultAwaitable<void> { std::unexpected(detail::fromWinsockError(err, "shutdown")) };
    }
    return ResultAwaitable<void> { std::expected<void, NetError> {} };
}

async::Task<IoResult> WindowsSocket::waitReadableTask()
{
    while (true)
    {
        if (auto const closed = closedError("waitReadable on closed socket"))
            co_return std::unexpected(*closed);

        // One byte, peeked: it consumes nothing the following read would have returned, and the
        // count it measures IS the contract -- `0` is EOF, `>0` is data pending.
        auto probe = std::array<char, 1> {};
        auto const got = ::recv(_socket, probe.data(), 1, MSG_PEEK);
        if (got >= 0)
            co_return got == 0 ? std::size_t { 0 } : std::size_t { 1 };

        auto const err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK)
        {
            auto const parked = co_await parkUntilReady(Ready::Read);
            if (parked == ParkEnd::Retired)
                co_return std::unexpected(
                    makeNetError(NetErrorCode::Cancelled, 0, "the watch was retired by cancelRead"));
            continue;
        }
        // Anything but "nothing yet" is the peer GONE -- a reset above all -- and answering "one
        // byte is pending" there says the opposite of what happened to a caller that only watches.
        co_return std::unexpected(detail::fromWinsockError(err, "recv"));
    }
}

async::Task<IoResult> WindowsSocket::writeTask(std::span<std::byte const> buffer)
{
    std::size_t total = 0;
    while (total < buffer.size())
    {
        if (auto const closed = closedError("write on closed socket"))
            co_return std::unexpected(*closed);

        auto const remaining = buffer.subspan(total);
        // Clamp to INT_MAX: ::send's length parameter is int; the write loop
        // handles the remainder on the next iteration, so a truncated length
        // is a safe partial-send rather than a silent failure.
        auto const n = ::send(_socket,
                              reinterpret_cast<char const*>(remaining.data()),
                              static_cast<int>(std::min(remaining.size(), static_cast<size_t>(INT_MAX))),
                              0);
        if (n > 0)
        {
            total += static_cast<std::size_t>(n);
            continue;
        }

        auto const err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK)
        {
            std::ignore = co_await parkUntilReady(Ready::Write);
            continue;
        }
        co_return std::unexpected(detail::fromWinsockError(err, "send"));
    }
    co_return total;
}

} // namespace core::net
