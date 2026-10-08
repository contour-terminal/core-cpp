// SPDX-License-Identifier: Apache-2.0
// The POSIX TerminalChannel: the terminal device opened beside standard I/O, with echo and line
// buffering off for as long as the channel is open.
#include <core/tui/TerminalChannel.hpp>

#include <core/tui/VtParser.hpp>
#include <core/tui/posix/PosixIO.hpp>
#include <core/tui/posix/TerminalChannelPosix.hpp>

#include <array>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace core::tui
{

namespace
{
    /// @brief An open terminal device, and the mode to give back when it is closed.
    class PosixTerminalChannel final: public TerminalChannel
    {
      public:
        /// @param fd The open terminal device; owned from here on.
        /// @param access Whether to read it. Only a reader changes the terminal's mode: a process
        ///               outside the foreground group that tried would be stopped by SIGTTOU.
        PosixTerminalChannel(int fd, ChannelAccess access): _fd(fd), _access(access)
        {
            if (_access == ChannelAccess::WriteOnly || ::tcgetattr(_fd, &_saved) != 0)
                return;
            auto raw = _saved;
            raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO); // ISIG stays: Ctrl+C still interrupts
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            _modeChanged = ::tcsetattr(_fd, TCSANOW, &raw) == 0 ? ModeChange::Changed : ModeChange::Unchanged;
        }

        ~PosixTerminalChannel() override
        {
            // TCSADRAIN: what was written reaches the terminal before echo comes back on.
            if (_modeChanged == ModeChange::Changed)
                std::ignore = ::tcsetattr(_fd, TCSADRAIN, &_saved);
            ::close(_fd);
        }

        PosixTerminalChannel(PosixTerminalChannel const&) = delete;
        auto operator=(PosixTerminalChannel const&) -> PosixTerminalChannel& = delete;
        PosixTerminalChannel(PosixTerminalChannel&&) = delete;
        auto operator=(PosixTerminalChannel&&) -> PosixTerminalChannel& = delete;

        [[nodiscard]] auto access() const noexcept -> ChannelAccess override { return _access; }

        [[nodiscard]] auto write(std::string_view bytes) -> std::expected<void, ClipboardWriteError> override
        {
            if (safeWrite(_fd, bytes.data(), bytes.size()) < 0)
                return std::unexpected { ClipboardWriteError::IoError };
            return {};
        }

        [[nodiscard]] auto poll(int timeoutMs)
            -> std::expected<std::vector<InputEvent>, ClipboardWriteError> override
        {
            auto watch = pollfd { .fd = _fd, .events = POLLIN, .revents = 0 };
            auto const ready = ::poll(&watch, 1, timeoutMs);
            if (ready < 0 && errno != EINTR)
                return std::unexpected { ClipboardWriteError::IoError };
            if (ready <= 0)
                return std::vector<InputEvent> {}; // timeout, or a signal: the caller's deadline decides

            auto buffer = std::array<char, 4096> {};
            auto const count = ::read(_fd, buffer.data(), buffer.size());
            if (count < 0 && errno != EINTR && errno != EAGAIN)
                return std::unexpected { ClipboardWriteError::IoError };
            if (count <= 0)
                return std::vector<InputEvent> {};
            return _parser.feed(std::string_view { buffer.data(), static_cast<std::size_t>(count) });
        }

      private:
        enum class ModeChange : std::uint8_t
        {
            Unchanged,
            Changed,
        };

        int _fd;
        ChannelAccess _access;
        termios _saved {};
        ModeChange _modeChanged = ModeChange::Unchanged;
        VtParser _parser { VtParser::Options { .osc = VtParser::OscRecognition::Response } };
    };

    /// @brief Read-write when this process is the terminal's foreground process group.
    auto foregroundAccess(int fd) noexcept -> ChannelAccess
    {
        return ::tcgetpgrp(fd) == ::getpgrp() ? ChannelAccess::ReadWrite : ChannelAccess::WriteOnly;
    }
} // namespace

auto openTerminalChannelAt(char const* path, std::optional<ChannelAccess> access) -> TerminalChannelResult
{
    auto const fd = ::open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd < 0)
        return std::unexpected { ClipboardWriteError::NoTerminal };
    return std::make_unique<PosixTerminalChannel>(fd, access ? *access : foregroundAccess(fd));
}

auto openControllingTerminal() -> TerminalChannelResult
{
    return openTerminalChannelAt("/dev/tty", std::nullopt);
}

} // namespace core::tui
