// SPDX-License-Identifier: Apache-2.0
#include <core/net/ThreadedAddressResolver.hpp>

#include <core/async/ParkedWork.hpp>
#include <core/net/EventLoop.hpp>

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

namespace core::net
{

namespace
{

    /// One lookup's rendezvous between a worker thread and the parked coroutine.
    ///
    /// Shared, because either side may reach it last: the worker must be able to publish into it
    /// after the task was abandoned, and the task must be able to read it after the worker is
    /// gone.
    struct ResolveSlot
    {
        std::mutex mutex;
        ResolveResult result { std::vector<ResolvedEndpoint> {} };
        async::ParkedWork waiter {};
        bool done = false;
    };

    /// Publishes a result and hands the waiter back to its loop.
    ///
    /// **It never calls `resume()`.** The consumer must continue on the loop's thread and not on
    /// whichever worker happened to finish the lookup — that is the entire point of the hand-back,
    /// and resuming here would run the caller's continuation on a resolver thread, which is the
    /// stall this class exists to remove wearing a different hat.
    /// @param slot The rendezvous to fill.
    /// @param loop Where to hand the waiter back, or null when there is nowhere.
    /// @param result What the lookup produced.
    void settle(ResolveSlot& slot, EventLoop* loop, ResolveResult result)
    {
        // An `async::detail::Parked` rather than a bare `ParkedWork`, so the branch that hands
        // nothing on FREES an unowned chain instead of dropping it. That is `ParkedWork`'s
        // contract — resumed or freed, never neither — and the no-loop arm is the only place here
        // that can decline to hand it on. Nothing changes for a BORROWED handle: the claim is
        // empty, so the guard destroys nothing and the caller's owner still frees it.
        auto waiter = async::detail::Parked {};
        {
            auto const guard = std::scoped_lock { slot.mutex };
            slot.result = std::move(result);
            slot.done = true;
            waiter = async::detail::Parked { std::exchange(slot.waiter, async::ParkedWork {}) };
        }
        if (waiter.handle() && loop != nullptr)
            loop->submit(waiter.take());
    }

    /// Suspends until a slot is filled.
    ///
    /// Race-free the way @c ResultAwaitable is: the completion check and the waiter registration
    /// happen under one lock, so a result landing between them resumes through the normal path
    /// rather than parking on an answer that has already arrived.
    struct SlotPark
    {
        std::shared_ptr<ResolveSlot> slot;

        [[nodiscard]] bool await_ready() const noexcept
        {
            auto const guard = std::scoped_lock { slot->mutex };
            return slot->done;
        }

        /// Templated on the promise so `parkedWorkFor` can ask the PARKING coroutine's own
        /// promise whether anything else owns its chain: @c settle hands this chain to a loop
        /// that may be destroyed before it runs it, and by then the handle is erased, so this is
        /// the last place the question can be asked.
        /// @tparam Promise The suspending coroutine's promise type.
        /// @param handle The suspended lookup.
        /// @return True to stay suspended; false when the answer arrived first.
        template <typename Promise>
        [[nodiscard]] bool await_suspend(std::coroutine_handle<Promise> handle) const noexcept
        {
            auto const guard = std::scoped_lock { slot->mutex };
            if (slot->done)
                return false;
            slot->waiter = async::detail::parkedWorkFor(handle);
            return true;
        }

        [[nodiscard]] ResolveResult await_resume() const
        {
            auto const guard = std::scoped_lock { slot->mutex };
            return slot->result;
        }
    };

} // namespace

/// Pool, queue, and the state @c stop has to reach.
struct ThreadedAddressResolver::Impl
{
    IAddressResolver& inner;
    ThreadedResolverOptions options;

    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;

    /// One queued lookup.
    struct Job
    {
        std::shared_ptr<ResolveSlot> slot;
        EventLoop* loop = nullptr;
        std::string host;
        std::uint16_t port = 0;
    };

    std::deque<Job> queue;
    std::vector<std::thread> threads;

    std::atomic<std::size_t> refusedCount { 0 };
    std::atomic<std::size_t> offloadedCount { 0 };

    Impl(IAddressResolver& resolver, ThreadedResolverOptions opts) noexcept: inner(resolver), options(opts) {}

    /// One worker's whole life: take a job, resolve it, publish it.
    void runWorker()
    {
        while (true)
        {
            auto job = Job {};
            {
                auto lock = std::unique_lock { mutex };
                wake.wait(lock, [this] { return stopping || !queue.empty(); });
                if (stopping && queue.empty())
                    return;
                job = std::move(queue.front());
                queue.pop_front();
            }

            auto resolved = inner.resolve(job.host, job.port);
            if (resolved.has_value())
                settle(*job.slot, job.loop, std::move(*resolved));
            else
                settle(*job.slot,
                       job.loop,
                       std::unexpected(resolveFailure(job.host, job.port, resolved.error())));
        }
    }

    /// Starts the pool on first use.
    ///
    /// Lazily, because a process whose dials are all literals — which is most of them — must not
    /// pay for threads it never uses. The caller holds @c mutex.
    void ensureStarted()
    {
        if (!threads.empty() || stopping)
            return;
        threads.reserve(options.threads);
        for ([[maybe_unused]] auto const index: std::views::iota(std::size_t { 0 }, options.threads))
            threads.emplace_back([this] { runWorker(); });
    }

    /// Joins every worker. Called with @c mutex NOT held, after @c stopping is set and the
    /// condition variable has been notified.
    void joinWorkers() noexcept
    {
        for (auto& worker: threads)
        {
            if (worker.joinable())
                worker.join();
        }
        threads.clear();
    }
};

ThreadedAddressResolver::ThreadedAddressResolver(IAddressResolver& inner, ThreadedResolverOptions options):
    _impl(std::make_unique<Impl>(inner, options))
{
}

ThreadedAddressResolver::~ThreadedAddressResolver()
{
    stop();
}

std::size_t ThreadedAddressResolver::refused() const noexcept
{
    return _impl->refusedCount.load(std::memory_order_relaxed);
}

std::size_t ThreadedAddressResolver::offloaded() const noexcept
{
    return _impl->offloadedCount.load(std::memory_order_relaxed);
}

void ThreadedAddressResolver::stop() noexcept
{
    auto abandoned = std::deque<Impl::Job> {};
    {
        auto const guard = std::scoped_lock { _impl->mutex };
        if (_impl->stopping)
            return;
        _impl->stopping = true;
        abandoned.swap(_impl->queue);
    }
    _impl->wake.notify_all();

    // Every queued lookup is resumed rather than dropped, so no coroutine is left parked on an
    // answer that will never come.
    for (auto& job: abandoned)
        settle(*job.slot,
               job.loop,
               std::unexpected(
                   makeNetError(NetErrorCode::Cancelled, 0, "the resolver stopped before the lookup ran")));

    _impl->joinWorkers();
}

async::Task<ResolveResult> ThreadedAddressResolver::resolve(std::string host,
                                                            std::uint16_t port,
                                                            EventLoop* loop)
{
    // A literal needs no lookup, so it needs no thread. See the class comment: here that is the
    // common case, not the exotic one.
    //
    // The null-loop arm is a correctness requirement rather than a second optimisation: with
    // nowhere to submit a result back to, offloading would park a coroutine nothing could ever
    // resume.
    if (loop == nullptr || detail::isNumericHost(host))
    {
        auto resolved = _impl->inner.resolve(host, port);
        if (!resolved.has_value())
            co_return std::unexpected(resolveFailure(host, port, resolved.error()));
        co_return std::move(*resolved);
    }

    auto slot = std::make_shared<ResolveSlot>();
    {
        auto const guard = std::scoped_lock { _impl->mutex };
        if (_impl->stopping)
            co_return std::unexpected(makeNetError(NetErrorCode::Cancelled, 0, "the resolver is stopping"));

        if (_impl->queue.size() >= _impl->options.maxQueueDepth)
        {
            _impl->refusedCount.fetch_add(1, std::memory_order_relaxed);
            co_return std::unexpected(
                makeNetError(NetErrorCode::WouldBlock,
                             0,
                             std::format("the resolver queue is full ({} waiting); not resolving {}:{}",
                                         _impl->queue.size(),
                                         host,
                                         port)));
        }

        _impl->ensureStarted();
        _impl->queue.push_back(
            Impl::Job { .slot = slot, .loop = loop, .host = std::move(host), .port = port });
        _impl->offloadedCount.fetch_add(1, std::memory_order_relaxed);
    }
    _impl->wake.notify_one();

    co_return co_await SlotPark { .slot = std::move(slot) };
}

ThreadedAddressResolver& defaultAsyncResolver()
{
    static ThreadedAddressResolver resolver;
    return resolver;
}

} // namespace core::net
