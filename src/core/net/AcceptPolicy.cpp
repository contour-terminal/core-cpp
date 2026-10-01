// SPDX-License-Identifier: Apache-2.0
#include <core/net/AcceptPolicy.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <utility>

namespace core::net
{

AcceptVerdict AcceptErrorPolicy::onError(NetErrorCode code, platform::SteadyTimePoint now) noexcept
{
    switch (acceptDispositionOf(code))
    {
        case AcceptDisposition::Closed:
            return AcceptVerdict { .delay = {}, .warning = std::nullopt, .action = AcceptAction::Stop };
        case AcceptDisposition::Dead:
            return AcceptVerdict { .delay = {}, .warning = std::nullopt, .action = AcceptAction::GiveUp };
        case AcceptDisposition::PollTick:
            return AcceptVerdict { .delay = {},
                                   .warning = std::nullopt,
                                   .action = AcceptAction::AcceptAgain };
        case AcceptDisposition::Exhausted:
            return AcceptVerdict { .delay = nextBackoff(),
                                   .warning = warningDue(now),
                                   .action = AcceptAction::AcceptAgain };
        case AcceptDisposition::Unclassified:
            if (++_unclassifiedInARow >= UnclassifiedBeforeGiveUp)
                return AcceptVerdict { .delay = {}, .warning = std::nullopt, .action = AcceptAction::GiveUp };
            return AcceptVerdict { .delay = nextBackoff(),
                                   .warning = warningDue(now),
                                   .action = AcceptAction::AcceptAgain };
        case AcceptDisposition::PeerFailed: {
            // The listener just dequeued a connection, so it is alive whatever came before.
            _unclassifiedInARow = 0;
            auto delay = std::chrono::milliseconds {};
            if (++_failuresInARow >= FailuresBeforeYield)
            {
                // A YIELD, constant and never growing: each of these failures consumed one queued
                // connection, so the loop is making progress through a backlog of dead ones.
                _failuresInARow = 0;
                delay = FirstBackoff;
            }
            return AcceptVerdict { .delay = delay,
                                   .warning = warningDue(now),
                                   .action = AcceptAction::AcceptAgain };
        }
    }
    // Not reached: every disposition returns above, and a disposition no row names cannot be made.
    return AcceptVerdict { .delay = nextBackoff(),
                           .warning = warningDue(now),
                           .action = AcceptAction::AcceptAgain };
}

void AcceptErrorPolicy::onAccepted() noexcept
{
    _backoff = {};
    _failuresInARow = 0;
    _unclassifiedInARow = 0;
}

std::optional<AcceptWarning> AcceptErrorPolicy::warningDue(platform::SteadyTimePoint now) noexcept
{
    // Rate-limited rather than said once: a loop that keeps meeting failures is a fact an operator
    // must be able to see NOW, and a line per failure is a line per client of a loaded machine,
    // which buries everything else in the log.
    if (_warnedAt.has_value() && now - *_warnedAt < WarnInterval)
    {
        ++_unreported;
        return std::nullopt;
    }
    _warnedAt = now;
    return AcceptWarning { .unreported = std::exchange(_unreported, 0) };
}

std::chrono::milliseconds AcceptErrorPolicy::nextBackoff() noexcept
{
    _backoff = _backoff == std::chrono::milliseconds {} ? FirstBackoff : std::min(_backoff * 2, MaxBackoff);
    return _backoff;
}

std::string describeAcceptFailure(std::string_view surface,
                                  NetError const& error,
                                  AcceptVerdict const& verdict)
{
    auto const unreported = verdict.warning.has_value() ? verdict.warning->unreported : 0;
    return std::format("{}: an accept failed ({}); {}{}",
                       surface,
                       error.toString(),
                       verdict.delay == std::chrono::milliseconds {}
                           ? std::string { "accepting again" }
                           : std::format("accepting again in {} ms", verdict.delay.count()),
                       unreported == 0 ? std::string {}
                                       : std::format(" ({} more since the last warning)", unreported));
}

std::string describeAcceptLoopEnded(std::string_view surface, NetError const& error)
{
    return std::format(
        "{}: accept loop ended ({}) on a listener that was not closed; it accepts no more connections",
        surface,
        error.toString());
}

} // namespace core::net
