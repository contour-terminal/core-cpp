// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file SystemPipe.hpp
/// @brief A cross-platform, in-process byte channel whose read end is *waitable*
///        by an event loop on every platform.
///
/// Unlike an anonymous OS pipe for talking to a child process, @c SystemPipe is
/// built so its read end can be multiplexed by an event loop
/// (poll(2) on POSIX, WaitForMultipleObjects on Windows). Anonymous Windows pipes
/// are NOT waitable objects, so on Windows the channel is a loopback TCP socket
/// pair with the read end mapped to a waitable event via WSAEventSelect; on POSIX
/// it is a socketpair(2) whose read fd polls directly. Both expose a
/// @c waitHandle() the loop can register and a read/write fd for the bytes.
///
/// This is what lets the same `co_await loop.waitReadable(pipe.waitHandle())`
/// readiness test (and any cross-thread wakeup-style channel, such as an event
/// loop's own post() self-pipe) work identically on Linux, macOS, and Windows.

#include <core/platform/PlatformError.hpp>
#include <core/platform/Types.hpp>

#include <cstddef>
#include <expected>
#include <memory>

namespace core::platform
{

/// A connected, in-process byte channel with a waitable read end.
///
/// Move-only RAII: closes all owned handles on destruction. The read and write
/// ends are connected — bytes written to @c writeFd() become readable on
/// @c readFd(), and @c waitHandle() signals when @c readFd() has data (or the
/// peer closed).
///
/// On POSIX both ends are non-blocking and close-on-exec. A producer never stalls on
/// a full channel: @c write() reports a write the full buffer refused as done, because
/// the bytes already pending wake the reader just the same, which is all a wakeup
/// channel's byte signals. And a drain after readiness never parks the loop on a
/// spurious wakeup: @c read() on an empty channel fails with @c PlatformError::IoError
/// instead of blocking.
class SystemPipe
{
  public:
    virtual ~SystemPipe() = default;

    SystemPipe() = default;
    SystemPipe(SystemPipe const&) = delete;
    SystemPipe& operator=(SystemPipe const&) = delete;
    SystemPipe(SystemPipe&&) = default;
    SystemPipe& operator=(SystemPipe&&) = default;

    /// @return The native handle the loop watches for read-readiness. On POSIX
    ///         this equals @c readFd(); on Windows it is a WSAEVENT associated with
    ///         the read socket via WSAEventSelect.
    [[nodiscard]] virtual NativeHandle waitHandle() const noexcept = 0;

    /// @return The native handle to read bytes from.
    [[nodiscard]] virtual NativeHandle readFd() const noexcept = 0;

    /// @return The native handle to write bytes to.
    [[nodiscard]] virtual NativeHandle writeFd() const noexcept = 0;

    /// Writes bytes into the channel. Thread-safe with respect to a concurrent
    /// reader on the other end (it is a socket send).
    /// @param data Pointer to the bytes to send.
    /// @param size Number of bytes to send.
    /// @return Bytes written, or a @c PlatformError on failure. On POSIX, a write the
    ///         full channel refused reports @p size: see the class documentation.
    [[nodiscard]] virtual std::expected<std::size_t, PlatformError> write(void const* data,
                                                                          std::size_t size) = 0;

    /// Reads available bytes from the channel (non-blocking once @c waitHandle()
    /// has signalled readiness). On Windows this also resets the readiness event.
    /// @param data Destination buffer.
    /// @param size Maximum bytes to read.
    /// @return Bytes read (0 on peer close), or a @c PlatformError on failure.
    [[nodiscard]] virtual std::expected<std::size_t, PlatformError> read(void* data, std::size_t size) = 0;

    /// @return True if both ends and the wait handle are valid.
    [[nodiscard]] virtual bool good() const noexcept = 0;
};

/// Creates a connected @c SystemPipe.
/// @return A unique pointer to the channel on success, or a @c PlatformError.
[[nodiscard]] std::expected<std::unique_ptr<SystemPipe>, PlatformError> createSystemPipe();

} // namespace core::platform
