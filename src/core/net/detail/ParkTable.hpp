// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `ParkId`, `ParkEntry` and the table an @c EventLoop holds its parked work in.
///
/// A loop parks coroutines on three things — handle readiness, a deadline, and "the next turn"
/// — and every one of them has to be cancellable by name from a stop callback that may be
/// running on another thread. A pointer cannot be that name: a park is announced as closing
/// before the turn consumes the announcement, and it may be resumed and destroyed in between, so
/// a recorded pointer would dangle where a recorded id resolves to "no such park" instead.
///
/// **The id IS the generation check.** Ids are allocated from one never-reused 64-bit counter, so
/// a cancel request that arrives after its park is gone finds nothing, however many parks have
/// been made since. A table of reusable slots would need a separate generation field to tell a
/// stale request from a live one; a counter that never wraps in a session is that field, folded
/// into the name. Sixty-four bits rather than thirty-two: four billion parks is five days for a
/// server doing ten thousand a second.

#include <core/async/ParkedWork.hpp>
#include <core/net/IoBackend.hpp>
#include <core/platform/Clock.hpp>
#include <core/platform/Types.hpp>

#include <algorithm>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core::net
{

/// Names one piece of work parked on an @c EventLoop, for the loop's own bookkeeping and for
/// @c EventLoop::requestCancel, which any thread may call.
///
/// A strong struct rather than an `enum class` because it is an opaque, monotonically-allocated
/// handle id — a wide value space that never wraps in a session — and not an enumeration of named
/// cases. (`performance-enum-size` refuses a `std::uint64_t` enumeration here, and it is an error
/// in this tree.)
struct ParkId
{
    std::uint64_t value = 0; ///< The park's id; 0 means none.

    /// @return True if two ids name the same park.
    [[nodiscard]] friend constexpr bool operator==(ParkId, ParkId) noexcept = default;

    /// @return True if this id names a live park (non-zero).
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }

    /// @return The sentinel for no park, which is what a failed registration reports.
    [[nodiscard]] static constexpr ParkId invalid() noexcept { return ParkId { 0 }; }
};

} // namespace core::net

namespace std
{

/// Hash specialization so @c ParkId can key an unordered container: the loop maps a park's id to
/// the park itself. Declared HERE, between the type and its first use, because a specialization
/// that arrives after the container is instantiated is not the one the container picked up.
template <>
struct hash<core::net::ParkId>
{
    /// @param park The id to hash.
    /// @return The hash of its underlying value.
    [[nodiscard]] std::size_t operator()(core::net::ParkId park) const noexcept
    {
        return std::hash<std::uint64_t> {}(park.value);
    }
};

} // namespace std

namespace core::net
{

class EventLoop;

/// What a flow hands @c EventLoop::registerPark to park itself.
///
/// One shape for every kind of park, because the cancellation path is one path: a readiness park
/// names a handle and an interest, a timer park names a deadline, and a park that is only waiting
/// for the next turn names neither.
struct ParkEntry
{
    /// The coroutine to resume, and — where that chain belongs to nobody — the root the loop may
    /// free if it never resumes it. Always built with `core::async::detail::parkedWorkFor`, so the
    /// ownership question is answered by the parking coroutine's promise rather than by the
    /// awaitable.
    async::ParkedWork work {};

    /// The handle to watch, or @c platform::InvalidHandle for a park with no readiness.
    platform::NativeHandle handle = platform::InvalidHandle;

    HandleKind kind = DefaultHandleKind; ///< What @c handle is; ignored when there is none.
    Interest interest = Interest::None;  ///< Which readiness to watch for; `None` for a timer park.

    /// When this park is due, or nullopt for a park with no deadline.
    std::optional<platform::SteadyTimePoint> deadline;

    /// @param work The coroutine to resume, and what to free if it is never resumed.
    /// @param deadline When to resume it.
    /// @return A park waiting on a deadline and nothing else.
    [[nodiscard]] static ParkEntry onDeadline(async::ParkedWork work, platform::SteadyTimePoint deadline)
    {
        return ParkEntry { .work = std::move(work),
                           .handle = platform::InvalidHandle,
                           .kind = DefaultHandleKind,
                           .interest = Interest::None,
                           .deadline = deadline };
    }

    /// @param work The coroutine to resume, and what to free if it is never resumed.
    /// @param handle The handle to watch.
    /// @param kind What @p handle is.
    /// @param interest Which readiness to watch for.
    /// @return A park waiting on handle readiness and nothing else.
    [[nodiscard]] static ParkEntry onReadiness(async::ParkedWork work,
                                               platform::NativeHandle handle,
                                               HandleKind kind,
                                               Interest interest)
    {
        return ParkEntry { .work = std::move(work),
                           .handle = handle,
                           .kind = kind,
                           .interest = interest,
                           .deadline = std::nullopt };
    }
};

namespace detail
{

    /// One piece of work parked on a loop.
    ///
    /// Held by `unique_ptr` in the table, because @c ReadinessHandler::owner points back at this
    /// and the backend holds the handler's address: a park may not move once registered.
    struct Park
    {
        ParkId id {};                    ///< This park's identity.
        async::detail::Parked parked {}; ///< The coroutine, and what to free if it is never resumed.
        ReadinessHandler handler {};     ///< What the backend has registered, for a readiness park.
        EventLoop* loop = nullptr;       ///< The loop to enqueue onto, reached from the handler.
        platform::NativeHandle handle = platform::InvalidHandle; ///< What it is parked on.

        /// The address `byWaiter` filed this park under. Recorded rather than re-derived, because
        /// the waiter is taken out of @c parked when the park becomes ready and the index still
        /// has to be droppable afterwards.
        void* waiterKey = nullptr;

        bool attached = false; ///< Whether @c handler is registered with the backend.

        /// Whether the chain parked here belongs to the LOOP — that is, whether `ParkedWork`
        /// carried a claim on it. Recorded at registration, because the answer decides what
        /// teardown does with this park and by then the claim has been moved into @c parked where
        /// nothing can ask it. See @c EventLoop::unparkEverything.
        bool ownedByLoop = false;

        std::optional<platform::SteadyTimePoint> deadline; ///< Set while this park waits on one.
        std::uint64_t sequence = 0;                        ///< Tie-break so equal deadlines fire FIFO.
    };

    /// The parks one loop holds, by id, with the reverse indices every resolution path needs.
    ///
    /// Three questions are asked of it, and each has its own index because each is on a path that
    /// must not scan: a backend's dispatch asks by @c ParkId, a stop callback asks by coroutine
    /// handle, and a closing descriptor asks by native handle — and a descriptor carries up to two
    /// parks at once, a reader beside a writer, so that last one is a multimap.
    class ParkTable
    {
      public:
        /// Files @p park and gives it an id.
        /// @param park The park to hold; must be non-null and not yet filed.
        /// @return Its id, which is never zero and never reused.
        [[nodiscard]] ParkId add(std::unique_ptr<Park> park)
        {
            auto const id = ParkId { ++_nextId };
            park->id = id;
            park->sequence = _nextSequence++;
            if (auto const waiter = park->parked.handle())
            {
                park->waiterKey = waiter.address();
                _byWaiter.emplace(park->waiterKey, id);
            }
            if (park->handle != platform::InvalidHandle)
                _byHandle.emplace(park->handle, id);
            if (park->deadline.has_value())
            {
                _timers.push_back(
                    TimerSlot { .deadline = *park->deadline, .sequence = park->sequence, .id = id });
                std::ranges::push_heap(_timers, soonestFirst);
                ++_liveTimers;
            }
            _parks.emplace(id, std::move(park));
            return id;
        }

        /// @param id The park to look up.
        /// @return The park, or null if it is no longer here — which is what a cancel request for
        ///         a park that has already resumed resolves to.
        [[nodiscard]] Park* find(ParkId id) noexcept
        {
            auto const found = _parks.find(id);
            return found == _parks.end() ? nullptr : found->second.get();
        }

        /// @param waiter The parked coroutine.
        /// @return The park holding it, or @c ParkId::invalid().
        [[nodiscard]] ParkId byWaiter(std::coroutine_handle<> waiter) const noexcept
        {
            if (!waiter)
                return ParkId::invalid();
            auto const found = _byWaiter.find(waiter.address());
            return found == _byWaiter.end() ? ParkId::invalid() : found->second;
        }

        /// @param handle The native handle.
        /// @return Every park on it. A copy rather than a range, because the caller detaches and
        ///         destroys parks while walking it, which would invalidate a live range.
        [[nodiscard]] std::vector<ParkId> parksOn(platform::NativeHandle handle) const
        {
            auto found = std::vector<ParkId> {};
            auto const [first, last] = _byHandle.equal_range(handle);
            for (auto const& entry: std::ranges::subrange(first, last))
                found.push_back(entry.second);
            return found;
        }

        /// Takes the coroutine out of @p id, leaving the park in place.
        ///
        /// The park survives because its @c ReadinessHandler is still registered with the backend
        /// and the awaiter's own resume is what unregisters it; what must go now are the reverse
        /// indices, or a cancel or a closing descriptor arriving in the same turn would find a
        /// park whose waiter is already queued and queue it twice.
        /// @param id The park whose waiter to take.
        /// @return What was parked, or empty work if this park is gone or already taken.
        [[nodiscard]] async::ParkedWork takeWaiter(ParkId id) noexcept
        {
            auto* const park = find(id);
            if (park == nullptr || !park->parked)
                return {};
            auto work = park->parked.take();
            dropIndices(*park);
            return work;
        }

        /// Removes @p id from every index and hands the park back.
        /// @param id The park to take.
        /// @return The park, or null if it was not here. The deadline heap keeps a stale slot,
        ///         which @c pruneTimers drops lazily — an O(n) erase-and-reheap per cancellation
        ///         is how a loop with many deadlines becomes quadratic.
        [[nodiscard]] std::unique_ptr<Park> take(ParkId id) noexcept
        {
            auto const found = _parks.find(id);
            if (found == _parks.end())
                return {};
            auto park = std::move(found->second);
            _parks.erase(found);
            dropIndices(*park);
            return park;
        }

        /// Empties the table and hands back everything that was in it.
        /// @return Every park, in no particular order.
        [[nodiscard]] std::vector<std::unique_ptr<Park>> takeAll()
        {
            auto taken = std::vector<std::unique_ptr<Park>> {};
            taken.reserve(_parks.size());
            for (auto& entry: _parks)
                taken.push_back(std::move(entry.second));
            _parks.clear();
            _byWaiter.clear();
            _byHandle.clear();
            _timers.clear();
            _liveTimers = 0;
            return taken;
        }

        /// @return Every park's id, in no particular order. A copy, because the caller queues and
        ///         detaches while walking it and that mutates the table.
        [[nodiscard]] std::vector<ParkId> ids() const
        {
            auto found = std::vector<ParkId> {};
            found.reserve(_parks.size());
            for (auto const& entry: _parks)
                found.push_back(entry.first);
            return found;
        }

        /// @return How many parks are held, of every kind.
        [[nodiscard]] std::size_t size() const noexcept { return _parks.size(); }

        /// @return How many of them are waiting on a deadline.
        [[nodiscard]] std::size_t timerCount() const noexcept { return _liveTimers; }

        /// @return How many of them are waiting on handle readiness.
        [[nodiscard]] std::size_t readinessCount() const noexcept { return _byHandle.size(); }

        /// @return The soonest deadline any park is waiting on, or nullopt if none is.
        [[nodiscard]] std::optional<platform::SteadyTimePoint> nextDeadline() noexcept
        {
            pruneTimers();
            if (_timers.empty())
                return std::nullopt;
            return _timers.front().deadline;
        }

        /// Reports every park whose deadline has been reached, soonest first and FIFO on a tie.
        /// @param now The instant to measure against.
        /// @return Their ids, in firing order. The parks themselves stay in the table: the caller
        ///         queues their waiters, and the awaiter's own resume is what unregisters them.
        [[nodiscard]] std::vector<ParkId> takeExpired(platform::SteadyTimePoint now)
        {
            auto due = std::vector<ParkId> {};
            pruneTimers();
            while (!_timers.empty() && _timers.front().deadline <= now)
            {
                std::ranges::pop_heap(_timers, soonestFirst);
                auto const slot = _timers.back();
                _timers.pop_back();
                if (auto* const park = find(slot.id); park != nullptr && park->deadline.has_value())
                {
                    // Disarmed as it fires: a park is due once, and leaving the deadline set would
                    // have `nextDeadline()` keep asking for a wait of zero forever.
                    park->deadline.reset();
                    --_liveTimers;
                    due.push_back(slot.id);
                }
                pruneTimers();
            }
            return due;
        }

      private:
        /// One entry of the deadline heap. It names a park rather than owning one, so a park
        /// cancelled through any other path leaves a slot here that @c pruneTimers drops.
        struct TimerSlot
        {
            platform::SteadyTimePoint deadline {};
            std::uint64_t sequence = 0;
            ParkId id {};
        };

        /// Min-heap comparator over the standard max-heap: soonest deadline at the root, and FIFO
        /// among equal deadlines, which is what makes two timers armed for the same instant fire
        /// in the order they were armed.
        /// @param a The first slot.
        /// @param b The second slot.
        /// @return True if @p a should sort after @p b.
        [[nodiscard]] static bool soonestFirst(TimerSlot const& a, TimerSlot const& b) noexcept
        {
            if (a.deadline != b.deadline)
                return a.deadline > b.deadline;
            return a.sequence > b.sequence;
        }

        /// Drops heap slots naming parks that are gone or no longer waiting on a deadline.
        void pruneTimers() noexcept
        {
            while (!_timers.empty())
            {
                auto const& slot = _timers.front();
                auto const found = _parks.find(slot.id);
                if (found != _parks.end() && found->second->deadline.has_value()
                    && found->second->sequence == slot.sequence)
                    return;
                std::ranges::pop_heap(_timers, soonestFirst);
                _timers.pop_back();
            }
        }

        /// Forgets @p park in every index but the park map itself, and is idempotent: the waiter
        /// is taken one turn and the park itself another, and both paths come through here.
        /// @param park The park being removed from the indices.
        void dropIndices(Park& park) noexcept
        {
            if (park.waiterKey != nullptr)
            {
                _byWaiter.erase(park.waiterKey);
                park.waiterKey = nullptr;
            }
            if (park.handle != platform::InvalidHandle)
            {
                // Erase this park alone: a descriptor may carry a second one — a reader beside a
                // writer — and erasing by key would silently drop that one too.
                auto const [first, last] = _byHandle.equal_range(park.handle);
                auto const index = std::ranges::find_if(
                    first, last, [id = park.id](auto const& candidate) { return candidate.second == id; });
                if (index != last)
                    _byHandle.erase(index);
            }
            if (park.deadline.has_value())
            {
                park.deadline.reset();
                --_liveTimers;
            }
        }

        std::unordered_map<ParkId, std::unique_ptr<Park>> _parks;
        std::unordered_map<void*, ParkId> _byWaiter;
        std::unordered_multimap<platform::NativeHandle, ParkId> _byHandle;
        std::vector<TimerSlot> _timers;
        std::size_t _liveTimers = 0;
        std::uint64_t _nextId = 0;
        std::uint64_t _nextSequence = 0;
    };

} // namespace detail

} // namespace core::net
