// SPDX-License-Identifier: Apache-2.0
// The Windows TerminalChannel: the console the process is attached to, opened as CONIN$/CONOUT$
// whatever the standard handles are, with VT input and output on for as long as the channel is open.
#include <core/tui/TerminalChannel.hpp>

#include <core/tui/VtParser.hpp>
#include <core/tui/detail/Utf16ToUtf8.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

namespace core::tui
{

namespace
{
    /// @brief Whether a console mode was changed, and so must be given back.
    enum class ModeChange : std::uint8_t
    {
        Unchanged,
        Changed,
    };

    /// @brief The console's input and output, and the modes to give back when they are closed.
    class WindowsTerminalChannel final: public TerminalChannel
    {
      public:
        /// @param input CONIN$; owned from here on.
        /// @param output CONOUT$; owned from here on.
        WindowsTerminalChannel(HANDLE input, HANDLE output): _input(input), _output(output)
        {
            if (GetConsoleMode(_input, &_savedInput) != 0
                && SetConsoleMode(_input, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_PROCESSED_INPUT) != 0)
                _inputChanged = ModeChange::Changed;
            if (GetConsoleMode(_output, &_savedOutput) != 0
                && SetConsoleMode(_output,
                                  _savedOutput | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)
                       != 0)
                _outputChanged = ModeChange::Changed;
        }

        ~WindowsTerminalChannel() override
        {
            if (_inputChanged == ModeChange::Changed)
                SetConsoleMode(_input, _savedInput);
            if (_outputChanged == ModeChange::Changed)
                SetConsoleMode(_output, _savedOutput);
            CloseHandle(_input);
            CloseHandle(_output);
        }

        WindowsTerminalChannel(WindowsTerminalChannel const&) = delete;
        auto operator=(WindowsTerminalChannel const&) -> WindowsTerminalChannel& = delete;
        WindowsTerminalChannel(WindowsTerminalChannel&&) = delete;
        auto operator=(WindowsTerminalChannel&&) -> WindowsTerminalChannel& = delete;

        [[nodiscard]] auto access() const noexcept -> ChannelAccess override
        {
            return ChannelAccess::ReadWrite;
        }

        [[nodiscard]] auto write(std::string_view bytes) -> std::expected<void, ClipboardWriteError> override
        {
            while (!bytes.empty())
            {
                auto written = DWORD { 0 };
                if (WriteFile(_output, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) == 0
                    || written == 0)
                    return std::unexpected { ClipboardWriteError::IoError };
                bytes.remove_prefix(written);
            }
            return {};
        }

        [[nodiscard]] auto poll(int timeoutMs)
            -> std::expected<std::vector<InputEvent>, ClipboardWriteError> override
        {
            if (WaitForSingleObject(_input, static_cast<DWORD>(timeoutMs)) != WAIT_OBJECT_0)
                return std::vector<InputEvent> {};

            auto pending = DWORD { 0 };
            if (GetNumberOfConsoleInputEvents(_input, &pending) == 0)
                return std::unexpected { ClipboardWriteError::IoError };
            if (pending == 0)
                return std::vector<InputEvent> {};

            auto records = std::vector<INPUT_RECORD>(pending);
            auto read = DWORD { 0 };
            if (ReadConsoleInputW(_input, records.data(), pending, &read) == 0)
                return std::unexpected { ClipboardWriteError::IoError };

            // With ENABLE_VIRTUAL_TERMINAL_INPUT a terminal's replies arrive as key events, one
            // UTF-16 unit each, exactly as TerminalInput reads them.
            auto bytes = std::string {};
            for (auto const& record: std::span(records).first(read))
            {
                if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown
                    && record.Event.KeyEvent.uChar.UnicodeChar != 0)
                    _utf16.append(bytes, static_cast<char16_t>(record.Event.KeyEvent.uChar.UnicodeChar));
            }
            return _parser.feed(bytes);
        }

      private:
        HANDLE _input;
        HANDLE _output;
        DWORD _savedInput = 0;
        DWORD _savedOutput = 0;
        ModeChange _inputChanged = ModeChange::Unchanged;
        ModeChange _outputChanged = ModeChange::Unchanged;
        detail::Utf16ToUtf8 _utf16;
        VtParser _parser { VtParser::Options { .osc = VtParser::OscRecognition::Response } };
    };

    /// @brief Opens one of the console's pseudo-files, or INVALID_HANDLE_VALUE.
    auto openConsole(wchar_t const* name) noexcept -> HANDLE
    {
        return CreateFileW(name,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr,
                           OPEN_EXISTING,
                           0,
                           nullptr);
    }
} // namespace

auto openControllingTerminal() -> TerminalChannelResult
{
    auto const input = openConsole(L"CONIN$");
    if (input == INVALID_HANDLE_VALUE)
        return std::unexpected { ClipboardWriteError::NoTerminal };
    auto const output = openConsole(L"CONOUT$");
    if (output == INVALID_HANDLE_VALUE)
    {
        CloseHandle(input);
        return std::unexpected { ClipboardWriteError::NoTerminal };
    }
    return std::make_unique<WindowsTerminalChannel>(input, output);
}

} // namespace core::tui
