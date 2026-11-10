// SPDX-License-Identifier: Apache-2.0
#include <core/net/windows/NamedPipe.hpp>

#include <core/net/SocketContract.hpp>

// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace core::net::detail
{

namespace
{
    /// A named pipe client end, read and written synchronously.
    class NamedPipeSocket final: public ISocket
    {
      public:
        explicit NamedPipeSocket(HANDLE handle) noexcept: _handle(handle) {}
        ~NamedPipeSocket() override { close(); }

        NamedPipeSocket(NamedPipeSocket const&) = delete;
        NamedPipeSocket& operator=(NamedPipeSocket const&) = delete;
        NamedPipeSocket(NamedPipeSocket&&) = delete;
        NamedPipeSocket& operator=(NamedPipeSocket&&) = delete;

        IoAwaitable read(std::span<std::byte> buffer) override
        {
            contract::requireReadBuffer(buffer);
            if (_handle == INVALID_HANDLE_VALUE)
                return IoAwaitable { IoResult {
                    std::unexpected(makeNetError(NetErrorCode::BadHandle, 0, "pipe")) } };
            auto const want =
                static_cast<DWORD>(std::min<std::size_t>(buffer.size(), std::numeric_limits<DWORD>::max()));
            auto got = DWORD { 0 };
            if (::ReadFile(_handle, buffer.data(), want, &got, nullptr) == FALSE)
            {
                auto const error = ::GetLastError();
                // The agent hanging up is the pipe's clean end.
                if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
                    return IoAwaitable { IoResult { std::size_t { 0 } } };
                return IoAwaitable { IoResult { std::unexpected(
                    makeNetError(NetErrorCode::SystemError, static_cast<int>(error), "ReadFile(pipe)")) } };
            }
            return IoAwaitable { IoResult { static_cast<std::size_t>(got) } };
        }

        IoAwaitable write(std::span<std::byte const> buffer) override
        {
            if (_handle == INVALID_HANDLE_VALUE)
                return IoAwaitable { IoResult {
                    std::unexpected(makeNetError(NetErrorCode::BadHandle, 0, "pipe")) } };
            auto sent = std::size_t { 0 };
            while (sent < buffer.size())
            {
                auto const rest = buffer.subspan(sent);
                auto const want =
                    static_cast<DWORD>(std::min<std::size_t>(rest.size(), std::numeric_limits<DWORD>::max()));
                auto wrote = DWORD { 0 };
                if (::WriteFile(_handle, rest.data(), want, &wrote, nullptr) == FALSE)
                    return IoAwaitable { IoResult {
                        std::unexpected(makeNetError(NetErrorCode::SystemError,
                                                     static_cast<int>(::GetLastError()),
                                                     "WriteFile(pipe)")) } };
                sent += wrote;
            }
            return IoAwaitable { IoResult { sent } };
        }

        /// @copydoc ISocket::cancelRead
        /// A no-op: a read here is a blocking ReadFile that has returned before its awaitable
        /// exists, so there is no parked frame to free.
        void cancelRead() noexcept override {}

        void close() noexcept override
        {
            auto const handle = std::exchange(_handle, INVALID_HANDLE_VALUE);
            if (handle != INVALID_HANDLE_VALUE)
                ::CloseHandle(handle);
        }

        [[nodiscard]] bool isClosed() const noexcept override { return _handle == INVALID_HANDLE_VALUE; }

      private:
        HANDLE _handle;
    };
} // namespace

std::expected<std::unique_ptr<ISocket>, NetError> connectNamedPipe([[maybe_unused]] EventLoop& loop,
                                                                   std::wstring_view name)
{
    auto const path = std::wstring { name };
    // Opened for identification only, so a pipe server cannot act as this user (the access the
    // OpenSSH client itself asks for).
    auto const handle = ::CreateFileW(path.c_str(),
                                      GENERIC_READ | GENERIC_WRITE,
                                      0,
                                      nullptr,
                                      OPEN_EXISTING,
                                      SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
                                      nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        auto const error = ::GetLastError();
        auto const code = error == ERROR_FILE_NOT_FOUND  ? NetErrorCode::ConnRefused
                          : error == ERROR_ACCESS_DENIED ? NetErrorCode::PermissionDenied
                                                         : NetErrorCode::SystemError;
        return std::unexpected(makeNetError(code, static_cast<int>(error), "CreateFileW(pipe)"));
    }
    return std::make_unique<NamedPipeSocket>(handle);
}

} // namespace core::net::detail
