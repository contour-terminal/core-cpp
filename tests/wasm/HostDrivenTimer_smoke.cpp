// SPDX-License-Identifier: Apache-2.0
//
// The browser story, end to end, in the one environment that can actually tell you whether it
// works: a real `PlatformLoop` on a real host, pumped by a real JavaScript event loop.
//
// Every other test of the host-driven path uses `testing::ManualHostScheduler`, which is the right
// double — it makes the exact delay the backend asked for assertable — and which by construction
// cannot catch the things that only a browser does: that `emscripten_async_call` fires at all,
// that ASYNCIFY unwinds and rewinds a C++ stack holding a coroutine frame, and that the host gets
// the turn back. So this is not a unit test and does not pretend to be one. It runs under node, in
// the `emscripten` job, on both emsdk versions, and what it proves is that the loop advances when
// nothing but the host is driving it.
//
// **The wait is bounded and says what it waited for.** A timer that never fires would otherwise
// park this program for ctest's own timeout with nothing on stdout.

#include <core/async/Task.hpp>
#include <core/net/DeadlineTimer.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/PlatformLoop.hpp>

#include <chrono>
#include <cstdio>
#include <tuple>

#include <emscripten.h>

using namespace std::chrono_literals;

namespace
{

/// How long each arm waits for. Short, because the bound below is what makes a failure a failure
/// rather than a hang, and a long deadline would only make a green run slow.
constexpr auto Deadline = 20ms;

/// How long `main` gives the host before it calls the run failed.
constexpr auto Bound = 2000ms;

/// One step of the host's own loop, which is what `emscripten_sleep` yields for.
constexpr auto Step = 10ms;

/// The coroutine half: parks on the loop's deadline and sets a flag when it resumes.
/// @param loop The loop to park on.
/// @param fired Set once the delay elapses.
core::async::Task<void> setFlagAfterDelay(core::net::EventLoop* loop, bool* fired)
{
    co_await loop->delay(Deadline);
    *fired = true;
}

/// The callback half: what @c core::net::DeadlineTimer runs, with no coroutine frame behind it.
/// @param state A `bool*` set when the deadline arrives.
void setFlag(void* state)
{
    *static_cast<bool*>(state) = true;
}

} // namespace

int main()
{
    // A PlatformLoop here owns a HostDrivenBackend over the browser's timer: it has no thread and
    // no descriptor, so neither run() nor blockOn() may be called on it (both assert). The only
    // thing that advances it is the host calling back, which is what emscripten_sleep below yields
    // for.
    auto loop = core::net::PlatformLoop {};

    auto delayFired = false;
    auto timerFired = false;
    loop.spawn(setFlagAfterDelay(&loop, &delayFired));

    // Task B5's own half: a deadline with no frame behind it, on the same heap as the delay above,
    // and it has to reach the host through the same armWakeAt.
    auto const timer =
        core::net::DeadlineTimer { loop, loop.clock().now() + Deadline, &setFlag, &timerFired };
    std::ignore = timer;

    auto waited = 0ms;
    while (!(delayFired && timerFired) && waited < Bound)
    {
        emscripten_sleep(static_cast<unsigned>(Step.count()));
        waited += Step;
    }

    auto const status = delayFired && timerFired ? 0 : 1;
    if (status != 0)
        std::printf("FAIL host-driven-timer: after %lld ms the coroutine delay %s and the "
                    "DeadlineTimer %s -- the host never pumped the loop to its deadline\n",
                    static_cast<long long>(waited.count()),
                    delayFired ? "fired" : "did NOT fire",
                    timerFired ? "fired" : "did NOT fire");
    else
        std::printf("ok host-driven-timer: a coroutine delay and a DeadlineTimer both fired within "
                    "%lld ms, driven only by the host\n",
                    static_cast<long long>(waited.count()));

    // **This program's verdict is the line above, not the status below**, and that is why ctest
    // reads it with PASS_REGULAR_EXPRESSION -- see the reason, and the measurement, in
    // `CMakeLists.txt` beside that property. The status is still returned, so running this by hand
    // behaves the way a program should.
    return status;
}
