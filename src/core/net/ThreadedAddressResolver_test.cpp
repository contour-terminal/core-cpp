// SPDX-License-Identifier: Apache-2.0
#include <core/net/EventLoop.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/SocketAddress.hpp>
#include <core/net/ThreadedAddressResolver.hpp>
#include <core/net/testing/TestLoop.hpp>
#include <core/platform/Clock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using core::async::Task;
using core::net::EventLoop;
using core::net::ResolvedEndpoint;
using core::net::ResolveResult;
using core::net::ThreadedAddressResolver;

namespace
{

/// A blocking resolver with no DNS behind it: it answers from a script and records who asked.
///
/// The whole point of the seam is that a test can say what resolution costs and observe which
/// thread paid for it, so every property below is provable without a name server anywhere.
class ScriptedResolver final: public core::net::IAddressResolver
{
  public:
    [[nodiscard]] std::expected<std::vector<ResolvedEndpoint>, std::string> resolve(
        std::string_view host, std::uint16_t /*port*/) override
    {
        {
            auto const guard = std::scoped_lock { _mutex };
            _callers.push_back(std::this_thread::get_id());
            ++_calls;
        }
        _entered.notify_all();

        // Held here until a case releases it, so "the queue is full" and "a lookup is still in
        // flight" are arranged states rather than races.
        auto lock = std::unique_lock { _mutex };
        _release.wait(lock, [this] { return !_hold; });
        lock.unlock();

        if (host == "unresolvable.invalid")
            return std::unexpected(std::string { "scripted failure" });
        return std::vector<ResolvedEndpoint> { ResolvedEndpoint {
            .length = 16, .family = 2, .protocol = 6 } };
    }

    /// Makes every later lookup block inside @c resolve until @c release is called.
    void hold() noexcept
    {
        auto const guard = std::scoped_lock { _mutex };
        _hold = true;
    }

    void release() noexcept
    {
        {
            auto const guard = std::scoped_lock { _mutex };
            _hold = false;
        }
        _release.notify_all();
    }

    /// Blocks until @p count lookups have entered @c resolve.
    void awaitEntered(std::size_t count)
    {
        auto lock = std::unique_lock { _mutex };
        _entered.wait(lock, [&] { return _calls >= count; });
    }

    [[nodiscard]] std::size_t calls() const
    {
        auto const guard = std::scoped_lock { _mutex };
        return _calls;
    }

    /// @return The thread each lookup ran on, in call order.
    [[nodiscard]] std::vector<std::thread::id> callers() const
    {
        auto const guard = std::scoped_lock { _mutex };
        return _callers;
    }

  private:
    mutable std::mutex _mutex;
    std::condition_variable _release;
    std::condition_variable _entered;
    std::vector<std::thread::id> _callers;
    std::size_t _calls = 0;
    bool _hold = false;
};

/// Resolves through @p resolver on @p loop and records what came back.
Task<void> resolveOn(EventLoop* loop,
                     core::net::IAsyncAddressResolver* resolver,
                     std::string host,
                     ResolveResult* out,
                     std::thread::id* resumedOn)
{
    *out = co_await resolver->resolve(std::move(host), 80, loop);
    *resumedOn = std::this_thread::get_id();
}

} // namespace

TEST_CASE("a literal never reaches the resolver pool", "[net]")
{
    // Load-bearing rather than an optimisation: every internal dial in the consuming projects is
    // to a literal, and paying a thread hand-off plus two context switches for `inet_pton` would
    // be a real regression on the hot path. It is also what lets the whole connect path be
    // exercised with no thread existing at all.
    auto inner = ScriptedResolver {};
    auto resolver = ThreadedAddressResolver { inner };

    auto clock = core::platform::ManualClock {};
    auto loop = core::net::testing::TestLoop { clock };

    auto answer = ResolveResult {};
    auto resumedOn = std::thread::id {};
    loop.blockOn(resolveOn(&loop, &resolver, "127.0.0.1", &answer, &resumedOn));

    CHECK(answer.has_value());
    CHECK(resolver.offloaded() == 0);
    CHECK(inner.calls() == 1); // it still resolved — inline, on this thread
    REQUIRE(inner.callers().size() == 1);
    CHECK(inner.callers().front() == std::this_thread::get_id());
}

TEST_CASE("a name is resolved on another thread and resumes on the loop's", "[net]")
{
    // The property the whole task exists for, asserted at the seam rather than through a dial:
    // the lookup runs somewhere that is NOT the thread driving the loop, and the coroutine that
    // asked for it continues where it was — on the loop.
    auto inner = ScriptedResolver {};
    auto resolver = ThreadedAddressResolver { inner };

    auto backend = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(backend != nullptr);
    auto loop = EventLoop { *backend };

    auto answer = ResolveResult {};
    auto resumedOn = std::thread::id {};
    loop.blockOn(resolveOn(&loop, &resolver, "example.test", &answer, &resumedOn));

    REQUIRE(answer.has_value());
    CHECK(resolver.offloaded() == 1);
    REQUIRE(inner.callers().size() == 1);
    CHECK(inner.callers().front() != std::this_thread::get_id());
    CHECK(resumedOn == std::this_thread::get_id());
}

TEST_CASE("a null loop resolves inline, because there is nowhere to hand a result back to", "[net]")
{
    // A correctness requirement rather than a second optimisation: with nothing to submit the
    // result to, offloading would park a coroutine that nothing could ever resume.
    auto inner = ScriptedResolver {};
    auto resolver = ThreadedAddressResolver { inner };

    auto clock = core::platform::ManualClock {};
    auto loop = core::net::testing::TestLoop { clock };

    auto answer = ResolveResult {};
    auto resumedOn = std::thread::id {};
    loop.blockOn(resolveOn(nullptr, &resolver, "example.test", &answer, &resumedOn));

    CHECK(answer.has_value());
    CHECK(resolver.offloaded() == 0);
    REQUIRE(inner.callers().size() == 1);
    CHECK(inner.callers().front() == std::this_thread::get_id());
}

TEST_CASE("a full resolver queue is refused rather than waited on", "[net]")
{
    // An unbounded queue is a memory-exhaustion hole reachable by whatever provokes dials, and
    // blocking the caller to wait for room would reintroduce, on the loop thread, exactly the
    // stall this class exists to remove. `WouldBlock` and not `SystemError`, because a caller can
    // retry the first and can do nothing at all with the second.
    auto inner = ScriptedResolver {};
    inner.hold();
    auto resolver = ThreadedAddressResolver { inner, { .threads = 1, .maxQueueDepth = 1 } };

    auto backend = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(backend != nullptr);
    auto loop = EventLoop { *backend };

    // One lookup occupies the single worker; a second fills the one queue slot. Both are
    // arranged, not raced: the worker is parked INSIDE the scripted resolver.
    auto first = ResolveResult {};
    auto second = ResolveResult {};
    auto third = ResolveResult {};
    auto where = std::thread::id {};

    loop.spawn(resolveOn(&loop, &resolver, "one.test", &first, &where));
    loop.runUntilIdle();
    inner.awaitEntered(1);
    loop.spawn(resolveOn(&loop, &resolver, "two.test", &second, &where));
    loop.runUntilIdle();

    // The third has nowhere to go: the worker is busy and the queue slot is taken.
    loop.blockOn(resolveOn(&loop, &resolver, "three.test", &third, &where));
    REQUIRE_FALSE(third.has_value());
    CHECK(third.error().code == core::net::NetErrorCode::WouldBlock);
    CHECK(resolver.refused() == 1);

    inner.release();
    resolver.stop();
}

TEST_CASE("stopping the resolver resumes a queued lookup rather than stranding it", "[net]")
{
    // A queued lookup whose coroutine is never resumed is a leaked frame, so `stop` fails
    // everything queued instead of dropping it. A lookup already inside the blocking call cannot
    // be interrupted — there is no portable way — so the join waits for that one.
    auto inner = ScriptedResolver {};
    inner.hold();
    auto resolver = ThreadedAddressResolver { inner, { .threads = 1, .maxQueueDepth = 8 } };

    auto backend = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(backend != nullptr);
    auto loop = EventLoop { *backend };

    auto inFlight = ResolveResult {};
    auto queued = ResolveResult {};
    auto where = std::thread::id {};

    loop.spawn(resolveOn(&loop, &resolver, "in-flight.test", &inFlight, &where));
    loop.runUntilIdle();
    inner.awaitEntered(1);
    loop.spawn(resolveOn(&loop, &resolver, "queued.test", &queued, &where));
    loop.runUntilIdle();

    inner.release(); // so the in-flight lookup can finish and the join can complete
    resolver.stop();
    loop.runUntilIdle();

    REQUIRE_FALSE(queued.has_value());
    CHECK(queued.error().code == core::net::NetErrorCode::Cancelled);
}
