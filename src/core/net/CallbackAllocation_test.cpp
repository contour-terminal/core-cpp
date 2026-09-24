// SPDX-License-Identifier: Apache-2.0
//
// What a drain-step callback costs in heap allocations.
//
// A readiness completion is fastcached's hot path: every socket operation that parks is completed
// by one. Running a callback so that what it queues resumes in the callback's position must not
// make each of them allocate, so this binary counts: it replaces the global allocation functions,
// which is why it is a binary of its own -- a replacement reaches every test linked beside it.
#include <core/net/EventLoop.hpp>
#include <core/net/testing/TestLoop.hpp>
#include <core/platform/Clock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <tuple>

namespace
{

/// Every global `operator new` and `operator new[]` this process has served.
std::atomic<std::size_t> allocations { 0 };

/// Serves a counted allocation from `malloc`, so a sanitizer's interception of `malloc` still sees
/// it.
/// @param size The size asked for.
/// @return The storage.
/// @throws std::bad_alloc If `malloc` refuses.
void* countedAllocation(std::size_t size)
{
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* const storage = std::malloc(size == 0 ? 1 : size))
        return storage;
    throw std::bad_alloc {};
}

/// A timer callback that queues nothing.
/// @param state A @c std::size_t counter of its calls.
void countCall(void* state)
{
    ++*static_cast<std::size_t*>(state);
}

/// Arms a timer due now and runs the turn that queues it, so the NEXT turn's drain runs only the
/// callback.
/// @param loop The loop.
/// @param clock The loop's clock.
/// @param calls The callback's counter.
void queueOneCallback(core::net::EventLoop& loop, core::platform::ManualClock& clock, std::size_t* calls)
{
    std::ignore = loop.addTimer(clock.now(), &countCall, calls);
    std::ignore = loop.runOnce();
}

} // namespace

void* operator new(std::size_t size)
{
    return countedAllocation(size);
}

void* operator new[](std::size_t size)
{
    return countedAllocation(size);
}

void operator delete(void* storage) noexcept
{
    std::free(storage);
}

void operator delete[](void* storage) noexcept
{
    std::free(storage);
}

void operator delete(void* storage, std::size_t /*size*/) noexcept
{
    std::free(storage);
}

void operator delete[](void* storage, std::size_t /*size*/) noexcept
{
    std::free(storage);
}

TEST_CASE("A drain-step callback that queues nothing costs no allocation", "[EventLoop][turn][ordering]")
{
    // Measured against a turn that runs no callback at all, so whatever an idle turn allocates on
    // this platform cancels out. Warmed first, because a container's first growth is not the cost
    // of a callback.
    auto clock = core::platform::ManualClock {};
    auto loop = core::net::testing::TestLoop { clock };
    auto calls = std::size_t { 0 };
    for ([[maybe_unused]] auto const warm: { 0, 1, 2 })
    {
        queueOneCallback(loop, clock, &calls);
        std::ignore = loop.runOnce();
    }
    REQUIRE(calls == 3);

    auto const beforeIdle = allocations.load();
    std::ignore = loop.runOnce();
    auto const idleTurn = allocations.load() - beforeIdle;

    queueOneCallback(loop, clock, &calls);
    auto const beforeCallback = allocations.load();
    std::ignore = loop.runOnce(); // the drain runs the callback, and only that
    auto const callbackTurn = allocations.load() - beforeCallback;

    REQUIRE(calls == 4);
    CHECK(callbackTurn == idleTurn);
}
