// SPDX-License-Identifier: Apache-2.0
//
// What a strand does when an allocation it needs fails.
//
// A strand allocates its pump's frame lazily, on the submit that finds it without one, and again
// when a task's throw out of `resume()` kills the pump. Either allocation can fail, and a strand
// that had already published "a pump is scheduled" when it did would never schedule one again --
// every later submit would queue behind it, and `~Strand` would wait for a pump that is not
// running. This binary replaces the global allocation functions so a case can fail exactly the
// next one, which is why it is a binary of its own: a replacement reaches every test linked beside
// it.
#include <core/async/ResumeOn.hpp>
#include <core/async/Strand.hpp>
#include <core/async/Task.hpp>
#include <core/async/testing/ManualExecutor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace
{

/// How many allocations to serve before failing one; negative serves them all. Per thread, so
/// Catch2's own bookkeeping on another thread is never the one that fails.
thread_local int allocationsBeforeFailure = -1;

/// Serves an allocation from `malloc`, or fails it when the countdown reaches zero.
/// @param size The size asked for.
/// @return The storage.
/// @throws std::bad_alloc When the countdown says so, or when `malloc` refuses.
void* countedAllocation(std::size_t size)
{
    if (allocationsBeforeFailure == 0)
    {
        allocationsBeforeFailure = -1;
        throw std::bad_alloc {};
    }
    if (allocationsBeforeFailure > 0)
        --allocationsBeforeFailure;
    if (auto* const storage = std::malloc(size == 0 ? 1 : size))
        return storage;
    throw std::bad_alloc {};
}

/// Records that it ran, then ends.
core::async::Task<void> record(std::vector<int>* out, int value)
{
    out->push_back(value);
    co_return;
}

#if !defined(_MSC_VER) || defined(__clang__)
/// A coroutine whose `resume()` throws -- see `Strand_test.cpp` -- and which arms the allocation
/// failure on its way out, so the allocation that fails is the replacement pump's.
class ThrowingResume
{
  public:
    struct promise_type
    {
        [[nodiscard]] ThrowingResume get_return_object() noexcept
        {
            return ThrowingResume { std::coroutine_handle<promise_type>::from_promise(*this) };
        }
        [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }
        [[nodiscard]] std::suspend_always final_suspend() const noexcept { return {}; }
        void return_void() const noexcept {}
        [[noreturn]] void unhandled_exception() const { throw; }
    };

    explicit ThrowingResume(std::coroutine_handle<promise_type> handle) noexcept: _handle(handle) {}
    ThrowingResume(ThrowingResume const&) = delete;
    ThrowingResume(ThrowingResume&&) = delete;
    ThrowingResume& operator=(ThrowingResume const&) = delete;
    ThrowingResume& operator=(ThrowingResume&&) = delete;
    ~ThrowingResume() { _handle.destroy(); }

    [[nodiscard]] std::coroutine_handle<> handle() const noexcept { return _handle; }

  private:
    std::coroutine_handle<promise_type> _handle;
};

/// What the task throws: a type that allocates nothing, so the allocation the countdown fails is
/// the replacement pump's and not the exception's own message.
struct ThrownOutOfResume
{
};

/// Throws out of `resume()`, having armed the next allocation to fail.
ThrowingResume throwAndFailNextAllocation(bool really)
{
    if (really)
    {
        allocationsBeforeFailure = 0;
        throw ThrownOutOfResume {};
    }
    co_return;
}
#endif

} // namespace

void* operator new(std::size_t size)
{
    return countedAllocation(size);
}

void* operator new[](std::size_t size)
{
    return countedAllocation(size);
}

void operator delete(void* storage) noexcept
{
    std::free(storage);
}

void operator delete[](void* storage) noexcept
{
    std::free(storage);
}

void operator delete(void* storage, std::size_t /*size*/) noexcept
{
    std::free(storage);
}

void operator delete[](void* storage, std::size_t /*size*/) noexcept
{
    std::free(storage);
}

TEST_CASE("A submit whose allocation fails changes nothing, whichever allocation it is",
          "[Strand][exceptions]")
{
    // A strand's first submit allocates the pump's frame and room in its queue, in an order that is
    // the implementation's business. Each allocation is failed in turn, until a submit needs no
    // more than the ones that were let through: every refused submit must leave nothing queued and
    // nothing scheduled -- a pump published as scheduled that nobody will run is a strand no later
    // submit can restart, and a queued task whose submitter was told no runs anyway.
    auto failures = 0;
    auto reachedSuccess = false;
    for (auto const failAt: { 0, 1, 2, 3, 4, 5, 6, 7 })
    {
        auto base = core::async::testing::ManualExecutor {};
        auto strand = core::async::Strand { base };
        auto order = std::vector<int> {};
        order.reserve(4);
        auto refused = record(&order, 1);
        auto accepted = record(&order, 2);

        allocationsBeforeFailure = failAt;
        auto threw = false;
        try
        {
            strand.submit(refused.handle());
        }
        catch (std::bad_alloc const&)
        {
            threw = true;
        }
        allocationsBeforeFailure = -1;
        if (!threw)
        {
            INFO("the first submit needed " << failAt << " allocation(s)");
            CHECK(failures > 0);
            reachedSuccess = true;
            break;
        }
        ++failures;
        INFO("allocation " << failAt << " failed");
        CHECK(strand.queued() == 0);
        CHECK(base.pending() == 0);

        strand.submit(accepted.handle());
        std::ignore = base.drain();
        CHECK(order == std::vector { 2 });
        CHECK_FALSE(refused.done());
    }
    // Every allocation a first submit makes was failed once: the loop ended on a submit that needed
    // no more, not by running out of positions to try.
    CHECK(reachedSuccess);
}

TEST_CASE("A strand whose replacement pump cannot be allocated keeps its queue and restarts on the "
          "next submit",
          "[Strand][exceptions]")
{
#if defined(_MSC_VER) && !defined(__clang__)
    SKIP("under MSVC's cl a throw out of resume() on a strand terminates the process "
         "(core-cpp.strand-throw-canary)");
#else
    auto base = core::async::testing::ManualExecutor {};
    auto strand = core::async::Strand { base };
    auto order = std::vector<int> {};
    order.reserve(4);

    auto const thrower = throwAndFailNextAllocation(true);
    auto behind = record(&order, 1);
    strand.submit(thrower.handle());
    strand.submit(behind.handle());

    // The task's exception was replaced by the allocation failure of the pump meant to take over.
    CHECK_THROWS_AS(base.drain(), std::bad_alloc);
    allocationsBeforeFailure = -1;
    CHECK(order.empty());
    CHECK(strand.queued() == 1);

    // Nothing is scheduled, but nothing is wedged: the next submit makes a pump, and the task that
    // was waiting runs first.
    auto next = record(&order, 2);
    strand.submit(next.handle());
    std::ignore = base.drain();
    CHECK(order == std::vector { 1, 2 });
#endif
}
