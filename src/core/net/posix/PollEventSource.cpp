// SPDX-License-Identifier: Apache-2.0
#include <core/net/PollEventSource.hpp>

#include <cstddef>
#include <ranges>

#include <poll.h>

namespace core::net
{

namespace
{
    /// Translates a readiness interest mask into poll(2) event bits.
    /// @param interest The interest to translate.
    /// @return The corresponding POLLIN/POLLOUT bitmask.
    [[nodiscard]] short toPollEvents(FdInterest interest) noexcept
    {
        short events = 0;
        if (hasInterest(interest, FdInterest::Read))
            events |= POLLIN;
        if (hasInterest(interest, FdInterest::Write))
            events |= POLLOUT;
        return events;
    }

    /// Routes a registered fd's poll(2) revents into a wait outcome's ready-token
    /// lists. Read-readiness includes HUP/ERR/NVAL so a parked reader is resumed to
    /// observe EOF rather than the pump spinning.
    /// @param token The registration's token.
    /// @param revents The revents poll(2) reported for the fd.
    /// @param outcome The outcome to append the token to (readyRead / readyWrite).
    void routePollRevents(FdToken token, short revents, WaitOutcome& outcome)
    {
        if ((revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0)
            outcome.readyRead.push_back(token);
        if ((revents & POLLOUT) != 0)
            outcome.readyWrite.push_back(token);
    }
} // namespace

WaitOutcome PollEventSource::wait(int timeoutMs)
{
    auto const& registrations = _registry.registrations();
    static thread_local std::vector<pollfd> fds;
    fds.clear();
    for (auto const& reg: registrations)
        // A muted registration (FdInterest::None) is submitted with a NEGATIVE descriptor,
        // which poll(2) ignores and reports 0 revents for. Submitting the real descriptor with
        // events == 0 does not mute it: the kernel reports POLLHUP/POLLERR/POLLNVAL whatever
        // was asked for, so routePollRevents would wake a flow the caller asked to be silent.
        // Windows and kqueue already report nothing for such a registration; this is what makes
        // "mute the fd without detaching it" mean the same thing on every backend. The entry is
        // KEPT rather than skipped, because the routing below pairs fds[i] with
        // registrations[i].
        fds.push_back({ .fd = (reg.interest == FdInterest::None) ? -1 : reg.fd,
                        .events = toPollEvents(reg.interest),
                        .revents = 0 });

    auto outcome = WaitOutcome {};

    // Nothing to watch: honour the timeout (a parked timer supplies a finite one) so
    // the loop's timer can still fire; a negative timeout with no fds would block
    // forever, so report it as a benign timeout instead.
    if (fds.empty())
    {
        if (timeoutMs > 0)
            ::poll(nullptr, 0, timeoutMs);
        return outcome;
    }

    auto const result = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeoutMs);
    if (result <= 0)
        // 0: timed out. <0: EINTR or error — re-poll next pump (level-triggered fds
        // re-report readiness); a persistent error surfaces as the fd's HUP below on
        // the next successful poll. Either way, nothing ready this round.
        return outcome;

    for (auto const i: std::views::iota(std::size_t { 0 }, registrations.size()))
        routePollRevents(registrations[i].token, fds[i].revents, outcome);
    return outcome;
}

} // namespace core::net
