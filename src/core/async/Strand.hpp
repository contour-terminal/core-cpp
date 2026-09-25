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

#include <algorithm>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

// Internal to core::async, not an option: whether this build has threads, and so whether a strand
// can be running a task on a thread other than the one destroying it. Single-threaded Emscripten has
// none: there the wait below cannot be needed, and a blocking wait is not allowed in the WebAssembly
// subset (.agent/rules/library-hygiene.md).
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    #include <condition_variable>

    #define CORE_CPP_ASYNC_HAS_THREADS 1
#else
    #define CORE_CPP_ASYNC_HAS_THREADS 0
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

        /// Appends @p entry, which is moved from only if this does not throw.
        /// @param entry The work to queue.
        void push(Parked& entry) { _entries.push_back(std::move(entry)); }

        /// Takes the queued entry that would resume @p handle out of the queue, wherever it is.
        /// @param handle The coroutine to look for.
        /// @return Its entry, or an empty one where it is not queued.
        [[nodiscard]] Parked remove(std::coroutine_handle<> handle) noexcept
        {
            auto const first = _entries.begin() + static_cast<std::ptrdiff_t>(_head);
            auto const found = std::ranges::find_if(
                first, _entries.end(), [handle](Parked const& entry) { return entry.handle() == handle; });
            if (found == _entries.end())
                return Parked {};
            auto entry = std::move(*found);
            _entries.erase(found);
            if (_head == _entries.size())
            {
                _entries.clear();
                _head = 0;
            }
            return entry;
        }

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
    /// Shared between the strand's owner, its pump, and every @c ResumeTarget taken inside one of
    /// its tasks, so that neither closing the strand while the pump is queued on the base -- which
    /// the owner cannot take back -- nor a coroutine that parked on the strand and is handed back
    /// after the owner is gone finds freed storage: they find this, closed, and it drops what it is
    /// given. It is the @c IExecutor tasks see as current, for that reason: the owner object -- a
    /// `Strand`, or a `KeyedStrands` -- may be gone by the time a parked coroutine comes back.
    class StrandCore: public IExecutor, public std::enable_shared_from_this<StrandCore>
    {
      public:
        /// @param base Where the pump runs. Must outlive every pump this strand makes.
        /// @param options The batch bound.
        /// @param family The address `ExecutorScope::family()` answers inside a task, or null.
        /// @param reclaim Whether the strand ends when it runs out of work.
        StrandCore(IExecutor& base, StrandOptions options, void const* family, StrandReclaim reclaim) noexcept
            :
            _base(base), _options(options), _family(family), _reclaim(reclaim)
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
        ///
        /// **What this throws, it throws with nothing changed**: the work is not queued, and its
        /// claim is disarmed rather than released, because the caller -- `ResumeOn::await_suspend`
        /// -- is about to resume the coroutine with the exception, and releasing the claim of a
        /// detached chain would free the frame that is about to run.
        /// @param work The coroutine to resume on the strand, and its claim on the chain root.
        /// @throws What the base's `submit` throws, and `std::bad_alloc`.
        void submit(ParkedWork work) override
        {
            auto const handle = work.resume;
            auto entry = Parked { std::move(work) };
            auto pump = std::coroutine_handle<> {};
            auto rerouted = std::optional<ParkedWork> {};
            try
            {
                auto const lock = std::scoped_lock { _mutex };
                if (_closed)
                    return; // `entry` drops the work outside the lock, freeing what nobody owns.
                if (_retired)
                    rerouted.emplace(entry.take());
                else
                    pump = enqueueLocked(entry);
            }
            catch (...)
            {
                disarm(entry);
                throw;
            }
            // Both outside the lock: a base that resumes inline would run the pump -- and with it
            // the strand's tasks -- inside it, and a reroute takes the owner's lock.
            if (rerouted)
                reroute(std::move(*rerouted));
            else if (pump)
                queueOnBase(pump, handle);
        }

        /// Queues @p work when the caller already knows this strand is open and not retired:
        /// `KeyedStrands` holds its registry's lock across the lookup and this. Throws with nothing
        /// changed, as @c submit does.
        /// @param work The coroutine to resume on the strand.
        /// @return The pump to hand to @c queueOnBase once every lock is released, or an empty
        ///         handle.
        [[nodiscard]] std::coroutine_handle<> enqueue(ParkedWork work)
        {
            auto entry = Parked { std::move(work) };
            try
            {
                auto const lock = std::scoped_lock { _mutex };
                assert(!_closed && !_retired);
                return enqueueLocked(entry);
            }
            catch (...)
            {
                disarm(entry);
                throw;
            }
        }

        /// Hands @p pump, which @c enqueue answered, to the base; where the base refuses, takes the
        /// work queued with it back out and rethrows, so the strand is where it was.
        /// @param pump The pump, published as scheduled.
        /// @param withdraw The coroutine whose submit published it, to take back on a refusal.
        void queueOnBase(std::coroutine_handle<> pump, std::coroutine_handle<> withdraw)
        {
            try
            {
                _base.submit(pump);
            }
            catch (...)
            {
                auto taken = Parked {};
                auto orphan = std::coroutine_handle<> {};
                {
                    auto const lock = std::scoped_lock { _mutex };
                    taken = _queue.remove(withdraw);
                    orphan = unscheduleLocked();
                }
                disarm(taken);
                if (orphan)
                    orphan.destroy();
                throw;
            }
        }

        /// @return Whether the calling thread is inside one of this strand's tasks, at any depth.
        [[nodiscard]] bool runningHere() const noexcept
        {
            return ExecutorScope::anyInForce(
                [this](ExecutorScope const& scope) noexcept { return &scope.executor() == this; });
        }

        /// @return How many tasks are queued and not yet running.
        [[nodiscard]] std::size_t queued() const
        {
            auto const lock = std::scoped_lock { _mutex };
            return _queue.size();
        }

        /// Closes the strand: queued work is dropped, a task running on another thread is waited
        /// for, and an idle pump is freed. Work that arrives later is dropped. Idempotent.
        void close()
        {
            auto dropped = std::vector<Parked> {};
            auto idlePump = std::coroutine_handle<> {};
            {
                auto lock = std::unique_lock { _mutex };
                _closed = true;
                dropped = _queue.takeAll();
#if CORE_CPP_ASYNC_HAS_THREADS
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

        /// Gives up @p entry's work without freeing it: its chain belongs to whoever is about to
        /// resume it with an exception.
        /// @param entry The work to give up.
        static void disarm(Parked& entry) noexcept
        {
            auto const work = entry.take();
            work.abandon.disarm();
        }

        /// Queues @p entry and decides whether the pump must be handed to the base. Holds the lock.
        ///
        /// Every step that can throw comes before anything is published: the pump's frame is made
        /// first, then the entry is queued -- moved only by a `push_back` that has already made room
        /// -- and only then is the pump published as scheduled.
        /// @param entry The work; left as it was if this throws.
        /// @return The pump to hand to the base, or an empty handle.
        [[nodiscard]] std::coroutine_handle<> enqueueLocked(Parked& entry)
        {
            auto const schedule = _phase == StrandPhase::Idle;
            if (schedule && !_pump)
                _pump = runStrandPump(shared_from_this()).handle();
            _queue.push(entry);
            if (!schedule)
                return {};
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
        /// `resume()` propagates, through the pump, to whoever resumed the pump -- except under
        /// MSVC's `cl`, where it ends the process (see `Strand`).
        /// @param anchor What a @c ResumeTarget taken inside a task copies to keep this strand
        ///        alive: the pump's own reference.
        void runBatch(std::shared_ptr<void> const& anchor)
        {
            auto const scope = ExecutorScope { *this, &anchor, _family };
            for ([[maybe_unused]] auto const turn: std::views::iota(std::size_t { 0 }, _options.batch))
            {
                auto entry = Parked {};
                {
                    auto const lock = std::scoped_lock { _mutex };
                    if (_closed || _queue.empty())
                        return;
                    entry = _queue.pop();
                }
                resumeTask(entry);
            }
        }

        /// Resumes one task.
        /// @param entry The task.
        static void resumeTask(Parked& entry)
        {
#if defined(_MSC_VER) && !defined(__clang__)
            // A compiler workaround, not platform logic: under `cl` an exception thrown out of a
            // coroutine's `resume()` and on through this frame and the pump's was measured
            // corrupting the thread's executor scope chain (cl-release; cl-debug and clang-cl are
            // fine), after which nothing about the thread can be trusted. So it ends the process
            // here, at the first frame that sees it, with a message saying why.
            try
            {
                entry.resume();
            }
            catch (...)
            {
                taskThrewUnderMsvc();
            }
#else
            entry.resume();
#endif
        }

#if defined(_MSC_VER) && !defined(__clang__)
        /// Ends the process for a task that threw out of `resume()`, under MSVC's `cl`.
        [[noreturn]] static void taskThrewUnderMsvc() noexcept
        {
            std::fputs("core::async::Strand: a task threw out of resume(); under MSVC's cl that "
                       "exception cannot cross the strand's coroutine frames safely, so the process "
                       "ends (see Strand's documentation)\n",
                       stderr);
            std::fflush(stderr);
            std::terminate();
        }
#endif

        /// The pump's suspension between turns: queued again on the base if work is waiting, idle
        /// if not, and ended if the strand closed or retired.
        class EndTurn final
        {
          public:
            /// @param strand The strand, held by the pump's frame.
            explicit EndTurn(StrandCore& strand) noexcept: _strand(&strand) {}

            /// @return False: whether to suspend is `await_suspend`'s question, asked under the lock.
            [[nodiscard]] constexpr bool await_ready() const noexcept { return false; }

            /// Decides where the pump goes next. After it has published "idle" or queued the pump,
            /// another thread may resume it, so nothing reachable through the frame is touched
            /// after either.
            /// @param pump The suspended pump.
            /// @return True to stay suspended; false to go on, into another turn or the end.
            [[nodiscard]] bool await_suspend(std::coroutine_handle<> pump) const
            {
                auto& self = *_strand;
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
                auto& base = self._base;
                lock.unlock();
                try
                {
                    base.submit(pump);
                }
                catch (...)
                {
                    // The base refused to take the pump back. Nobody else can have moved it on --
                    // only the base resumes a scheduled pump -- and there is no caller to tell, so
                    // the pump keeps the thread it is on and runs the next turn now.
                    auto const relock = std::scoped_lock { self._mutex };
                    self._phase = StrandPhase::Running;
                    return false;
                }
                return true;
            }

            void await_resume() const noexcept {}

          private:
            StrandCore* _strand;
        };

        /// A task threw out of `resume()`, which ended the pump: a new one takes over the queue.
        /// Called from the dead pump's `unhandled_exception`, on the thread it threw on.
        ///
        /// Throws with the strand in a state that restarts: if the new pump cannot be made, or the
        /// base refuses it, the strand is left idle with no pump, and the next submit makes one.
        void replaceDeadPump()
        {
            auto fresh = std::coroutine_handle<> {};
            try
            {
                fresh = runStrandPump(shared_from_this()).handle();
            }
            catch (...)
            {
                auto const lock = std::scoped_lock { _mutex };
                _pump = {};
                _phase = _closed ? StrandPhase::Exited : StrandPhase::Idle;
                notifySettledLocked();
                throw;
            }

            auto pump = std::coroutine_handle<> {};
            auto discard = std::coroutine_handle<> {};
            {
                auto const lock = std::scoped_lock { _mutex };
                if (_closed)
                {
                    _phase = StrandPhase::Exited;
                    _pump = {};
                    discard = fresh;
                }
                else
                {
                    _pump = fresh;
                    // A strand that is reclaimed when idle is queued even with nothing to do, so
                    // the new pump's first turn is what retires it.
                    if (_queue.empty() && _reclaim == StrandReclaim::Never)
                        _phase = StrandPhase::Idle;
                    else
                    {
                        _phase = StrandPhase::Scheduled;
                        pump = fresh;
                    }
                }
                notifySettledLocked();
            }
            if (discard)
                discard.destroy();
            if (!pump)
                return;
            try
            {
                _base.submit(pump);
            }
            catch (...)
            {
                auto orphan = std::coroutine_handle<> {};
                {
                    auto const lock = std::scoped_lock { _mutex };
                    orphan = unscheduleLocked();
                }
                if (orphan)
                    orphan.destroy();
                throw;
            }
        }

        /// Takes back a scheduled pump the base refused. Nothing else can have moved it on --
        /// only the base resumes a scheduled pump -- so it is idle again; or, where the strand
        /// closed meanwhile and so will never queue it again, it is handed back to be freed.
        /// Holds the lock.
        /// @return The pump to destroy once the lock is released, or an empty handle.
        [[nodiscard]] std::coroutine_handle<> unscheduleLocked() noexcept
        {
            auto orphan = std::coroutine_handle<> {};
            if (_phase == StrandPhase::Scheduled)
            {
                if (_closed)
                {
                    _phase = StrandPhase::Exited;
                    orphan = std::exchange(_pump, {});
                }
                else
                    _phase = StrandPhase::Idle;
            }
            notifySettledLocked();
            return orphan;
        }

        /// Wakes a close waiting for the pump to stop running. Holds the lock.
        void notifySettledLocked() noexcept
        {
#if CORE_CPP_ASYNC_HAS_THREADS
            _settled.notify_all();
#endif
        }

        IExecutor& _base;
        StrandOptions _options;
        void const* _family;

        /// Guards everything below.
        mutable std::mutex _mutex;
#if CORE_CPP_ASYNC_HAS_THREADS
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
        // Moved out of the parameter first, so a pump that dies by exception -- whose frame, and
        // with it the parameter, lives on until the thread's reaper frees it -- pins nothing.
        auto const self = std::move(strand);
        // Held for the pump's life and handed to every scope a task runs in, so a ResumeTarget taken
        // inside a task keeps the strand's state alive after its owner is gone.
        auto const anchor = std::shared_ptr<void> { self };
        while (self->beginTurn())
        {
            self->runBatch(anchor);
            co_await StrandCore::EndTurn { *self };
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
            // The replacement could not be made or queued, or its first turn ran inline and threw
            // too: that exception is the one that leaves, and this frame is still buried rather
            // than leaked.
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
/// **Current executor.** Inside a task, `runningHere()` is true -- also inside anything the task
/// resumes synchronously -- and `currentExecutor()` is the strand's shared state: an executor that
/// submits to this strand, and not this object's address, so that a coroutine which parks on the
/// strand and is handed back after the strand is destroyed finds that state, closed, rather than
/// freed storage. Ask `runningHere()`, never compare the pointer.
///
/// **A task that throws out of `resume()`** -- which no coroutine type of this module does, since
/// `Task` hands an exception to its awaiter and `DetachedTask` terminates -- propagates to
/// whoever resumed the strand on the base, and the strand goes on with the tasks behind it.
/// **Under MSVC's `cl` it ends the process instead**, with a message on `stderr`: an exception
/// crossing the strand's coroutine frames was measured corrupting the thread's executor scopes
/// there (`core-cpp.strand-throw-canary` watches it).
///
/// **A base that refuses** -- whose `submit` throws -- makes `submit` throw with nothing queued; a
/// refusal between two turns, which nobody could be told about, runs the next turn on the thread
/// the strand already has.
///
/// **Destruction.** Tasks still queued are dropped, never run: a chain rooted in a `DetachedTask`
/// is freed, and a coroutine a `Task` owns is left to its owner, suspended. A task running on
/// another thread is waited for (not where threads do not exist, and not from inside one of the
/// strand's own tasks, which the strand finishes once it returns). So a task may release the last
/// reference to the strand's owner: the destructor returns at once, the task runs to its end, and
/// the pump then ends without running anything more. A coroutine that parked on the strand and
/// is handed back after it is gone -- by an `AsyncQueue` push, close or stop -- is dropped, which
/// frees a chain nobody owns. The base must outlive the strand and run what the strand queued on
/// it: a pump queued there finds the strand closed and ends. **`core::net::EventLoop` as a base
/// drops what is still in its inbound queue when it is destroyed**, so a strand whose pump was
/// handed to a loop from another thread and not yet taken up by a turn leaks its state with the
/// loop; run one more turn, or destroy the strand first.
class Strand final: public IExecutor
{
  public:
    /// @param base Where the strand's tasks run. Must outlive the strand.
    /// @param options How the strand shares its base.
    explicit Strand(IExecutor& base, StrandOptions options = {}):
        _core(std::make_shared<detail::StrandCore>(base, options, nullptr, detail::StrandReclaim::Never))
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
    /// @throws What the base's `submit` throws, with nothing queued; `std::bad_alloc`.
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
