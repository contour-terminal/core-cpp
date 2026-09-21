// SPDX-License-Identifier: Apache-2.0
#include <core/net/EventLoop.hpp>

#include <core/net/detail/ReadyBatch.hpp>
#include <core/net/detail/ScopeGuard.hpp>

#include <algorithm>
#include <limits>
#include <ranges>
#include <tuple>
#include <utility>

namespace core::net
{

namespace
{
    /// How many drain passes @c ~EventLoop runs before it stops resuming and starts freeing.
    ///
    /// **Bounded, and that is the whole point.** A cancelled awaitable commonly re-parks — a
    /// bounded wait arms the next step, a teardown drain awaits one more flush — so an unbounded
    /// "drain until empty" spins forever on exactly the shutdown it exists to make clean. What
    /// the passes buy is the ORDINARY case: a flow cancelled once, resumed once, unwound, with
    /// its RAII cleanup run. What survives the bound is handled by step 4, which frees rather
    /// than resumes. Sixteen because a chain of eight `co_await`s unwinding one frame per pass is
    /// already deeper than anything in this tree, and the cost of a pass that finds nothing is a
    /// container check.
    constexpr std::size_t TeardownDrainPasses = 16;

    /// The batch bound a drain uses when it must take everything: teardown, where leaving work
    /// queued would leave it for step 4 to free rather than for the flow to unwind through.
    constexpr std::size_t UnboundedDrain = std::numeric_limits<std::size_t>::max();
} // namespace

EventLoop::EventLoop(IoBackend& backend, platform::IClock& clock, EventLoopOptions options):
    _backend(backend), _clock(clock), _options(options)
{
    // A host-driven backend has no wait of its own: the HOST is what waits, and what it calls
    // when that wait ends is one turn of this loop. Registered once, here, because the backend
    // needs a pointer to a loop that does not exist until this constructor runs — and cleared in
    // the last step of ~EventLoop, or the host's next pump would call into freed storage.
    // Every other backend ignores it.
    _backend.setPump(&EventLoop::onHostPump, this);
}

EventLoop::~EventLoop()
{
    // ---- 1. Teardown is serialised with dispatch. -----------------------------------------
    // Clearing a pending awaitable races the readiness dispatch, so a loop is destroyed on its
    // own worker thread or with nothing driving it. Any other thread, while a turn has not
    // returned, is the violation (guarantee G5).
    assert(teardownIsSerialisedWithDispatch()
           && "an EventLoop must be destroyed on its own worker thread, or with nothing driving "
              "it -- otherwise clearing a pending awaitable races the readiness dispatch");

    // ---- 2. Request stop, then move every park to the ready queue. ------------------------
    // **What the loop OWNS is taken aside to be freed; what it borrows stays to be resumed.**
    // The same rule the park table follows, for the same reason: a chain the loop owns is a
    // `DetachedTask`, which carries no stop token -- a detached flow has no awaiting coroutine to
    // inherit one from -- so resuming it would not cancel it, it would run the rest of its body on
    // a loop that is being destroyed. A chain the loop BORROWS belongs to a `Task` somebody holds;
    // that owner set a stop token on it, and resuming it is what makes the frame unwind and run
    // its cleanup.
    //
    // Order matters both ways round. Stop FIRST, so that when the parked handles are queued, the
    // drain below resumes them into await_resume, which sees stop_requested() and throws
    // OperationCancelled -- unwinding the frame's locals. A waiter queued before the stop was
    // requested would resume on its NORMAL path instead, into an owner that is already destroyed.
    //
    // Note what is deliberately NOT done here: _closedParks is left unconsumed. Those wakes
    // resume a flow on its normal path, which is right while the loop runs (the owner just called
    // close) and wrong now. unparkEverything still finds every such waiter, because
    // notifyHandleClosing leaves the park in place.
    //
    // A due TIMER CALLBACK is in neither queue: it is dropped. It is not work to unwind and not a
    // frame to free -- it is a call into a `DeadlineTimer`'s owner, and that owner is being
    // destroyed with this loop or is already gone. Running it here would be the one path on which
    // a callback reaches an object whose loop has stopped existing.
    auto ownedQueue = std::deque<ReadyEntry> {};
    auto borrowedQueue = std::deque<ReadyEntry> {};
    for (auto& entry: _ready)
    {
        if (entry.callbackPark)
            continue;
        (entry.ownedByLoop ? ownedQueue : borrowedQueue).push_back(std::move(entry));
    }
    _ready = std::move(borrowedQueue);

    _rootStop.request_stop();
    unparkEverything();

    // ---- 3. Bounded drain passes. ---------------------------------------------------------
    // Cancelled re-awaits resume synchronously (await_suspend returns false when stop is
    // requested), so one pass usually converges; the bound is what stops a flow that re-parks
    // from spinning here forever. See TeardownDrainPasses.
    for ([[maybe_unused]] auto const pass: std::views::iota(std::size_t { 0 }, TeardownDrainPasses))
    {
        if (_ready.empty())
            break;
        std::ignore = drainReadyQueue(UnboundedDrain);
    }

    // ---- 4. Abandon to a fixpoint. --------------------------------------------------------
    // Step 2's owned queue entries go first: freeing a chain can park again, and the fixpoint
    // below is what collects whatever that produces.
    ownedQueue.clear();
    abandonParkedWork();

    // ---- 5. Destroy the spawned roots. ----------------------------------------------------
    // After the abandonment, not before: a spawned flow's frame is owned HERE, so destroying it
    // first would pull the ground out from under anything still parked on it.
    _rootByHandle.clear();
    _roots.clear();

    // ---- 6. Unregister the wake. ----------------------------------------------------------
    // A host-driven backend holds a pointer to this loop and an armed host timer; either one
    // outliving the loop is a call into freed storage on the host's next turn. Every other
    // backend ignores both.
    _backend.setPump(nullptr, nullptr);
    _backend.armWakeAt(std::nullopt);
}

void EventLoop::abandonParkedWork() noexcept
{
    // **A fixpoint, and both containers taken before either is freed.** Freeing a chain RE-ENTERS
    // the loop: a frame holding a deadline runs its disarm into cancelPending, which reads the
    // park table, and a frame holding a bounded wait can reach schedule, which writes to it. So a
    // single fixed-order pass would leave whatever that re-entry produced for MEMBER destruction
    // -- and members die in reverse declaration order, so a chain freed from the later one then
    // searches a container whose destructor has already run.
    //
    // Origin: [fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025),
    // [fastcached#1054](https://github.com/LASTRADA-Software/fastcached/issues/1054).
    while (true)
    {
        auto ready = std::exchange(_ready, {});
        auto parks = _parks.takeAll();
        // The inbound queue too, and it is not an afterthought: a coroutine that parked with
        // `ResumeOn` from any thread but the loop's is sitting HERE and nowhere else, so a
        // teardown that swept only the loop's own containers would leak exactly the shape #1025
        // was reported on. Its posts are dropped rather than run -- a callback handed to a loop
        // that is being destroyed has nothing left to run against.
        auto inbound = Inbound {};
        {
            auto const lock = std::scoped_lock { _inboundMutex };
            std::swap(inbound, _inbound);
        }
        if (ready.empty() && parks.empty() && inbound.empty())
            return;

        // Detached BEFORE anything is freed: the backend holds each handler's address, and a
        // handler freed while it is still registered leaves the backend walking dead storage.
        for (auto const& park: parks)
            if (park->attached)
                _backend.detach(park->handler);

        // And now the frees. Each `Parked` releases its claim on the chain it was holding, and
        // the LAST claim on a chain destroys it -- so what goes is exactly the chains nothing else
        // owns, and borrowed work is left alone.
        ready.clear();
        parks.clear();
        inbound = Inbound {};
    }
}

void EventLoop::run()
{
    assert(!_backend.isHostDriven()
           && "EventLoop::run on a host-driven loop: such a loop does not own its thread and "
              "advances only through host pumps, so run() would spin without ever yielding one");

    // Claimed HERE rather than by each turn, and that is the obligation half of the teardown rule:
    // a loop cannot be inside run() without answering running(), so no loop can forget to claim
    // and leave teardownIsSerialisedWithDispatch() answering `true` unconditionally.
    auto const onWorker = detail::WorkerIdentity::Scope { _worker };
    // What tells step 4 that an idle turn should BLOCK rather than return: see the wait there.
    // A plain member with an RAII reset rather than a parameter, because it is a property of the
    // drive and `runOnce` is reached from three of them.
    _inRun = true;
    auto const leaveRun = detail::ScopeGuard { [this] noexcept { _inRun = false; } };
    while (!stopRequested())
        std::ignore = runOnce();
}

std::size_t EventLoop::runUntilIdle()
{
    auto total = std::size_t { 0 };
    while (true)
    {
        // Zero, so this never blocks whatever the idle policy is: a caller asking the loop to run
        // out its queued work is not asking it to wait for more.
        auto const turn = runOnce(platform::SteadyDuration::zero());
        total += turn.resumed;
        if (turn.idle)
            return total;
    }
}

RunOnceResult EventLoop::runOnce(std::optional<platform::SteadyDuration> maxWait)
{
    return turn(maxWait, {});
}

RunOnceResult EventLoop::turn(std::optional<platform::SteadyDuration> maxWait, std::coroutine_handle<> until)
{
    // G1: exactly one thread dequeues a loop. Asserted here rather than only in run(), because
    // this is the entry point a host pump, a test driver and blockOn all reach.
    assert(teardownIsSerialisedWithDispatch()
           && "an EventLoop turn was entered from a second thread while another is driving this "
              "loop -- run(), runOnce() and blockOn() all reach here (G1: exactly one thread "
              "dequeues a loop)");
    auto const onWorker = detail::WorkerIdentity::Scope { _worker };

    auto result = RunOnceResult {};

    // ---- 1. Swap the inbound queue: run the posts, then resolve the cancels. --------------
    auto const hadInbound = runInbound();

    // ---- 2. Drain the ready queue. --------------------------------------------------------
    // THE one place a coroutine is resumed (guarantee G2). Readiness dispatched in step 4 and
    // deadlines fired in step 5 are resumed by the NEXT turn's step 2, which is what lets the
    // loop state G2 rather than trust each backend with it.
    result.resumed = drainReadyQueue(_options.dispatchBatch);

    // Handles closed since the last turn. The backend cannot report these -- epoll drops a closed
    // descriptor from its set and kqueue drops its filters, both silently -- so the loop supplies
    // that readiness itself and MERGES it into this turn below.
    //
    // Merging rather than short-circuiting is load-bearing. Returning early here would skip the
    // wait, and every other descriptor that became ready in the same instant -- a peer's EOF,
    // most importantly -- would go unreported until some later turn that may never come. poll(2)
    // never had that problem: it reports POLLNVAL for the closed descriptor alongside every other
    // revent, in one call. This keeps every backend doing the same.
    //
    // Taken BEFORE the wait so a pending close can turn it into a non-blocking poll: blocking
    // would wait for readiness that can no longer arrive.
    auto const closed = std::exchange(_closedParks, {});

    // ---- 3. Refresh the clock, then compute the timeout. ----------------------------------
    // The clock is re-sampled around the one blocking call, as IClock::refresh() asks of whoever
    // owns the loop: BEFORE the timeout is computed, so the time this turn has already spent is
    // not waited for again. A clock that reads the OS on every now() ignores it; a CachedClock
    // would otherwise never move.
    _clock.refresh();
    auto timeout = computeTimeout(maxWait);
    if (!closed.empty())
        timeout = platform::SteadyDuration::zero();

    // ---- 4. Wait. -------------------------------------------------------------------------
    // The wait DISPATCHES: every ready park's callback has already queued its coroutine by the
    // time this returns, and nothing has been resumed.
    //
    // It is skipped when nothing could possibly come back from it. A loop with no park and no
    // closed handle has nothing the backend can report, so a wait there is a syscall that can
    // only time out -- and for `blockOn`, whose root flow has just finished, one that would never
    // return at all. Work already queued still waits, with a timeout of zero: readiness that
    // arrived in the same instant is collected rather than deferred a turn.
    //
    // `run()` is the one exception, and it is the whole of what `IdlePolicy::Block` means: a loop
    // that owns its thread and has nothing to do BLOCKS, because another thread may still `post`
    // and the backend's wake channel is what ends that wait. A loop somebody else drives a turn at
    // a time must not, because the caller is what waits.
    auto const idleWait = _inRun && _options.idle == IdlePolicy::Block;
    // And skipped for `blockOn` the moment the flow it exists to finish HAS finished. That drive
    // is bounded by one flow rather than by a stop, so a wait entered after step 2 completed it
    // could only end on a deadline or a wake belonging to work nobody is waiting for -- and on a
    // loop with a spawned flow parked on a socket, on nothing at all.
    auto const driven = !until || !until.done();
    if (driven && (_parks.size() != 0 || !closed.empty() || !_ready.empty() || idleWait))
        result.dispatched = _backend.wait(timeout).dispatched;

    // ---- 5. Refresh the clock, then fire expired deadlines, FIFO by sequence. --------------
    // And after the wait too, so the deadlines fired here see the instant the wait ENDED at
    // rather than the one it started from.
    _clock.refresh();

    // The closed handles first, as one more source of readiness. After the wait, so a park the
    // backend also reported is queued exactly once -- the first queueing takes its waiter, and a
    // park with no waiter left is skipped.
    for (auto const park: closed)
        queueParkedWaiter(park);

    auto const fired = fireExpiredTimers();

    // Nothing posted, nothing resumed, nothing dispatched, nothing due: this turn did nothing,
    // which is what runUntilIdle and TestLoop::drain stop on. It deliberately says nothing about
    // whether work is still PARKED -- a flow waiting on a socket that never becomes readable
    // leaves a loop idle turn after turn, and a drain that waited for the park to go would never
    // return.
    result.idle = !hadInbound && result.resumed == 0 && result.dispatched == 0 && fired == 0;

    // A host-driven backend has no wait of its own, so the loop's next deadline reaches the host
    // instead. After every turn, because the turn is what changed the answer.
    armHostWake();
    return result;
}

bool EventLoop::runInbound()
{
    // Swap under the lock, run outside it: a callback may itself post (or spawn, or resume
    // coroutines that do), and must not deadlock or invalidate the container mid-iteration. Work
    // handed over DURING the run lands in the fresh queue and is picked up on the next turn.
    auto pending = Inbound {};
    {
        auto const lock = std::scoped_lock { _inboundMutex };
        pending.posts.swap(_inbound.posts);
        pending.submissions.swap(_inbound.submissions);
        pending.scheduled.swap(_inbound.scheduled);
        pending.cancels.swap(_inbound.cancels);
    }
    if (pending.empty())
        return false;

    for (auto const& callback: pending.posts)
        callback();
    for (auto& work: pending.submissions)
        queueReady(std::move(work));
    for (auto& timed: pending.scheduled)
        std::ignore = registerPark(ParkEntry::onDeadline(std::move(timed.work), timed.deadline));

    // **Cancels LAST, and before the drain.** Last within the step, because a post may itself
    // park the very flow a cancel names, and a cancel resolved before that park existed would
    // resolve to nothing and leave the flow parked forever. Before the drain, because resolving a
    // cancel is what puts the cancelled flow's handle into the ready queue -- resolved after the
    // drain it would sit there while steps 3 and 4 computed a timeout and BLOCKED, and a cancel
    // from another thread would then take effect only when something unrelated woke the loop.
    for (auto const park: pending.cancels)
        resolveCancel(park);
    return true;
}

std::size_t EventLoop::drainReadyQueue(std::size_t bound)
{
    // Rule 1, asserted where it would be broken: a backend dispatches, and the loop resumes. A
    // resume from inside a backend's walk over its own ready list lets the resumed frame free the
    // object whose entry the walk has not reached yet. Origin:
    // [fastcached#475](https://github.com/LASTRADA-Software/fastcached/issues/475).
    assert(!detail::readinessDispatchInFlight()
           && "EventLoop::drainReadyQueue reached from inside a backend dispatch: "
              "backend callbacks may only enqueue");

    auto resumed = std::size_t { 0 };
    while (resumed < bound && !_ready.empty())
    {
        auto entry = std::move(_ready.front());
        _ready.pop_front();

        // A due timer callback runs HERE, where a coroutine resumption runs, and nowhere else.
        // Step 5 could have called it the moment it found the deadline due -- and then user code
        // would run at a second point in the turn, outside the bound, outside the one assertion
        // that says no backend dispatch is in flight, and after the drain rather than in it. One
        // place that hands control outside the loop is worth the extra queue hop.
        if (entry.callbackPark)
        {
            runDueCallback(entry.callbackPark);
            ++resumed;
            continue;
        }

        auto const handle = entry.parked.handle();

        // Looked up BEFORE the resume. A spawned flow's frame is owned by _roots and survives its
        // own completion, so asking it `done()` afterwards is safe -- but a DETACHED chain frees
        // itself there, and an address freed and then reused would read as somebody else's root.
        // The list iterator is stable across the resume; the map is not, because a flow may spawn.
        auto const root = handle ? _rootByHandle.find(handle.address()) : _rootByHandle.end();
        auto const wasRoot = root != _rootByHandle.end();
        auto const slot = wasRoot ? root->second : _roots.end();

        // `resume()` disowns and resumes in one expression, so work that runs normally is never
        // also freed by the entry going out of scope here -- and a handle it DECLINES to resume
        // has its chain freed rather than dropped.
        entry.parked.resume();
        ++resumed;

        // O(1) self-unlink: the turn that runs a spawned flow to its end releases its frame there
        // and then. A sweep over every spawned flow at the top of each turn is O(n) per turn,
        // which a server spawning one flow per connection pays forever.
        if (wasRoot && handle.done())
        {
            _rootByHandle.erase(handle.address());
            _roots.erase(slot);
        }
    }
    return resumed;
}

std::optional<platform::SteadyDuration> EventLoop::computeTimeout(
    std::optional<platform::SteadyDuration> maxWait)
{
    auto timeout = std::optional<platform::SteadyDuration> {};

    // Work is already queued, so the wait is a POLL: readiness that arrived in the same instant is
    // still collected, but nothing is blocked on behind work the loop could be doing. Without
    // this, a turn whose drain hit its batch bound would go on to block indefinitely with a full
    // ready queue, which is a hang rather than a slow loop.
    if (!_ready.empty())
        timeout = platform::SteadyDuration::zero();
    else if (auto const due = _parks.nextDeadline(); due.has_value())
    {
        auto const now = _clock.now();
        timeout = *due <= now ? platform::SteadyDuration::zero() : (*due - now);
    }

    if (maxWait.has_value() && (!timeout.has_value() || *maxWait < *timeout))
        timeout = maxWait;

    // A loop somebody else drives never blocks inside a turn: the caller is what waits, and a
    // turn that slept through a deadline would sleep through the caller's own work too.
    if (_options.idle == IdlePolicy::Return)
        timeout = platform::SteadyDuration::zero();

    // The rounding -- a sub-millisecond remainder must not become a zero-timeout spin -- belongs
    // to the backend's own conversion (detail::toTimeoutMillis), which is where the unit is.
    return timeout;
}

std::size_t EventLoop::fireExpiredTimers()
{
    auto fired = std::size_t { 0 };
    for (auto const park: _parks.takeExpired(_clock.now()))
    {
        // Two kinds of park come back from one heap, in one order: a coroutine to resume, and a
        // callback to call. Both are QUEUED here and run by the next turn's drain, which is what
        // makes the order between them the heap's -- soonest first, then by arming sequence --
        // rather than an artefact of which mechanism got to fire first.
        auto const* const entry = _parks.find(park);
        if (entry != nullptr && entry->onExpired != nullptr)
            _ready.push_back(ReadyEntry { .parked = {}, .ownedByLoop = false, .callbackPark = park });
        else
            queueParkedWaiter(park);
        ++fired;
    }
    return fired;
}

void EventLoop::runDueCallback(ParkId park)
{
    // Taken out of the table BEFORE the call, and that is what makes two things true at once: a
    // `cancelTimer` from inside the callback finds nothing (this timer HAS fired), and the
    // callback may destroy whatever owns it, because nothing here reads the table afterwards.
    //
    // A park that is already gone is a timer cancelled between step 5 queueing it and this drain
    // reaching it -- the window `cancelTimer` documents -- and skipping it is what closes it.
    auto const entry = _parks.take(park);
    if (!entry || entry->onExpired == nullptr)
        return;
    entry->onExpired(entry->callbackState);
}

void EventLoop::armHostWake()
{
    if (!_backend.isHostDriven())
        return;
    // Work already queued means "as soon as you can", which is a deadline of now rather than no
    // deadline at all: a host-driven loop has no other way of getting another turn.
    if (!_ready.empty() || hasInbound())
    {
        _backend.armWakeAt(_clock.now());
        return;
    }
    _backend.armWakeAt(_parks.nextDeadline());
}

void EventLoop::onHostPump(void* state) noexcept
{
    // Zero, because the host is what waits: this turn collects whatever is due and returns, and
    // the turn's own armWakeAt asks for the next one.
    std::ignore = static_cast<EventLoop*>(state)->runOnce(platform::SteadyDuration::zero());
}

void EventLoop::submit(std::coroutine_handle<> handle)
{
    submit(async::ParkedWork { .resume = handle });
}

void EventLoop::submit(async::ParkedWork work)
{
    if (!work.resume)
        return;
    // Inline only from the loop's OWN thread. Anywhere else -- another thread, or this one while
    // no turn is in flight -- it goes through the inbound queue, because "nobody is driving right
    // now" is not the same fact as "nobody else can start", and a queue two threads write to
    // without a lock is a data race whether or not one of them happens to be the owner.
    if (isOnWorkerThread())
    {
        queueReady(std::move(work));
        return;
    }
    {
        auto const lock = std::scoped_lock { _inboundMutex };
        _inbound.submissions.push_back(std::move(work));
    }
    _backend.wake();
}

void EventLoop::schedule(platform::SteadyTimePoint deadline, std::coroutine_handle<> handle)
{
    schedule(deadline, async::ParkedWork { .resume = handle });
}

void EventLoop::schedule(platform::SteadyTimePoint deadline, async::ParkedWork work)
{
    if (!work.resume)
        return;
    if (isOnWorkerThread())
    {
        std::ignore = registerPark(ParkEntry::onDeadline(std::move(work), deadline));
        return;
    }
    {
        auto const lock = std::scoped_lock { _inboundMutex };
        _inbound.scheduled.push_back(TimedWork { .deadline = deadline, .work = std::move(work) });
    }
    _backend.wake();
}

bool EventLoop::cancelPending(std::coroutine_handle<> handle) noexcept
{
    if (!handle)
        return false;

    // The ready queue first: a handle submitted and not yet resumed.
    auto const found = std::ranges::find_if(
        _ready, [handle](ReadyEntry const& entry) { return entry.parked.handle() == handle; });
    if (found != _ready.end())
    {
        // Taken rather than only erased: the caller becomes the only one who may resume or destroy
        // it, so this entry must do neither on its way out. And the chain is DISARMED rather than
        // released -- releasing the last claim would free the very frame the caller has just been
        // handed, which is the opposite of an ownership transfer.
        found->parked.take().abandon.disarm();
        _ready.erase(found);
        return true;
    }

    if (auto const id = _parks.byWaiter(handle))
    {
        auto park = _parks.take(id);
        if (park)
        {
            if (park->attached)
                _backend.detach(park->handler);
            park->parked.take().abandon.disarm();
            return true;
        }
    }

    // And the inbound queue, which is where work handed over from another thread -- or from this
    // one between turns -- waits for step 1. Leaving it out would make the answer depend on which
    // thread submitted, which is exactly the kind of "true here, false there" an ownership
    // transfer cannot afford.
    auto const lock = std::scoped_lock { _inboundMutex };
    auto const queued = std::ranges::find_if(
        _inbound.submissions, [handle](async::ParkedWork const& work) { return work.resume == handle; });
    if (queued != _inbound.submissions.end())
    {
        queued->abandon.disarm();
        _inbound.submissions.erase(queued);
        return true;
    }
    auto const timed = std::ranges::find_if(
        _inbound.scheduled, [handle](TimedWork const& entry) { return entry.work.resume == handle; });
    if (timed != _inbound.scheduled.end())
    {
        timed->work.abandon.disarm();
        _inbound.scheduled.erase(timed);
        return true;
    }
    return false;
}

void EventLoop::post(std::function<void()> callback)
{
    {
        auto const lock = std::scoped_lock { _inboundMutex };
        _inbound.posts.push_back(std::move(callback));
    }
    // Break a possibly-blocked wait. The wakeup channel belongs to the backend -- `wake()` is the
    // one member of IoBackend another thread may call -- so the loop holds no descriptor of its
    // own for this, and a backend with no wait to break (HostDriven) still gets told there is
    // work.
    _backend.wake();
}

void EventLoop::stop() noexcept
{
    {
        auto const lock = std::scoped_lock { _inboundMutex };
        _stopRequested = true;
    }
    _backend.wake();
}

bool EventLoop::stopRequested() const
{
    auto const lock = std::scoped_lock { _inboundMutex };
    return _stopRequested;
}

void EventLoop::requestStop()
{
    _rootStop.request_stop();
    unparkEverything();
}

void EventLoop::spawn(async::Task<void> task)
{
    auto const handle = task.handle();
    if (!handle)
        return;
    handle.promise().setStopToken(_rootStop.get_token());
    auto const slot = _roots.insert(_roots.end(), std::move(task));
    _rootByHandle.emplace(handle.address(), slot);
    // Borrowed, not owned: the frame belongs to the Task in _roots above, so the ready entry must
    // not carry a claim on it. Step 5 of the teardown is what frees these.
    queueReady(async::ParkedWork { .resume = handle });

    // A spawn from outside a turn has to WAKE the loop, or a flow queued before anything drives it
    // is one nothing will ever start: a blocked wait does not know the queue changed, and a
    // host-driven backend has no wait at all to notice -- there, `wake()` IS how the host is asked
    // for the turn that runs this flow. Inside a turn it is redundant, because the turn drains
    // what it queued and arms the host itself, so it is asked only where it is needed.
    if (!isOnWorkerThread())
        _backend.wake();
}

TimerId EventLoop::addTimer(platform::SteadyTimePoint deadline, TimerCallback onExpired, void* state)
{
    // The same predicate the turn and the destructor use: the loop's own thread, or nobody
    // driving. Anywhere else this would write the park table beside a turn that is reading it.
    assert(teardownIsSerialisedWithDispatch()
           && "EventLoop::addTimer from a second thread while another is driving this loop: "
              "post() a call to it instead");
    assert(onExpired != nullptr && "EventLoop::addTimer with no callback: a timer with nothing to "
                                   "run would be filed and fired into nothing");
    if (onExpired == nullptr)
        return TimerId::invalid();
    return TimerId { registerPark(ParkEntry::onCallback(onExpired, state, deadline)) };
}

bool EventLoop::cancelTimer(TimerId timer) noexcept
{
    if (!timer)
        return false;

    // Looked up before it is taken, and the kind is checked: a `ParkId` that named a COROUTINE
    // park would otherwise be unparked here -- silently freeing a flow's park and leaving it
    // waiting forever -- by a caller who only had the wrong strong type to begin with.
    auto const* const entry = _parks.find(timer.park);
    if (entry == nullptr || entry->onExpired == nullptr)
        return false;

    // The park goes; any ReadyEntry naming it resolves to nothing when the drain reaches it. That
    // is the generation check doing the work, and it is why cancelling a timer that step 5 has
    // already queued costs no scan of the ready queue.
    std::ignore = _parks.take(timer.park);
    return true;
}

void EventLoop::resumeSoon(async::ParkedWork work)
{
    if (!work.resume)
        return;
    queueReady(std::move(work));
}

void EventLoop::queueReady(async::ParkedWork work)
{
    auto const owned = static_cast<bool>(work.abandon);
    _ready.push_back(
        ReadyEntry { .parked = async::detail::Parked { std::move(work) }, .ownedByLoop = owned });
}

ParkId EventLoop::registerPark(ParkEntry entry)
{
    // A park is a coroutine to resume OR a callback to call; one without either would be filed,
    // indexed and fired into nothing.
    if (!entry.work.resume && entry.onExpired == nullptr)
        return ParkId::invalid();

    auto park = std::make_unique<detail::Park>();
    park->loop = this;
    park->handle = entry.handle;
    park->deadline = entry.deadline;
    park->onExpired = entry.onExpired;
    park->callbackState = entry.callbackState;
    park->ownedByLoop = static_cast<bool>(entry.work.abandon);
    park->parked = async::detail::Parked { std::move(entry.work) };

    if (entry.handle != platform::InvalidHandle)
    {
        // Both directions point at the same callback, and there is deliberately no onError: a
        // failure then reaches whichever direction this park watches, which is what a parked read
        // and a parked accept both want -- they resume, look, and report what they find. A
        // dedicated onError would have to decide that for them.
        park->handler = ReadinessHandler { .handle = entry.handle,
                                           .kind = entry.kind,
                                           .owner = park.get(),
                                           .onReadable = &EventLoop::onParkReady,
                                           .onWritable = &EventLoop::onParkReady,
                                           .onError = nullptr };

        auto const attached = _backend.attach(park->handler);
        // A refused interest must not leave a park behind claiming the handle is watched: the
        // awaiting flow has to fail rather than park on an interest the kernel never accepted,
        // which nothing could ever resume.
        auto const armed = attached ? _backend.setInterest(park->handler, entry.interest) : attached;
        if (!armed)
        {
            _backend.detach(park->handler);
            // The flow is about to resume and report the refusal, so the chain is ITS again.
            // Letting this park's claim go out of scope instead would free -- if it held the last
            // one -- the very frame that is about to run await_resume.
            park->parked.take().abandon.disarm();
            return ParkId::invalid();
        }
        park->attached = true;
    }

    return _parks.add(std::move(park));
}

void EventLoop::unregisterPark(ParkId park) noexcept
{
    auto entry = _parks.take(park);
    if (!entry)
        return;
    if (entry->attached)
        _backend.detach(entry->handler);
    // Whatever is still here is a frame that is resuming right now -- await_resume is what calls
    // this -- so the chain belongs to it again rather than to the loop.
    if (entry->parked)
        entry->parked.take().abandon.disarm();
}

FdWakeReason EventLoop::wakeReasonOf(ParkId park) noexcept
{
    // Consumed rather than merely read: the awaiter asks exactly once, and an id left behind here
    // would outlive its park and grow without bound on a long-lived loop.
    return _abandoned.erase(park) != 0 ? FdWakeReason::Abandoned : FdWakeReason::Ready;
}

void EventLoop::requestCancel(ParkId park) noexcept
{
    if (!park)
        return;

    // Resolved HERE when the caller is already the loop's thread, which a stop callback commonly
    // is: `whenAny` stops its losers from inside the drain that ran the winner. A park left live
    // until the next turn is one whose frame its OWNER may destroy in between -- a `whenAny` loser
    // is freed the moment the winner returns -- and the loop would then hold a handle to freed
    // storage.
    if (isOnWorkerThread())
    {
        resolveCancel(park);
        return;
    }

    // From another thread the loop's state is not ours to touch, so it goes through the inbound
    // queue and step 1 resolves it. push_back can throw and this is noexcept: a lost cancel is a
    // flow that hangs, so terminating is the honest answer rather than swallowing it.
    {
        auto const lock = std::scoped_lock { _inboundMutex };
        _inbound.cancels.push_back(park);
    }
    _backend.wake();
}

void EventLoop::resolveCancel(ParkId park)
{
    auto* const entry = _parks.find(park);
    // No such park: it has already resumed, or it was never here. That is the generation check,
    // and the id itself is what performs it -- ids are never reused, so a stale request can never
    // name a park made since.
    if (entry == nullptr || !entry->parked)
        return;

    // Drop the kernel registration NOW, while the park is still alive: a stale registration could
    // fire again and queue a frame that has since unwound.
    if (entry->attached)
    {
        _backend.detach(entry->handler);
        entry->attached = false;
    }
    queueParkedWaiter(park);
}

void EventLoop::onParkReady(ReadinessHandler& handler) noexcept
{
    auto* const park = static_cast<detail::Park*>(handler.owner);
    park->loop->queueParkedWaiter(park->id);
}

void EventLoop::queueParkedWaiter(ParkId park)
{
    auto work = _parks.takeWaiter(park);
    if (!work.resume)
        return; // already taken this turn, or no such park
    if (work.resume.done())
        // Nothing will run await_resume for a finished frame, so nothing would ever unregister
        // this park; drop it here or the loop waits forever on a registration whose owner is gone.
        unregisterPark(park);
    queueReady(std::move(work));
}

void EventLoop::notifyHandleClosing(platform::NativeHandle handle, FdWakePolicy policy)
{
    if (handle == platform::InvalidHandle)
        return;

    for (auto const park: _parks.parksOn(handle))
    {
        auto* const entry = _parks.find(park);
        if (entry == nullptr)
            continue;
        // Drop the kernel registration NOW, while the descriptor is still open. Left to the
        // awaiter's own unregister it would be issued after the close, against a descriptor number
        // the kernel may already have reassigned -- unregistering whichever socket now holds it.
        // Detaching here also releases the private dup() a duplicate registration holds, which
        // would otherwise keep the peer's connection open past the close.
        if (entry->attached)
        {
            _backend.detach(entry->handler);
            entry->attached = false;
        }
        // The park itself stays: requestStop() and ~EventLoop find their waiters there, and must
        // still be able to cancel this one if either runs before the next turn.
        _closedParks.push_back(park);
        if (policy == FdWakePolicy::Cancel)
            _abandoned.insert(park);
    }
}

void EventLoop::unparkEverything()
{
    // **Only what the loop BORROWS is queued; what the loop owns stays parked and is freed.**
    //
    // The two are different questions with different answers, and `ParkedWork` is what tells them
    // apart. A chain the loop merely borrows is owned by a `Task` somebody holds -- a spawned
    // flow, a `blockOn` root -- and that owner set a stop token on it, so resuming it makes
    // `await_resume` observe the stop and throw `OperationCancelled`: the frame unwinds and its
    // RAII cleanup runs, which is the whole reason a teardown drains at all.
    //
    // A chain the loop OWNS is a `DetachedTask`, and a detached flow carries no stop token by
    // construction -- there is no awaiting coroutine to inherit one from. Resuming it would not
    // cancel it; it would run the rest of its body, on a loop that is being destroyed, with
    // nothing left for it to park on and its owner already gone. So it is left where it is and
    // freed by step 4, which is what
    // [fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025) concluded.
    // A CALLBACK park is neither, and it falls out of the `!entry->parked` test below: there is no
    // frame to unwind and nothing to free, and calling it would reach an owner that is being
    // destroyed. It is left for the table to drop, which teardown step 4 does.
    for (auto const park: _parks.ids())
    {
        auto* const entry = _parks.find(park);
        if (entry == nullptr || !entry->parked || entry->ownedByLoop)
            continue;
        if (entry->attached)
        {
            _backend.detach(entry->handler);
            entry->attached = false;
        }
        queueParkedWaiter(park);
    }
}

bool EventLoop::hasInbound() const
{
    auto const lock = std::scoped_lock { _inboundMutex };
    return !_inbound.empty();
}

bool EventLoop::hasPendingWork() const
{
    return !_ready.empty() || _parks.size() != 0 || !_closedParks.empty() || hasInbound();
}

DelayAwaiter EventLoop::delay(platform::SteadyDuration duration) noexcept
{
    return DelayAwaiter { *this, _clock.now() + duration };
}

DelayAwaiter EventLoop::sleepUntil(platform::SteadyTimePoint deadline) noexcept
{
    return DelayAwaiter { *this, deadline };
}

WaitHandleAwaiter EventLoop::waitReadable(platform::NativeHandle handle, HandleKind kind) noexcept
{
    return WaitHandleAwaiter { *this, handle, kind, Interest::Read };
}

WaitHandleAwaiter EventLoop::waitWritable(platform::NativeHandle handle, HandleKind kind) noexcept
{
    return WaitHandleAwaiter { *this, handle, kind, Interest::Write };
}

async::Task<void> pollUntil(EventLoop* loop,
                            std::function<bool()> predicate,
                            std::chrono::milliseconds interval)
{
    while (!predicate())
        co_await loop->delay(interval);
}

} // namespace core::net
