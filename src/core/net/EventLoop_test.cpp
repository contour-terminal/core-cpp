// SPDX-License-Identifier: Apache-2.0
#include <core/async/Cancellation.hpp>
#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/WithTimeout.hpp>
#include <core/net/detail/WaitChunking.hpp>
#include <core/net/testing/ScriptedBackend.hpp>
#include <core/platform/Clock.hpp>
#include <core/platform/SystemPipe.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <ranges>
#include <thread>
#include <tuple>

using core::async::OperationCancelled;
using core::async::Task;
using core::net::EventLoop;
using core::net::testing::HandlerId;
using core::net::testing::ScriptedBackend;
using core::platform::ManualClock;

// Note on scripted registration ids: the loop no longer attaches a wakeup channel of
// its own — that belongs to the backend now, and ScriptedBackend has none — so the
// first coroutine fd waiter receives HandlerId{1}.

namespace
{

/// @param timeout A timeout a backend recorded.
/// @return The milliseconds it names, or -1 for an indefinite wait. Cases assert on
///         milliseconds because that is the unit the deadlines in them are written in;
///         the backend takes a duration so that the rounding to whatever its native
///         wait accepts happens once, in the backend, and not in every caller.
[[nodiscard]] long long timeoutMs(std::optional<core::platform::SteadyDuration> const& timeout)
{
    return timeout.has_value() ? std::chrono::duration_cast<std::chrono::milliseconds>(*timeout).count() : -1;
}

/// Resumes immediately when the delay has already elapsed (the ready path).
Task<int> awaitZeroDelay(EventLoop* loop)
{
    co_await loop->delay(std::chrono::milliseconds { 0 });
    co_return 7;
}

/// Parks on a delay of @p delayMs, then sets *fired and returns it. Used with a
/// ManualClock to prove the timer fires only once the clock crosses the deadline.
Task<int> awaitDelayThenFire(EventLoop* loop, int delayMs, bool* fired)
{
    co_await loop->delay(std::chrono::milliseconds { delayMs });
    *fired = true;
    co_return delayMs;
}

/// Spends @p spent of @p clock's time, as a batch of work that long would, then parks on a delay of
/// @p delayMs and sets *fired once it fires.
Task<void> spendThenDelay(
    EventLoop* loop, ManualClock* clock, std::chrono::milliseconds spent, int delayMs, bool* fired)
{
    clock->advance(spent);
    co_await loop->delay(std::chrono::milliseconds { delayMs });
    *fired = true;
}

/// Sets *flag true after @p delayMs — a stand-in for the async condition
/// (queue drained, debounce fired) that pollUntil waits on.
Task<void> setFlagAfter(EventLoop* loop, int delayMs, bool* flag)
{
    co_await loop->delay(std::chrono::milliseconds { delayMs });
    *flag = true;
}

/// Polls until @p flag is set, then reports how it exited (true) plus the number
/// of poll iterations it took (via @p polls).
Task<bool> pollForFlag(EventLoop* loop, bool* flag, int* polls)
{
    co_await core::net::pollUntil(loop, [flag, polls] {
        ++*polls;
        return *flag;
    });
    co_return *flag;
}

/// Waits for @p fd to become readable; returns 1 on readiness or @p cancelSentinel
/// if cancelled while parked.
Task<int> awaitReadableOrCancel(EventLoop* loop, core::platform::NativeHandle fd, int cancelSentinel)
{
    try
    {
        co_await loop->waitReadable(fd);
        co_return 1;
    }
    catch (OperationCancelled const&)
    {
        co_return cancelSentinel;
    }
}

/// Waits for @p fd to become readable, distinguishing a refused registration from a
/// cancellation: the first is a plumbing failure the caller can report, the second is a
/// deliberate stop, and a flow that cannot tell them apart logs the wrong one.
Task<int> awaitReadableOrRefusal(EventLoop* loop, core::platform::NativeHandle fd)
{
    try
    {
        co_await loop->waitReadable(fd);
        co_return 1;
    }
    catch (core::net::FdRegistrationFailed const&)
    {
        co_return -2;
    }
    catch (OperationCancelled const&)
    {
        co_return -1;
    }
}

/// A value-producing task that completes synchronously — the "work wins" arm.
Task<int> produceValue(int value)
{
    co_return value;
}

/// Parks on a never-ready fd forever (until cancelled) — the "timeout wins" arm.
Task<int> parkOnFdForever(EventLoop* loop, core::platform::NativeHandle fd)
{
    co_await loop->waitReadable(fd);
    co_return 0;
}

/// A background flow with an observable completion, for the spawn-reap test.
Task<void> incrementAndFinish(int* counter)
{
    ++*counter;
    co_return;
}

/// A root flow that completes at once, so `blockOn` pumps exactly as far as the
/// spawned flows need and no further.
Task<void> justReturn()
{
    co_return;
}

/// Sets *destroyed = true when its frame unwinds (RAII), so a test can prove a
/// parked flow was cancelled-and-unwound rather than raw-destroyed.
struct UnwindFlag
{
    bool* destroyed;

    ~UnwindFlag() { *destroyed = true; }
};

/// Parks on waitReadable with an RAII guard; proves the frame unwinds (guard runs)
/// if the loop is torn down while the fd wait is parked.
Task<void> waitReadableWithGuard(EventLoop* loop, core::platform::NativeHandle fd, bool* destroyed)
{
    *destroyed = false;
    auto guard = UnwindFlag { destroyed };
    try
    {
        co_await loop->waitReadable(fd);
    }
    catch (OperationCancelled const&)
    {
        // Expected on loop teardown: the frame unwinds and `guard` destructs,
        // which is exactly what this flow exists to demonstrate.
        static_cast<void>(destroyed);
    }
}

/// A ScriptedBackend that advances an injected ManualClock by a fixed step on every
/// wait(). This models the passage of time deterministically: the loop schedules a
/// delay against the clock, and each blocking wait "elapses" exactly `step` of clock
/// time, so a delay fires after a known number of waits — with no real sleeping.
class ClockAdvancingBackend: public ScriptedBackend
{
  public:
    ClockAdvancingBackend(ManualClock& clock, std::chrono::milliseconds step) noexcept:
        _clock(clock), _step(step)
    {
    }

    core::net::WaitResult wait(std::optional<core::platform::SteadyDuration> timeout) override
    {
        _clock.advance(_step);
        return ScriptedBackend::wait(timeout);
    }

  private:
    ManualClock& _clock;
    std::chrono::milliseconds _step;
};

} // namespace

TEST_CASE("delay(0) resumes without waiting", "[EventLoop]")
{
    auto source = ScriptedBackend {};
    auto loop = EventLoop { source };

    auto const result = loop.blockOn(awaitZeroDelay(&loop));

    REQUIRE(result == 7);
    REQUIRE(source.waitCount() == 0); // ready path never blocks
}

TEST_CASE("A pending delay bounds the wait timeout and fires deterministically", "[EventLoop][clock]")
{
    // With time frozen except for the scripted 250ms step per wait, a delay(500)
    // must bound the FIRST wait to exactly 500ms (not block indefinitely at -1)
    // and fire on the second wait, once the clock has crossed the deadline.
    auto clock = ManualClock {};
    auto source = ClockAdvancingBackend { clock, std::chrono::milliseconds { 250 } };
    source.pushTimeout(); // 250ms elapsed: still pending
    source.pushTimeout(); // 500ms elapsed: timer is now due -> flow resumes
    auto loop = EventLoop { source, clock };

    auto fired = false;
    auto const result = loop.blockOn(awaitDelayThenFire(&loop, 500, &fired));

    REQUIRE(fired);
    REQUIRE(result == 500);
    REQUIRE(source.waitCount() == 2);
    REQUIRE(timeoutMs(source.recordedTimeouts().front()) == 500); // exact: no real-clock jitter
    REQUIRE(timeoutMs(source.recordedTimeouts().back()) == 250);  // the remaining half
}

TEST_CASE("The loop refreshes a caching clock before each timeout and after each wait", "[EventLoop][clock]")
{
    // A CachedClock serves the instant of its last refresh(), and IClock's contract makes whoever
    // owns the loop refresh it: after the wait returns, so the turn sees the instant the wait ended
    // at, and before a timeout is computed, so the time the turn spent is not waited for again.
    // Each blockOn(justReturn()) below is one pump, and so one wait.
    auto manual = ManualClock {};
    auto cached = core::platform::CachedClock { manual };
    auto source = ClockAdvancingBackend { manual, std::chrono::milliseconds { 250 } };
    source.pushTimeout();
    source.pushTimeout();
    auto fired = false; // declared before the loop, which may still hold the flow when it goes
    auto loop = EventLoop { source, cached };

    // The delay is scheduled against the clock's first sample, 0, and so is due at 500. The flow
    // spends 100 of that before the first wait, which only a refresh before the timeout counts.
    loop.spawn(spendThenDelay(&loop, &manual, std::chrono::milliseconds { 100 }, 500, &fired));
    loop.blockOn(justReturn());
    CHECK(timeoutMs(source.recordedTimeouts().back()) == 400);
    CHECK_FALSE(fired);

    // The first wait ended at 350 and the second ends at 600: only a refresh after each wait lets
    // the loop see either, and so time the second wait by 150 and fire the delay after it.
    loop.blockOn(justReturn());
    CHECK(timeoutMs(source.recordedTimeouts().back()) == 150);
    CHECK(fired);
    CHECK(source.waitCount() == 2);
}

TEST_CASE("pollUntil returns as soon as its predicate holds", "[EventLoop][poll]")
{
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };

    auto flag = false;
    auto polls = 0;
    loop.spawn(setFlagAfter(&loop, 5, &flag)); // flips true a few poll ticks in
    auto const done = loop.blockOn(pollForFlag(&loop, &flag, &polls));

    CHECK(done);
    CHECK(polls >= 2); // checked at least once before and once after the flag flipped
}

TEST_CASE("pollUntil returns immediately when the predicate already holds", "[EventLoop][poll]")
{
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };

    auto flag = true; // already satisfied: no delay should be awaited
    auto polls = 0;
    auto const done = loop.blockOn(pollForFlag(&loop, &flag, &polls));

    CHECK(done);
    CHECK(polls == 1); // one check, then a prompt return
}

namespace
{

/// Counts a dispatch into the `int` its handler's owner points at.
void countDispatch(core::net::ReadinessHandler& handler) noexcept
{
    ++*static_cast<int*>(handler.owner);
}

} // namespace

TEST_CASE("the scripted backend dispatches to the registration its script names", "[net][backend]")
{
    // The scripted backend ignores the handle value (it names registrations by attach
    // order); a real SystemPipe supplies portable valid handles, because NativeHandle
    // is void* on Windows and integer literals would not compile.
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    auto reader = 0;
    auto writer = 0;
    auto readHandler = core::net::ReadinessHandler { .handle = (*pipe)->readFd(),
                                                     .owner = &reader,
                                                     .onReadable = &countDispatch };
    auto writeHandler = core::net::ReadinessHandler { .handle = (*pipe)->writeFd(),
                                                      .owner = &writer,
                                                      .onWritable = &countDispatch };

    auto source = ScriptedBackend {};
    REQUIRE(source.attach(readHandler).has_value());
    auto const a = source.lastHandlerId();
    REQUIRE(source.attach(writeHandler).has_value());
    auto const b = source.lastHandlerId();

    REQUIRE(static_cast<bool>(a));
    REQUIRE(static_cast<bool>(b));
    REQUIRE(a != b);
    REQUIRE(source.attachedCount() == 2);

    REQUIRE(source.setInterest(readHandler, core::net::Interest::Read).has_value());
    REQUIRE(source.setInterest(writeHandler, core::net::Interest::Write).has_value());
    CHECK(source.interestOf(a) == core::net::Interest::Read);
    CHECK(source.interestOf(b) == core::net::Interest::Write);

    source.pushReadable(a);
    source.pushWritable(b);

    CHECK(source.wait(core::platform::SteadyDuration::zero()).dispatched == 1);
    CHECK(reader == 1);
    CHECK(writer == 0);

    CHECK(source.wait(core::platform::SteadyDuration::zero()).dispatched == 1);
    CHECK(writer == 1);
    CHECK(reader == 1);

    source.detach(readHandler);
    source.detach(writeHandler);
    REQUIRE(source.attachedCount() == 0);
}

TEST_CASE("the scripted backend detaches idempotently, like every real one", "[net][backend]")
{
    // IoBackend::detach is documented idempotent, and the loop really does detach twice
    // on normal paths — notifyHandleClosing then unregisterFdWaiter;
    // requeueForCancellation and wakeAllWaiters before await_resume. The scripted
    // double once counted DETACH CALLS instead of live registrations, so a second
    // detach of one registration cancelled out a different one: attachedCount() then
    // under-reported, and a leak assertion against it would pass on a registration that
    // never went away.
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    auto a = core::net::ReadinessHandler { .handle = (*pipe)->readFd() };
    auto b = core::net::ReadinessHandler { .handle = (*pipe)->writeFd() };
    auto stranger = core::net::ReadinessHandler { .handle = (*pipe)->readFd() };

    auto source = ScriptedBackend {};
    REQUIRE(source.attach(a).has_value());
    REQUIRE(source.attach(b).has_value());
    REQUIRE(source.attachedCount() == 2);

    source.detach(a);
    source.detach(a); // the second detach of the SAME handler must change nothing
    CHECK(source.attachedCount() == 1);

    source.detach(stranger); // one that was never attached is a no-op too
    CHECK(source.attachedCount() == 1);

    source.detach(b);
    CHECK(source.attachedCount() == 0);
}

TEST_CASE("waitReadable resumes when the registered fd becomes readable", "[EventLoop][fd]")
{
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    auto source = ScriptedBackend {};
    // The awaiter attaches the fd during await_suspend, and it is the loop's first
    // registration — the wakeup channel belongs to the backend now, and this one has
    // none — so the waiter receives HandlerId{1}. Script that one readable.
    source.pushReadable(HandlerId { 1 });
    auto loop = EventLoop { source };

    constexpr auto Cancelled = -1;
    auto const result = loop.blockOn(awaitReadableOrCancel(&loop, (*pipe)->readFd(), Cancelled));

    REQUIRE(result == 1);
}

TEST_CASE("notifyHandleClosing on an unwatched fd records nothing", "[EventLoop][fd][closehang]")
{
    // Closing a descriptor nobody is parked on must not schedule any wake. If it did,
    // the next pump would deliver a ParkId naming a park that no longer exists —
    // harmless today only because queueParkedWaiter skips an unknown one, but an id
    // can be reused, and then the wake would land on an unrelated flow.
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    auto source = ScriptedBackend {};
    auto loop = EventLoop { source };

    loop.notifyHandleClosing((*pipe)->readFd(), core::net::FdWakePolicy::Resume);
    // And an invalid handle is refused outright rather than looked up: on Windows a
    // NativeHandle is a pointer, so a null one would otherwise be a perfectly good
    // multimap key that any other unset handle could collide with.
    loop.notifyHandleClosing(core::platform::InvalidHandle, core::net::FdWakePolicy::Cancel);

    // Nothing parked and nothing recorded, so this pump neither waits nor resumes.
    // A recorded wake would have turned the wait into a poll; an exhausted script
    // would have thrown had one been attempted.
    auto counter = 0;
    loop.blockOn(incrementAndFinish(&counter));
    REQUIRE(counter == 1);
    REQUIRE(source.waitCount() == 0);
}

TEST_CASE("notifyHandleClosing detaches the registration while the fd is still valid",
          "[EventLoop][fd][closehang]")
{
    // The kernel registration has to be dropped at CLOSE time, not left for the
    // awaiter's own detach. By then the descriptor number may have been reassigned
    // to a new socket, and epoll_ctl(EPOLL_CTL_DEL) / kqueue's delete-by-descriptor
    // would unregister THAT one instead. attachedCount is the observable proof.
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    // Declared BEFORE the loop, so it outlives it: ~EventLoop resumes the still-
    // parked flow, whose RAII guard writes here as it unwinds.
    auto destroyed = false;

    auto source = ScriptedBackend {};
    source.pushTimeout(); // the park's first wait reports nothing
    auto loop = EventLoop { source };

    loop.spawn(waitReadableWithGuard(&loop, (*pipe)->readFd(), &destroyed));
    loop.blockOn(justReturn()); // let the spawned flow reach its park

    REQUIRE(source.attachedCount() == 1); // the parked waiter's registration
    REQUIRE(loop.parkedWaiterCount() == 1);

    loop.notifyHandleClosing((*pipe)->readFd(), core::net::FdWakePolicy::Resume);
    REQUIRE(source.attachedCount() == 0); // detached immediately, not at resume
    // ... and the park itself stays, because requestStop() and ~EventLoop must still
    // find this waiter if either runs before the next pump.
    REQUIRE(loop.parkedWaiterCount() == 1);
}

TEST_CASE("a recorded close is delivered without blocking the pump", "[EventLoop][fd][closehang]")
{
    // A closed descriptor can no longer produce readiness, so the pump must not
    // block waiting for it. It still performs its wait — merged, not skipped, so
    // nothing else ready in the same instant is starved — but with a zero timeout.
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    // Declared BEFORE the loop, so it outlives it (see the case above).
    auto destroyed = false;

    auto source = ScriptedBackend {};
    source.pushTimeout(); // the park's wait
    source.pushTimeout(); // the close-wake pump's wait, which must be a poll
    auto loop = EventLoop { source };

    loop.spawn(waitReadableWithGuard(&loop, (*pipe)->readFd(), &destroyed));
    loop.blockOn(justReturn());
    auto const waitsBeforeClose = source.waitCount();

    loop.notifyHandleClosing((*pipe)->readFd(), core::net::FdWakePolicy::Resume);
    loop.blockOn(justReturn());

    REQUIRE(source.waitCount() == waitsBeforeClose + 1);
    // Zero, not -1: an indefinite wait would never return on the closed fd's account.
    REQUIRE(timeoutMs(source.recordedTimeouts().back()) == 0);
    REQUIRE(destroyed); // the parked flow resumed and unwound
}

TEST_CASE("a registration the backend refuses fails the await rather than parking it", "[EventLoop][fd]")
{
    // The whole reason IoBackend::attach answers an expected. A flow parked on a
    // registration the backend never made has nothing left to resume it: no message,
    // no stack, just a hang. So the awaiter resumes at once and throws
    // FdRegistrationFailed, which is distinct from OperationCancelled because a
    // plumbing failure and a deliberate stop are different things to report.
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    auto source = ScriptedBackend {};
    source.refuseNextAttach();
    auto loop = EventLoop { source };

    REQUIRE(loop.blockOn(awaitReadableOrRefusal(&loop, (*pipe)->readFd())) == -2);
    CHECK(source.attachedCount() == 0);
    CHECK(loop.parkedWaiterCount() == 0);
    CHECK(source.waitCount() == 0); // it never parked, so the loop never waited
}

TEST_CASE("a kernel that refuses the interest leaves no registration behind", "[EventLoop][fd]")
{
    // fastcached#1054's shape as the loop meets it: `attach` succeeded and
    // `setInterest` did not, which on kqueue is the ordinary way a refusal arrives
    // because only the filter reaches the kernel at all. The park must not survive
    // that, or the backend keeps a registration for a flow that has already unwound —
    // and on a real backend that registration names a descriptor the caller is about
    // to close.
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    auto source = ScriptedBackend {};
    source.refuseNextSetInterest();
    auto loop = EventLoop { source };

    REQUIRE(loop.blockOn(awaitReadableOrRefusal(&loop, (*pipe)->readFd())) == -2);
    CHECK(source.attachedCount() == 0); // the attach was undone, not left dangling
    CHECK(loop.parkedWaiterCount() == 0);
    CHECK(source.waitCount() == 0);
}

TEST_CASE("a hangup resumes a parked reader, because a park watches one direction", "[EventLoop][fd]")
{
    // A park registers one direction and NO onError, so a failure the kernel
    // volunteers — a hangup, a peer's reset — reaches the direction it does watch.
    // That is what lets the flow resume, look, and report EOF; routing it nowhere
    // would leave it parked on a descriptor that is level-triggered and reported
    // again on every wait, which is a loop at 100% CPU telling nobody.
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    auto source = ScriptedBackend {};
    source.pushFailure(HandlerId { 1 });
    auto loop = EventLoop { source };

    constexpr auto Cancelled = -1;
    REQUIRE(loop.blockOn(awaitReadableOrCancel(&loop, (*pipe)->readFd(), Cancelled)) == 1);
    CHECK(loop.parkedWaiterCount() == 0);
}

TEST_CASE("waitReadable on an invalid fd resolves immediately as cancelled", "[EventLoop][fd]")
{
    auto source = ScriptedBackend {};
    auto loop = EventLoop { source };

    constexpr auto Cancelled = -3;
    auto const result = loop.blockOn(awaitReadableOrCancel(&loop, core::platform::InvalidHandle, Cancelled));

    REQUIRE(result == Cancelled);
    REQUIRE(source.waitCount() == 0); // never blocked: an unwaitable fd resolves inline
}

TEST_CASE("waitReadable resolves over a real SystemPipe via the default backend", "[EventLoop][fd][poll]")
{
    // End-to-end through the real OS readiness path (epoll, kqueue, poll(2) or
    // WaitForMultipleObjects), not the scripted one: a SystemPipe whose write end
    // already holds a byte is
    // readable, so a flow parked on waitReadable resolves on the first real wait and
    // reads the byte back.
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    char const payload = 'Z';
    REQUIRE((*pipe)->write(&payload, 1).has_value());

    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };

    auto readByte = [](EventLoop* l, core::platform::SystemPipe* p) -> Task<char> {
        co_await l->waitReadable(p->waitHandle());
        char buf = 0;
        auto const got = p->read(&buf, 1);
        co_return (got.has_value() && got->bytesRead() == 1) ? buf : '\0';
    };

    auto const result = loop.blockOn(readByte(&loop, pipe->get()));
    REQUIRE(result == 'Z');
    REQUIRE(loop.parkedWaiterCount() == 0); // the resumed waiter unregistered its park
}

TEST_CASE("post() wakes a blocked wait and runs its callback on the loop thread", "[EventLoop][post]")
{
    // The root flow parks on a pipe that never receives data, so the backend blocks
    // indefinitely: ONLY IoBackend::wake, which post() calls, can end that wait. A
    // second thread posts a callback that feeds the pipe; the flow completing at all
    // proves the cross-thread wakeup, and the recorded thread id proves the callback
    // ran on the loop thread, not the poster's.
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };

    auto const loopThread = std::this_thread::get_id();
    auto callbackThread = std::thread::id {};

    auto poster = std::thread { [&] {
        loop.post([&] {
            callbackThread = std::this_thread::get_id();
            char const byte = 'x';
            std::ignore = (*pipe)->write(&byte, 1);
        });
    } };

    constexpr auto Cancelled = -1;
    auto const result = loop.blockOn(awaitReadableOrCancel(&loop, (*pipe)->waitHandle(), Cancelled));
    poster.join();

    REQUIRE(result == 1);
    REQUIRE(callbackThread == loopThread);
}

TEST_CASE("requestStop() posted from another thread cancels a parked flow", "[EventLoop][post]")
{
    // The daemon's shutdown path: a signal handler thread posts requestStop(); the
    // parked waitReadable unwinds via OperationCancelled instead of waiting for an
    // fd that will never become ready.
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };

    auto poster = std::thread { [&] { loop.post([&] { loop.requestStop(); }); } };

    constexpr auto Cancelled = -7;
    auto const result = loop.blockOn(awaitReadableOrCancel(&loop, (*pipe)->waitHandle(), Cancelled));
    poster.join();

    REQUIRE(result == Cancelled);
    REQUIRE(loop.parkedWaiterCount() == 0); // the cancelled waiter unregistered its park
}

TEST_CASE("Finished spawned flows are reaped on the next pump", "[EventLoop][spawn]")
{
    // Upstream Endo held every spawned frame until destruction — an unbounded leak
    // for a long-lived loop spawning per-connection flows. The reap runs at the top
    // of every pump, so frames finished during one blockOn are reclaimed by the
    // first pump of the next.
    auto source = ScriptedBackend {};
    auto loop = EventLoop { source };

    auto counter = 0;
    loop.spawn(incrementAndFinish(&counter));
    loop.spawn(incrementAndFinish(&counter));
    REQUIRE(loop.spawnedCount() == 2);

    loop.blockOn(awaitZeroDelay(&loop));
    REQUIRE(counter == 2); // both flows ran to completion...
    // ...but were not yet reaped: the reap preceding their completion already ran.
    REQUIRE(loop.spawnedCount() == 2);

    loop.blockOn(awaitZeroDelay(&loop));
    REQUIRE(loop.spawnedCount() == 0); // the next pump's reap reclaimed the frames
}

TEST_CASE("withTimeout returns the work's value when it finishes first", "[EventLoop][timeout]")
{
    auto source = ScriptedBackend {};
    auto loop = EventLoop { source };

    // The work completes synchronously, so the timeout arm never matters.
    auto result = loop.blockOn(core::net::withTimeout(&loop, produceValue(42), std::chrono::seconds { 10 }));

    REQUIRE(result.has_value());
    REQUIRE(*result == 42);
}

TEST_CASE("withTimeout returns nullopt and cancels the work when the deadline fires",
          "[EventLoop][timeout][poll]")
{
    // The work parks on a SystemPipe that never receives data, so only the timeout
    // arm can win. When it does, whenAny requests stop; the parked waitReadable is
    // re-queued via the stop-callback (requeueForCancellation) and unwinds, so the
    // work is genuinely cancelled rather than leaking. A short real timeout drives
    // the loop's timer.
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };

    auto result = loop.blockOn(core::net::withTimeout(
        &loop, parkOnFdForever(&loop, (*pipe)->waitHandle()), std::chrono::milliseconds { 20 }));

    REQUIRE_FALSE(result.has_value());      // the timeout won
    REQUIRE(loop.parkedWaiterCount() == 0); // the cancelled work unregistered its park
}

TEST_CASE("withTimeout drops the loser's timer entry when the work wins after parking",
          "[EventLoop][timeout][clock]")
{
    // Regression: the work first PARKS on a timer, then wins before the deadline.
    // The timeout arm is therefore genuinely scheduled on the timer heap (unlike the
    // synchronous-work case above, where it never parks). When the work wins, whenAny
    // cancels the timeout arm via requeueForCancellation, which MUST drop its heap
    // entry — otherwise the arm's frame is destroyed with a live timer entry still
    // pointing at it, and the loop later dereferences that dangling handle
    // (fireExpiredTimers() at the deadline, or ~EventLoop's wakeAllWaiters at teardown
    // below) — a use-after-free.
    auto clock = ManualClock {};
    auto source = ClockAdvancingBackend { clock, std::chrono::milliseconds { 20 } };
    source.pushTimeout(); // one wait: advances the clock past the work's 10ms delay
    source.pushTimeout(); // spare, should the drain need another pump
    auto loop = EventLoop { source, clock };

    auto fired = false;
    auto const result = loop.blockOn(core::net::withTimeout(
        &loop, awaitDelayThenFire(&loop, 10, &fired), std::chrono::milliseconds { 500 }));

    REQUIRE(fired);
    REQUIRE(result.has_value());
    REQUIRE(*result == 10);
    // The cancelled timeout arm left NO timer entry behind, so no dangling handle
    // survives for the loop teardown (and ASan) to trip over.
    REQUIRE(loop.pendingTimerCount() == 0);
}

TEST_CASE("Destroying the loop unwinds a flow parked on waitReadable", "[EventLoop][fd]")
{
    auto pipe = core::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    auto source = ScriptedBackend {};
    source.pushTimeout(); // benign wait for the root's post-completion pump
    auto destroyed = false;
    {
        auto loop = EventLoop { source };
        loop.spawn(waitReadableWithGuard(&loop, (*pipe)->readFd(), &destroyed));
        loop.blockOn(awaitZeroDelay(&loop)); // drive the spawned flow to its park
        REQUIRE_FALSE(destroyed);
    } // ~EventLoop: stop + flush fd waiters + drain -> the frame unwinds, guard runs

    REQUIRE(destroyed);
}

// The pure chunking / rotation math that the Windows backend uses to
// wait on more than MAXIMUM_WAIT_OBJECTS handles. Platform-neutral (no windows.h)
// so it is exercised here on every platform, including this Linux CI.
TEST_CASE("WaitChunking splits a handle set into wait-sized chunks", "[WaitChunking]")
{
    constexpr std::size_t MaxChunk = 64; // MAXIMUM_WAIT_OBJECTS on Windows.

    SECTION("chunk count is the ceiling of the total over the max chunk size")
    {
        CHECK(core::net::waitChunkCount(0, MaxChunk) == 0);
        CHECK(core::net::waitChunkCount(1, MaxChunk) == 1);
        CHECK(core::net::waitChunkCount(64, MaxChunk) == 1);
        CHECK(core::net::waitChunkCount(65, MaxChunk) == 2);
        CHECK(core::net::waitChunkCount(128, MaxChunk) == 2);
        CHECK(core::net::waitChunkCount(129, MaxChunk) == 3);
        CHECK(core::net::waitChunkCount(200, MaxChunk) == 4);
    }

    SECTION("the boundary case just past one wait yields a full chunk and a remainder")
    {
        REQUIRE(core::net::waitChunkCount(65, MaxChunk) == 2);
        CHECK(core::net::waitChunkAt(65, MaxChunk, 0) == core::net::WaitChunk { .offset = 0, .count = 64 });
        CHECK(core::net::waitChunkAt(65, MaxChunk, 1) == core::net::WaitChunk { .offset = 64, .count = 1 });
    }

    SECTION("chunks tile the handle array with no gaps, overlaps, or oversized spans")
    {
        constexpr std::size_t Total = 200;
        auto const chunks = core::net::waitChunkCount(Total, MaxChunk);
        REQUIRE(chunks == 4);

        auto expectedOffset = std::size_t { 0 };
        for (auto const i: std::views::iota(std::size_t { 0 }, chunks))
        {
            auto const chunk = core::net::waitChunkAt(Total, MaxChunk, i);
            CHECK(chunk.offset == expectedOffset); // contiguous: no gap and no overlap
            CHECK(chunk.count >= 1);
            CHECK(chunk.count <= MaxChunk); // never larger than one wait accepts
            expectedOffset += chunk.count;
        }
        CHECK(expectedOffset == Total); // every handle covered exactly once
    }
}

TEST_CASE("WaitChunking rotates the start chunk fairly and maps indices back", "[WaitChunking]")
{
    SECTION("rotation advances by one and wraps at the chunk count")
    {
        CHECK(core::net::nextWaitRotation(4, 0) == 1);
        CHECK(core::net::nextWaitRotation(4, 1) == 2);
        CHECK(core::net::nextWaitRotation(4, 2) == 3);
        CHECK(core::net::nextWaitRotation(4, 3) == 0); // wrap
    }

    SECTION("a stale cursor past the chunk count is folded back into range")
    {
        CHECK(core::net::nextWaitRotation(4, 10) == 3); // 10 % 4 == 2, then +1
        CHECK(core::net::nextWaitRotation(4, 7) == 0);  // 7 % 4 == 3, then wraps
        CHECK(core::net::nextWaitRotation(1, 0) == 0);  // a single chunk always stays put
    }
}
