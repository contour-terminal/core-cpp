// SPDX-License-Identifier: Apache-2.0
#include <core/net/windows/NamedPipe.hpp>

#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/IoAwaitable.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/SocketContract.hpp>
#include <core/net/detail/ScopeGuard.hpp>

// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

#include <algorithm>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <utility>

namespace core::net::detail
{

namespace
{
    /// How long, and how often, a busy pipe is waited for: every instance of the agent's pipe is
    /// taken while it serves another client.
    constexpr auto BusyAttempts = 3;
    constexpr auto BusyWaitMs = DWORD { 500 };

    /// @return The error of a failed Win32 call, classified; the pipe hanging up is its clean end,
    ///         which is @c NetErrorCode::Eof here and a zero-byte read to the caller.
    NetError pipeError(DWORD error, char const* what)
    {
        auto code = NetErrorCode::SystemError;
        if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
            code = NetErrorCode::Eof;
        else if (error == ERROR_OPERATION_ABORTED)
            code = NetErrorCode::Cancelled;
        else if (error == ERROR_ACCESS_DENIED)
            code = NetErrorCode::PermissionDenied;
        return makeNetError(code, static_cast<int>(error), what);
    }

    /// What a pipe's operations share: the handle, the events and the read in flight.
    ///
    /// Held by the socket and by every operation, as @c IocpSocket's state is, so the owner may
    /// destroy the socket after close() while a flow is still parked (ISocket::close): the
    /// operation keeps the handle and the OVERLAPPED's event alive until it has settled, and the
    /// last share closes the handle.
    struct PipeState
    {
        EventLoop* loop;
        HANDLE handle;
        HANDLE readEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE writeEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        /// The read in flight, for cancelRead; set and cleared by the read path alone.
        OVERLAPPED* readOp = nullptr;

        /// @param owner The loop the operations wait on.
        /// @param pipe The open pipe; owned from here on, closed with the last share.
        PipeState(EventLoop* owner, HANDLE pipe) noexcept: loop(owner), handle(pipe) {}

        PipeState(PipeState const&) = delete;
        PipeState& operator=(PipeState const&) = delete;
        PipeState(PipeState&&) = delete;
        PipeState& operator=(PipeState&&) = delete;

        ~PipeState()
        {
            for (auto const event: { readEvent, writeEvent })
                if (event != nullptr)
                    ::CloseHandle(event);
            ::CloseHandle(handle);
        }

        /// @return Whether both events could be made.
        [[nodiscard]] bool usable() const noexcept { return readEvent != nullptr && writeEvent != nullptr; }
    };

    /// Settles an overlapped operation that answered ERROR_IO_PENDING: waits on its event, and
    /// however it ends -- completion, a stop, or the frame destroyed while parked -- leaves the
    /// kernel done with the OVERLAPPED (which lives in the caller's frame) before returning.
    /// @return The bytes transferred, or the classified error; a failure to register the wait is
    ///         a value, a stop of the flow still throws @c async::OperationCancelled.
    async::Task<std::expected<DWORD, NetError>> settle(std::shared_ptr<PipeState> state,
                                                       OVERLAPPED* overlapped,
                                                       HANDLE event,
                                                       char const* what)
    {
        auto settled = false;
        auto const release = detail::ScopeGuard { [&]() noexcept {
            if (settled)
                return;
            ::CancelIoEx(state->handle, overlapped);
            auto ignored = DWORD { 0 };
            ::GetOverlappedResult(state->handle, overlapped, &ignored, TRUE);
        } };
        try
        {
            co_await state->loop->waitReadable(event, HandleKind::Waitable);
        }
        catch (FdRegistrationFailed const&)
        {
            co_return std::unexpected(makeNetError(NetErrorCode::SystemError, 0, "pipe wait registration"));
        }
        auto transferred = DWORD { 0 };
        if (::GetOverlappedResult(state->handle, overlapped, &transferred, FALSE) == FALSE)
        {
            settled = true;
            co_return std::unexpected(pipeError(::GetLastError(), what));
        }
        settled = true;
        co_return transferred;
    }

    async::Task<IoResult> readOnce(std::shared_ptr<PipeState> state, std::span<std::byte> buffer)
    {
        auto const want =
            static_cast<DWORD>(std::min<std::size_t>(buffer.size(), std::numeric_limits<DWORD>::max()));
        auto overlapped = OVERLAPPED {};
        overlapped.hEvent = state->readEvent;
        ::ResetEvent(state->readEvent);
        auto const clearReadOp = detail::ScopeGuard { [&]() noexcept { state->readOp = nullptr; } };
        auto got = DWORD { 0 };
        if (::ReadFile(state->handle, buffer.data(), want, &got, &overlapped) != FALSE)
            co_return static_cast<std::size_t>(got);

        auto const error = ::GetLastError();
        if (error != ERROR_IO_PENDING)
        {
            auto failure = pipeError(error, "ReadFile(pipe)");
            if (failure.code == NetErrorCode::Eof)
                co_return std::size_t { 0 };
            co_return std::unexpected(std::move(failure));
        }
        state->readOp = &overlapped;
        auto const settled = co_await settle(state, &overlapped, state->readEvent, "ReadFile(pipe)");
        if (settled)
            co_return static_cast<std::size_t>(*settled);
        if (settled.error().code == NetErrorCode::Eof)
            co_return std::size_t { 0 };
        co_return std::unexpected(settled.error());
    }

    async::Task<IoResult> writeAll(std::shared_ptr<PipeState> state, std::span<std::byte const> buffer)
    {
        auto sent = std::size_t { 0 };
        while (sent < buffer.size())
        {
            auto const rest = buffer.subspan(sent);
            auto const want =
                static_cast<DWORD>(std::min<std::size_t>(rest.size(), std::numeric_limits<DWORD>::max()));
            auto overlapped = OVERLAPPED {};
            overlapped.hEvent = state->writeEvent;
            ::ResetEvent(state->writeEvent);
            auto wrote = DWORD { 0 };
            if (::WriteFile(state->handle, rest.data(), want, &wrote, &overlapped) == FALSE)
            {
                auto const error = ::GetLastError();
                if (error != ERROR_IO_PENDING)
                    co_return std::unexpected(pipeError(error, "WriteFile(pipe)"));
                auto const settled =
                    co_await settle(state, &overlapped, state->writeEvent, "WriteFile(pipe)");
                if (!settled)
                    co_return std::unexpected(settled.error());
                wrote = *settled;
            }
            // A write that moved nothing would loop for ever; the pipe is not taking bytes.
            if (wrote == 0)
                co_return std::unexpected(
                    makeNetError(NetErrorCode::SystemError, 0, "WriteFile(pipe) wrote 0"));
            sent += wrote;
        }
        co_return sent;
    }

    /// A named pipe client end, opened for overlapped I/O.
    ///
    /// Each read and write carries an OVERLAPPED whose event the loop waits on as a waitable HANDLE
    /// (the loop's `HandleKind::Waitable`), so a slow agent (a security key awaiting a touch, an
    /// `ssh-add -c` confirmation) parks the awaiting flow and nothing else. A named pipe is not a
    /// SOCKET, which @c IocpSocket serves.
    class NamedPipeSocket final: public ISocket
    {
      public:
        explicit NamedPipeSocket(std::shared_ptr<PipeState> state) noexcept: _state(std::move(state)) {}

        ~NamedPipeSocket() override { close(); }

        NamedPipeSocket(NamedPipeSocket const&) = delete;
        NamedPipeSocket& operator=(NamedPipeSocket const&) = delete;
        NamedPipeSocket(NamedPipeSocket&&) = delete;
        NamedPipeSocket& operator=(NamedPipeSocket&&) = delete;

        IoAwaitable read(std::span<std::byte> buffer) override
        {
            contract::requireReadBuffer(buffer);
            if (_state == nullptr)
                return closedAnswer();
            return IoAwaitable { readOnce(_state, buffer) };
        }

        IoAwaitable write(std::span<std::byte const> buffer) override
        {
            if (_state == nullptr)
                return closedAnswer();
            return IoAwaitable { writeAll(_state, buffer) };
        }

        /// @copydoc ISocket::cancelRead
        /// Cancels the read in flight, which then answers @c NetErrorCode::Cancelled.
        void cancelRead() noexcept override
        {
            if (_state != nullptr && _state->readOp != nullptr)
                ::CancelIoEx(_state->handle, _state->readOp);
        }

        /// Cancels what is in flight and lets go of the pipe. Each operation holds a share of the
        /// state, so the handle closes when the last has settled: a parked flow answers
        /// @c NetErrorCode::Cancelled when the loop resumes it, even if the socket is gone by then.
        void close() noexcept override
        {
            if (_state == nullptr)
                return;
            ::CancelIoEx(_state->handle, nullptr);
            _state.reset();
        }

        [[nodiscard]] bool isClosed() const noexcept override { return _state == nullptr; }

      private:
        static IoAwaitable closedAnswer()
        {
            return IoAwaitable { IoResult {
                std::unexpected(makeNetError(NetErrorCode::BadHandle, 0, "pipe")) } };
        }

        std::shared_ptr<PipeState> _state;
    };
} // namespace

std::expected<std::unique_ptr<ISocket>, NetError> connectNamedPipe(EventLoop& loop, std::wstring_view name)
{
    auto const path = std::wstring { name };
    // Identification only, so a pipe server cannot act as this user (the access the OpenSSH client
    // itself asks for); overlapped, so a slow agent parks a flow rather than the loop.
    auto handle = INVALID_HANDLE_VALUE;
    auto error = DWORD { ERROR_SUCCESS };
    for (auto const attempt: std::views::iota(0, BusyAttempts))
    {
        handle = ::CreateFileW(path.c_str(),
                               GENERIC_READ | GENERIC_WRITE,
                               0,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
                               nullptr);
        if (handle != INVALID_HANDLE_VALUE)
            break;
        error = ::GetLastError();
        // Every instance is serving another client; as OpenSSH does, wait a short while for one.
        // After the last attempt there is nothing to wait for, and the error stays ERROR_PIPE_BUSY.
        if (error != ERROR_PIPE_BUSY || attempt + 1 == BusyAttempts)
            break;
        ::WaitNamedPipeW(path.c_str(), BusyWaitMs);
    }
    if (handle == INVALID_HANDLE_VALUE)
    {
        auto const code = error == ERROR_FILE_NOT_FOUND  ? NetErrorCode::ConnRefused
                          : error == ERROR_ACCESS_DENIED ? NetErrorCode::PermissionDenied
                                                         : NetErrorCode::SystemError;
        return std::unexpected(makeNetError(code, static_cast<int>(error), "CreateFileW(pipe)"));
    }
    // From here the state owns the handle, whatever follows.
    auto state = std::make_shared<PipeState>(&loop, handle);
    if (!state->usable())
        return std::unexpected(makeNetError(
            NetErrorCode::ResourceExhausted, static_cast<int>(::GetLastError()), "CreateEventW(pipe)"));
    return std::make_unique<NamedPipeSocket>(std::move(state));
}

} // namespace core::net::detail
