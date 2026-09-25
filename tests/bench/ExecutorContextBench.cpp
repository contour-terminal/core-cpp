// SPDX-License-Identifier: Apache-2.0
//
// What the current-executor context costs an EventLoop turn: the benchmark behind the 0.4.0
// CHANGELOG's numbers.
//
// A program, not a test: it reports and asserts nothing. It uses only public API that 0.3.0 already
// had, so the same source builds against a 0.3.0 tree and against this one, and the two are compared
// by running them interleaved, pinned to one core:
//
//   for i in 1 2 3 4 5; do for bin in base head; do taskset -c 3 ./$bin 4000000 7; done; done
//
// Two shapes, each in user CPU (getrusage) and wall time per resumption, median and best of the
// repetitions:
//   turn1  -- dispatchBatch 1: every turn resumes one flow, so a per-turn cost is paid per
//             resumption. The worst case for a store per turn, and the shape of a server whose every
//             request is one readiness report and one resumption.
//   turn64 -- the default batch: a turn resumes up to 64 flows.
#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/testing/TestLoop.hpp>
#include <core/platform/Clock.hpp>

#include <sys/resource.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdio>
#include <format>
#include <ranges>
#include <span>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

namespace
{

/// Hands the awaiting flow straight back to the loop's ready queue.
struct Yield
{
    core::net::EventLoop* loop; ///< The loop to go back to.

    [[nodiscard]] bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) const { loop->submit(handle); }
    void await_resume() const noexcept {}
};

/// Yields @p count times.
core::async::Task<void> spinner(core::net::EventLoop* loop, long count)
{
    for ([[maybe_unused]] auto const turn: std::views::iota(0L, count))
        co_await Yield { loop };
}

/// @return This process's user CPU time so far, in seconds.
double userSeconds()
{
    auto usage = rusage {};
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<double>(usage.ru_utime.tv_sec) + (static_cast<double>(usage.ru_utime.tv_usec) / 1e6);
}

/// One measurement, per resumption.
struct Sample
{
    double userNs;
    double wallNs;
};

/// Drains @p resumptions yields of four flows through a loop whose turn resumes @p batch.
Sample measure(std::size_t batch, long resumptions)
{
    constexpr long Flows = 4;
    auto clock = core::platform::ManualClock {};
    auto options = core::net::EventLoopOptions {};
    options.dispatchBatch = batch;
    auto loop = core::net::testing::TestLoop { clock, options };
    for ([[maybe_unused]] auto const flow: std::views::iota(0L, Flows))
        loop.spawn(spinner(&loop, resumptions / Flows));
    auto const user0 = userSeconds();
    auto const wall0 = std::chrono::steady_clock::now();
    auto const drained = static_cast<double>(loop.drain());
    auto const wall1 = std::chrono::steady_clock::now();
    auto const user1 = userSeconds();
    return Sample { .userNs = (user1 - user0) * 1e9 / drained,
                    .wallNs = static_cast<double>(
                                  std::chrono::duration_cast<std::chrono::nanoseconds>(wall1 - wall0).count())
                              / drained };
}

/// @return @p text as a number, or @p fallback where it is not one.
template <typename T>
T numberOr(std::string_view text, T fallback)
{
    auto value = T {};
    auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc {} && end == text.data() + text.size() ? value : fallback;
}

} // namespace

int main(int argc, char** argv)
{
    auto const args = std::span { argv, static_cast<std::size_t>(argc) };
    auto const resumptions = args.size() > 1 ? numberOr(args[1], 4'000'000L) : 4'000'000L;
    auto const repetitions = args.size() > 2 ? numberOr(args[2], 7) : 7;
    for (auto const batch: { std::size_t { 1 }, std::size_t { 64 } })
    {
        auto user = std::vector<double> {};
        auto wall = std::vector<double> {};
        std::ignore = measure(batch, resumptions / 10); // warm-up
        for ([[maybe_unused]] auto const repetition: std::views::iota(0, repetitions))
        {
            auto const sample = measure(batch, resumptions);
            user.push_back(sample.userNs);
            wall.push_back(sample.wallNs);
        }
        std::ranges::sort(user);
        std::ranges::sort(wall);
        // std::format rather than std::println: <print> is newer than some libc++ this builds with.
        auto const line = std::format("turn{} user-median {:.2f} ns/resume (min {:.2f})  wall-median "
                                      "{:.2f} ns/resume (min {:.2f})\n",
                                      batch,
                                      user[user.size() / 2],
                                      user.front(),
                                      wall[wall.size() / 2],
                                      wall.front());
        std::fputs(line.c_str(), stdout);
    }
    return 0;
}
