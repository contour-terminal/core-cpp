// SPDX-License-Identifier: Apache-2.0
#include <core/tui/TerminalOutput.hpp>

#include <core/Base64.hpp>
#include <core/tui/SgrBuilder.hpp>
#include <core/tui/TerminalProtocols.hpp>

#include <format>

/// @file
/// Every @c TerminalOutput member that only composes bytes into the buffer. What
/// touches the operating system — the write itself, the terminal's size and the
/// capability probe — is in `posix/TerminalOutput.cpp` and
/// `windows/TerminalOutput.cpp`, which is all that differs between the two. In
/// endo those two files each carried a full copy of this composition (`platform/
/// TerminalOutput.cpp` and `platform/TerminalOutputWin32.cpp`, f774a210), so an
/// escape sequence had to be changed twice.

namespace core::tui
{

void TerminalOutput::writeText(std::string_view text, Style const& style)
{
    appendSgr(style);
    _buffer.append(text);
    appendSgrReset();
}

void TerminalOutput::writeRaw(std::string_view text)
{
    _buffer.append(text);
}

void TerminalOutput::moveTo(int row, int col)
{
    _buffer += std::format("\033[{};{}H", row, col);
}

void TerminalOutput::moveUp(int n)
{
    if (n > 0)
        _buffer += std::format("\033[{}A", n);
}

void TerminalOutput::moveDown(int n)
{
    if (n > 0)
        _buffer += std::format("\033[{}B", n);
}

void TerminalOutput::moveLeft(int n)
{
    if (n > 0)
        _buffer += std::format("\033[{}D", n);
}

void TerminalOutput::moveRight(int n)
{
    if (n > 0)
        _buffer += std::format("\033[{}C", n);
}

void TerminalOutput::carriageReturn()
{
    _buffer += '\r';
}

void TerminalOutput::linefeed()
{
    _buffer += '\n';
}

void TerminalOutput::clearToEndOfDisplay()
{
    _buffer += "\033[J";
}

void TerminalOutput::requestCellSize()
{
    _buffer += "\033[16t";
}

void TerminalOutput::requestCursorPosition()
{
    _buffer += "\033[6n";
}

void TerminalOutput::requestDecMode(int mode)
{
    _buffer += std::format("\033[?{}$p", mode);
}

void TerminalOutput::requestDeviceAttributes()
{
    _buffer += protocols::QueryPrimaryDeviceAttributes;
}

void TerminalOutput::clearLine()
{
    _buffer += "\033[2K";
}

void TerminalOutput::clearToEndOfLine()
{
    _buffer += "\033[K";
}

void TerminalOutput::clearToStartOfLine()
{
    _buffer += "\033[1K";
}

void TerminalOutput::clearScreen()
{
    _buffer += "\033[2J\033[H";
}

void TerminalOutput::clearScrollback()
{
    _buffer += "\033[3J";
}

void TerminalOutput::enterAltScreen()
{
    _buffer += "\033[?1049h";
}

void TerminalOutput::leaveAltScreen()
{
    _buffer += "\033[?1049l";
}

void TerminalOutput::setDoubleWidth()
{
    _buffer += "\033#6";
}

void TerminalOutput::setDoubleHeightTop()
{
    _buffer += "\033#3";
}

void TerminalOutput::setDoubleHeightBottom()
{
    _buffer += "\033#4";
}

void TerminalOutput::setSingleWidth()
{
    _buffer += "\033#5";
}

void TerminalOutput::disableReflow()
{
    _buffer += "\033[?2028l";
}

void TerminalOutput::enableReflow()
{
    _buffer += "\033[?2028h";
}

void TerminalOutput::showCursor()
{
    _buffer += "\033[?25h";
}

void TerminalOutput::hideCursor()
{
    _buffer += "\033[?25l";
}

void TerminalOutput::saveCursor()
{
    // Spelled in two pieces because "\0337" reads as an octal escape followed by a digit.
    _buffer += "\033"
               "7";
}

void TerminalOutput::restoreCursor()
{
    _buffer += "\033"
               "8";
}

void TerminalOutput::setCursorShape(CursorShape shape)
{
    _buffer += std::format("\033[{} q", static_cast<int>(shape));
}

void TerminalOutput::setScrollRegion(int top, int bottom)
{
    _buffer += std::format("\033[{};{}r", top, bottom);
}

void TerminalOutput::resetScrollRegion()
{
    _buffer += "\033[r";
}

void TerminalOutput::writeSixel(std::string_view sixelData)
{
    _buffer += "\033P0;1q";
    _buffer.append(sixelData);
    _buffer += "\033\\";
}

void TerminalOutput::copyToClipboard(std::string_view text)
{
    // OSC 52 format: 'ESC ] 52 ; c ; <base64-data> ESC \'
    // 'c' means system clipboard (could also use 'p' for primary selection)
    _buffer += "\033]52;c;";
    _buffer += base64::encode(text);
    _buffer += "\033\\";
}

void TerminalOutput::unscroll(int n)
{
    // Kitty unscroll extension: CSI Ps + T
    // This is an extension to SD (Scroll Down / Pan Up) that restores
    // lines from the scrollback buffer instead of inserting blank lines.
    // See: https://sw.kovidgoyal.net/kitty/unscroll/
    if (n > 0)
        _buffer += std::format("\033[{}+T", n);
}

bool TerminalOutput::supportsUnscroll() const noexcept
{
    return _unscrollSupported;
}

void TerminalOutput::beginHyperlink(std::string_view url, std::string_view id)
{
    protocols::appendHyperlinkOpen(_buffer, url, id);
}

void TerminalOutput::endHyperlink()
{
    _buffer += protocols::HyperlinkClose;
}

void TerminalOutput::writeHyperlink(std::string_view text, std::string_view url, Style const& style)
{
    beginHyperlink(url, {});
    writeText(text, style);
    endHyperlink();
}

void TerminalOutput::flush()
{
    if (!_buffer.empty())
    {
        writeToDestination(_buffer);
        _buffer.clear();
    }
}

auto TerminalOutput::columns() const noexcept -> int
{
    return _cols;
}

auto TerminalOutput::rows() const noexcept -> int
{
    return _rows;
}

void TerminalOutput::appendSgr(Style const& style)
{
    _buffer += buildSgrSequence(style);
}

void TerminalOutput::appendSgrReset()
{
    _buffer += "\033[m";
}

} // namespace core::tui
