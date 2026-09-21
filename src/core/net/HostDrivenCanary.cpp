// SPDX-License-Identifier: Apache-2.0
///
/// @file
/// The canary that proves a host-driven loop REFUSES to be run or blocked on.
///
/// `EventLoop::run()` and `EventLoop::blockOn()` assert that the backend is not host-driven, and
/// an assertion cannot be asserted from inside a Catch case: it aborts the process, which ends
/// the binary rather than the case. So each mode is its own process, registered with `WILL_FAIL`,
/// and a run that exits 0 is the regression — the loop accepted a drive it cannot honour.
///
/// It exists because the alternative failure is silent and remote. A consumer that calls `run()`
/// on a `PlatformLoop` in a WebAssembly build gets a turn that waits on a backend with nothing to
/// wait on, forever, with the page frozen and no diagnostic anywhere. A "precondition violation"
/// that nothing ever violates in CI is a comment.
///
/// Skips (exit 77) where assertions are compiled out: with `NDEBUG` the refusal is not there to
/// observe, and a `WILL_FAIL` run that exits 0 for that reason would report a defect that is not
/// one. `SKIP_RETURN_CODE` takes precedence over `WILL_FAIL`, so ctest reads it as a skip.

#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/HostDrivenBackend.hpp>
#include <core/net/testing/ManualHostScheduler.hpp>
#include <core/platform/Clock.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <tuple>

// Everything below is what a build WITH assertions needs, and nothing else compiles it: with
// `NDEBUG` there is no refusal to provoke, `main` skips, and a helper left visible there is an
// unused function -- which is an error in this tree, as it should be.
#ifndef NDEBUG

namespace
{

/// What this process exits with once the refusal has fired.
///
/// **An `abort()` is not a failed exit code, and ctest tells them apart.** A signal is an
/// "exception" there, and `WILL_FAIL` inverts a return code and not an exception -- so an
/// assertion left to abort on its own reports as a failure however it is registered. Converting
/// it here keeps the assertion real (it still had to fire to get here) and keeps a genuine crash
/// distinguishable: SIGSEGV is not handled, so it still arrives as the exception it is.
constexpr int RefusedExitCode = 1;

/// Turns the refusal's abort into an exit code. `_exit`-shaped on purpose: an abort handler runs
/// with the process already committed to dying, so it must not unwind, flush or allocate.
/// @param signalNumber Ignored; only SIGABRT is handled.
extern "C" void onAbort(int signalNumber)
{
    std::ignore = signalNumber;
    std::_Exit(RefusedExitCode);
}

/// A flow that never completes on its own, so `blockOn` has to reach its refusal rather than
/// finishing first.
/// @param loop The loop to park on.
/// @return A task that waits an hour.
core::async::Task<void> parkForever(core::net::EventLoop* loop)
{
    co_await loop->delay(std::chrono::hours { 1 });
}

/// A flow that does nothing, for the thread-affinity mode: what is spawned does not matter, only
/// which thread spawns it.
/// @return A task that completes at once.
core::async::Task<void> doNothing()
{
    co_return;
}

} // namespace

#endif

/// @param argc The argument count.
/// @param argv `run`, `blockOn` or `spawnOffThread`, naming which refusal to provoke.
/// @return Never, in a build with assertions: the refusal aborts.
int main(int argc, char** argv)
{
#ifdef NDEBUG
    /// The exit code ctest is told to read as "this configuration could not run the case".
    constexpr auto SkipExitCode = 77;
    std::ignore = argc;
    std::ignore = argv;
    std::fputs("hostdriven-canary: SKIPPED -- assertions are compiled out in this configuration\n", stderr);
    return SkipExitCode;
#else
    if (argc != 2)
    {
        std::fputs("usage: core-cpp-hostdriven-canary <run|blockOn>\n", stderr);
        return 2;
    }

    std::signal(SIGABRT, &onAbort);

    auto clock = core::platform::ManualClock {};
    auto host = core::net::testing::ManualHostScheduler {};
    auto backend = core::net::HostDrivenBackend { host, clock };
    auto loop = core::net::EventLoop { backend, clock };

    if (std::strcmp(argv[1], "run") == 0)
    {
        loop.run();
        std::fputs("hostdriven-canary: run() returned on a host-driven loop\n", stderr);
        return 0;
    }
    if (std::strcmp(argv[1], "blockOn") == 0)
    {
        loop.blockOn(parkForever(&loop));
        std::fputs("hostdriven-canary: blockOn() returned on a host-driven loop\n", stderr);
        return 0;
    }

    if (std::strcmp(argv[1], "spawnOffThread") == 0)
    {
        // The thread-affinity family (G1/G5), proved to FIRE rather than merely to exist. Six
        // members assert `teardownIsSerialisedWithDispatch()` and nothing drove any of them into
        // its assertion, so a predicate inverted by a later edit would have gone unnoticed.
        //
        // A NATIVE backend, not the host-driven one above: this mode is about which thread calls,
        // not about who owns the wait, and `run()` on a host-driven loop refuses for a different
        // reason entirely -- which would make this mode pass for that reason instead.
        auto const native = core::net::makeDefaultBackend();
        auto nativeLoop = core::net::EventLoop { *native };

        auto entered = std::atomic<bool> { false };
        nativeLoop.post([&entered] { entered.store(true, std::memory_order_release); });
        auto worker = std::thread { [&nativeLoop] { nativeLoop.run(); } };

        // Spawning before the loop is genuinely running is the LEGITIMATE call, so waiting for a
        // turn to have happened is what makes this the violation rather than a race.
        while (!entered.load(std::memory_order_acquire))
            std::this_thread::yield();

        nativeLoop.spawn(doNothing());

        // Unreachable where assertions are on. Reaching it means the predicate answered true from
        // a second thread while another was driving, which is the defect.
        nativeLoop.stop();
        worker.join();
        std::fputs("hostdriven-canary: spawn() from a second thread was accepted\n", stderr);
        return 0;
    }

    std::fputs("hostdriven-canary: unknown mode\n", stderr);
    return 2;
#endif
}
