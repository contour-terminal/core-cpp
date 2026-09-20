// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// The production @c EventSource backing the TUI runtime: it multiplexes
/// terminal input, resize, the agent-message wakeup, the interrupt wakeup, and
/// (on POSIX) the signal fd into one blocking wait.

#include <core/platform/SignalHandler.hpp>
#include <core/platform/Types.hpp>
#include <core/platform/Wakeup.hpp>
#include <core/tui/Terminal.hpp>
#include <core/tui/runtime/EventSource.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace core::tui::runtime
{

/// Multiplexes every real input/wakeup source the runtime cares about.
///
/// Waiting includes the agent and interrupt wakeups in the OS wait set, so an
/// agent message or Ctrl+C wakes the pump immediately rather than at the next
/// timeout — closing the Windows gap where @c TerminalInput::poll() never
/// waited on the agent wakeup. SIGINT detection is unified through
/// @c SignalHandler so the same interrupt path works on every platform.
///
/// Protocol-response events (color-scheme / focus / cursor-position / cell-size)
/// are consumed here exactly as @c Terminal::poll() does, so they never surface
/// as application input and the terminal's focus/color-scheme handlers keep
/// firing.
class TerminalEventSource: public EventSource
{
  public:
    /// @param terminal The terminal (provides input handles, decode, and report handlers; not owned).
    /// @param agentWakeup Wakeup the agent worker signals on a new message, or nullptr.
    /// @param interruptWakeup Wakeup signalled by @c SignalHandler on SIGINT, or nullptr.
    /// @param signalFd POSIX signal fd to also wait on, or @c InvalidHandle.
    TerminalEventSource(Terminal& terminal,
                        core::platform::Wakeup* agentWakeup = nullptr,
                        core::platform::Wakeup* interruptWakeup = nullptr,
                        core::platform::NativeHandle signalFd = core::platform::InvalidHandle) noexcept:
        _terminal(terminal), _agentWakeup(agentWakeup), _interruptWakeup(interruptWakeup), _signalFd(signalFd)
    {
    }

    /// Delivers events a terminal query handed back (@c TerminalInput::unread) without waiting,
    /// since they were read before anything still on the input handle; otherwise waits for a
    /// source to become ready.
    /// @param timeoutMs -1 = block, 0 = non-blocking, >0 = timeout in ms.
    /// @return The outcome of this wait.
    [[nodiscard]] WaitOutcome wait(int timeoutMs) override
    {
        if (auto pending = _terminal.input().takePending(); !pending.empty())
            return finalize(WaitOutcome { .events = std::move(pending) });
        return waitForReadiness(timeoutMs);
    }

    [[nodiscard]] FdToken attach(core::platform::NativeHandle fd, FdInterest interest) override
    {
        return _registry.attach(fd, interest);
    }

    void detach(FdToken token) override { _registry.detach(token); }

  private:
    /// The platform wait: blocks until a source is ready or @p timeoutMs elapses.
    /// @param timeoutMs -1 = block, 0 = non-blocking, >0 = timeout in ms.
    /// @return The outcome of this wait.
    [[nodiscard]] WaitOutcome waitForReadiness(int timeoutMs);

    /// Consumes protocol-response events (via @c Terminal::consumeProtocolReports
    /// so the policy lives in one place) and folds a pending SIGINT into the
    /// outcome, clearing the flag so the runtime observes each interrupt once. A
    /// dispatched focus change is reported as @c activity so an idle waiter wakes
    /// and can redraw (e.g. the prompt's focus-dim chrome).
    /// @param outcome The outcome assembled from the wait so far.
    /// @return @p outcome with reports stripped, @c interrupted set if a SIGINT
    ///         is pending, and @c activity set if focus changed.
    [[nodiscard]] WaitOutcome finalize(WaitOutcome outcome)
    {
        if (_terminal.consumeProtocolReports(outcome.events))
            outcome.activity = true;
        if (core::platform::SignalHandler::hasPendingSigint())
        {
            core::platform::SignalHandler::clearPendingSigint();
            outcome.interrupted = true;
        }
        return outcome;
    }

    Terminal& _terminal;                      ///< Provides input handles, decode, report handlers.
    core::platform::Wakeup* _agentWakeup;     ///< Agent-message wakeup, or nullptr.
    core::platform::Wakeup* _interruptWakeup; ///< Interrupt wakeup, or nullptr.
    /// POSIX signal fd, or InvalidHandle. Windows has none: there the constructor's argument is
    /// carried and nothing waits on it, which is what `[[maybe_unused]]` says -- as
    /// `core::net::PollEventSource::_waitRotation` does for the mirror-image case.
    [[maybe_unused]] core::platform::NativeHandle _signalFd;
    FdRegistry _registry; ///< User-attached fds (waitReadable/waitWritable).
};

} // namespace core::tui::runtime
