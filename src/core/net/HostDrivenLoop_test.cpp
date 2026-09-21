// SPDX-License-Identifier: Apache-2.0
//
// A loop that does not own its thread.
//
// Every other backend blocks, and the loop's turn is what waits. A host-driven one cannot: under
// single-threaded WebAssembly there is no thread to block, and inside somebody else's event loop
// — Qt's, GLib's, a game engine's frame callback — the thread is not ours to stop. So the loop is
// PUMPED: it runs one turn, asks the host for the next pump, and returns.
//
// **It is portable, and the browser is only one of its hosts.** Every case here runs on every
// platform over `testing::ManualHostScheduler`, which is what makes the behaviour something this
// repository can hold still rather than something only a node run can observe. What a node run
// adds is that the same code compiles and links under Emscripten, which this file's membership of
// the WebAssembly test binary is what proves.
#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/HostDrivenBackend.hpp>
#include <core/net/testing/ManualHostScheduler.hpp>
#include <core/platform/Clock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <tuple>

using core::async::Task;
using core::net::EventLoop;
using core::net::HostDrivenBackend;
using core::net::testing::ManualHostScheduler;
using core::platform::ManualClock;
using namespace std::chrono_literals;

namespace
{

/// Parks on a delay and records that it came back.
/// @param loop The loop to park on.
/// @param duration How long to wait.
/// @param fired Set once it resumes.
Task<void> delayThenFlag(EventLoop* loop, core::platform::SteadyDuration duration, bool* fired)
{
    co_await loop->delay(duration);
    *fired = true;
}

/// @param host The host to ask.
/// @return The delay of the host's soonest pending request, in milliseconds.
[[nodiscard]] long long soonestDelayMs(ManualHostScheduler const& host)
{
    auto soonest = std::chrono::milliseconds::max();
    for (auto const& request: host.pending())
        soonest = request.delay < soonest ? request.delay : soonest;
    return soonest == std::chrono::milliseconds::max() ? -1 : soonest.count();
}

} // namespace

TEST_CASE("A host-driven loop runs a posted callback only once the host pumps", "[EventLoop][hostdriven]")
{
    // The whole of what "does not own its thread" means, in one assertion. `post` hands work over
    // and asks the host for a turn; nothing runs until the host gives one. A loop that ran the
    // callback inline would be running application code on whichever thread called `post`, which
    // is the one thing a single-threaded host cannot survive.
    auto clock = ManualClock {};
    auto host = ManualHostScheduler {};
    auto backend = HostDrivenBackend { host, clock };
    auto loop = EventLoop { backend, clock };

    auto ran = 0;
    loop.post([&ran] { ++ran; });

    CHECK(ran == 0);
    REQUIRE(host.pendingCount() >= 1); // the post asked for a pump
    CHECK(backend.pumpScheduled());

    host.pump();
    CHECK(ran == 1);
    CHECK(backend.pumpCount() == 1);
}

TEST_CASE("A host-driven loop arms the host at its next deadline, and resumes when that pump comes",
          "[EventLoop][hostdriven]")
{
    // After every turn the loop tells the backend when its next deadline is, and the backend asks
    // the host to pump THEN. The deadline itself is asserted, not merely that something was asked:
    // a loop that armed the host for "as soon as possible" would spin the browser's timer at full
    // rate while its flow waited fifty milliseconds, and a loop that armed it too late would be a
    // timer that fires late with nothing to say so.
    auto clock = ManualClock {};
    auto host = ManualHostScheduler {};
    auto backend = HostDrivenBackend { host, clock };
    auto loop = EventLoop { backend, clock };

    auto fired = false;
    loop.spawn(delayThenFlag(&loop, 50ms, &fired));

    // The spawn queued work, so the first pump is asked for at once.
    host.pump();
    CHECK_FALSE(fired);
    REQUIRE(loop.pendingTimerCount() == 1);

    // And that turn armed the host at the deadline it parked on: 50ms out, exactly.
    REQUIRE(host.pendingCount() == 1);
    CHECK(soonestDelayMs(host) == 50);

    // The host's timer is what waits; a ManualClock is what makes the waiting exact.
    clock.advance(50ms);
    host.pump();
    CHECK(loop.pendingTimerCount() == 0); // step 5 of that turn found the deadline due
    CHECK_FALSE(fired);                   // and queued it: resumption is the NEXT turn's step 2

    host.pump();
    CHECK(fired);

    // Nothing is left to wait for, so nothing further is asked of the host: a loop that kept
    // arming would keep the page's timer alive with no work behind it.
    host.pump();
    CHECK(host.pendingCount() == 0);
}

TEST_CASE("A host-driven loop asks for one pump however many wakes it takes", "[EventLoop][hostdriven]")
{
    // Coalescing, seen from the loop rather than the backend. A burst of posts from a worker — or
    // from inside the turn — must cost the page ONE scheduled callback, not one per post, or a
    // busy moment spends the frame budget in the scheduler.
    auto clock = ManualClock {};
    auto host = ManualHostScheduler {};
    auto backend = HostDrivenBackend { host, clock };
    auto loop = EventLoop { backend, clock };

    auto ran = 0;
    for ([[maybe_unused]] auto const index: { 0, 1, 2, 3, 4 })
        loop.post([&ran] { ++ran; });

    CHECK(host.pendingCount() == 1);

    host.pump();
    CHECK(ran == 5); // all five, in one turn
}

TEST_CASE("A destroyed host-driven loop leaves nothing for the host to call", "[EventLoop][hostdriven]")
{
    // The last step of the teardown. A host-driven backend holds a pointer to the loop it pumps,
    // and a host holds whatever the backend asked for; a pump that arrives after the loop is gone
    // would call into freed storage on the host's own thread, at a point where nothing in the
    // stack names this loop. The backend outlives the loop here on purpose, because that is the
    // arrangement that makes the mistake possible at all.
    auto clock = ManualClock {};
    auto host = ManualHostScheduler {};
    auto backend = HostDrivenBackend { host, clock };

    auto ran = 0;
    {
        auto loop = EventLoop { backend, clock };
        loop.post([&ran] { ++ran; });
        host.pump();
        REQUIRE(ran == 1);
    }

    // Whatever is still pending with the host may fire; it must find nothing to pump.
    auto const pumpsBefore = backend.pumpCount();
    host.pump();
    CHECK(ran == 1);
    CHECK(backend.pumpCount() >= pumpsBefore); // the backend still counts, it just has nobody to call
    CHECK_FALSE(backend.pumpScheduled());
}

TEST_CASE("run and blockOn are declared and compiled on every platform, host-driven or not",
          "[EventLoop][hostdriven]")
{
    // They are a PRECONDITION VIOLATION on a host-driven loop rather than something the build
    // removes, and that is deliberate: the browser reaches them through a consumer's mistake, not
    // through ours, so they must exist there to be asserted in. What runs them and watches the
    // assertion fire is `core-cpp.hostdriven-canary`, which cannot be a Catch case because the
    // failure is an abort.
    //
    // What this one holds is the other half: that both are instantiable in a build where the
    // default backend IS host-driven, which is the WebAssembly one. A `requires` expression
    // rather than a call, because calling them here would abort this binary.
    STATIC_REQUIRE(requires(EventLoop& loop) { loop.run(); });
    STATIC_REQUIRE(requires(EventLoop& loop) { loop.blockOn(Task<void> {}); });
}
