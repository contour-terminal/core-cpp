// SPDX-License-Identifier: Apache-2.0
//
// What a socket completion costs on the platform's default backend, measured rather than argued.
//
// Not run by default -- `core-cpp-net-test "[bench]"` -- because a time is not an assertion: it is
// what `CompletionClaim_test.cpp`'s cases explain. Flows nobody owns, the shape of a server's
// connections, ping-pong one byte over connected pairs: every read parks, readiness completes it,
// and the waiter resumes in the drain step. Two completions per round trip. USER time is what the
// loop costs; the system time is the syscalls, the same before and after any change to it. POSIX
// only, for `getrusage`.
#include <core/async/DetachedTask.hpp>
#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/testing/InMemoryTransport.hpp>

#include <catch2/catch_test_macros.hpp>

#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <ranges>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <unistd.h>

using core::async::DetachedTask;
using core::async::Task;
using core::net::EventLoop;
using core::net::ISocket;

namespace
{

/// Which end of the exchange a flow is.
enum class Side : std::uint8_t
{
    Serves, ///< Reads, then answers.
    Asks,   ///< Writes, then reads the answer.
};

/// What the flows of one run report.
struct Progress
{
    std::size_t served = 0; ///< Round trips the serving ends finished.
    std::size_t asked = 0;  ///< Round trips the asking ends finished.
    std::size_t ended = 0;  ///< Flows that returned, whether done or not.
    std::size_t failed = 0; ///< Flows that returned early, on an error or an EOF.
};

/// One end of a one-byte ping-pong.
/// @param socket Its end of the pair.
/// @param rounds How many round trips.
/// @param side Which end.
/// @param progress Where it reports.
Task<void> pingPong(ISocket* socket, std::size_t rounds, Side side, Progress* progress)
{
    auto byte = std::array<std::byte, 1> {};
    auto finished = std::size_t { 0 };
    while (finished < rounds)
    {
        if (side == Side::Asks && !(co_await socket->write(byte)).has_value())
            break;
        if (auto const got = co_await socket->read(byte); !got.has_value() || *got == 0)
            break;
        if (side == Side::Serves && !(co_await socket->write(byte)).has_value())
            break;
        ++finished;
        ++(side == Side::Serves ? progress->served : progress->asked);
    }
    ++progress->ended;
    if (finished < rounds)
        ++progress->failed;
}

/// The root nobody owns, as a server's per-connection flow is.
/// @param flow The handler it awaits.
DetachedTask runDetached(Task<void> flow)
{
    co_await std::move(flow);
}

/// What one run measured, per completion.
struct Sample
{
    double wallNs = 0;
    double userNs = 0;
    double systemNs = 0;
};

/// @return The process's user and system CPU time so far.
std::pair<std::chrono::nanoseconds, std::chrono::nanoseconds> cpuTimes()
{
    auto usage = rusage {};
    std::ignore = ::getrusage(RUSAGE_SELF, &usage);
    auto const toNs = [](timeval const& time) {
        return std::chrono::seconds { time.tv_sec } + std::chrono::microseconds { time.tv_usec };
    };
    return { toNs(usage.ru_utime), toNs(usage.ru_stime) };
}

/// Runs @p pairs connected pairs at once, @p rounds round trips each, on one loop.
/// @return What it cost per completion.
Sample measure(std::size_t pairs, std::size_t rounds)
{
    auto backend = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(backend != nullptr);
    auto loop = EventLoop { *backend };
    auto sockets = std::vector<core::net::testing::SocketPair> {};
    for ([[maybe_unused]] auto const index: std::views::iota(std::size_t { 0 }, pairs))
    {
        auto pair = core::net::testing::makeSocketPair(loop);
        REQUIRE(pair.has_value());
        sockets.push_back(std::move(*pair));
    }
    auto progress = Progress {};

    auto const [userStarted, systemStarted] = cpuTimes();
    auto const started = std::chrono::steady_clock::now();
    for (auto const& pair: sockets)
    {
        runDetached(pingPong(pair.first.get(), rounds, Side::Serves, &progress));
        runDetached(pingPong(pair.second.get(), rounds, Side::Asks, &progress));
    }
    // Bounded, and by the flows rather than by the count they are aiming for: a flow that returns
    // early would otherwise leave the loop idle and this spinning.
    constexpr auto Bound = std::chrono::seconds { 60 };
    while (progress.ended < 2 * pairs && std::chrono::steady_clock::now() - started < Bound)
        std::ignore = loop.runOnce(std::chrono::milliseconds { 100 });
    auto const elapsed = std::chrono::steady_clock::now() - started;
    auto const [userEnded, systemEnded] = cpuTimes();
    INFO("pairs " << pairs << ", served " << progress.served << ", asked " << progress.asked
                  << ", flows ended " << progress.ended << " of " << 2 * pairs << ", early "
                  << progress.failed << ", after " << std::chrono::duration<double>(elapsed).count()
                  << "s of " << Bound.count() << "s");
    REQUIRE(progress.ended == 2 * pairs);
    REQUIRE(progress.failed == 0);

    // Two completions per round trip: each side's read parks and is completed by readiness.
    auto const completions = 2.0 * static_cast<double>(pairs * rounds);
    auto const perCompletion = [completions](auto duration) {
        return std::chrono::duration<double, std::nano>(duration).count() / completions;
    };
    return Sample { .wallNs = perCompletion(elapsed),
                    .userNs = perCompletion(userEnded - userStarted),
                    .systemNs = perCompletion(systemEnded - systemStarted) };
}

} // namespace

TEST_CASE("Socket completion cost over connected pairs", "[.][bench][net][socket]")
{
    constexpr auto Completions = std::size_t { 400'000 };
    constexpr auto Runs = std::size_t { 5 };

    for (auto const pairs: { std::size_t { 1 }, std::size_t { 16 }, std::size_t { 64 } })
    {
        auto samples = std::array<Sample, Runs> {};
        std::ranges::generate(samples, [pairs] { return measure(pairs, Completions / 2 / pairs); });
        auto const median = [&samples](double Sample::* field) {
            auto values = std::array<double, Runs> {};
            std::ranges::transform(
                samples, values.begin(), [field](Sample const& sample) { return sample.*field; });
            std::ranges::sort(values);
            return std::array { values[Runs / 2], values.front(), values.back() };
        };
        auto const wall = median(&Sample::wallNs);
        auto const user = median(&Sample::userNs);
        auto const system = median(&Sample::systemNs);
        std::cout << std::format(
            "socket completion, backend {}, {} pairs: median user {:.1f} ns/op (min {:.1f}, "
            "max {:.1f}), system {:.1f}, wall {:.1f}; {} completions x {} runs\n",
            core::net::toString(core::net::preferredBackendKind()),
            pairs,
            user[0],
            user[1],
            user[2],
            system[0],
            wall[0],
            Completions,
            Runs);
    }
}

TEST_CASE("Park filed and taken per socket operation", "[.][bench][net][park]")
{
    // What a socket operation pays the loop's bookkeeping when it has to wait, with no kernel wait
    // and no readiness: file a frameless read park on the socket's kept registration, then take it,
    // as `PosixSocket::armRead` and `takeRead` do. The registration is attached and armed for
    // reading by the first park and never touched again, so this is the loop's own work alone --
    // core-cpp#52's target, which the ping-pong above spreads over a syscall-heavy round trip.
    constexpr auto Operations = std::size_t { 2'000'000 };
    constexpr auto Runs = std::size_t { 5 };

    auto backend = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(backend != nullptr);
    auto loop = EventLoop { *backend };
    auto descriptors = std::array<int, 2> {};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors.data()) == 0);
    auto const fd = descriptors[0];
    auto const onReady = [](void*, core::net::ParkWake) {
    };

    auto samples = std::array<double, Runs> {};
    for (auto& sample: samples)
    {
        auto const started = std::chrono::steady_clock::now();
        auto refused = std::size_t { 0 };
        for ([[maybe_unused]] auto const operation: std::views::iota(std::size_t { 0 }, Operations))
        {
            auto const park = loop.registerPark(
                core::net::ParkEntry::onReadyCallback(onReady,
                                                      nullptr,
                                                      fd,
                                                      core::net::DefaultHandleKind,
                                                      core::net::Interest::Read,
                                                      core::net::RegistrationLifetime::UntilClosed));
            if (!park)
                ++refused;
            loop.unregisterPark(park);
        }
        auto const elapsed = std::chrono::steady_clock::now() - started;
        REQUIRE(refused == 0);
        sample = std::chrono::duration<double, std::nano>(elapsed).count() / static_cast<double>(Operations);
    }
    CHECK(loop.parkedWaiterCount() == 0);
    loop.notifyHandleClosing(fd, core::net::FdWakePolicy::Resume);
    std::ignore = ::close(descriptors[0]);
    std::ignore = ::close(descriptors[1]);

    std::ranges::sort(samples);
    std::cout << std::format(
        "park filed and taken, backend {}: median {:.1f} ns/op (min {:.1f}, max {:.1f}); "
        "{} operations x {} runs\n",
        core::net::toString(core::net::preferredBackendKind()),
        samples[Runs / 2],
        samples.front(),
        samples.back(),
        Operations,
        Runs);
}
