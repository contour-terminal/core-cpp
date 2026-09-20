// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `EventLoop` — the single-threaded coroutine driver for the async socket layer.
///
/// The loop owns the one blocking primitive (an injected @c IoBackend) and
/// multiplexes handle readiness and timers over it. Flows (`async::Task`s) suspend on
/// the awaitables the loop hands out — `waitReadable()`, `waitWritable()`,
/// `delay()` — and the pump resumes them when what they wait on is ready.
///
/// **Backends dispatch, the loop resumes.** A backend's wait invokes the callbacks on
/// the @c ReadinessHandler each park registers, and those callbacks only ENQUEUE;
/// every resumption happens in @c drainReadyQueue, on the loop thread, after the wait
/// has returned. @c drainReadyQueue asserts that, so a backend that ever resumed from
/// inside its own ready-list walk fails with a stack rather than corrupting the walk.
///
/// Ported from Endo's TuiRuntime (see contour's src/coro/README.md for provenance) with the
/// terminal-input and agent machinery removed, plus two additions the daemon
/// needs: finished spawned flows are reaped every pump (upstream accumulated
/// them until destruction), and a thread-safe @c post() that marshals work onto the
/// loop thread AND breaks an in-flight blocking wait — one mechanism for both, now
/// that the wakeup channel belongs to the backend (@c IoBackend::wake).
///
/// Threading: all scheduler state is touched only on the loop thread. The sole
/// cross-thread surface is @c post().

#include <core/async/Cancellation.hpp>
#include <core/async/Task.hpp>
#include <core/net/IoBackend.hpp>
#include <core/platform/Clock.hpp>
#include <core/platform/Types.hpp>

#include <cassert>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace core::net
{

/// Thrown by WaitFdAwaiter::await_resume when the fd could not be registered
/// with the backend (fd table exhausted, or a kernel that refused the interest).
/// Distinct from OperationCancelled so the caller can tell a plumbing failure from a
/// deliberate cancellation.
struct FdRegistrationFailed
{
};

/// Identifies one parked readiness wait for the loop's own bookkeeping.
///
/// An id rather than a pointer to the park, because a park is announced as closing
/// (@c notifyHandleClosing) before the pump consumes that announcement, and the park
/// may be resumed and destroyed in between: a recorded pointer would dangle, and a
/// recorded id resolves to "no such park" instead. Task B4 widens this into the
/// spec's `ParkId` over every kind of parked work; here it names an fd wait alone.
///
/// A strong struct rather than an `enum class` because it is an opaque,
/// monotonically-allocated handle id — a wide value space that never wraps in a
/// session — and not an enumeration of named cases. 32 bits would wrap after four
/// billion parks, which a server doing ten thousand a second reaches in five days.
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

/// Hash specialization so @c ParkId can key an unordered container: the loop maps a
/// park's id to the park itself, and a descriptor to the parks on it. Declared HERE,
/// between the type and its first use, because a specialization that arrives after
/// the container is instantiated is not the one the container picked up.
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

/// How a flow parked on a descriptor is resumed when that descriptor closes.
///
/// A readiness poller cannot report a CLOSED descriptor: epoll drops it from the
/// set and kqueue drops its filters, both silently, so a parked flow would never
/// be resumed at all (poll(2) reports POLLNVAL and Windows reports the handle as
/// failed, which is why those two backends never had the bug). @c
/// EventLoop::notifyHandleClosing is how a closing descriptor supplies that
/// missing readiness — and this says what the flow should observe once it wakes.
enum class FdWakePolicy : std::uint8_t
{
    /// Resume on the flow's normal path. For an explicit `close()`, where the
    /// owner is alive — it is the one that called close — so the flow can safely
    /// re-read the owner's closed flag and report the close as an error.
    Resume = 0,

    /// Resume by throwing @c OperationCancelled. For a destructor, where the owner
    /// is already gone: unwinding through @c await_resume never re-enters the flow
    /// body, so nothing dereferences the dead owner. This is the same reason
    /// ~EventLoop requests stop BEFORE waking its parked waiters.
    Cancel,
};

/// Why a parked fd waiter was resumed, reported back to the awaiter by
/// @c EventLoop::unregisterFdWaiter.
enum class FdWakeReason : std::uint8_t
{
    Ready = 0, ///< Ordinary readiness (or an explicit close under FdWakePolicy::Resume).
    Abandoned, ///< The descriptor closed under FdWakePolicy::Cancel; unwind instead of resuming.
};

class DelayAwaiter;
class WaitFdAwaiter;

/// Single-threaded cooperative scheduler driving coroutine flows over handle
/// readiness and timers.
///
/// Construct with an @c IoBackend, `spawn` background flows and/or `blockOn`
/// a root flow; the pump runs on the calling thread until the root flow
/// completes.
class EventLoop
{
  public:
    /// @param backend The multiplexed wait the pump drives (not owned; outlives the
    ///        loop, because every registration this loop made is detached in
    ///        ~EventLoop and not a moment later).
    /// @param clock The monotonic time source for timers and delays (not owned;
    ///        outlives the loop). Defaults to the process steady clock; tests
    ///        inject a @c platform::ManualClock for deterministic timing. The loop
    ///        calls its @c refresh() before it computes a wait's timeout and after
    ///        the wait returns, so a @c platform::CachedClock serves the current turn.
    explicit EventLoop(IoBackend& backend, platform::IClock& clock = platform::defaultSteadyClock());

    EventLoop(EventLoop const&) = delete;
    EventLoop& operator=(EventLoop const&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    /// Cancels and unwinds any still-parked spawned flows before their frames are
    /// destroyed: requests stop, wakes every waiter, and drains the ready queue so
    /// parked awaiters resume, observe cancellation (OperationCancelled), and run
    /// their RAII cleanup. Members (including the spawned-flow storage) destruct
    /// afterward. A no-op when nothing is parked (the common case).
    ~EventLoop();

    /// Drives the pump until @p task completes, then returns its result.
    /// @param task The root flow to run (its frame is kept alive for the call).
    /// @return The value produced by @p task (or void).
    template <typename T>
    T blockOn(async::Task<T> task)
    {
        task.handle().promise().setStopToken(_rootStop.get_token());
        _ready.push_back(task.handle());
        while (!task.done())
            pumpOnce();
        return task.result();
    }

    /// Starts a background flow that runs alongside the root flow. Its frame is
    /// kept alive by the loop and reclaimed on the pump after it completes.
    /// @param task The flow to run.
    void spawn(async::Task<void> task);

    /// @return The number of spawned background flows whose frames are still held
    ///         (completed flows are reaped at the top of every pump).
    [[nodiscard]] std::size_t spawnedCount() const noexcept { return _roots.size(); }

    /// @return The number of coroutines currently parked on a timer. A cancelled
    ///         timer-parked flow detaches its entry here (see requeueForCancellation),
    ///         so a leaked entry after a `whenAny`/`withTimeout` loser unwinds is
    ///         observable as a nonzero count — the invariant the cancellation path
    ///         must preserve.
    [[nodiscard]] std::size_t pendingTimerCount() const noexcept { return _timers.size(); }

    /// @return The number of readiness parks the loop still holds — one per
    ///         registration it has with the backend. The same invariant as
    ///         @c pendingTimerCount, for the other kind of park: a flow that resumed
    ///         or unwound without unregistering leaves its handler attached to the
    ///         backend, and a count that never returns to zero is how that shows
    ///         before it becomes a wait on a handle nobody is waiting for.
    [[nodiscard]] std::size_t parkedWaiterCount() const noexcept { return _parks.size(); }

    /// Enqueues @p callback to run on the loop thread and wakes the loop if it is
    /// blocked inside a wait. The ONLY EventLoop entry point that is safe to call
    /// from other threads; everything else must run on the loop thread (use post
    /// to get there). The wake goes through @c IoBackend::wake, which is the one
    /// thread-safe member of that interface and the one channel every backend owns.
    /// @param callback The work to run on the loop thread.
    void post(std::function<void()> callback);

    /// Requests cancellation of every flow and wakes all parked waiters so they
    /// unwind promptly via @c OperationCancelled. Must be called on the loop
    /// thread — from a signal handler or another thread, `post()` a call to it.
    void requestStop();

    /// @return The root cancellation source; `request_stop()` cancels every flow
    ///         (but does not wake parked waiters — prefer requestStop()).
    [[nodiscard]] async::StopSource& rootStopSource() noexcept { return _rootStop; }

    /// @return The monotonic clock backing all timers and delays. Awaiters read
    ///         deadlines through this so tests can drive time deterministically
    ///         via an injected @c platform::ManualClock.
    [[nodiscard]] platform::IClock& clock() const noexcept { return _clock; }

    /// @param duration How long to suspend.
    /// @return An awaitable that resumes after @p duration elapses.
    [[nodiscard]] DelayAwaiter delay(std::chrono::milliseconds duration) noexcept;

    /// @param deadline The absolute instant (on this loop's clock) to resume at.
    /// @return An awaitable that resumes once the clock reaches @p deadline.
    [[nodiscard]] DelayAwaiter sleepUntil(platform::SteadyTimePoint deadline) noexcept;

    /// Suspends until @p fd is readable (data, EOF, or HUP/ERR), without consuming
    /// any bytes — the caller then performs a non-blocking read.
    /// @param fd The native handle to wait on (must outlive the await).
    /// @return An awaitable resolving when @p fd is readable; throws
    ///         @c OperationCancelled if the flow is cancelled while parked.
    [[nodiscard]] WaitFdAwaiter waitReadable(platform::NativeHandle fd) noexcept;

    /// Suspends until @p fd is writable (space available in the send buffer).
    /// @param fd The native handle to wait on (must outlive the await).
    /// @return An awaitable resolving when @p fd is writable; throws
    ///         @c OperationCancelled if the flow is cancelled while parked.
    [[nodiscard]] WaitFdAwaiter waitWritable(platform::NativeHandle fd) noexcept;

    /// Announces that @p fd is ABOUT TO BE CLOSED, so any flow parked on it is
    /// resumed instead of waiting forever for readiness that can no longer arrive.
    ///
    /// Call this BEFORE the `close()` syscall, on the loop thread: the descriptor
    /// must still be valid so the backend can drop its kernel registration
    /// cleanly. Deferring that to the awaiter's own detach would issue the removal
    /// against a descriptor number the kernel may already have handed to a new
    /// socket, silently unregistering that one instead.
    ///
    /// The wake is only RECORDED here and delivered by the next pump as ordinary
    /// readiness. It is deliberately not queued for resumption from this call: a
    /// coroutine queued outside the pump still has its cancellation callback armed,
    /// so a later `requestStop()` would queue it a second time and the pump would
    /// then resume a frame the first resume had already destroyed.
    /// Not @c noexcept, though every caller is: recording a wake appends to a vector
    /// (and, for @c Cancel, a set), so allocation failure propagates as termination
    /// from a `close()` that cannot report it. Swallowing it would be worse — the
    /// wake would be lost and the flow would hang, which is the bug this exists to
    /// fix — and by that point the process is out of memory anyway.
    /// @param fd The descriptor about to be closed.
    /// @param policy How a flow parked on @p fd should observe the close — normally
    ///        (an explicit close, owner alive) or as cancellation (a destructor).
    void notifyHandleClosing(platform::NativeHandle fd, FdWakePolicy policy);

    /// @name Awaiter-facing scheduler primitives (internal)
    /// Called by the loop's awaitables; not part of the consumer API.
    /// @{

    void scheduleTimer(platform::SteadyTimePoint deadline, std::coroutine_handle<> waiter);

    /// Registers @p fd with the backend for @p interest and parks @p waiter until it
    /// becomes ready. Several waiters may be parked concurrently — including two on
    /// one descriptor, a reader beside a writer — so each park owns its own
    /// @c ReadinessHandler and is named by its own @c ParkId.
    /// @param fd The native handle to wait on.
    /// @param interest The readiness to wait for (Read or Write).
    /// @param waiter The coroutine to resume on readiness or cancellation.
    /// @return The park's id (to unregister on resume/cancel), or @c ParkId::invalid()
    ///         if the backend refused the registration — which it does for an invalid
    ///         handle, and for a kernel that would not arm the interest.
    [[nodiscard]] ParkId registerFdWaiter(platform::NativeHandle fd,
                                          Interest interest,
                                          std::coroutine_handle<> waiter);

    /// Detaches @p park from the backend and drops it, if it is still there.
    /// Idempotent. Called by the awaiter on resume (ready or cancelled).
    /// @param park The park to remove.
    /// @return @c FdWakeReason::Abandoned if the descriptor was closed under
    ///         @c FdWakePolicy::Cancel while this waiter was parked on it — the
    ///         awaiter then unwinds instead of resuming into an owner that is gone.
    ///         @c FdWakeReason::Ready otherwise.
    [[nodiscard]] FdWakeReason unregisterFdWaiter(ParkId park) noexcept;

    /// Re-queues @p waiter for resumption because its cancellation token fired while
    /// it was parked on a timer or fd. Used by the timed/fd awaiters' stop-callbacks
    /// so a `whenAny`/`withTimeout` loser parked on `delay`/`waitReadable` unwinds
    /// promptly instead of only when its deadline/fd eventually fires. The awaiter
    /// then observes stop_requested() in await_resume and throws OperationCancelled.
    /// Its timer entry / fd registration is detached HERE (not left for a later fire),
    /// so no stale coroutine_handle can outlive the awaiter's frame once it unwinds.
    /// Safe to call once per parked waiter.
    /// @param waiter The parked coroutine to resume for cancellation.
    void requeueForCancellation(std::coroutine_handle<> waiter);

    /// @}

  private:
    /// One scheduled timer: a deadline and the coroutine to resume at it.
    struct TimerEntry
    {
        platform::SteadyTimePoint deadline;
        std::coroutine_handle<> handle;
    };

    /// Heap comparator placing the soonest deadline at the heap root (a min-heap
    /// over the standard max-heap, by reversing the comparison).
    /// @return True if @p a is later than @p b.
    [[nodiscard]] static bool soonestFirst(TimerEntry const& a, TimerEntry const& b) noexcept
    {
        return a.deadline > b.deadline;
    }

    /// Runs one iteration: reap finished spawns, run posted work, resume ready
    /// coroutines, then wait and route readiness.
    void pumpOnce();

    /// Destroys the frames of spawned flows that have completed. Upstream Endo
    /// only released them in the destructor, which is an unbounded leak for a
    /// long-lived loop spawning per-connection flows.
    void reapFinishedSpawns();

    /// Runs every callback handed to post() since the last drain, outside the lock.
    void runPostedCallbacks();

    /// Resumes every coroutine currently in the ready queue.
    ///
    /// The one place a coroutine is resumed, which is what makes Rule 1 assertable:
    /// it requires that no backend dispatch is in flight on this thread, so a backend
    /// that resumed from inside its own ready-list walk is caught here rather than
    /// when the walk reads the entry a resumed frame has freed.
    void drainReadyQueue();

    /// @return How long the next wait may block: until the soonest timer, or nullopt
    ///         if no timer is pending. The rounding — a sub-millisecond remainder must
    ///         not become a zero-timeout spin — belongs to the backend's own
    ///         conversion (@c detail::toTimeoutMillis), which is where the unit is.
    [[nodiscard]] std::optional<platform::SteadyDuration> computeTimeout() const;

    /// Moves expired timers' coroutines into the ready queue.
    void fireExpiredTimers();

    /// Queues the coroutine parked at @p park for resumption, and takes the park out
    /// of the scheduling indices. This is what a backend's readiness callback reaches,
    /// and it ENQUEUES — it never resumes. Idempotent per park within one pump: the
    /// second call finds the waiter already taken and does nothing.
    ///
    /// The park itself survives (its @c ReadinessHandler is still registered with the
    /// backend); @c unregisterFdWaiter is what detaches and destroys it, from the
    /// awaiter that owns it.
    /// @param park The park whose waiter to queue.
    void queueParkedWaiter(ParkId park);

    /// The readiness callback every park registers, for both directions.
    ///
    /// Static and `noexcept`, because that is what a @c ReadinessCallback is. It only
    /// enqueues. Allocation failure inside the queue terminates rather than being
    /// swallowed, which is the same trade @c notifyHandleClosing makes and for the
    /// same reason: a lost wake is a flow that hangs.
    /// @param handler The ready park's handler, whose `owner` is its @c FdPark.
    static void onParkReady(ReadinessHandler& handler) noexcept;

    /// Wakes every parked flow so cancelled awaitables can unwind. Detaches each park
    /// from the backend first, so nothing stays registered past this call.
    void wakeAllWaiters();

    /// Forgets the waiter parked at @p park, across both indices that name it, and
    /// hands it back. The single place that does so: several call sites would
    /// otherwise each have to remember every container a park is recorded in, and one
    /// that forgot would leave a stale entry pointing at a frame about to be
    /// destroyed. The park itself is left in place for @c unregisterFdWaiter.
    /// @param park The park whose waiter is being taken.
    /// @return The coroutine that was parked, or a null handle if there was none.
    [[nodiscard]] std::coroutine_handle<> takeParkedWaiter(ParkId park) noexcept;

    /// One parked readiness wait: the registration the backend holds, the coroutine to
    /// resume, and the descriptor it parked on.
    ///
    /// Held by unique_ptr, because @c ReadinessHandler::owner points back at this and
    /// the backend holds the handler's address: a park may not move once registered.
    struct FdPark
    {
        ReadinessHandler handler {};                         ///< What the backend has registered.
        EventLoop* loop = nullptr;                           ///< The loop to enqueue onto.
        ParkId id {};                                        ///< This park's identity.
        std::coroutine_handle<> waiter;                      ///< The suspended coroutine; null once queued.
        platform::NativeHandle fd = platform::InvalidHandle; ///< The descriptor it is parked on.
    };

    /// Detaches @p park from the backend and destroys it, dropping every index that
    /// names it. A no-op for a park that is already gone.
    /// @param park The park to remove.
    void destroyPark(ParkId park) noexcept;

    IoBackend& _backend;                        ///< The injected multiplexed wait and dispatcher.
    platform::IClock& _clock;                   ///< The injected monotonic time source.
    std::deque<std::coroutine_handle<>> _ready; ///< Coroutines ready to resume now.
    std::vector<TimerEntry> _timers;            ///< Min-heap by deadline (soonest at front).
    std::unordered_map<ParkId, std::unique_ptr<FdPark>> _parks;        ///< Live readiness parks, by id.
    std::unordered_map<std::coroutine_handle<>, ParkId> _waiterToPark; ///< Reverse map for O(1) cancellation.
    /// Reverse index from descriptor to the parks on it, so a closing descriptor finds
    /// its waiters in O(1) rather than scanning every park. A multimap because one
    /// descriptor can carry two parks at once — a reader and a writer — and closing it
    /// must resume both.
    std::unordered_multimap<platform::NativeHandle, ParkId> _fdToParks;
    /// Parks whose descriptor closed since the last pump, merged into the next pump as
    /// one more source of readiness. Consumed ONLY in pumpOnce: ~EventLoop must resume
    /// parked flows through its own request_stop()-first path, not on their normal
    /// path, because by then their owners are already destroyed.
    std::vector<ParkId> _closedParks;
    /// The subset of @c _closedParks whose descriptor closed under
    /// @c FdWakePolicy::Cancel, so @c unregisterFdWaiter can tell the awaiter to
    /// unwind rather than resume.
    std::unordered_set<ParkId> _abandoned;
    std::vector<async::Task<void>> _roots; ///< Keeps live spawned background flows alive.
    async::StopSource _rootStop;           ///< Root cancellation source.
    std::uint64_t _nextParkId = 0;         ///< Source of never-zero park ids.

    std::mutex _postMutex;                      ///< Guards _posted (the only cross-thread state).
    std::vector<std::function<void()>> _posted; ///< Callbacks awaiting the loop thread.
};

/// Awaitable that resumes after a delay (or throws on cancellation).
///
/// While parked it registers a stop-callback so that if its cancellation token is
/// stopped before the deadline (e.g. a `whenAny`/`withTimeout` sibling won), the
/// parked coroutine is re-queued promptly and unwinds via @c OperationCancelled,
/// rather than lingering until the deadline elapses. Its timer entry is removed
/// when it is re-queued, so no handle dangles once the frame unwinds.
class DelayAwaiter
{
  public:
    DelayAwaiter(EventLoop& loop, platform::SteadyTimePoint deadline) noexcept:
        _loop(loop), _deadline(deadline)
    {
    }

    [[nodiscard]] bool await_ready() const noexcept { return _deadline <= _loop.clock().now(); }

    template <typename Promise>
    [[nodiscard]] bool await_suspend(std::coroutine_handle<Promise> awaiting)
    {
        if constexpr (requires { awaiting.promise().stopToken(); })
            _token = awaiting.promise().stopToken();
        if (_token.stop_requested())
            return false;
        _loop.scheduleTimer(_deadline, awaiting);
        _cancelReg.emplace(_token, [&loop = _loop, awaiting] { loop.requeueForCancellation(awaiting); });
        return true;
    }

    /// @throws OperationCancelled if the flow was cancelled while parked.
    void await_resume()
    {
        _cancelReg.reset();
        if (_token.stop_requested())
            throw async::OperationCancelled {};
    }

  private:
    std::optional<async::StopCallback<std::function<void()>>> _cancelReg;
    EventLoop& _loop;
    platform::SteadyTimePoint _deadline;
    async::StopToken _token;
};

/// Awaitable that resumes when a registered fd reaches a given readiness (Read or
/// Write), or throws @c OperationCancelled if the awaiting flow is cancelled while
/// parked. Returned by @c EventLoop::waitReadable / @c waitWritable.
///
/// Readiness is observed via the OS wait, so the awaiter is never ready before it
/// suspends: it always parks (after registering the fd with the backend), and the
/// loop resumes it when the backend dispatches readiness for it. On resume — whether
/// ready or cancelled — it unregisters, so the registration never outlives the await.
class WaitFdAwaiter
{
  public:
    /// @param loop The loop whose backend the fd is registered with.
    /// @param fd The native handle to wait on.
    /// @param interest The readiness to wait for (Read or Write).
    WaitFdAwaiter(EventLoop& loop, platform::NativeHandle fd, Interest interest) noexcept:
        _loop(loop), _fd(fd), _interest(interest)
    {
    }

    /// Readiness is only known after the OS wait, so a valid fd never reports ready
    /// before suspending. An invalid fd resolves immediately (await_resume then
    /// reports cancellation), avoiding a pointless park on a handle that can never
    /// signal.
    [[nodiscard]] bool await_ready() const noexcept { return _fd == platform::InvalidHandle; }

    /// Captures the cancellation token, then (unless already cancelled) attaches the
    /// fd and parks. Checks cancellation BEFORE registering so a cancelled flow
    /// resumes immediately without leaving a dangling registration.
    /// @param awaiting The coroutine performing the `co_await`.
    /// @return False (resume now) if already cancelled or the attach failed; true to park.
    template <typename Promise>
    [[nodiscard]] bool await_suspend(std::coroutine_handle<Promise> awaiting)
    {
        if constexpr (requires { awaiting.promise().stopToken(); })
            _token = awaiting.promise().stopToken();
        if (_token.stop_requested())
            return false;
        _registration = _loop.registerFdWaiter(_fd, _interest, awaiting);
        if (!_registration)
            return false; // registration failed: resume and surface it in await_resume
        // If the token is stopped while parked (a whenAny/withTimeout sibling won),
        // re-queue this coroutine promptly so it unwinds instead of waiting for the
        // fd to become ready (which may never happen).
        _cancelReg.emplace(_token, [&loop = _loop, awaiting] { loop.requeueForCancellation(awaiting); });
        return true;
    }

    /// Unregisters the park and, if the flow was cancelled while parked, the
    /// descriptor was abandoned under it, or the registration failed, reports the
    /// failure.
    /// @throws FdRegistrationFailed if the fd could not be registered with the
    ///         backend (resource exhaustion, or a kernel that refused the interest —
    ///         distinct from cancellation).
    /// @throws OperationCancelled if cancelled while parked, the fd was invalid, or
    ///         the fd was closed under @c FdWakePolicy::Cancel while parked. That
    ///         last case is what keeps a destructor from resuming this flow into an
    ///         owner that no longer exists: throwing here unwinds the frame without
    ///         ever re-entering its body.
    void await_resume()
    {
        _cancelReg.reset();
        auto reason = FdWakeReason::Ready;
        if (_registration)
            reason = _loop.unregisterFdWaiter(_registration);
        else if (_fd != platform::InvalidHandle && !_token.stop_requested())
            throw FdRegistrationFailed {};
        if (_token.stop_requested() || _fd == platform::InvalidHandle || reason == FdWakeReason::Abandoned)
            throw async::OperationCancelled {};
    }

  private:
    std::optional<async::StopCallback<std::function<void()>>> _cancelReg;
    EventLoop& _loop;
    platform::NativeHandle _fd;
    Interest _interest;
    ParkId _registration {};
    async::StopToken _token;
};

/// Suspends until @p predicate returns true, re-checking every @p interval on
/// @p loop's clock. This is the shared teardown-drain idiom — wait for a write
/// queue to flush, a debounce to fire, an output pacer to empty — in ONE place.
///
/// It POLLS rather than parking on a completion signal, which is acceptable on
/// the low-frequency connection-teardown paths that use it (the cost is at most
/// one @p interval of extra latency at close); it is NOT for hot paths.
/// @param loop The loop whose delay drives the poll (and cancels it on shutdown).
/// @param predicate Checked before each wait; the poll returns once it holds.
/// @param interval How long to suspend between checks.
[[nodiscard]] async::Task<void> pollUntil(EventLoop* loop,
                                          std::function<bool()> predicate,
                                          std::chrono::milliseconds interval = std::chrono::milliseconds {
                                              1 });

} // namespace core::net
