// SPDX-License-Identifier: Apache-2.0
//
// `DeadlineTimer` — a deadline that can be disarmed, over `EventLoop::addTimer`.
//
// Ported from fastcached's `Async/DeadlineTimer_test.cpp` at `0708dd54`, **without its
// poll-interval cases**: upstream's timer could not take a deadline back off the reactor, so it
// slept in steps of 50ms and re-read a flag. Here `cancelTimer` retires the park by id, so the
// only wake-up an armed timer causes is the one at its deadline. `anArmedTimerBoundsTheWait`
// below is what says so, and it is the case upstream could not have written.

#include <core/net/DeadlineTimer.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/testing/ScriptedBackend.hpp>
#include <core/net/testing/TestLoop.hpp>
#include <core/platform/Clock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <tuple>

using core::net::DeadlineTimer;
using core::net::EventLoop;
using core::net::testing::ScriptedBackend;
using core::net::testing::TestLoop;
using core::platform::ManualClock;
using namespace std::chrono_literals;

namespace
{

/// A @c DeadlineTimer::Callback that counts its calls.
/// @param state A @c std::size_t counter, which must outlive the timer.
void countCall(void* state)
{
    ++*static_cast<std::size_t*>(state);
}

/// What a callback that destroys its own timer needs: the owning handle, and a record that it ran.
struct SelfDestroying
{
    std::unique_ptr<DeadlineTimer> timer; ///< The timer whose callback destroys it.
    std::size_t calls = 0;                ///< How many times that callback ran.

    /// What @c DeadlineTimer::settled answered while the callback was running.
    ///
    /// Asserted, and it is what makes the case below fail on a VALUE rather than only under a
    /// sanitizer: a timer that marked itself settled after its callback returned would destroy an
    /// object from inside the call and then write to it, which reads as green everywhere but ASan.
    bool settledWhenCalled = false;
};

/// A @c DeadlineTimer::Callback that destroys the timer it was called from.
/// @param state A @c SelfDestroying, which must outlive the loop.
void destroySelf(void* state)
{
    auto* const owner = static_cast<SelfDestroying*>(state);
    ++owner->calls;
    owner->settledWhenCalled = owner->timer->settled();
    owner->timer.reset(); // ~DeadlineTimer disarms, from inside the callback it is disarming
}

} // namespace

TEST_CASE("A DeadlineTimer fires its callback when the deadline arrives", "[DeadlineTimer]")
{
    auto clock = ManualClock {};
    auto loop = TestLoop { clock };

    auto calls = std::size_t { 0 };
    auto timer = DeadlineTimer { loop, clock.now() + 50ms, &countCall, &calls };
    REQUIRE_FALSE(timer.settled());

    std::ignore = loop.drain();
    REQUIRE(calls == 0);

    clock.advance(50ms);
    std::ignore = loop.drain();
    CHECK(calls == 1);
    CHECK(timer.settled());
}

TEST_CASE("A deadline already in the past fires on a turn, not from the constructor", "[DeadlineTimer]")
{
    // Upstream states this promise and keeps it by hopping onto the reactor from a detached
    // coroutine; here the arming simply files a park, and the turn is the only thing that fires
    // one. Either way a caller is never re-entered from its own constructor.
    auto clock = ManualClock {};
    auto loop = TestLoop { clock };

    auto calls = std::size_t { 0 };
    auto timer = DeadlineTimer { loop, clock.now() - 1s, &countCall, &calls };
    REQUIRE(calls == 0);
    REQUIRE_FALSE(timer.settled());

    std::ignore = loop.drain();
    CHECK(calls == 1);
}

TEST_CASE("A DeadlineTimer destroyed before its deadline never fires and leaves nothing parked",
          "[DeadlineTimer]")
{
    // Declared BEFORE the loop so it outlives it: the case's whole claim is about what did NOT
    // happen, and a counter destroyed first could not report it.
    auto calls = std::size_t { 0 };
    auto clock = ManualClock {};
    auto loop = TestLoop { clock };

    {
        auto const timer = DeadlineTimer { loop, clock.now() + 50ms, &countCall, &calls };
        REQUIRE(loop.pendingTimerCount() == 1);
    }

    // The park is retired by the destructor, not left to expire: upstream left one parked frame
    // per settled operation, which is the leak that made an ASan build of fastcache-cc exit
    // non-zero.
    CHECK(loop.pendingTimerCount() == 0);

    clock.advance(1s);
    std::ignore = loop.drain();
    CHECK(calls == 0);
}

TEST_CASE("disarm() is idempotent and safe after the callback has already run", "[DeadlineTimer]")
{
    auto clock = ManualClock {};
    auto loop = TestLoop { clock };

    auto calls = std::size_t { 0 };
    auto timer = DeadlineTimer { loop, clock.now() + 10ms, &countCall, &calls };

    timer.disarm();
    timer.disarm();
    CHECK(timer.settled());

    clock.advance(1s);
    std::ignore = loop.drain();
    CHECK(calls == 0);

    timer.disarm(); // after the deadline it would have fired at
    CHECK(timer.settled());
}

TEST_CASE("A settled timer stays settled once its callback has run", "[DeadlineTimer]")
{
    auto clock = ManualClock {};
    auto loop = TestLoop { clock };

    auto calls = std::size_t { 0 };
    auto timer = DeadlineTimer { loop, clock.now(), &countCall, &calls };
    std::ignore = loop.drain();
    REQUIRE(calls == 1);
    REQUIRE(timer.settled());

    timer.disarm();
    CHECK(timer.settled());
    CHECK(calls == 1);
}

TEST_CASE("A DeadlineTimer may be destroyed from inside its own callback", "[DeadlineTimer]")
{
    // The timer is marked settled and its id dropped BEFORE the callback runs, so the ~DeadlineTimer
    // that the callback triggers finds nothing left to disarm. Without that, the destructor would
    // ask the loop to cancel a timer that is running -- and the loop has already taken the park out,
    // so it would report false and the timer would be destroyed while the turn still named it.
    auto clock = ManualClock {};
    auto loop = TestLoop { clock };

    auto owner = SelfDestroying {};
    owner.timer = std::make_unique<DeadlineTimer>(loop, clock.now() + 10ms, &destroySelf, &owner);

    clock.advance(10ms);
    std::ignore = loop.drain();

    CHECK(owner.calls == 1);
    CHECK(owner.settledWhenCalled); // marked BEFORE the call, so the destructor finds nothing to do
    CHECK(owner.timer == nullptr);
    CHECK(loop.pendingTimerCount() == 0);
}

TEST_CASE("An armed DeadlineTimer bounds the turn's wait to its own deadline", "[DeadlineTimer]")
{
    // The case that names what this task removed. Upstream's timer woke every
    // DefaultPollInterval (50ms) whatever its deadline was, because a `Schedule` could not be
    // taken back and a disarmed timer had to be noticed rather than retired. Here the loop's own
    // deadline heap is what the wait is computed from, so a timer 500ms out costs exactly one
    // wake-up.
    auto clock = ManualClock {};
    auto backend = ScriptedBackend {};
    backend.pushTimeout();
    auto loop = EventLoop { backend, clock };

    auto calls = std::size_t { 0 };
    auto const timer = DeadlineTimer { loop, clock.now() + 500ms, &countCall, &calls };
    std::ignore = loop.runOnce();

    REQUIRE(backend.waitCount() == 1);
    CHECK(backend.recordedTimeouts().front() == std::optional { core::platform::SteadyDuration { 500ms } });
    CHECK_FALSE(timer.settled());
}
