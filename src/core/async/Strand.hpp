// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `Strand` — an executor that runs what it is given one at a time, in order, on another.
///
/// A strand removes the need for a lock around state that several coroutines touch: everything
/// that touches it runs on the strand, and the strand never runs two things at once, whatever
/// executor is underneath -- a thread pool included. `KeyedStrands` is the same thing, one per
/// key. The design, and why a coroutine that parks on something another thread completes comes
/// back to the strand, are in `docs/design/strands.md`.

#include <core/async/ExecutorContext.hpp>
#include <core/async/IExecutor.hpp>
#include <core/async/ParkedWork.hpp>

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

// Whether this build has threads, and so whether a strand can be running a task on a thread other
// than the one destroying it. Single-threaded Emscripten has none: there the wait below cannot be
// needed, and a blocking wait is not allowed in the WebAssembly subset
// (.agent/rules/library-hygiene.md).
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    #include <condition_variable>

    #define CORE_ASYNC_STRAND_HAS_THREADS 1
#else
    #define CORE_ASYNC_STRAND_HAS_THREADS 0
#endif

namespace core::async
{

/// How a strand shares its base executor.
struct StrandOptions
{
    /// How many tasks one turn on the base runs before the strand hands the base back and queues
    /// itself again. At least 1.
    ///
    /// A strand with a long queue would otherwise hold a pool thread, or an event loop's turn, for
    /// as long as work keeps arriving; handing back is what lets an `EventLoop`'s own
    /// `dispatchBatch` bound mean anything with a strand on it. Each hand-back costs one `submit`
    /// on the base.
    std::size_t batch { 32 };
};

namespace detail
{

    class StrandCore;

    /// Where a strand's pump is. The one state the strand's mutex guards besides its queue.
    enum class StrandPhase : std::uint8_t
    {
        Idle = 0,  ///< Suspended and queued nowhere: the next submit queues it on the base.
        Scheduled, ///< Queued on the base, not yet running.
        Running,   ///< Running tasks, on whichever thread the base resumed it on.
        Exited,    ///< Ended, because the strand was closed or retired.
    };

    /// Whether a strand ends when it runs out of work, which is what `KeyedStrands` does with one
    /// key's strand.
    enum class StrandReclaim : std::uint8_t
    {
        Never = 0, ///< It waits, idle, for the next submit.
        WhenIdle,  ///< It asks its owner to retire it, and ends if the owner does.
    };

    /// The coroutine that runs a strand's tasks on the strand's base executor.
    ///
    /// One per strand, alive for as long as the strand has one: a strand that goes idle suspends
    /// it rather than ending it, so the steady state costs no allocation. It holds the strand's
    /// state, not the strand, so a strand destroyed while its pump is queued on the base leaves it
    /// something to run: the pump finds the state closed and ends.
    class StrandPump final
    {
      public:
        /// The promise; the standard looks up `StrandPump::promise_type`.
        struct promise_type
        {
            /// @param pumped The strand this pump runs; the same pointer the body takes.
            explicit promise_type(std::shared_ptr<StrandCore> const& pumped) noexcept: strand(pumped) {}

            /// @return The pump, suspended before its first statement.
            [[nodiscard]] StrandPump get_return_object() noexcept
            {
                return StrandPump { std::coroutine_handle<promise_type>::from_promise(*this) };
            }

            /// Suspended until the first submit queues it on the base.
            [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }

            /// Frees the frame at the end: a pump ends only when nothing will queue it again.
            [[nodiscard]] std::suspend_never final_suspend() const noexcept { return {}; }

            void return_void() const noexcept {}

            /// A task threw out of `resume()`. Defined after @c StrandCore.
            [[noreturn]] void unhandled_exception();

            /// The strand, for @c unhandled_exception, which cannot reach the body's parameter.
            std::shared_ptr<StrandCore> strand;
        };

        /// @param handle The pump's frame.
        explicit StrandPump(std::coroutine_handle<promise_type> handle) noexcept: _handle(handle) {}

        /// @return The pump's frame.
        [[nodiscard]] std::coroutine_handle<promise_type> handle() const noexcept { return _handle; }

      private:
        std::coroutine_handle<promise_type> _handle;
    };

    /// Runs @p core's tasks, a batch per turn on its base, for as long as the strand lives.
    /// @param strand The strand; held, so a pump queued on the base outlives a closed strand safely.
    /// @return The pump, suspended.
    inline StrandPump runStrandPump(std::shared_ptr<StrandCore> strand);

    /// Keeps the frame of a pump that ended by exception until it is safe to free.
    ///
    /// A coroutine whose `resume()` throws is left suspended at its final point, and nothing may
    /// free it until that `resume()` has returned -- the compiler still marks the frame on the way
    /// out. The thread it threw on is the one thread that knows when that is: the next time it
    /// buries one, or when it exits, the previous one is long finished. So at most one dead pump
    /// per thread is ever held.
    class DeadPumpReaper final
    {
      public:
        DeadPumpReaper() noexcept = default;
        DeadPumpReaper(DeadPumpReaper const&) = delete;
        DeadPumpReaper(DeadPumpReaper&&) = delete;
        DeadPumpReaper& operator=(DeadPumpReaper const&) = delete;
        DeadPumpReaper& operator=(DeadPumpReaper&&) = delete;

        ~DeadPumpReaper()
        {
            if (_dead)
                _dead.destroy();
        }

        /// Frees the pump buried before, and keeps @p dead in its place.
        /// @param dead A pump whose `resume()` is throwing on this thread right now.
        void bury(std::coroutine_handle<> dead) noexcept
        {
            if (auto const previous = std::exchange(_dead, dead))
                previous.destroy();
        }

      private:
        std::coroutine_handle<> _dead;
    };

    /// Hands @p dead to the calling thread's @c DeadPumpReaper.
    /// @param dead A pump whose `resume()` is throwing on this thread right now.
    inline void buryDeadPump(std::coroutine_handle<> dead) noexcept
    {
        thread_local DeadPumpReaper reaper;
        reaper.bury(dead);
    }

    /// A first-in first-out queue of parked work that allocates nothing until it is first used.
    ///
    /// A `std::deque` allocates its map and a first block when it is constructed, on some standard
    /// libraries, and `KeyedStrands` constructs a strand every time a key goes from idle to busy.
    class ParkedQueue final
    {
      public:
        /// @return Whether nothing is queued.
        [[nodiscard]] bool empty() const noexcept { return _head == _entries.size(); }

        /// @return How many entries are queued.
        [[nodiscard]] std::size_t size() const noexcept { return _entries.size() - _head; }

        /// Appends @p entry.
        /// @param entry The work to queue.
        void push(Parked entry) { _entries.push_back(std::move(entry)); }

        /// Takes the oldest entry. @pre `!empty()`.
        /// @return The oldest entry.
        [[nodiscard]] Parked pop() noexcept
        {
            auto entry = std::move(_entries[_head]);
            ++_head;
            if (_head == _entries.size())
            {
                _entries.clear(); // keeps the capacity
                _head = 0;
            }
            else if (_head >= CompactAfter && _head * 2 >= _entries.size())
            {
                // A strand that never runs dry would otherwise keep every entry it ever took.
                _entries.erase(_entries.begin(), _entries.begin() + static_cast<std::ptrdiff_t>(_head));
                _head = 0;
            }
            return entry;
        }

        /// Takes everything queued, leaving this empty.
        /// @return What was queued, oldest first from @c head.
        [[nodiscard]] std::vector<Parked> takeAll() noexcept
        {
            _entries.erase(_entries.begin(), _entries.begin() + static_cast<std::ptrdiff_t>(_head));
            _head = 0;
            return std::exchange(_entries, {});
        }

      private:
        static constexpr std::size_t CompactAfter = 64;

        std::vector<Parked> _entries;
        std::size_t _head { 0 };
    };

    /// What a strand is: a queue, a pump, and the state that says where the pump is.
    ///
    /// Shared between the strand's owner and its pump, so that closing the strand while the pump
    /// is queued on the base -- which the owner cannot take back -- leaves the pump something to
    /// find. It is an @c IExecutor itself because a key's strand in `KeyedStrands` has no owner
    /// object of its own to be current as: there, this is what `currentExecutor()` names, and a
    /// @c ResumeTarget taken there keeps it alive.
    class StrandCore: public IExecutor, public std::enable_shared_from_this<StrandCore>
    {
      public:
        /// @param base Where the pump runs. Must outlive every pump this strand makes.
        /// @param options The batch bound.
        /// @param reportedAs What `currentExecutor()` names inside a task, or null for this core.
        /// @param family The address `ExecutorScope::family()` answers inside a task, or null.
        /// @param reclaim Whether the strand ends when it runs out of work.
        StrandCore(IExecutor& base,
                   StrandOptions options,
                   IExecutor* reportedAs,
                   void const* family,
                   StrandReclaim reclaim) noexcept:
            _base(base),
            _options(options),
            _reportedAs(reportedAs != nullptr ? reportedAs : this),
            _family(family),
            _reclaim(reclaim)
        {
            assert(options.batch > 0
                   && "StrandOptions::batch must be at least 1: a turn that runs nothing never empties "
                      "the strand");
        }

        StrandCore(StrandCore const&) = delete;
        StrandCore(StrandCore&&) = delete;
        StrandCore& operator=(StrandCore const&) = delete;
        StrandCore& operator=(StrandCore&&) = delete;
        ~StrandCore() override = default;

        using IExecutor::submit;

        /// Queues @p handle, borrowed.
        /// @param handle The coroutine to resume on the strand.
        void submit(std::coroutine_handle<> handle) override { submit(ParkedWork { .resume = handle }); }

        /// Queues @p work; a closed strand drops it, and a retired one hands it to its owner.
        /// @param work The coroutine to resume on the strand, and its claim on the chain root.
        void submit(ParkedWork work) override
        {
            auto entry = Parked { std::move(work) };
            auto pump = std::coroutine_handle<> {};
            auto rerouted = std::optional<ParkedWork> {};
            {
                auto const lock = std::scoped_lock { _mutex };
                if (_closed)
                    return; // `entry` drops the work outside the lock, freeing what nobody owns.
                if (_retired)
                    rerouted.emplace(entry.take());
                else
                    pump = enqueueLocked(std::move(entry));
            }
            // Both outside the lock: a base that resumes inline would run the pump -- and with it
            // the strand's tasks -- inside it, and a reroute takes the owner's lock.
            if (rerouted)
                reroute(std::move(*rerouted));
            else if (pump)
                _base.submit(pump);
        }

        /// Queues @p work when the caller already knows this strand is open and not retired:
        /// `KeyedStrands` holds its registry's lock across the lookup and this.
        /// @param work The coroutine to resume on the strand.
        /// @return The pump to hand to the base once every lock is released, or an empty handle.
        [[nodiscard]] std::coroutine_handle<> enqueue(ParkedWork work)
        {
            auto const lock = std::scoped_lock { _mutex };
            assert(!_closed && !_retired);
            return enqueueLocked(Parked { std::move(work) });
        }

        /// @return Whether the calling thread is inside one of this strand's tasks, at any depth.
        [[nodiscard]] bool runningHere() const noexcept
        {
            return ExecutorScope::anyInForce(
                [this](ExecutorScope const& scope) noexcept { return &scope.executor() == _reportedAs; });
        }

        /// @return How many tasks are queued and not yet running.
        [[nodiscard]] std::size_t queued() const
        {
            auto const lock = std::scoped_lock { _mutex };
            return _queue.size();
        }

        /// Closes the strand: queued work is dropped, a task running on another thread is waited
        /// for, and an idle pump is freed. Idempotent.
        void close()
        {
            auto dropped = std::vector<Parked> {};
            auto idlePump = std::coroutine_handle<> {};
            {
                auto lock = std::unique_lock { _mutex };
                _closed = true;
                dropped = _queue.takeAll();
#if CORE_ASYNC_STRAND_HAS_THREADS
                // Not from inside one of its own tasks, which would wait for itself: the pump ends
                // when that task returns, because it reads `_closed` before it takes another.
                if (!runningHere())
                    _settled.wait(lock, [this] { return _phase != StrandPhase::Running; });
#endif
                if (_phase == StrandPhase::Idle)
                {
                    idlePump = std::exchange(_pump, {});
                    _phase = StrandPhase::Exited;
                }
            }
            // Outside the lock: freeing a chain runs its destructors, which may submit here again.
            dropped.clear();
            if (idlePump)
                idlePump.destroy();
        }

        /// Retires this strand if it has nothing queued: from now on it hands what it is given to
        /// its owner. Called by the owner, holding the owner's own lock.
        /// @return Whether it was retired.
        [[nodiscard]] bool retireIfEmpty() noexcept
        {
            auto const lock = std::scoped_lock { _mutex };
            if (_closed || !_queue.empty())
                return false;
            _retired = true;
            return true;
        }

      protected:
        /// Asks the owner to retire this strand, which ran out of work. Only a strand made with
        /// @c StrandReclaim::WhenIdle is asked.
        /// @return Whether it was retired.
        [[nodiscard]] virtual bool tryRetire() { return false; }

        /// Hands @p work, which arrived after this strand retired, to whatever now serves its
        /// work. A strand that never retires is never asked, and drops it if it is.
        /// @param work The work.
        virtual void reroute(ParkedWork work)
        {
            assert(false && "StrandCore::reroute on a strand that never retires");
            auto const dropped = Parked { std::move(work) };
        }

      private:
        friend StrandPump runStrandPump(std::shared_ptr<StrandCore> strand);
        friend struct StrandPump::promise_type;

        /// Queues @p entry and decides whether the pump must be queued on the base. Holds the lock.
        /// @return The pump to hand to the base, or an empty handle.
        [[nodiscard]] std::coroutine_handle<> enqueueLocked(Parked entry)
        {
            _queue.push(std::move(entry));
            if (_phase != StrandPhase::Idle)
                return {};
            if (!_pump)
                _pump = runStrandPump(shared_from_this()).handle();
            _phase = StrandPhase::Scheduled;
            return _pump;
        }

        /// The pump starts a turn. @return False where the strand closed or retired, and the
        /// pump is to end.
        [[nodiscard]] bool beginTurn()
        {
            auto const lock = std::scoped_lock { _mutex };
            if (_closed || _retired)
            {
                _phase = StrandPhase::Exited;
                _pump = {};
                notifySettledLocked();
                return false;
            }
            _phase = StrandPhase::Running;
            return true;
        }

        /// Runs up to one batch of tasks, with this strand current. What a task throws out of
        /// `resume()` propagates, through the pump, to whoever resumed the pump.
        /// @param anchor What a @c ResumeTarget taken inside a task copies to keep this strand
        ///        alive, or empty where the strand's owner does.
        void runBatch(std::shared_ptr<void> const& anchor)
        {
            auto const scope = ExecutorScope { *_reportedAs, anchor ? &anchor : nullptr, _family };
            for ([[maybe_unused]] auto const turn: std::views::iota(std::size_t { 0 }, _options.batch))
            {
                auto entry = Parked {};
                {
                    auto const lock = std::scoped_lock { _mutex };
                    if (_closed || _queue.empty())
                        return;
                    entry = _queue.pop();
                }
                entry.resume();
            }
        }

        /// The pump's suspension between turns: queued again on the base if work is waiting, idle
        /// if not, and ended if the strand closed or retired.
        class EndTurn final
        {
          public:
            /// @param strand The pump's own reference to its strand, in the pump's frame.
            explicit EndTurn(std::shared_ptr<StrandCore> const& strand) noexcept: _strand(&strand) {}

            /// @return False: whether to suspend is `await_suspend`'s question, asked under the lock.
            [[nodiscard]] constexpr bool await_ready() const noexcept { return false; }

            /// Decides where the pump goes next. After it has published "idle" or queued the pump,
            /// another thread may resume it, so nothing reachable through the frame is touched
            /// after either.
            /// @param pump The suspended pump.
            /// @return True to stay suspended; false to go on, into a turn that ends the pump.
            [[nodiscard]] bool await_suspend(std::coroutine_handle<> pump) const
            {
                auto& self = **_strand;
                auto lock = std::unique_lock { self._mutex };
                if (self._closed)
                    return false;
                if (self._queue.empty() && self._reclaim == StrandReclaim::WhenIdle)
                {
                    // Stays Running while the owner decides, so no submit queues the pump in the
                    // meantime: work that arrives now is queued, and seen below.
                    lock.unlock();
                    if (self.tryRetire())
                        return false;
                    lock.lock();
                    if (self._closed)
                        return false;
                }
                if (self._queue.empty())
                {
                    self._phase = StrandPhase::Idle;
                    self.notifySettledLocked();
                    return true;
                }
                self._phase = StrandPhase::Scheduled;
                self.notifySettledLocked();
                auto& base = self._base;
                lock.unlock();
                base.submit(pump);
                return true;
            }

            void await_resume() const noexcept {}

          private:
            std::shared_ptr<StrandCore> const* _strand;
        };

        /// A task threw out of `resume()`, which ended the pump: a new one takes over the queue.
        /// Called from the dead pump's `unhandled_exception`, on the thread it threw on.
        void replaceDeadPump()
        {
            auto pump = std::coroutine_handle<> {};
            {
                auto const lock = std::scoped_lock { _mutex };
                if (_closed)
                {
                    _phase = StrandPhase::Exited;
                    _pump = {};
                    notifySettledLocked();
                    return;
                }
                _pump = runStrandPump(shared_from_this()).handle();
                // A strand that is reclaimed when idle is queued even with nothing to do, so the
                // new pump's first turn is what retires it.
                if (_queue.empty() && _reclaim == StrandReclaim::Never)
                    _phase = StrandPhase::Idle;
                else
                {
                    _phase = StrandPhase::Scheduled;
                    pump = _pump;
                }
                notifySettledLocked();
            }
            if (pump)
                _base.submit(pump);
        }

        /// Wakes a close waiting for the pump to stop running. Holds the lock.
        void notifySettledLocked() noexcept
        {
#if CORE_ASYNC_STRAND_HAS_THREADS
            _settled.notify_all();
#endif
        }

        IExecutor& _base;
        StrandOptions _options;
        IExecutor* _reportedAs;
        void const* _family;

        /// Guards everything below.
        mutable std::mutex _mutex;
#if CORE_ASYNC_STRAND_HAS_THREADS
        std::condition_variable _settled; ///< Signalled whenever the pump stops running.
#endif
        ParkedQueue _queue;
        std::coroutine_handle<> _pump;
        StrandPhase _phase { StrandPhase::Idle };
        StrandReclaim _reclaim;
        bool _closed { false };  ///< The owner is gone; nothing runs any more.
        bool _retired { false }; ///< The owner reclaimed it; work goes to the owner.
    };

    inline StrandPump runStrandPump(std::shared_ptr<StrandCore> strand)
    {
        // Held for the pump's life, so a ResumeTarget taken inside a task can keep a strand whose
        // owner is its registry alive; empty where an owner object is what tasks see.
        auto const anchor =
            strand->_reportedAs == strand.get() ? std::shared_ptr<void> { strand } : std::shared_ptr<void> {};
        while (strand->beginTurn())
        {
            strand->runBatch(anchor);
            co_await StrandCore::EndTurn { strand };
        }
    }

    inline void StrandPump::promise_type::unhandled_exception()
    {
        auto const self = std::coroutine_handle<promise_type>::from_promise(*this);
        auto const owner = std::move(strand);
        try
        {
            owner->replaceDeadPump();
        }
        catch (...)
        {
            // The new pump's first turn ran inline and threw too: that exception is the one that
            // leaves, and this frame is still buried rather than leaked.
            buryDeadPump(self);
            throw;
        }
        // AFTER queuing the replacement: an inline base runs it inside the call above, and a
        // replacement that died too would bury its own frame, freeing this one while it still
        // unwinds. Buried now, it is freed only once this thread next buries one or exits.
        buryDeadPump(self);
        throw;
    }

} // namespace detail

/// An executor that runs what it is given one at a time, in the order given, on a base executor.
///
/// **Serial.** At most one task runs at a time, whatever the base is, so state touched only from
/// the strand needs no lock. Two strands over one pool run concurrently with each other.
///
/// **FIFO.** Tasks run in the order `submit` received them; `co_await ResumeOn { strand }` is a
/// submit.
///
/// **What a task is.** A resumption: from `submit` until the coroutine next suspends. A coroutine
/// that suspends has left the strand, and another task may run before it comes back; it comes
/// back to the strand when what it awaited is a `ResumeOn { strand }`, or an awaitable that
/// resumes on the current executor (`AsyncQueue::pop`, see `ExecutorContext.hpp`). Socket and
/// timer awaitables of `core::net` resume on their `EventLoop` instead, and a coroutine hops back
/// with `co_await ResumeOn { strand }`.
///
/// **Current executor.** Inside a task, `currentExecutor()` is this strand and `runningHere()` is
/// true -- also inside anything the task resumes synchronously.
///
/// **A task that throws out of `resume()`** -- which no coroutine type of this module does, since
/// `Task` hands an exception to its awaiter and `DetachedTask` terminates -- propagates to
/// whoever resumed the strand on the base, and the strand goes on with the tasks behind it.
///
/// **Destruction.** Tasks still queued are dropped, never run: a chain rooted in a `DetachedTask`
/// is freed, and a coroutine a `Task` owns is left to its owner, suspended. A task running on
/// another thread is waited for (not where threads do not exist, and not from inside one of the
/// strand's own tasks, which the strand finishes once it returns). So a task may release the last
/// reference to the strand's owner: the destructor returns at once, the task runs to its end, and
/// the pump then ends without running anything more. The base must outlive the
/// strand and run what the strand queued on it: a pump queued there finds the strand closed and
/// ends.
class Strand final: public IExecutor
{
  public:
    /// @param base Where the strand's tasks run. Must outlive the strand.
    /// @param options How the strand shares its base.
    explicit Strand(IExecutor& base, StrandOptions options = {}):
        _core(
            std::make_shared<detail::StrandCore>(base, options, this, nullptr, detail::StrandReclaim::Never))
    {
    }

    Strand(Strand const&) = delete;
    Strand(Strand&&) = delete;
    Strand& operator=(Strand const&) = delete;
    Strand& operator=(Strand&&) = delete;

    /// Drops what is queued and waits for a task running on another thread. See the class.
    ~Strand() override { _core->close(); }

    using IExecutor::submit;

    /// Queues @p handle, borrowed. Callable from any thread.
    /// @param handle The coroutine to resume on the strand.
    void submit(std::coroutine_handle<> handle) override { _core->submit(handle); }

    /// Queues @p work, holding its claim until it runs. Callable from any thread.
    /// @param work The coroutine to resume on the strand, and its claim on the chain root.
    void submit(ParkedWork work) override { _core->submit(std::move(work)); }

    /// @return Whether the calling thread is inside one of this strand's tasks.
    [[nodiscard]] bool runningHere() const noexcept { return _core->runningHere(); }

    /// @return How many tasks are queued and not yet running. Racy by nature; for tests.
    [[nodiscard]] std::size_t queued() const { return _core->queued(); }

  private:
    std::shared_ptr<detail::StrandCore> _core;
};

} // namespace core::async
