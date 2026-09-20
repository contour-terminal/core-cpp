// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `ThreadPoolExecutor` — an @c IExecutor whose "somewhere else" is a fixed set of threads.
///
/// The one header of `core::async` that a single-threaded WebAssembly build does not get: it is
/// excluded from that build's `FILE_SET`, and including it there is refused below rather than
/// failing later in `<thread>`.
///
/// It is header-only because `core::async` is an INTERFACE target that every consumer gets for
/// free, and a compiled body would change what each of them links (design spec, Part I §1).

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    #error "core::async::ThreadPoolExecutor needs threads; single-threaded Emscripten has none"
#endif

#include <core/async/IExecutor.hpp>
#include <core/async/ParkedWork.hpp>

#include <algorithm>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <mutex>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

namespace core::async
{

/// An executor that runs what it is given on a fixed set of threads.
///
/// For work that BLOCKS. An event loop multiplexes many coroutines on one thread and is the right
/// answer whenever every one of them suspends; it is the wrong answer for a job that occupies its
/// thread for seconds, because awaiting one there would stall every other flow that loop owns.
///
/// `co_await ResumeOn { pool }` is the whole interface: a caller writes its work linearly and hops
/// threads on one line, rather than splitting into a callback.
///
/// The pool does NOT bound admission. It runs what it is given as threads come free, and a caller
/// that must refuse rather than queue decides that before submitting. Sizing the pool to that cap
/// means an admitted job always finds a thread.
class ThreadPoolExecutor final: public IExecutor
{
  public:
    /// @param threads How many run at once. Clamped to at least one, because a pool nothing runs
    ///        on would accept handles and never resume them, which is a coroutine frame nobody
    ///        frees rather than an idle pool.
    explicit ThreadPoolExecutor(std::size_t threads)
    {
        auto const wanted = std::max<std::size_t>(threads, 1);
        _threads.reserve(wanted);
        try
        {
            for ([[maybe_unused]] auto const index: std::views::iota(std::size_t { 0 }, wanted))
                _threads.emplace_back([this] { worker(); });
        }
        catch (...)
        {
            // A thread this process could not create leaves the ones it did create parked on
            // `_wake` forever, and joining them would then HANG rather than propagate. No
            // destructor runs for an object whose constructor threw, so releasing them is this
            // handler's job.
            stop();
            joinAll();
            throw;
        }
    }

    /// Stops and joins. Every handle still queued is resumed first.
    ///
    /// Draining rather than discarding: a coroutine that is never resumed never runs its
    /// destructors and never frees its frame, so dropping the queue would leak every job in it
    /// along with whatever it holds.
    ~ThreadPoolExecutor() override
    {
        stop();
        joinAll();
    }

    ThreadPoolExecutor(ThreadPoolExecutor const&) = delete;
    ThreadPoolExecutor& operator=(ThreadPoolExecutor const&) = delete;
    ThreadPoolExecutor(ThreadPoolExecutor&&) = delete;
    ThreadPoolExecutor& operator=(ThreadPoolExecutor&&) = delete;

    using IExecutor::submit;

    /// Posts a coroutine for resumption on one of the threads.
    ///
    /// After `stop()`, the handle is resumed **inline on the calling thread** rather than dropped,
    /// for the reason the destructor drains: a refused handle is a leaked frame. A caller that
    /// must not run work on its own thread stops submitting, which it can see by having called
    /// `stop()`.
    /// @param handle Coroutine to resume; must remain alive until it is.
    void submit(std::coroutine_handle<> handle) override
    {
        {
            auto const guard = std::scoped_lock { _mutex };
            if (!_stopping)
            {
                _queue.push_back(handle);
                // Notified with the lock RELEASED would be the usual advice; held is correct here
                // because a worker waking to an empty queue and a set `_stopping` must not race
                // the push it was notified for.
                _wake.notify_one();
                return;
            }
        }

        // Stopped, so nothing will pick it up. Resumed here rather than dropped: an unresumed
        // coroutine never frees its frame.
        handle.resume();
    }

    /// Posts a coroutine, ignoring the chain root it names.
    ///
    /// **This pool never abandons work, which is why it can.** `submit` drains its queue even
    /// while stopping and resumes anything handed to it after that inline, so there is no moment
    /// at which a queued handle stops being resumable and nothing for @c ParkedWork::abandon to
    /// answer. An event loop is the opposite shape, and that is where the whole question comes
    /// from ([fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025)).
    /// @param work The coroutine to resume; only @c ParkedWork::resume is used.
    void submit(ParkedWork work) override { submit(work.resume); }

    /// Asks the threads to finish and stop taking new work. Idempotent, and callable from any
    /// thread.
    void stop() noexcept
    {
        {
            auto const guard = std::scoped_lock { _mutex };
            _stopping = true;
        }
        _wake.notify_all();
    }

    /// @return How many threads this pool runs work on.
    [[nodiscard]] std::size_t threads() const noexcept { return _threads.size(); }

  private:
    /// Joins every thread this pool started. Called only after `stop()`.
    void joinAll() noexcept
    {
        for (auto& thread: _threads)
            if (thread.joinable())
                thread.join();
    }

    /// One worker: takes handles off the queue and resumes them until the queue is empty and the
    /// pool is stopping.
    void worker()
    {
        while (true)
        {
            auto handle = std::coroutine_handle<> {};
            {
                auto guard = std::unique_lock { _mutex };
                _wake.wait(guard, [this] { return _stopping || !_queue.empty(); });
                // The queue is drained even while stopping, so work already admitted runs to
                // completion rather than being abandoned mid-frame.
                if (_queue.empty())
                    return;
                handle = _queue.front();
                _queue.pop_front();
            }
            handle.resume();
        }
    }

    std::mutex _mutex;
    std::condition_variable _wake;
    std::deque<std::coroutine_handle<>> _queue;
    bool _stopping { false };

    /// Declared LAST, so they are joined before the queue they read is gone.
    std::vector<std::thread> _threads;
};

} // namespace core::async
