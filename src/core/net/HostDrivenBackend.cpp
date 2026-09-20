// SPDX-License-Identifier: Apache-2.0
#include <core/net/HostDrivenBackend.hpp>

#include <algorithm>
#include <chrono>
#include <utility>

namespace core::net
{

HostDrivenBackend::HostDrivenBackend(IHostScheduler& host, platform::IClock& clock) noexcept:
    _host(host), _clock(clock)
{
}

std::expected<void, NetError> HostDrivenBackend::attach(ReadinessHandler& /*handler*/)
{
    return std::unexpected { makeNetError(
        NetErrorCode::Unsupported, 0, "HostDrivenBackend: this backend has no readiness") };
}

std::expected<void, NetError> HostDrivenBackend::setInterest(ReadinessHandler& /*handler*/,
                                                             Interest /*interest*/)
{
    return std::unexpected { makeNetError(
        NetErrorCode::Unsupported, 0, "HostDrivenBackend: this backend has no readiness") };
}

void HostDrivenBackend::detach(ReadinessHandler& /*handler*/) noexcept
{
}

WaitResult HostDrivenBackend::wait(std::optional<platform::SteadyDuration> /*timeout*/)
{
    return WaitResult {};
}

void HostDrivenBackend::setPump(HostCallback pump, void* state) noexcept
{
    _pump = pump;
    _pumpState = state;
}

void HostDrivenBackend::scheduleAt(platform::SteadyTimePoint when) noexcept
{
    // Already covered: a pump is out with the host, and it is due no later than this
    // one wants. The loop re-arms after every turn, so whatever this request was for
    // is asked again then.
    if (_scheduledAt.has_value() && *_scheduledAt <= when)
        return;

    auto const now = _clock.now();
    // Clamped at zero: a deadline already past asks for the next turn of the host's
    // loop, not for a negative delay — which `setTimeout` reads as zero on one host
    // and refuses on another.
    auto const delay = std::max(std::chrono::duration_cast<std::chrono::milliseconds>(when - now),
                                std::chrono::milliseconds { 0 });
    _scheduledAt = when;
    _host.callAfter(delay, &HostDrivenBackend::onHostPump, this);
}

void HostDrivenBackend::wake() noexcept
{
    scheduleAt(_clock.now());
}

void HostDrivenBackend::armWakeAt(std::optional<platform::SteadyTimePoint> deadline) noexcept
{
    // Nothing is scheduled on the loop's side, so only a wake should bring it back.
    // Asking the host for a pump here would spin the page at the host's timer
    // resolution for a loop that has nothing to do.
    if (!deadline.has_value())
        return;
    scheduleAt(*deadline);
}

void HostDrivenBackend::onHostPump(void* state) noexcept
{
    auto* const self = static_cast<HostDrivenBackend*>(state);
    // Cleared BEFORE the pump runs, not after: the turn it drives will arm the next
    // deadline and may wake for work it queues, and neither may be dropped as "one is
    // already scheduled" when the one scheduled is the pump that is running.
    self->_scheduledAt.reset();
    ++self->_pumpCount;
    if (self->_pump != nullptr)
        self->_pump(self->_pumpState);
}

} // namespace core::net
