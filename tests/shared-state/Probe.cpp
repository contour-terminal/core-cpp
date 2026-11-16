// SPDX-License-Identifier: Apache-2.0

#include "Probe.hpp"

#include <core/Assert.hpp>
#include <core/async/Strand.hpp>
#include <core/net/detail/ReadyBatch.hpp>

namespace core::shared_state_probe
{

void enterScope(async::IExecutor& executor, void (*inside)(void*), void* context)
{
    auto const scope = async::ExecutorScope { executor };
    inside(context);
}

async::ExecutorScope const* innermostScope() noexcept
{
    return async::ExecutorScope::innermost();
}

log::Category const* errorCategory() noexcept
{
    return &log::errorLog;
}

log::Category const* categoryNamed(char const* name) noexcept
{
    return log::get(name);
}

IClock const* steadyClock() noexcept
{
    return &defaultSteadyClock();
}

platform::IWallClock const* wallClock() noexcept
{
    return &platform::defaultSystemWallClock();
}

bool failHandlerInstalled() noexcept
{
    return static_cast<bool>(detail::failHandler());
}

bool readinessDispatchInFlight() noexcept
{
    return net::detail::readinessDispatchInFlight();
}

bool freeingAbandonedFor(void const* owner) noexcept
{
    return async::detail::FreeingAbandoned::active(owner);
}

} // namespace core::shared_state_probe
