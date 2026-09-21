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

/// Yields to the host until @p flag is set, or until @c Bound elapses.
///
/// Yielding is the whole of what this program does that a unit test cannot: `emscripten_sleep`
/// unwinds the C++ stack (which is what `-sASYNCIFY` is for), lets the host run its own callbacks
/// — which is where the loop's turns happen — and rewinds.
/// @param flag What to wait for.
/// @return How long it waited, so a failure can say so.
[[nodiscard]] std::chrono::milliseconds waitForFlag(bool const* flag)
{
    auto waited = 0ms;
    while (!*flag && waited < Bound)
    {
        emscripten_sleep(static_cast<unsigned>(Step.count()));
        waited += Step;
    }
    return waited;
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

    // ---- Phase 1: the frameless timer, ON ITS OWN. ----------------------------------------
    //
    // **The order here IS the test, and nothing may be armed before this.** A `spawn` wakes the
    // backend, and the turn that wake buys ends in `armHostWake()`, which picks up every deadline
    // in the heap -- including one that was filed without asking the host for anything. So a
    // program that spawns first cannot tell whether `addTimer` reaches a host-driven backend or
    // merely rode on the spawn's wake. This program did exactly that, and was green for that
    // reason, until review C1.
    //
    // Reordering alone would not have fixed it: whichever is armed first, the spawn's wake still
    // buys a turn that arms the host for both. Only a phase with NO spawn in it asks the question.
    //
    // **If you are tempted to move a `spawn` above this line for convenience, you are deleting a
    // test, not tidying one.** The assertion will not change and the coverage will vanish.
    [[maybe_unused]] auto const timer =
        core::net::DeadlineTimer { loop, loop.clock().now() + Deadline, &setFlag, &timerFired };

    auto const timerWaited = waitForFlag(&timerFired);

    // **Read at the end of phase 1, and the verdict below uses THIS, not `timerFired`.** Phase 2's
    // spawn wakes the loop, and the turn that wake buys arms the host for every deadline still in
    // the heap -- including this timer's, if phase 1 timed out waiting for it. So `timerFired` is
    // true by the end of the program either way, and a verdict that read it there would pass while
    // the timer fired two seconds late, for the wrong reason, because of the spawn. Measured: with
    // the fix reverted this program printed `ok ... both fired within 2030 ms` until the flag was
    // snapshotted here.
    auto const timerFiredAlone = timerFired;

    // ---- Phase 2: the coroutine deadline, once the loop is quiescent again. ----------------
    // The dispatch's own scenario. Its `spawn` may wake whatever it likes now: phase 1 has
    // already been answered, and phase 2 has a bound of its own so a phase-1 failure cannot
    // starve it and make the message blame both mechanisms for one defect.
    loop.spawn(setFlagAfterDelay(&loop, &delayFired));
    auto const delayWaited = waitForFlag(&delayFired);

    auto const status = delayFired && timerFiredAlone ? 0 : 1;
    if (status != 0)
        std::printf("FAIL host-driven-timer: the DeadlineTimer %s after %lld ms (armed alone, with "
                    "nothing else on the loop) and the coroutine delay %s after %lld ms -- the host "
                    "was never asked for the turn that would run it\n",
                    timerFiredAlone ? "fired" : "did NOT fire",
                    static_cast<long long>(timerWaited.count()),
                    delayFired ? "fired" : "did NOT fire",
                    static_cast<long long>(delayWaited.count()));
    else
        std::printf("ok host-driven-timer: a coroutine delay and a DeadlineTimer both fired within "
                    "%lld ms, driven only by the host\n",
                    static_cast<long long>((timerWaited + delayWaited).count()));

    // **This program's verdict is the line above, not the status below**, and that is why ctest
    // reads it with PASS_REGULAR_EXPRESSION -- see the reason, and the measurement, in
    // `CMakeLists.txt` beside that property. The status is still returned, so running this by hand
    // behaves the way a program should.
    return status;
}
