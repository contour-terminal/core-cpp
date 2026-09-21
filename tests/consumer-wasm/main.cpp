// SPDX-License-Identifier: Apache-2.0
//
// What a WebAssembly consumer runs under node: a coroutine chain from core::async resumed by the
// program itself, a value from core::base, the error vocabulary of core::net_types, and -- since
// Task B5 -- a `core::net::PlatformLoop` advanced by nothing but the host's own timer, with a
// coroutine `delay` and a frameless `DeadlineTimer` parked on it.
//
// That last step is the one a consumer cannot write from the documentation alone, and the one no
// native preset can run: a WebAssembly loop has no thread to block and no descriptor to poll, so
// neither `run()` nor `blockOn()` may be called on it. What a program does instead is yield to the
// host -- here with `emscripten_sleep`, which needs `-sASYNCIFY` in the CONSUMER's link line --
// and let the loop be pumped between the yields.
//
// Every step reports through Checks, so a failure names the step rather than only an exit code.

#include <core/Base64.hpp>
#include <core/async/Task.hpp>
#include <core/net/DeadlineTimer.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/NetError.hpp>
#include <core/net/PlatformLoop.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>

#include <emscripten.h>

using core::async::Task;
using namespace std::chrono_literals;

namespace
{

/// What this program checked, so a failure names the step instead of only setting an exit code.
class Checks
{
  public:
    void expect(bool ok, std::string_view what)
    {
        std::cout << (ok ? "ok   " : "FAIL ") << what << '\n';
        if (!ok)
            ++_failed;
    }

    [[nodiscard]] int failed() const noexcept { return _failed; }

  private:
    int _failed = 0;
};

/// The inner half of the chain: a task that produces a value.
Task<int> increment(int value)
{
    co_return value + 1;
}

/// The outer half: a task that awaits another task and works on what it produced.
Task<int> doubledIncrement(int value)
{
    auto const incremented = co_await increment(value);
    co_return incremented * 2;
}

/// core::async: a root task the program resumes itself, which is what a driver does.
void checkCoroutines(Checks& checks)
{
    auto task = doubledIncrement(20);
    task.handle().resume();
    checks.expect(task.done(), "the coroutine chain ran to completion");
    checks.expect(task.done() && task.result() == 42, "the coroutine chain produced its value");
}

/// core::base: code that has nothing to do with an operating system, and so builds everywhere.
void checkBase(Checks& checks)
{
    auto const encoded = core::base64::encode("core-cpp");
    checks.expect(core::base64::decode(encoded) == "core-cpp", "core::base64 round-tripped a string");
}

/// How long the loop's two deadlines are set for.
constexpr auto Deadline = 20ms;

/// How long the program gives the host before it reports the step failed. Bounded, and it says
/// which arm did not fire: a deadline that never arrives would otherwise hang this program for
/// ctest's whole timeout with nothing on stdout.
constexpr auto Bound = 2000ms;

/// One step of the host's own loop, which is what `emscripten_sleep` yields for.
constexpr auto Step = 10ms;

/// Parks on the loop's deadline and records that it resumed.
/// @param loop The loop to park on.
/// @param fired Set once the delay elapses.
Task<void> setFlagAfterDelay(core::net::EventLoop* loop, bool* fired)
{
    co_await loop->delay(Deadline);
    *fired = true;
}

/// What a `DeadlineTimer` runs: a callback, with no coroutine frame behind it.
/// @param state A `bool*` set when the deadline arrives.
void setFlag(void* state)
{
    *static_cast<bool*>(state) = true;
}

/// Yields to the host until @p flag is set, or until @c Bound elapses.
///
/// Yielding is the whole of what a WebAssembly consumer does differently: `emscripten_sleep`
/// unwinds the C++ stack (which is what `-sASYNCIFY` is for), lets the host run its own callbacks
/// — which is where the loop's turns happen — and rewinds.
/// @param flag What to wait for.
void waitForFlag(bool const* flag)
{
    auto waited = 0ms;
    while (!*flag && waited < Bound)
    {
        emscripten_sleep(static_cast<unsigned>(Step.count()));
        waited += Step;
    }
}

/// core::net: a loop the host drives, which is the whole of how a browser consumer runs one.
/// @param checks Where each step reports.
void checkHostDrivenLoop(Checks& checks)
{
    auto loop = core::net::PlatformLoop {};

    auto delayFired = false;
    auto timerFired = false;

    // **The frameless timer FIRST and alone, and the order is the test.** A `spawn` wakes the
    // backend, and the turn that wake buys arms the host for every deadline in the heap — so a
    // check that spawned first could not tell whether arming a timer reaches a host-driven
    // backend on its own, or merely rode on the spawn. Reordering would not be enough either:
    // whichever is armed first, the spawn's wake still buys a turn that arms both. Only a phase
    // with no spawn in it asks the question. **Moving a `spawn` above this line deletes a check
    // without changing an assertion.**
    [[maybe_unused]] auto const timer =
        core::net::DeadlineTimer { loop, loop.clock().now() + Deadline, &setFlag, &timerFired };
    waitForFlag(&timerFired);
    checks.expect(timerFired, "a DeadlineTimer alone on a host-driven loop fired within its bound");

    // And then the coroutine half, whose spawn may wake whatever it likes now.
    loop.spawn(setFlagAfterDelay(&loop, &delayFired));
    waitForFlag(&delayFired);
    checks.expect(delayFired, "a coroutine delay on a host-driven loop resumed within its bound");
}

/// core::net_types: the error vocabulary, which is header-only and links nothing.
void checkNetTypes(Checks& checks)
{
    auto const error = core::net::makeNetError(core::net::NetErrorCode::ConnRefused, 0, "connect");
    checks.expect(error.toString().find("connection refused") != std::string::npos
                      && error.toString().find("connect") != std::string::npos,
                  "core::net_types named the error and its context");
}

} // namespace

int main()
{
    auto checks = Checks {};
    checkCoroutines(checks);
    checkBase(checks);
    checkNetTypes(checks);
    checkHostDrivenLoop(checks);

    auto const status = checks.failed() != 0 ? 1 : 0;
    if (status != 0)
        std::cout << checks.failed() << " check(s) failed\n";
    else
        std::cout << "consumer-wasm: every check passed\n";

    // **The line above is this program's verdict, and its exit status is not**, now that it owns
    // an event loop. A loop with an armed deadline leaves a pending `emscripten_async_call`
    // behind -- that call is how the host is asked for its next turn -- and Emscripten takes a
    // runtime keepalive per pending timer, so `main` returning is an IMPLICIT exit while the
    // runtime is kept alive: the status is recorded and never published, and node exits 0.
    // Measured on `tests/wasm/HostDrivenTimer_smoke.cpp`, whose failing run printed FAIL and
    // exited 0. That is why the `add_test` for this program reads its OUTPUT
    // (PASS_REGULAR_EXPRESSION) -- which is what a WebAssembly consumer has to do too, and the
    // reason this program has said `every check passed` on its last line since before it had a
    // loop.
    return status;
}
