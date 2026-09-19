// SPDX-License-Identifier: Apache-2.0
#include <core/tui/TerminalInput.hpp>

#include <core/tui/TerminalProtocols.hpp>

/// @file
/// The @c TerminalInput members that touch no operating-system state, so that the two platform
/// files hold only what genuinely differs: the wait, the raw-mode switch and the protocol write.
/// endo had a copy of each of these in `platform/TerminalInput.cpp` and
/// `platform/TerminalInputWin32.cpp` (f774a210).

namespace core::tui
{

auto TerminalInput::parserTimeout() -> std::vector<InputEvent>
{
    return _parser.timeout();
}

void TerminalInput::setWakeup(core::platform::Wakeup* wakeup)
{
    _wakeup = wakeup;
}

void TerminalInput::suspend()
{
    if (_suspended || !_rawMode)
        return;

    disableProtocols();
    disableRawMode();
    _suspended = true;
}

void TerminalInput::resume()
{
    if (!_suspended)
        return;

    enableRawMode();
    enableProtocols();
    _suspended = false;
}

auto TerminalInput::isSuspended() const noexcept -> bool
{
    return _suspended;
}

void TerminalInput::setAnyMotionTracking(bool enabled)
{
    _anyMotionTracking = enabled;
    if (_rawMode)
        writeProtocol(enabled ? protocols::EnableAnyMotionTracking : protocols::DisableAnyMotionTracking);
}

} // namespace core::tui
