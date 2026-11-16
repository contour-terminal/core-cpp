// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// The entry points of a shared library that is compiled apart from the test executable beside it:
/// each asks core-cpp for some piece of process or thread state and returns where it found it, so
/// that the executable can compare with where it found the same piece.

#include <core/Clock.hpp>
#include <core/async/ExecutorContext.hpp>
#include <core/async/IExecutor.hpp>
#include <core/log/LogStore.hpp>
#include <core/platform/Clock.hpp>

#include <cstddef>

#if defined(_WIN32)
    #if defined(CORE_CPP_SHARED_STATE_PROBE_BUILDING)
        #define CORE_CPP_SHARED_STATE_PROBE_API __declspec(dllexport)
    #else
        #define CORE_CPP_SHARED_STATE_PROBE_API __declspec(dllimport)
    #endif
#else
    #define CORE_CPP_SHARED_STATE_PROBE_API __attribute__((visibility("default")))
#endif

namespace core::shared_state_probe
{

/// Constructs an `ExecutorScope` for @p executor in the probe library, and calls @p inside while it
/// is in force.
/// @param executor The executor the scope names.
/// @param inside Called with @p context inside the scope.
/// @param context Passed to @p inside.
CORE_CPP_SHARED_STATE_PROBE_API void enterScope(async::IExecutor& executor,
                                                void (*inside)(void*),
                                                void* context);

/// @return What `ExecutorScope::innermost()` answers when it is asked in the probe library.
CORE_CPP_SHARED_STATE_PROBE_API async::ExecutorScope const* innermostScope() noexcept;

/// @return What `log::errorLog` is when it is asked in the probe library.
CORE_CPP_SHARED_STATE_PROBE_API log::Category const* errorCategory() noexcept;

/// @return What `log::get()` answers for a category named @p name, asked in the probe library.
/// @param name A category name.
CORE_CPP_SHARED_STATE_PROBE_API log::Category const* categoryNamed(char const* name) noexcept;

/// @return The steady clock `defaultSteadyClock()` answers in the probe library.
CORE_CPP_SHARED_STATE_PROBE_API IClock const* steadyClock() noexcept;

/// @return The wall clock `defaultSystemWallClock()` answers in the probe library.
CORE_CPP_SHARED_STATE_PROBE_API platform::IWallClock const* wallClock() noexcept;

/// @return Whether a fail handler is installed, asked in the probe library.
CORE_CPP_SHARED_STATE_PROBE_API bool failHandlerInstalled() noexcept;

/// @return Whether the probe library sees a readiness dispatch in flight on this thread.
CORE_CPP_SHARED_STATE_PROBE_API bool readinessDispatchInFlight() noexcept;

/// @return Whether the probe library sees work abandoned by @p owner being freed on this thread.
/// @param owner The strand, or family, the executable says it is freeing for.
CORE_CPP_SHARED_STATE_PROBE_API bool freeingAbandonedFor(void const* owner) noexcept;

} // namespace core::shared_state_probe
