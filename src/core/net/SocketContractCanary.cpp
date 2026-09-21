// SPDX-License-Identifier: Apache-2.0
///
/// @file
/// The canaries that prove `core::net::contract`'s three guards actually fire, against a REAL
/// socket.
///
/// **What is driven here is the CALL SITE, not the guard.** Asserting the assertion would prove
/// `assert` works and say nothing about whether a transport ever reaches it — and every one of
/// these rules was, before it had a guard, a thing the code was simply expected to do. A guard with
/// no arriving caller is a comment with an `assert` in it.
///
/// Each mode is its own process, because an assertion cannot be asserted from inside a Catch case:
/// it aborts, which ends the binary rather than the case.
///
/// **Registered on MARKERS, not on `WILL_FAIL`, and that is the load-bearing part.** `WILL_FAIL`
/// inverts ANY non-zero exit, so a canary that dies before it reaches the guarded call — a bad
/// argument, a backend this platform does not build, a socket pair that could not be made — reads
/// exactly like one whose assertion fired. Worse, an uncaught throw reaches `abort()`, this
/// program's own SIGABRT handler and `_Exit(1)` with stderr EMPTY, because `core::testing_dialogs`
/// suppresses the abort text: exit 1 and silent, byte-identical to the answer we want. No
/// alternation of failure strings can catch that one, because there is no string to match.
///
/// So each mode prints `reached the guarded call` IMMEDIATELY BEFORE the forbidden operation, and
/// `SURVIVED` immediately after it. The PASS criterion is the first marker, which proves the
/// process got as far as the call; the FAIL criterion is the second, plus every early-exit text.
/// `PASS_REGULAR_EXPRESSION` ignores the exit code and `WILL_FAIL` inverts the pass criteria, so
/// the two are alternatives and never companions — `ctest` checks the fail expression first, which
/// is what lets the pass marker sit on both paths.
///
/// Skips (exit 77) where assertions are compiled out: with `NDEBUG` there is no refusal to observe.
/// `SKIP_RETURN_CODE` is evaluated ahead of the expressions, so a Release run abstains rather than
/// failing for want of a marker it was never going to reach. **These canaries are therefore
/// exercised on Debug legs only** — `clang-debug`, `clang-asan-ubsan`, `clang-tsan`, `cl-debug`.

#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/testing/InMemoryTransport.hpp>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>

// Everything below is what a build WITH assertions needs, and nothing else compiles it: with
// `NDEBUG` there is no refusal to provoke, `main` skips, and a helper left visible there is an
// unused function -- which is an error in this tree, as it should be.
#ifndef NDEBUG

    #include <array>
    #include <cstddef>
    #include <memory>
    #include <span>
    #include <vector>

namespace
{

/// What this process exits with once a guard has fired.
///
/// **An `abort()` is not a failed exit code, and ctest tells them apart.** A signal is an
/// "exception" there, and a `FAIL_REGULAR_EXPRESSION` match is not consulted for one. Converting it
/// here keeps the assertion real -- it still had to fire to get here -- and keeps a genuine crash
/// distinguishable: SIGSEGV is not handled, so it still arrives as the exception it is.
constexpr int RefusedExitCode = 1;

/// Big enough that no platform's send buffer takes it, so the write parks rather than completing.
constexpr std::size_t UnsendablePayload = std::size_t { 8 } * 1024 * 1024;

/// Turns a guard's abort into an exit code.
///
/// `_exit`-shaped on purpose: an abort handler runs with the process already committed to dying, so
/// it must not unwind, flush or allocate, and only async-signal-safe calls are legal in it. That is
/// why the markers below flush when they are PRINTED rather than here -- `fflush` is not one of
/// them, and a buffer left for this handler to flush would die with the process.
/// @param signalNumber Ignored; only SIGABRT is handled.
extern "C" void onAbort(int signalNumber)
{
    std::ignore = signalNumber;
    std::_Exit(RefusedExitCode);
}

/// Prints the PASS marker: this process reached the guarded call, so whatever happens next is the
/// guard's answer and not an early exit.
///
/// Flushed here, because the very next statement is expected to `abort()` and an unflushed buffer
/// dies with the process -- which would leave ctest reading the same empty stderr that an uncaught
/// throw produces, and those are the two outcomes this registration exists to tell apart.
///
/// Emitted as literal pieces rather than one format string, as every other canary in this module
/// does: `modernize-use-std-print` rewrites a `fprintf` into `std::println`, and `<print>` is past
/// the libc++ 17 floor this tree builds against.
///
/// **It names the MODE, and that is what the registration matches on.** A marker shared by all
/// three modes would be printed by whichever guard the process actually reached, so a mode that
/// fell into the wrong branch -- or whose condition was inverted -- would still print it and still
/// pass. The mode-specific marker is what ties the verdict to the guard the test asked for.
/// @param mode Which mode is running, as ctest named it on the command line.
/// @param what Which guard is about to be provoked.
void announce(char const* mode, char const* what)
{
    std::fputs("socket-contract-canary: ", stderr);
    std::fputs(mode, stderr);
    std::fputs(": reached the guarded call (", stderr);
    std::fputs(what, stderr);
    std::fputs(")\n", stderr);
    std::fflush(stderr);
}

/// Prints the FAIL marker: the guarded call returned, so the guard did not fire.
/// @param what What the socket wrongly accepted.
void survived(char const* what)
{
    std::fputs("socket-contract-canary: SURVIVED -- ", stderr);
    std::fputs(what, stderr);
    std::fputs("\n", stderr);
    std::fflush(stderr);
}

/// Parks a readability watch and never resumes -- the stale parked wait a second read arms over.
/// @param sock The socket to watch.
/// @return A flow that suspends on the watch.
core::async::Task<void> watchForever(core::net::ISocket* sock)
{
    std::ignore = co_await sock->waitReadable();
}

/// Parks a write nothing will drain -- the stale parked write a second write arms over.
/// @param sock The socket to write to.
/// @param payload The bytes; must outlive the flow.
/// @return A flow that suspends on the write.
core::async::Task<void> writeForever(core::net::ISocket* sock, std::vector<std::byte> const* payload)
{
    std::ignore = co_await sock->write(std::span<std::byte const> { *payload });
}

} // namespace

#endif

/// @param argc The argument count.
/// @param argv `read-slot`, `write-slot` or `empty-read-buffer`, naming which guard to provoke.
/// @return Never, in a build with assertions: the guard aborts.
int main(int argc, char** argv)
{
#ifdef NDEBUG
    /// The exit code ctest is told to read as "this configuration could not run the case".
    constexpr auto SkipExitCode = 77;
    std::ignore = argc;
    std::ignore = argv;
    std::fputs("socket-contract-canary: SKIPPED -- assertions are compiled out in this configuration\n",
               stderr);
    return SkipExitCode;
#else
    if (argc != 2)
    {
        std::fputs("usage: core-cpp-socket-contract-canary <read-slot|write-slot|empty-read-buffer>\n",
                   stderr);
        return 2;
    }

    std::signal(SIGABRT, &onAbort);

    auto backend = core::net::makeDefaultBackend();
    if (!backend)
    {
        std::fputs("socket-contract-canary: no default backend on this platform\n", stderr);
        return 2;
    }
    auto loop = core::net::EventLoop { *backend };
    auto pair = core::net::testing::makeSocketPair(loop);
    if (!pair.has_value())
    {
        std::fputs("socket-contract-canary: could not make a socket pair\n", stderr);
        return 2;
    }
    auto* const sock = pair->first.get();

    if (std::strcmp(argv[1], "empty-read-buffer") == 0)
    {
        // No loop turn needed: the guard is at the top of the verb, before anything can park.
        announce("empty-read-buffer", "empty read buffer");
        auto const refused = sock->read(std::span<std::byte> {});
        std::ignore = refused.await_ready();
        survived("read() accepted an empty buffer");
        return 0;
    }

    if (std::strcmp(argv[1], "read-slot") == 0)
    {
        // The watch has to be genuinely PARKED, not merely created: the slot is claimed when the
        // operation arms, which is inside `await_suspend`. A canary that only called the verb twice
        // would trip nothing and pass while the rule was gone.
        loop.spawn(watchForever(sock));
        std::ignore = loop.runOnce();
        if (loop.parkedWaiterCount() == 0)
        {
            std::fputs("socket-contract-canary: the watch did not park, so nothing was tested\n", stderr);
            return 2;
        }
        auto buffer = std::array<std::byte, 16> {};
        announce("read-slot", "a read armed over a parked readability watch");
        auto const second = sock->read(buffer);
        std::ignore = second.await_ready();
        survived("read() armed over a parked readability watch");
        return 0;
    }

    if (std::strcmp(argv[1], "write-slot") == 0)
    {
        auto const payload = std::vector<std::byte>(UnsendablePayload, std::byte { 0xA5 });
        loop.spawn(writeForever(sock, &payload));
        std::ignore = loop.runOnce();
        if (loop.parkedWaiterCount() == 0)
        {
            std::fputs("socket-contract-canary: the write did not park, so nothing was tested\n", stderr);
            return 2;
        }
        announce("write-slot", "a write armed over a parked write");
        auto const second = sock->write(std::span<std::byte const> { payload });
        std::ignore = second.await_ready();
        survived("write() armed over a parked write");
        return 0;
    }

    std::fputs("socket-contract-canary: unknown mode\n", stderr);
    return 2;
#endif
}
