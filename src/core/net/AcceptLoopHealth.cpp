// SPDX-License-Identifier: Apache-2.0
#include <core/net/AcceptLoopHealth.hpp>

#include <utility>

namespace core::net
{

void AcceptLoopHealth::subscribe(Listener listener)
{
    auto const guard = std::scoped_lock { _mutex };
    _listener = std::move(listener);
}

void AcceptLoopHealth::forward(AcceptLoopHealth& target)
{
    auto already = std::vector<StoppedSurface> {};
    {
        auto const guard = std::scoped_lock { _mutex };
        _forward = &target;
        already = _stopped;
    }
    // A loop that stopped before the process was assembled far enough to forward is still told.
    for (auto const& stopped: already)
        target.stopped(stopped.surface, stopped.reason);
}

void AcceptLoopHealth::stopped(std::string_view surface, std::string_view reason)
{
    auto stopped = StoppedSurface { .surface = std::string { surface }, .reason = std::string { reason } };
    auto listener = Listener {};
    AcceptLoopHealth* forward = nullptr;
    {
        auto const guard = std::scoped_lock { _mutex };
        _stopped.push_back(stopped);
        listener = _listener;
        forward = _forward;
    }
    // Outside the lock: a listener that takes another registry's lock, or reads this one back,
    // must not deadlock on it.
    if (listener)
        listener(stopped);
    if (forward != nullptr)
        forward->stopped(stopped.surface, stopped.reason);
}

std::vector<StoppedSurface> AcceptLoopHealth::snapshot() const
{
    auto const guard = std::scoped_lock { _mutex };
    return _stopped;
}

} // namespace core::net
