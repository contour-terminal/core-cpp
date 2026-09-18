// SPDX-License-Identifier: Apache-2.0

// On Windows, winsock2.h MUST precede windows.h, so this block comes before any other include.
// clang-format off
#ifdef _WIN32
    #include <winsock2.h>
    #include <windows.h>
#endif
// clang-format on

#include <core/platform/SystemPipe.hpp>

#include <core/platform/WinsockInit.hpp>

#include <algorithm>
#include <array>
#include <climits>

#ifndef _WIN32
    #include <sys/socket.h>

    #include <cerrno>

    #include <fcntl.h>
    #include <unistd.h>

    // macOS and the BSDs lack MSG_NOSIGNAL; a SystemPipe owns both of its ends, so no peer can
    // close under a write and raise the SIGPIPE the flag exists to suppress.
    #ifndef MSG_NOSIGNAL
        #define MSG_NOSIGNAL 0
    #endif
#endif

namespace core::platform
{

namespace
{

#ifndef _WIN32
    /// Tests whether @p err is the errno a non-blocking descriptor reports when the operation
    /// would have blocked.
    ///
    /// POSIX permits EAGAIN and EWOULDBLOCK to name the same value, as they do on Linux and
    /// macOS, where testing both in one `||` is a tautology GCC's -Wlogical-op rejects. The
    /// preprocessor selects the comparison rather than `if constexpr`, whose discarded branch
    /// in a non-template function is still parsed and still warned about.
    /// @param err An errno value.
    /// @return True if @p err means "would block; retry when ready".
    [[nodiscard]] constexpr bool isWouldBlock(int err) noexcept
    {
    #if EAGAIN == EWOULDBLOCK
        return err == EAGAIN;
    #else
        return err == EAGAIN || err == EWOULDBLOCK;
    #endif
    }

    /// Makes @p fd non-blocking and close-on-exec.
    /// @return True on success.
    [[nodiscard]] bool makeNonBlockingCloexec(int fd) noexcept
    {
        auto const flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
            return false;
        auto const fdFlags = ::fcntl(fd, F_GETFD, 0);
        return fdFlags >= 0 && ::fcntl(fd, F_SETFD, fdFlags | FD_CLOEXEC) >= 0;
    }

    /// POSIX SystemPipe: a connected AF_UNIX socketpair. The read fd polls directly,
    /// so waitHandle() == readFd().
    class PosixSystemPipe final: public SystemPipe
    {
      public:
        PosixSystemPipe(int readFd, int writeFd) noexcept: _readFd(readFd), _writeFd(writeFd) {}

        ~PosixSystemPipe() override
        {
            if (_readFd >= 0)
                ::close(_readFd);
            if (_writeFd >= 0)
                ::close(_writeFd);
        }

        PosixSystemPipe(PosixSystemPipe const&) = delete;
        PosixSystemPipe& operator=(PosixSystemPipe const&) = delete;
        PosixSystemPipe(PosixSystemPipe&&) = delete;
        PosixSystemPipe& operator=(PosixSystemPipe&&) = delete;

        [[nodiscard]] NativeHandle waitHandle() const noexcept override { return _readFd; }

        [[nodiscard]] NativeHandle readFd() const noexcept override { return _readFd; }

        [[nodiscard]] NativeHandle writeFd() const noexcept override { return _writeFd; }

        [[nodiscard]] std::expected<std::size_t, PlatformError> write(void const* data,
                                                                      std::size_t size) override
        {
            auto const n = ::send(_writeFd, data, size, MSG_NOSIGNAL);
            if (n < 0 && isWouldBlock(errno))
                // The pair is non-blocking: a full buffer means a wakeup is already
                // pending, which is all this byte would have signalled. Report the
                // write as done so a busy loop never stalls a producer thread.
                return size;
            if (n < 0)
                return std::unexpected(PlatformError::IoError);
            return static_cast<std::size_t>(n);
        }

        [[nodiscard]] std::expected<ChannelResult, PlatformError> read(void* data, std::size_t size) override
        {
            if (size == 0)
                return ChannelResult {};
            auto const n = ::recv(_readFd, data, size, 0);
            if (n > 0)
                return ChannelResult::bytes(static_cast<std::size_t>(n));
            if (n == 0)
                return ChannelResult::endOfStream();
            if (isWouldBlock(errno) || errno == EINTR)
                return ChannelResult {};
            return std::unexpected(PlatformError::IoError);
        }

        [[nodiscard]] bool good() const noexcept override { return _readFd >= 0 && _writeFd >= 0; }

      private:
        int _readFd;
        int _writeFd;
    };

#else // _WIN32

    /// Windows SystemPipe: a loopback TCP socket pair with the read socket mapped to a
    /// waitable WSAEVENT via WSAEventSelect, so waitHandle() (the event) integrates
    /// with WaitForMultipleObjects while readFd()/writeFd() carry the bytes.
    class WindowsSystemPipe final: public SystemPipe
    {
      public:
        WindowsSystemPipe(SOCKET readSock, SOCKET writeSock, WSAEVENT event) noexcept:
            _readSock(readSock), _writeSock(writeSock), _event(event)
        {
        }

        ~WindowsSystemPipe() override
        {
            if (_event != WSA_INVALID_EVENT)
                WSACloseEvent(_event);
            if (_readSock != INVALID_SOCKET)
                closesocket(_readSock);
            if (_writeSock != INVALID_SOCKET)
                closesocket(_writeSock);
        }

        WindowsSystemPipe(WindowsSystemPipe const&) = delete;
        WindowsSystemPipe& operator=(WindowsSystemPipe const&) = delete;
        WindowsSystemPipe(WindowsSystemPipe&&) = delete;
        WindowsSystemPipe& operator=(WindowsSystemPipe&&) = delete;

        [[nodiscard]] NativeHandle waitHandle() const noexcept override
        {
            return static_cast<HANDLE>(_event);
        }

        [[nodiscard]] NativeHandle readFd() const noexcept override
        {
            return reinterpret_cast<HANDLE>(_readSock);
        }

        [[nodiscard]] NativeHandle writeFd() const noexcept override
        {
            return reinterpret_cast<HANDLE>(_writeSock);
        }

        [[nodiscard]] std::expected<std::size_t, PlatformError> write(void const* data,
                                                                      std::size_t size) override
        {
            auto const n = ::send(_writeSock, static_cast<char const*>(data), static_cast<int>(size), 0);
            if (n == SOCKET_ERROR)
                return std::unexpected(PlatformError::IoError);
            return static_cast<std::size_t>(n);
        }

        [[nodiscard]] std::expected<ChannelResult, PlatformError> read(void* data, std::size_t size) override
        {
            if (size == 0)
                return ChannelResult {};
            // Reset the readiness event before draining: WSAEventSelect is
            // edge-triggered per event type, and recv re-arms FD_READ if data remains.
            WSAResetEvent(_event);
            // recv() takes an int; a larger buffer is filled up to INT_MAX bytes.
            auto const capacity = static_cast<int>(std::min<std::size_t>(size, INT_MAX));
            auto const n = ::recv(_readSock, static_cast<char*>(data), capacity, 0);
            if (n > 0)
                return ChannelResult::bytes(static_cast<std::size_t>(n));
            if (n == 0)
                return ChannelResult::endOfStream();
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                return ChannelResult {};
            return std::unexpected(PlatformError::IoError);
        }

        [[nodiscard]] bool good() const noexcept override
        {
            return _readSock != INVALID_SOCKET && _writeSock != INVALID_SOCKET && _event != WSA_INVALID_EVENT;
        }

      private:
        SOCKET _readSock;
        SOCKET _writeSock;
        WSAEVENT _event;
    };

    /// Creates a connected loopback TCP socket pair (Windows lacks socketpair(2)).
    /// @param out The two connected sockets {accepted-server, client} on success.
    /// @return True on success.
    [[nodiscard]] bool makeLoopbackPair(std::array<SOCKET, 2>& out) noexcept
    {
        auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET)
            return false;

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // ask the OS for an ephemeral port
        auto cleanupListener = [&] {
            closesocket(listener);
        };

        if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR
            || ::listen(listener, 1) == SOCKET_ERROR)
        {
            cleanupListener();
            return false;
        }

        int addrLen = sizeof(addr);
        if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addrLen) == SOCKET_ERROR)
        {
            cleanupListener();
            return false;
        }

        auto client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client == INVALID_SOCKET)
        {
            cleanupListener();
            return false;
        }
        if (::connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        {
            closesocket(client);
            cleanupListener();
            return false;
        }

        auto server = ::accept(listener, nullptr, nullptr);
        cleanupListener();
        if (server == INVALID_SOCKET)
        {
            closesocket(client);
            return false;
        }

        out = { server, client };
        return true;
    }

#endif // _WIN32

} // namespace

std::expected<std::unique_ptr<SystemPipe>, PlatformError> createSystemPipe()
{
    ensureWinsockInitialized(); // no-op on POSIX; required before any socket call on Windows
#ifndef _WIN32
    auto fds = std::array<int, 2> {};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) != 0)
        return std::unexpected(PlatformError::PipeCreationFailed);
    // Non-blocking on both ends: producers must never stall on a full wakeup pipe,
    // and a loop's bounded drain must never park on a spurious readiness. The
    // write side reports EAGAIN as success (a wakeup is already pending then).
    if (!makeNonBlockingCloexec(fds[0]) || !makeNonBlockingCloexec(fds[1]))
    {
        ::close(fds[0]);
        ::close(fds[1]);
        return std::unexpected(PlatformError::PipeCreationFailed);
    }
    return std::make_unique<PosixSystemPipe>(fds[0], fds[1]);
#else
    auto pair = std::array<SOCKET, 2> {};
    if (!makeLoopbackPair(pair))
        return std::unexpected(PlatformError::PipeCreationFailed);

    auto const event = WSACreateEvent();
    if (event == WSA_INVALID_EVENT)
    {
        closesocket(pair[0]);
        closesocket(pair[1]);
        return std::unexpected(PlatformError::PipeCreationFailed);
    }
    // Watch the read socket for incoming data and peer close. WSAEventSelect also
    // puts the socket into non-blocking mode, which is what we want for the
    // loop-driven drain in read().
    if (WSAEventSelect(pair[0], event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
    {
        WSACloseEvent(event);
        closesocket(pair[0]);
        closesocket(pair[1]);
        return std::unexpected(PlatformError::PipeCreationFailed);
    }
    return std::make_unique<WindowsSystemPipe>(pair[0], pair[1], event);
#endif
}

} // namespace core::platform
