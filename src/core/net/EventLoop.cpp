// SPDX-License-Identifier: Apache-2.0
#include <core/net/EventLoop.hpp>

#include <core/net/detail/ReadyBatch.hpp>

#include <algorithm>
#include <ranges>
#include <tuple>
#include <utility>

namespace core::net
{

EventLoop::EventLoop(IoBackend& backend, platform::IClock& clock): _backend(backend), _clock(clock)
{
}

EventLoop::~EventLoop()
{
    // Cancel every spawned flow and let parked awaiters unwind via RAII before
    // their frames are destroyed. Order matters: request_stop() first so that when
    // wakeAllWaiters() requeues parked handles, drainReadyQueue() resumes them into
    // await_resume(), which sees stop_requested() and throws OperationCancelled —
    // unwinding the frame's locals. The frames then complete (done() == true), so
    // the spawned-flow Tasks destroy already-finished frames. Cancelled re-awaits
    // resume synchronously (await_suspend returns false when stop is requested), so
    // a single drain converges. No-op and effectively free when nothing is parked.
    //
    // Note what is deliberately NOT done here: _closedParks is left unconsumed.
    // Those wakes resume a flow on its NORMAL path, which is right while the loop
    // runs (the owner just called close) and wrong now — an owner declared after the
    // loop is already destroyed by the time this runs, so the flow must unwind via
    // the cancellation below instead. wakeAllWaiters still finds every such waiter,
    // because notifyHandleClosing leaves the park in place.
    _rootStop.request_stop();
    wakeAllWaiters();
    drainReadyQueue();
}

void EventLoop::spawn(async::Task<void> task)
{
    task.handle().promise().setStopToken(_rootStop.get_token());
    _ready.push_back(task.handle());
    _roots.push_back(std::move(task));
}

void EventLoop::post(std::function<void()> callback)
{
    {
        auto const lock = std::scoped_lock { _postMutex };
        _posted.push_back(std::move(callback));
    }
    // Break a possibly-blocked wait. The wakeup channel belongs to the backend —
    // `wake()` is the one member of IoBackend another thread may call — so the loop
    // holds no descriptor of its own for this and a backend with no wait to break
    // (HostDriven) still gets told there is work.
    _backend.wake();
}

void EventLoop::requestStop()
{
    _rootStop.request_stop();
    wakeAllWaiters();
}

void EventLoop::reapFinishedSpawns()
{
    std::erase_if(_roots, [](async::Task<void> const& task) { return task.done(); });
}

void EventLoop::runPostedCallbacks()
{
    // Swap under the lock, run outside it: a callback may itself post() (or spawn,
    // or resume coroutines that do), and must not deadlock or invalidate the
    // container mid-iteration. Work posted DURING the run lands in the fresh
    // vector and is picked up on the next pump.
    auto pending = std::vector<std::function<void()>> {};
    {
        auto const lock = std::scoped_lock { _postMutex };
        pending.swap(_posted);
    }
    for (auto const& callback: pending)
        callback();
}

void EventLoop::scheduleTimer(platform::SteadyTimePoint deadline, std::coroutine_handle<> waiter)
{
    _timers.push_back(TimerEntry { .deadline = deadline, .handle = waiter });
    std::ranges::push_heap(_timers, soonestFirst);
}

void EventLoop::onParkReady(ReadinessHandler& handler) noexcept
{
    auto* const park = static_cast<FdPark*>(handler.owner);
    park->loop->queueParkedWaiter(park->id);
}

ParkId EventLoop::registerFdWaiter(platform::NativeHandle fd,
                                   Interest interest,
                                   std::coroutine_handle<> waiter)
{
    if (fd == platform::InvalidHandle)
        return ParkId::invalid();

    auto const id = ParkId { ++_nextParkId };
    auto park = std::make_unique<FdPark>();
    park->loop = this;
    park->id = id;
    park->waiter = waiter;
    park->fd = fd;
    // Both directions point at the same callback, and there is deliberately no
    // onError: a failure then reaches whichever direction this park watches, which is
    // what a parked read and a parked accept both want — they resume, look, and
    // report what they find. A dedicated onError would have to decide that for them.
    park->handler = ReadinessHandler { .handle = fd,
                                       .kind = DefaultHandleKind,
                                       .owner = park.get(),
                                       .onReadable = &EventLoop::onParkReady,
                                       .onWritable = &EventLoop::onParkReady,
                                       .onError = nullptr };

    if (!_backend.attach(park->handler))
        return ParkId::invalid();
    // A refused interest must not leave a park behind claiming the handle is watched:
    // the awaiting flow has to fail rather than park on an interest the kernel never
    // accepted, which nothing could ever resume.
    if (!_backend.setInterest(park->handler, interest))
    {
        _backend.detach(park->handler);
        return ParkId::invalid();
    }

    _waiterToPark.emplace(waiter, id);
    _fdToParks.emplace(fd, id);
    _parks.emplace(id, std::move(park));
    return id;
}

std::coroutine_handle<> EventLoop::takeParkedWaiter(ParkId park) noexcept
{
    auto const found = _parks.find(park);
    if (found == _parks.end())
        return {};

    auto& entry = *found->second;
    auto const waiter = std::exchange(entry.waiter, {});
    if (!waiter)
        return {}; // already taken this pump

    _waiterToPark.erase(waiter);
    // Erase this park alone: a descriptor may carry a second one (a reader beside a
    // writer), and erasing by key would silently drop that one too.
    auto const [first, last] = _fdToParks.equal_range(entry.fd);
    auto const index =
        std::ranges::find_if(first, last, [park](auto const& candidate) { return candidate.second == park; });
    if (index != last)
        _fdToParks.erase(index);
    return waiter;
}

void EventLoop::queueParkedWaiter(ParkId park)
{
    auto const waiter = takeParkedWaiter(park);
    if (!waiter)
        return;
    if (waiter.done())
    {
        // Nothing will run await_resume for a finished frame, so nothing would ever
        // unregister this park; drop it here or the loop blocks forever on a
        // registration whose owner is gone.
        destroyPark(park);
        return;
    }
    _ready.push_back(waiter);
}

void EventLoop::destroyPark(ParkId park) noexcept
{
    auto const found = _parks.find(park);
    if (found == _parks.end())
        return;

    auto& entry = *found->second;
    if (entry.waiter)
    {
        _waiterToPark.erase(entry.waiter);
        auto const [first, last] = _fdToParks.equal_range(entry.fd);
        auto const index = std::ranges::find_if(
            first, last, [park](auto const& candidate) { return candidate.second == park; });
        if (index != last)
            _fdToParks.erase(index);
    }
    _backend.detach(entry.handler);
    _parks.erase(found);
}

FdWakeReason EventLoop::unregisterFdWaiter(ParkId park) noexcept
{
    if (!park)
        return FdWakeReason::Ready;
    destroyPark(park);
    // Consume the mark rather than merely reading it: the awaiter asks exactly once,
    // and an id left behind here would outlive its park and grow without bound on a
    // long-lived loop.
    return _abandoned.erase(park) != 0 ? FdWakeReason::Abandoned : FdWakeReason::Ready;
}

void EventLoop::notifyHandleClosing(platform::NativeHandle fd, FdWakePolicy policy)
{
    if (fd == platform::InvalidHandle)
        return;

    auto const [first, last] = _fdToParks.equal_range(fd);
    for (auto const& entry: std::ranges::subrange(first, last))
    {
        auto const park = entry.second;
        auto const found = _parks.find(park);
        if (found == _parks.end())
            continue;
        // Drop the kernel registration NOW, while the descriptor is still open. Left
        // to the awaiter's own unregister it would be issued after the close, against
        // a descriptor number the kernel may already have reassigned — unregistering
        // whichever socket now holds it. Detaching here also releases the private
        // dup() a duplicate registration holds, which would otherwise keep the peer's
        // connection open past the close.
        _backend.detach(found->second->handler);
        // The park itself stays: requestStop() and ~EventLoop find their waiters
        // there, and must still be able to cancel this one if either runs before the
        // next pump.
        _closedParks.push_back(park);
        if (policy == FdWakePolicy::Cancel)
            _abandoned.insert(park);
    }
}

void EventLoop::requeueForCancellation(std::coroutine_handle<> waiter)
{
    if (!waiter || waiter.done())
        return;

    // If parked on readiness, take it back and drop its registration so the stale
    // park cannot also fire. Use the reverse map for O(1) lookup.
    auto wasParked = false;
    if (auto const rt = _waiterToPark.find(waiter); rt != _waiterToPark.end())
    {
        destroyPark(rt->second);
        wasParked = true;
    }

    // If parked on a timer, remove its heap entry too. We re-queue the waiter below
    // so it unwinds via OperationCancelled and its frame is then destroyed; a
    // lingering entry would leave a dangling coroutine_handle that fireExpiredTimers()
    // or wakeAllWaiters() later dereference through .done()/.resume() — a
    // use-after-free. Detaching it here mirrors the readiness branch above. (A
    // coroutine is parked on at most one source, so at most one branch matches.)
    if (std::erase_if(_timers, [waiter](TimerEntry const& entry) { return entry.handle == waiter; }) != 0)
    {
        std::ranges::make_heap(_timers, soonestFirst);
        wasParked = true;
    }

    // Re-queue ONLY a waiter this call actually unparked. A waiter that was already
    // woken is sitting in _ready with its frame intact but its cancellation callback
    // still armed — await_resume, which disarms it, has not run yet — so a stop
    // requested in that window lands here and would queue it a SECOND time. The first
    // resume runs the flow to completion and its owner destroys the frame; the second
    // then calls .done() on freed memory. That is the use-after-free that made the
    // first attempt at close-wakeup segfault, and this guard is what removes it.
    if (wasParked)
        _ready.push_back(waiter);
}

void EventLoop::drainReadyQueue()
{
    // Rule 1, asserted where it would be broken: a backend dispatches, and the loop
    // resumes. A resume from inside a backend's walk over its own ready list lets the
    // resumed frame free the object whose entry the walk has not reached yet.
    assert(!detail::readinessDispatchInFlight()
           && "EventLoop::drainReadyQueue reached from inside a backend dispatch: "
              "backend callbacks may only enqueue");
    while (!_ready.empty())
    {
        auto const handle = _ready.front();
        _ready.pop_front();
        if (handle && !handle.done())
            handle.resume();
    }
}

std::optional<platform::SteadyDuration> EventLoop::computeTimeout() const
{
    if (_timers.empty())
        return std::nullopt; // block until something becomes ready

    auto const now = _clock.now();
    if (_timers.front().deadline <= now)
        return platform::SteadyDuration::zero();
    return _timers.front().deadline - now;
}

void EventLoop::fireExpiredTimers()
{
    auto const now = _clock.now();
    while (!_timers.empty() && _timers.front().deadline <= now)
    {
        std::ranges::pop_heap(_timers, soonestFirst);
        auto const entry = _timers.back();
        _timers.pop_back();
        if (entry.handle && !entry.handle.done())
            _ready.push_back(entry.handle);
    }
}

void EventLoop::wakeAllWaiters()
{
    for (auto const& entry: _timers)
        if (entry.handle && !entry.handle.done())
            _ready.push_back(entry.handle);
    _timers.clear();

    // Flush every parked flow so a cancelled awaitable can unwind. Move the parks out
    // first, so a resumed frame re-entering the loop cannot mutate the container
    // mid-iteration, then detach each from the backend and re-queue its handle;
    // await_resume then observes the requested stop and throws OperationCancelled.
    // The parks die with `parked` at the end of this function — after every detach,
    // so no handler is freed while the backend still holds its address.
    auto parked = std::exchange(_parks, {});
    _waiterToPark.clear();
    _fdToParks.clear();
    for (auto const& [park, entry]: parked)
    {
        _backend.detach(entry->handler);
        if (entry->waiter && !entry->waiter.done())
            _ready.push_back(entry->waiter);
    }
}

void EventLoop::pumpOnce()
{
    reapFinishedSpawns();
    runPostedCallbacks();
    drainReadyQueue();

    // Descriptors closed since the last pump. The backend cannot report these — epoll
    // drops a closed descriptor from its set and kqueue drops its filters, both
    // silently — so the loop supplies that readiness itself and MERGES it into this
    // pump below.
    //
    // Merging rather than short-circuiting is load-bearing. Returning early here
    // would skip the wait, and every other descriptor that became ready in the same
    // instant — a peer's EOF, most importantly — would go unreported until some
    // later pump that may never come, because the caller's blockOn exits as soon as
    // its root flow is done. poll(2) never had that problem: it reports POLLNVAL for
    // the closed descriptor alongside every other revent, in one call. This keeps
    // every backend doing the same.
    //
    // Taken BEFORE the wait so a pending close can turn it into a non-blocking poll:
    // blocking would wait for readiness that can no longer arrive.
    auto const closed = std::exchange(_closedParks, {});

    // Nothing is parked: a well-formed root flow either completed (the caller's loop
    // will observe `done()`) or is awaiting a child task that will itself park. A
    // cross-thread post() may have enqueued callbacks between the top-of-pump drain
    // and this check — drain them once more before returning so the blockOn loop,
    // which exits when done(), does not strand them.
    auto const hasParked = !_timers.empty() || !_parks.empty();
    if (!hasParked)
    {
        runPostedCallbacks();
        drainReadyQueue();
        return;
    }

    // The clock is re-sampled around the one blocking call, as IClock::refresh() asks of
    // whoever owns the loop: before the timeout is computed, so the time this turn spent
    // is not waited for again, and after the wait, so the timers fired and the flows
    // resumed below see the instant the wait ended at. A clock that reads the OS on every
    // now() ignores both; a CachedClock would otherwise never move.
    _clock.refresh();

    // A pending close polls instead of blocking: the closed descriptor can no longer
    // produce readiness, so an indefinite wait would never return on its account.
    // The wait DISPATCHES: every ready park's callback has already queued its
    // coroutine by the time this returns, and nothing has been resumed.
    std::ignore = _backend.wait(
        closed.empty() ? computeTimeout()
                       : std::optional<platform::SteadyDuration> { platform::SteadyDuration::zero() });
    _clock.refresh();

    // A cross-thread post both queues work and wakes the backend; the wake has been
    // consumed by the wait above, and this is where the work runs.
    runPostedCallbacks();

    // Then the closed descriptors, as one more source of readiness. After the wait, so
    // a park the backend also reported is queued exactly once — the first queueing
    // takes its waiter, and a park with no waiter left is skipped.
    for (auto const park: closed)
        queueParkedWaiter(park);

    fireExpiredTimers();

    // Resume coroutines queued during this iteration so readiness is delivered in
    // the same pump it arrived, rather than on the next one.
    drainReadyQueue();
}

DelayAwaiter EventLoop::delay(std::chrono::milliseconds duration) noexcept
{
    return DelayAwaiter { *this, _clock.now() + duration };
}

DelayAwaiter EventLoop::sleepUntil(platform::SteadyTimePoint deadline) noexcept
{
    return DelayAwaiter { *this, deadline };
}

WaitFdAwaiter EventLoop::waitReadable(platform::NativeHandle fd) noexcept
{
    return WaitFdAwaiter { *this, fd, Interest::Read };
}

WaitFdAwaiter EventLoop::waitWritable(platform::NativeHandle fd) noexcept
{
    return WaitFdAwaiter { *this, fd, Interest::Write };
}

async::Task<void> pollUntil(EventLoop* loop,
                            std::function<bool()> predicate,
                            std::chrono::milliseconds interval)
{
    while (!predicate())
        co_await loop->delay(interval);
}

} // namespace core::net
