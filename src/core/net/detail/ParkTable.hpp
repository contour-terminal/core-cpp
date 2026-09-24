// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `ParkEntry` and the table an @c EventLoop holds its parked work in; `ParkId` is in
/// `detail/ParkId.hpp`.
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
#include <core/net/detail/ParkId.hpp>
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

class EventLoop;

/// What @c EventLoop::addTimer runs when its deadline arrives.
///
/// A function pointer and a `void*` rather than a `std::function`, matching @c ReadinessCallback:
/// it is the house shape for a loop-side callback, and it allocates nothing on a path a server
/// runs once per request. Invoked on the loop's thread, in turn step 2, at most once.
///
/// **Not `noexcept`**, because the coroutine resumptions it is queued beside are not either: an
/// exception leaving one propagates out of the turn and out of `run()`. The exception is a
/// host-driven loop, whose pump is `noexcept` — there an escaping exception terminates, which is
/// the same trade @c async::DetachedTask makes and for the same reason.
using TimerCallback = void (*)(void* state);

/// Why a frameless park's owner is being called.
///
/// A park with a coroutine behind it needs no such thing: readiness resumes it on its normal path
/// and a cancel resumes it into an `await_resume` that throws, so the two are told apart by what
/// the frame observes. A FRAMELESS park has no frame to observe anything, so the reason has to be
/// handed to the callback — and it is the whole reason the callback can be one function rather
/// than three.
enum class ParkWake : std::uint8_t
{
    /// The backend reported the readiness this park watches. For a socket that means *try the
    /// syscall again*; it does NOT mean the operation can complete, because a level-triggered
    /// poller may report a readable descriptor whose `recv` still answers `EAGAIN`.
    Ready,

    /// @c EventLoop::requestCancel named this park — the awaiting flow's stop token was stopped,
    /// possibly from another thread. The owner settles its operation and stops watching.
    Cancelled,

    /// The handle was announced closing under @c FdWakePolicy::Cancel, so the owner is going away
    /// and must not be resumed on its normal path. The registration is already detached.
    Abandoned,
};

/// What a frameless readiness park's owner is called with when its handle wakes.
///
/// A function pointer and a `void*` rather than a `std::function`, matching @c TimerCallback and
/// @c ReadinessCallback: it is the house shape for a loop-side callback and it allocates nothing
/// on a path a server runs once per read. Invoked on the loop's thread, in turn step 2, **as often
/// as the handle wakes** — unlike @c TimerCallback, which is invoked at most once, because a
/// readiness park survives its own dispatch and only its owner retires it.
///
/// **This is what makes a socket operation frame-free.** `ISocket::write` writes every byte of its
/// buffer, so it is inherently multi-step — send, partial, wait writable, send more — and a
/// `co_await` expression suspends exactly once, so `await_resume` cannot re-park. The retry loop
/// therefore cannot live in the awaiting coroutine and has to run where the readiness is
/// delivered. That is here.
///
/// Not `noexcept`, for the reason @c TimerCallback gives: it is queued beside coroutine
/// resumptions, which are not either.
using ReadyCallback = void (*)(void* state, ParkWake wake);

/// Names one callback timer armed on an @c EventLoop.
///
/// **It is a park, and this is a distinct type over the same table.** A callback timer is filed in
/// the park table beside the coroutine deadlines, so it inherits the generation check for free —
/// ids come from the one never-reused counter, so a cancel naming a timer that has already run
/// resolves to nothing. What the wrapper buys over using @c ParkId directly is that
/// @c EventLoop::cancelTimer and @c EventLoop::requestCancel cannot be handed each other's
/// arguments: the first retires a callback, the second unwinds a coroutine, and only one of them
/// is meaningful for any given id.
struct TimerId
{
    ParkId park {}; ///< The park this timer is filed as; @c ParkId::invalid() means none.

    /// @return True if two ids name the same timer.
    [[nodiscard]] friend constexpr bool operator==(TimerId, TimerId) noexcept = default;

    /// @return True if this id names a timer that was armed (non-zero).
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return static_cast<bool>(park); }

    /// @return The sentinel for no timer, which is what a refused arming reports.
    [[nodiscard]] static constexpr TimerId invalid() noexcept { return TimerId {}; }
};

/// How long the backend registration behind a readiness park lives.
///
/// **A registration per park is two kernel calls per park**, and on a socket that parks once per
/// request that is the difference between a loop and a reactor. epoll pays an `EPOLL_CTL_ADD` to
/// file a park and an `EPOLL_CTL_DEL` to take it -- the expensive pair, which allocates and frees
/// the kernel's own entry and hooks and unhooks the socket's wait queue each time.
///
/// So a caller that OWNS a handle for its whole life, and parks on it over and over, may ask for
/// one registration for that life instead. The loop then attaches the handle once, the first time
/// it is parked on, and a park only ever changes what the registration is armed for -- which in
/// the steady state of a request/response socket is nothing at all.
enum class RegistrationLifetime : std::uint8_t
{
    /// Attached when the park is filed and detached when it is taken. The default, because it asks
    /// nothing of the caller: the registration cannot outlive the park that made it.
    PerPark,

    /// Kept by the loop from the first park on the handle until @c EventLoop::notifyHandleClosing
    /// names it, and shared by every park on the handle that asks for it: a reader and a writer
    /// are two slots on one registration, not two registrations.
    ///
    /// **The caller promises to announce the close.** Nothing else can end the registration,
    /// because nothing else can tell the loop that a descriptor number is about to mean something
    /// else: epoll forgets a closed descriptor silently, and a registration the loop still believed
    /// in would then be "armed" for a socket the kernel has never heard of -- a park on the next
    /// socket to get that number would wait for ever. Every @c PosixSocket close announces it
    /// already, which is why that is the caller this exists for.
    UntilClosed,
};

/// What a flow hands @c EventLoop::registerPark to park itself.
///
/// One shape for every kind of park, because the cancellation path is one path: a readiness park
/// names a handle and an interest, a timer park names a deadline, a callback park names a deadline
/// and what to call, and a park that is only waiting for the next turn names neither.
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

    /// What to call when the deadline arrives, for a park with no coroutine behind it; null for
    /// every other kind. Exactly one of @c work, this and @c onReady is set.
    TimerCallback onExpired = nullptr;

    /// What to call when the handle wakes, for a readiness park with no coroutine behind it; null
    /// for every other kind. Exactly one of @c work, @c onExpired and this is set.
    ReadyCallback onReady = nullptr;

    /// The opaque pointer handed to @c onExpired or @c onReady. Borrowed: it must outlive the
    /// park, which for a socket operation means the socket outlives its own registration.
    void* callbackState = nullptr;

    /// How long the backend registration behind a readiness park lives; ignored when there is no
    /// handle. See @c RegistrationLifetime for the promise @c RegistrationLifetime::UntilClosed
    /// asks of the caller.
    RegistrationLifetime lifetime = RegistrationLifetime::PerPark;

    /// @param work The coroutine to resume, and what to free if it is never resumed.
    /// @param deadline When to resume it.
    /// @return A park waiting on a deadline and nothing else.
    [[nodiscard]] static ParkEntry onDeadline(async::ParkedWork work, platform::SteadyTimePoint deadline)
    {
        return ParkEntry { .work = std::move(work),
                           .handle = platform::InvalidHandle,
                           .kind = DefaultHandleKind,
                           .interest = Interest::None,
                           .deadline = deadline,
                           .onExpired = nullptr,
                           .onReady = nullptr,
                           .callbackState = nullptr,
                           .lifetime = RegistrationLifetime::PerPark };
    }

    /// @param onExpired What to call when @p deadline arrives; must not be null.
    /// @param state The opaque pointer handed to @p onExpired; must outlive the park.
    /// @param deadline When to call it.
    /// @return A park waiting on a deadline with no coroutine behind it.
    [[nodiscard]] static ParkEntry onCallback(TimerCallback onExpired,
                                              void* state,
                                              platform::SteadyTimePoint deadline)
    {
        return ParkEntry { .work = {},
                           .handle = platform::InvalidHandle,
                           .kind = DefaultHandleKind,
                           .interest = Interest::None,
                           .deadline = deadline,
                           .onExpired = onExpired,
                           .onReady = nullptr,
                           .callbackState = state,
                           .lifetime = RegistrationLifetime::PerPark };
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
                           .deadline = std::nullopt,
                           .onExpired = nullptr,
                           .onReady = nullptr,
                           .callbackState = nullptr,
                           .lifetime = RegistrationLifetime::PerPark };
    }

    /// @param onReady What to call each time @p handle wakes; must not be null.
    /// @param state The opaque pointer handed to @p onReady; must outlive the park.
    /// @param handle The handle to watch.
    /// @param kind What @p handle is.
    /// @param interest Which readiness to watch for.
    /// @param lifetime How long the registration behind it lives; see @c RegistrationLifetime.
    /// @return A park waiting on handle readiness with no coroutine behind it.
    [[nodiscard]] static ParkEntry onReadyCallback(
        ReadyCallback onReady,
        void* state,
        platform::NativeHandle handle,
        HandleKind kind,
        Interest interest,
        RegistrationLifetime lifetime = RegistrationLifetime::PerPark)
    {
        return ParkEntry { .work = {},
                           .handle = handle,
                           .kind = kind,
                           .interest = interest,
                           .deadline = std::nullopt,
                           .onExpired = nullptr,
                           .onReady = onReady,
                           .callbackState = state,
                           .lifetime = lifetime };
    }
};

namespace detail
{

    struct HandleWatch;

    /// One piece of work parked on a loop.
    ///
    /// Held by `unique_ptr` in the table, because @c ReadinessHandler::owner points back at this
    /// and the backend holds the handler's address: a park may not move once registered.
    ///
    /// **A field added here is reset in @c ParkTable::recycle as well**, which clears a park field
    /// by field rather than assigning a fresh one; `ParkTable_test.cpp` sets every field and checks
    /// that a recycled park holds none of them.
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

        /// The @c HandleWatch this park holds a slot on, rather than a registration of its own
        /// (@c RegistrationLifetime::UntilClosed), or null. Such a park never sets @c attached: the
        /// registration is the watch's, and taking the park only frees the slot. The loop clears it
        /// in every park the watch names before the watch goes, so it never dangles.
        HandleWatch* watch = nullptr;

        /// Whether @c ParkTable filed this park in its handle index. A watched park is not: its
        /// watch's slots already name it, and a closing handle asks the watch.
        bool handleIndexed = false;

        /// What to call when this park's deadline arrives, for a callback timer; null for a park
        /// with a coroutine behind it. **This is the whole of how a frameless timer joins the
        /// table**: a callback park is a park whose @c parked is empty and whose deadline names
        /// this instead, so the heap, the sequence numbers, the ids and the turn step are shared
        /// rather than duplicated. See @c EventLoop::addTimer.
        TimerCallback onExpired = nullptr;

        /// What to call each time this park's handle wakes, for a frameless readiness park; null
        /// for a park with a coroutine behind it. **This is the whole of how a frame-free socket
        /// operation joins the table**: the retry loop of a multi-step read or write runs here,
        /// where the readiness arrives, because the awaiting coroutine suspends only once and
        /// therefore cannot re-park itself. Unlike @c onExpired, this park SURVIVES its own
        /// dispatch — only its owner retires it. See @c EventLoop::registerPark.
        ReadyCallback onReady = nullptr;

        /// The opaque pointer handed to @c onExpired or @c onReady. Borrowed.
        void* callbackState = nullptr;

        /// Whether the chain parked here belongs to the LOOP — that is, whether `ParkedWork`
        /// carried a claim on it. Recorded at registration, because the answer decides what
        /// teardown does with this park and by then the claim has been moved into @c parked where
        /// nothing can ask it. See @c EventLoop::unparkEverything.
        bool ownedByLoop = false;

        std::optional<platform::SteadyTimePoint> deadline; ///< Set while this park waits on one.
        std::uint64_t sequence = 0;                        ///< Tie-break so equal deadlines fire FIFO.
    };

    /// The one backend registration a loop keeps for a handle parked on with
    /// @c RegistrationLifetime::UntilClosed, from the first such park until the handle is announced
    /// closing.
    ///
    /// **It holds ids, never parks.** A park comes and goes once per operation while this stays, so
    /// the slots name parks the way every other long-lived reference into the table does: an id
    /// that resolves to nothing once its park is gone. One slot per direction, which is the socket
    /// contract's own shape -- one read operation and one write operation per socket.
    ///
    /// **What it is armed for may be MORE than its slots ask for, and only in one direction.**
    /// Readability stays armed after the read that wanted it completes, because the next thing a
    /// request/response socket does is read again, and re-arming it would be the very kernel call
    /// this type exists to save. A report that arrives with no park to take it is then not lost
    /// work but a registration to narrow, and the loop narrows it once that wait returns.
    /// Writability is dropped the moment its write is taken instead: a socket with room in its send
    /// buffer is writable on every wait, so leaving it armed would buy a spurious report per turn.
    struct HandleWatch
    {
        ReadinessHandler handler {};     ///< The registration; its address is its identity.
        EventLoop* loop = nullptr;       ///< The loop to enqueue onto, reached from the handler.
        Interest armed = Interest::None; ///< What the backend is armed for right now.
        ParkId reader {};                ///< The park waiting to read, or none.
        ParkId writer {};                ///< The park waiting to write, or none.
        bool narrowQueued = false;       ///< Whether a wait has already asked for it to be narrowed.
    };

    /// The parks one loop holds, keyed by id: an open-addressing table with linear probing.
    ///
    /// **A park is filed and taken once per parked operation, so this is a hot path, and the node
    /// container it replaces was the cost.** `std::unordered_map` allocates a node per insert and
    /// frees it per erase and chases a pointer per lookup; on a request/response socket that was two
    /// heap operations and three hashed lookups per request in the table alone. Here an entry lives
    /// in one flat array, a lookup is a multiply and usually one probe, and nothing is allocated
    /// until the table grows. The ids are unchanged -- still the one never-reused 64-bit counter,
    /// still the generation check -- because the key is the id itself, not a slot index.
    ///
    /// Deletion shifts the following run back rather than leaving a tombstone, so a long-lived
    /// loop's probe lengths depend on how many parks are live, never on how many have come and gone.
    class ParkMap
    {
      public:
        /// @param id The park to find.
        /// @return It, or null.
        [[nodiscard]] Park* find(ParkId id) const noexcept
        {
            if (_size == 0)
                return nullptr;
            auto index = home(id.value);
            while (_entries[index].key != 0)
            {
                if (_entries[index].key == id.value)
                    return _entries[index].park.get();
                index = (index + 1) & mask();
            }
            return nullptr;
        }

        /// Files @p park under @p id, which must not be present.
        /// @param id The key; never zero.
        /// @param park What to hold.
        void insert(ParkId id, std::unique_ptr<Park> park)
        {
            if ((_size + 1) * 2 > _entries.size())
                grow();
            auto index = home(id.value);
            while (_entries[index].key != 0)
                index = (index + 1) & mask();
            _entries[index] = Entry { .key = id.value, .park = std::move(park) };
            ++_size;
        }

        /// Removes @p id and hands its park back.
        /// @param id The park to take.
        /// @return The park, or null if it was not here.
        [[nodiscard]] std::unique_ptr<Park> erase(ParkId id) noexcept
        {
            // Zero is the empty-slot marker, so it would "match" the first free slot on the probe
            // and the removal would shift live entries out of their runs. Callers do pass it:
            // `unregisterPark(ParkId::invalid())` is documented as a no-op.
            if (_size == 0 || !id)
                return {};
            auto index = home(id.value);
            while (_entries[index].key != id.value)
            {
                if (_entries[index].key == 0)
                    return {};
                index = (index + 1) & mask();
            }
            auto park = std::move(_entries[index].park);
            --_size;
            // Backward shift: every entry after the hole whose home is not strictly inside the gap
            // moves into it, so no probe sequence is ever cut short by the removal.
            auto hole = index;
            auto next = (hole + 1) & mask();
            while (_entries[next].key != 0)
            {
                auto const wanted = home(_entries[next].key);
                auto const distanceToNext = (next - wanted) & mask();
                auto const distanceToHole = (hole - wanted) & mask();
                if (distanceToHole <= distanceToNext)
                {
                    _entries[hole] = std::move(_entries[next]);
                    hole = next;
                }
                next = (next + 1) & mask();
            }
            _entries[hole] = Entry {};
            return park;
        }

        /// @return How many parks are held.
        [[nodiscard]] std::size_t size() const noexcept { return _size; }

        /// Calls @p visit with every held id and park, in no particular order. @p visit must not
        /// insert or erase.
        /// @param visit What to call, as `visit(ParkId, std::unique_ptr<Park>&)`.
        template <typename Visit>
        void forEach(Visit const& visit)
        {
            for (auto& entry: _entries)
                if (entry.key != 0)
                    visit(ParkId { entry.key }, entry.park);
        }

        /// Calls @p visit with every held id, in no particular order.
        /// @param visit What to call, as `visit(ParkId)`.
        template <typename Visit>
        void forEachId(Visit const& visit) const
        {
            for (auto const& entry: _entries)
                if (entry.key != 0)
                    visit(ParkId { entry.key });
        }

        /// Forgets every entry, keeping the capacity.
        void clear() noexcept
        {
            for (auto& entry: _entries)
                entry = Entry {};
            _size = 0;
        }

      private:
        /// One slot of the table: a key of zero is an empty slot, which the id space never uses.
        struct Entry
        {
            std::uint64_t key = 0;
            std::unique_ptr<Park> park;
        };

        /// Where @p key's probe sequence starts: a multiplicative (Fibonacci) hash, so the
        /// sequential ids a loop hands out spread over the table rather than filling one run. The
        /// product's upper half, masked to the table, rather than its top bits shifted down by a
        /// width that depends on the table: the shift is a constant, so it is never the full 64
        /// bits an empty table would ask for, and nothing here depends on `std::size_t` being 64
        /// bits wide, which it is not under WebAssembly.
        [[nodiscard]] std::size_t home(std::uint64_t key) const noexcept
        {
            return static_cast<std::size_t>((key * 0x9E37'79B9'7F4A'7C15ULL) >> 32U) & mask();
        }

        [[nodiscard]] std::size_t mask() const noexcept { return _entries.size() - 1; }

        /// Doubles the table and refiles every entry.
        void grow()
        {
            auto old =
                std::exchange(_entries, std::vector<Entry>(_entries.empty() ? 16 : _entries.size() * 2));
            _size = 0;
            for (auto& entry: old)
                if (entry.key != 0)
                    insert(ParkId { entry.key }, std::move(entry.park));
        }

        std::vector<Entry> _entries;
        std::size_t _size = 0;
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
        /// @return A park to fill in: one recycled by @c recycle, or a new one. Every field holds
        ///         its default.
        [[nodiscard]] std::unique_ptr<Park> acquire()
        {
            if (_spare.empty())
                return std::make_unique<Park>();
            auto park = std::move(_spare.back());
            _spare.pop_back();
            return park;
        }

        /// Keeps @p park for the next @c acquire rather than freeing it -- a park is made and
        /// dropped once per parked operation, and on a request/response socket that was a heap
        /// allocation and a free per request.
        /// @param park A park just taken out of the table, with nothing left parked in it: it is
        ///        reset here, and resetting a park that still held a chain would free the chain.
        void recycle(std::unique_ptr<Park> park) noexcept
        {
            if (!park || park->parked || _spare.size() >= MaxSpareParks)
                return;
            // Field by field rather than `*park = Park {}`: assigning a whole park moves an empty
            // `Parked` over an empty `Parked`, and that was a visible share of the once-per-operation
            // path for nothing. `parked` is empty already -- checked above -- and `take` has cleared
            // the indices' own fields.
            park->id = ParkId::invalid();
            park->handler = ReadinessHandler {};
            park->loop = nullptr;
            park->handle = platform::InvalidHandle;
            park->waiterKey = nullptr;
            park->attached = false;
            park->watch = nullptr;
            park->handleIndexed = false;
            park->onExpired = nullptr;
            park->onReady = nullptr;
            park->callbackState = nullptr;
            park->ownedByLoop = false;
            park->deadline.reset();
            park->sequence = 0;
            _spare.push_back(std::move(park));
        }

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
            {
                ++_readiness;
                if (park->watch == nullptr)
                {
                    _byHandle.emplace(park->handle, id);
                    park->handleIndexed = true;
                }
            }
            if (park->deadline.has_value())
            {
                _timers.push_back(
                    TimerSlot { .deadline = *park->deadline, .sequence = park->sequence, .id = id });
                std::ranges::push_heap(_timers, soonestFirst);
                ++_liveTimers;
            }
            _parks.insert(id, std::move(park));
            return id;
        }

        /// @param id The park to look up.
        /// @return The park, or null if it is no longer here — which is what a cancel request for
        ///         a park that has already resumed resolves to.
        [[nodiscard]] Park* find(ParkId id) noexcept { return _parks.find(id); }

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
        /// @return Every park on it that has a registration of its own -- a WATCHED park is named
        ///         by its handle's watch instead. A copy rather than a range, because the caller detaches and
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
        /// and the awaiter's own resume is what unregisters it. What must go now is the WAITER
        /// index, or a cancel arriving in the same turn would hand back work that is already
        /// queued. The handle index stays, so a descriptor closing before the resumption can
        /// still find this park and detach it while the descriptor is valid.
        /// @param id The park whose waiter to take.
        /// @return What was parked, or empty work if this park is gone or already taken.
        [[nodiscard]] async::ParkedWork takeWaiter(ParkId id) noexcept
        {
            auto* const park = find(id);
            if (park == nullptr || !park->parked)
                return {};
            auto work = park->parked.take();
            dropWaiterIndices(*park);
            return work;
        }

        /// Removes @p id from every index and hands the park back.
        /// @param id The park to take.
        /// @return The park, or null if it was not here. The deadline heap keeps a stale slot,
        ///         which @c pruneTimers drops lazily — an O(n) erase-and-reheap per cancellation
        ///         is how a loop with many deadlines becomes quadratic.
        [[nodiscard]] std::unique_ptr<Park> take(ParkId id) noexcept
        {
            auto park = _parks.erase(id);
            if (!park)
                return {};
            dropWaiterIndices(*park);
            dropHandleIndex(*park);
            return park;
        }

        /// Empties the table and hands back everything that was in it.
        /// @return Every park, in no particular order.
        [[nodiscard]] std::vector<std::unique_ptr<Park>> takeAll()
        {
            auto taken = std::vector<std::unique_ptr<Park>> {};
            taken.reserve(_parks.size());
            _parks.forEach(
                [&taken](ParkId, std::unique_ptr<Park>& park) { taken.push_back(std::move(park)); });
            _parks.clear();
            _byWaiter.clear();
            _byHandle.clear();
            _readiness = 0;
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
            _parks.forEachId([&found](ParkId id) { found.push_back(id); });
            return found;
        }

        /// @return How many parks are held, of every kind.
        [[nodiscard]] std::size_t size() const noexcept { return _parks.size(); }

        /// @return How many of them are waiting on a deadline.
        [[nodiscard]] std::size_t timerCount() const noexcept { return _liveTimers; }

        /// @return How many slots the deadline heap holds, live and stale together.
        ///
        /// A diagnostic, and it exists because the difference from @c timerCount is the whole of
        /// what lazy pruning costs: a cancelled deadline leaves its slot until the heap ROOT
        /// reaches it. `Timers_test.cpp`'s *Lazy timer pruning is bounded by the deadlines armed
        /// behind the live root* is the measurement, and without this it could not be made.
        [[nodiscard]] std::size_t timerSlotCount() const noexcept { return _timers.size(); }

        /// @return How many parks still hold a handle key — INCLUDING one whose waiter has been
        ///         queued and not yet resumed, which was the count's defect before.
        ///
        /// Not "one per backend registration", which it used to say and does not mean:
        /// `notifyHandleClosing`, `resolveCancel` and `unparkEverything` each detach a park and
        /// clear `Park::attached` while leaving it in the table, so in those windows this exceeds
        /// what the backend holds. `Park::attached` is the attachment; this is the key. The
        /// over-count is the safe direction — a "no registration leaked" assertion now fails
        /// loudly rather than reading zero while a registration is live.
        [[nodiscard]] std::size_t readinessCount() const noexcept { return _readiness; }

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
        ///
        /// **Only from the ROOT, and it stops at the first live one.** A stale slot deeper in the
        /// heap survives until the root reaches it, which is what bounds the heap by deadlines
        /// ever armed rather than by deadlines live. The alternative — erase-and-reheap per
        /// cancellation — is O(n) per cancel, and is what makes a loop with many deadlines
        /// quadratic. A caller that arms and cancels far more than it fires pays memory for that
        /// choice, which is a measurement worth taking before changing it.
        void pruneTimers() noexcept
        {
            while (!_timers.empty())
            {
                auto const& slot = _timers.front();
                auto const* const found = _parks.find(slot.id);
                if (found != nullptr && found->deadline.has_value() && found->sequence == slot.sequence)
                    return;
                std::ranges::pop_heap(_timers, soonestFirst);
                _timers.pop_back();
            }
        }

        /// Forgets what @p park was WAITING on: its waiter key, and its claim on the deadline
        /// heap. Idempotent, because the waiter is taken one turn and the park itself another.
        ///
        /// Deliberately not the handle index. A park whose waiter has been queued is still
        /// REGISTERED with the backend — the awaiter's own resume is what detaches it, a full turn
        /// later — and @c parksOn is how a closing descriptor finds it in between. Erasing the
        /// handle here made that window invisible to @c EventLoop::notifyHandleClosing, which is
        /// the one thing that exists to close it.
        /// @param park The park whose waiter is being taken.
        void dropWaiterIndices(Park& park) noexcept
        {
            if (park.waiterKey != nullptr)
            {
                _byWaiter.erase(park.waiterKey);
                park.waiterKey = nullptr;
            }
            if (park.deadline.has_value())
            {
                park.deadline.reset();
                --_liveTimers;
            }
        }

        /// Forgets @p park's readiness registration, which only @c take() may do: it is the one
        /// path after which nothing is registered with the backend on this park's account.
        /// @param park The park being removed from the table.
        void dropHandleIndex(Park& park) noexcept
        {
            if (park.handle == platform::InvalidHandle)
                return;
            --_readiness;
            if (!park.handleIndexed)
                return;
            park.handleIndexed = false;
            // Erase this park alone: a descriptor may carry a second one — a reader beside a
            // writer — and erasing by key would silently drop that one too.
            auto const [first, last] = _byHandle.equal_range(park.handle);
            auto const index = std::ranges::find_if(
                first, last, [id = park.id](auto const& candidate) { return candidate.second == id; });
            if (index != last)
                _byHandle.erase(index);
        }

        /// How many taken parks @c recycle keeps for reuse. Enough for a loop's steady churn -- a
        /// park is taken and another filed per operation -- without holding a burst's worth of
        /// memory for ever.
        static constexpr std::size_t MaxSpareParks = 64;

        ParkMap _parks;
        std::vector<std::unique_ptr<Park>> _spare;
        std::size_t _readiness = 0; ///< Parks holding a handle key, indexed or watched.
        std::unordered_map<void*, ParkId> _byWaiter;
        std::unordered_multimap<platform::NativeHandle, ParkId> _byHandle;
        std::vector<TimerSlot> _timers;
        std::size_t _liveTimers = 0;
        std::uint64_t _nextId = 0;
        std::uint64_t _nextSequence = 0;
    };

} // namespace detail

} // namespace core::net
