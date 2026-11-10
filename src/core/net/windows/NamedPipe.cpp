// SPDX-License-Identifier: Apache-2.0
#include <core/net/windows/NamedPipe.hpp>

#include <core/async/Task.hpp>
#include <core/net/IoAwaitable.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/SocketContract.hpp>

// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

#include <algorithm>
#include <array>
#include <limits>
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

    /// @return The error of a failed Win32 call, classified.
    NetError pipeError(DWORD error, char const* what)
    {
        auto const code = error == ERROR_OPERATION_ABORTED ? NetErrorCode::Cancelled
                          : error == ERROR_ACCESS_DENIED   ? NetErrorCode::PermissionDenied
                                                           : NetErrorCode::SystemError;
        return makeNetError(code, static_cast<int>(error), what);
    }

    /// The pipe hanging up is its clean end.
    bool isHangUp(DWORD error) noexcept
    {
        return error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED;
    }

    /// A manual-reset event, closed on destruction.
    struct Event
    {
        HANDLE handle = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

        Event() = default;
        Event(Event const&) = delete;
        Event& operator=(Event const&) = delete;
        Event(Event&&) = delete;
        Event& operator=(Event&&) = delete;
        ~Event()
        {
            if (handle != nullptr)
                ::CloseHandle(handle);
        }
    };

    /// A named pipe client end, opened for overlapped I/O.
    ///
    /// Each read and write carries an OVERLAPPED whose event the loop waits on as a waitable HANDLE
    /// (the loop's `HandleKind::Waitable`), so a slow agent (a security key awaiting a touch, an
    /// `ssh-add -c` confirmation) parks the awaiting flow and nothing else. A named pipe is not a
    /// SOCKET, so the loop's completion port cannot serve it as it serves an @c IocpSocket.
    class NamedPipeSocket final: public ISocket
    {
      public:
        NamedPipeSocket(EventLoop* loop, HANDLE handle) noexcept: _loop(loop), _handle(handle) {}

        ~NamedPipeSocket() override { close(); }

        NamedPipeSocket(NamedPipeSocket const&) = delete;
        NamedPipeSocket& operator=(NamedPipeSocket const&) = delete;
        NamedPipeSocket(NamedPipeSocket&&) = delete;
        NamedPipeSocket& operator=(NamedPipeSocket&&) = delete;

        IoAwaitable read(std::span<std::byte> buffer) override
        {
            contract::requireReadBuffer(buffer);
            return IoAwaitable { readOnce(buffer) };
        }

        IoAwaitable write(std::span<std::byte const> buffer) override
        {
            return IoAwaitable { writeAll(buffer) };
        }

        /// @copydoc ISocket::cancelRead
        /// Cancels the read in flight, which then answers @c NetErrorCode::Cancelled.
        void cancelRead() noexcept override
        {
            if (_readOp != nullptr)
                ::CancelIoEx(_handle, _readOp);
        }

        /// Cancels what is in flight and closes the handle, once nothing owns an OVERLAPPED any more.
        void close() noexcept override
        {
            if (_closing)
                return;
            _closing = true;
            if (_handle != INVALID_HANDLE_VALUE)
                ::CancelIoEx(_handle, nullptr);
            closeHandleIfIdle();
        }

        [[nodiscard]] bool isClosed() const noexcept override { return _closing; }

      private:
        /// Closes the handle once the kernel has let go of every OVERLAPPED.
        void closeHandleIfIdle() noexcept
        {
            if (_closing && _inFlight == 0 && _handle != INVALID_HANDLE_VALUE)
                ::CloseHandle(std::exchange(_handle, INVALID_HANDLE_VALUE));
        }

        /// Settles @p overlapped after ReadFile/WriteFile answered ERROR_IO_PENDING: waits on its
        /// event, and, if the flow is stopped meanwhile, cancels and waits for the kernel to let go
        /// of the OVERLAPPED (it lives in the caller's frame) before the stop unwinds.
        /// @return The bytes transferred, or the Win32 error.
        async::Task<std::expected<DWORD, DWORD>> settle(OVERLAPPED* overlapped, HANDLE event)
        {
            try
            {
                co_await _loop->waitReadable(event, HandleKind::Waitable);
            }
            catch (...)
            {
                ::CancelIoEx(_handle, overlapped);
                auto ignored = DWORD { 0 };
                ::GetOverlappedResult(_handle, overlapped, &ignored, TRUE);
                throw;
            }
            auto transferred = DWORD { 0 };
            if (::GetOverlappedResult(_handle, overlapped, &transferred, FALSE) == FALSE)
                co_return std::unexpected(::GetLastError());
            co_return transferred;
        }

        async::Task<IoResult> readOnce(std::span<std::byte> buffer)
        {
            if (_closing)
                co_return std::unexpected(makeNetError(NetErrorCode::BadHandle, 0, "pipe"));
            auto const want =
                static_cast<DWORD>(std::min<std::size_t>(buffer.size(), std::numeric_limits<DWORD>::max()));
            auto overlapped = OVERLAPPED {};
            overlapped.hEvent = _readEvent.handle;
            ::ResetEvent(_readEvent.handle);
            auto const inFlight = InFlight { *this };
            auto got = DWORD { 0 };
            if (::ReadFile(_handle, buffer.data(), want, &got, &overlapped) == FALSE)
            {
                auto error = ::GetLastError();
                if (error == ERROR_IO_PENDING)
                {
                    _readOp = &overlapped;
                    auto const settled = co_await settle(&overlapped, _readEvent.handle);
                    if (settled)
                        co_return static_cast<std::size_t>(*settled);
                    error = settled.error();
                }
                if (isHangUp(error))
                    co_return std::size_t { 0 };
                co_return std::unexpected(pipeError(error, "ReadFile(pipe)"));
            }
            co_return static_cast<std::size_t>(got);
        }

        async::Task<IoResult> writeAll(std::span<std::byte const> buffer)
        {
            auto sent = std::size_t { 0 };
            while (sent < buffer.size())
            {
                if (_closing)
                    co_return std::unexpected(makeNetError(NetErrorCode::BadHandle, 0, "pipe"));
                auto const rest = buffer.subspan(sent);
                auto const want =
                    static_cast<DWORD>(std::min<std::size_t>(rest.size(), std::numeric_limits<DWORD>::max()));
                auto overlapped = OVERLAPPED {};
                overlapped.hEvent = _writeEvent.handle;
                ::ResetEvent(_writeEvent.handle);
                auto const inFlight = InFlight { *this };
                auto wrote = DWORD { 0 };
                if (::WriteFile(_handle, rest.data(), want, &wrote, &overlapped) == FALSE)
                {
                    auto error = ::GetLastError();
                    if (error != ERROR_IO_PENDING)
                        co_return std::unexpected(pipeError(error, "WriteFile(pipe)"));
                    auto const settled = co_await settle(&overlapped, _writeEvent.handle);
                    if (!settled)
                        co_return std::unexpected(pipeError(settled.error(), "WriteFile(pipe)"));
                    wrote = *settled;
                }
                sent += wrote;
            }
            co_return sent;
        }

        /// Counts an operation whose OVERLAPPED the kernel may still hold, so close() waits for it.
        struct InFlight
        {
            explicit InFlight(NamedPipeSocket& owner) noexcept: socket(owner) { ++socket._inFlight; }
            InFlight(InFlight const&) = delete;
            InFlight& operator=(InFlight const&) = delete;
            InFlight(InFlight&&) = delete;
            InFlight& operator=(InFlight&&) = delete;
            ~InFlight()
            {
                --socket._inFlight;
                socket._readOp = nullptr;
                socket.closeHandleIfIdle();
            }
            NamedPipeSocket& socket;
        };

        EventLoop* _loop;
        HANDLE _handle;
        Event _readEvent;
        Event _writeEvent;
        OVERLAPPED* _readOp = nullptr;
        int _inFlight = 0;
        bool _closing = false;
    };
} // namespace

std::expected<std::unique_ptr<ISocket>, NetError> connectNamedPipe(EventLoop& loop, std::wstring_view name)
{
    auto const path = std::wstring { name };
    // Identification only, so a pipe server cannot act as this user (the access the OpenSSH client
    // itself asks for); overlapped, so a slow agent parks a flow rather than the loop.
    auto handle = INVALID_HANDLE_VALUE;
    for ([[maybe_unused]] auto const attempt: std::views::iota(0, BusyAttempts))
    {
        handle = ::CreateFileW(path.c_str(),
                               GENERIC_READ | GENERIC_WRITE,
                               0,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
                               nullptr);
        if (handle != INVALID_HANDLE_VALUE || ::GetLastError() != ERROR_PIPE_BUSY)
            break;
        // Every instance is serving another client; as OpenSSH does, wait a short while for one.
        ::WaitNamedPipeW(path.c_str(), BusyWaitMs);
    }
    if (handle == INVALID_HANDLE_VALUE)
    {
        auto const error = ::GetLastError();
        auto const code = error == ERROR_FILE_NOT_FOUND  ? NetErrorCode::ConnRefused
                          : error == ERROR_ACCESS_DENIED ? NetErrorCode::PermissionDenied
                                                         : NetErrorCode::SystemError;
        return std::unexpected(makeNetError(code, static_cast<int>(error), "CreateFileW(pipe)"));
    }
    return std::make_unique<NamedPipeSocket>(&loop, handle);
}

} // namespace core::net::detail
