// SPDX-License-Identifier: Apache-2.0
#include <core/async/Cancellation.hpp>
#include <core/async/Task.hpp>
#include <core/async/WhenAll.hpp>
#include <core/async/WhenAny.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/WithTimeout.hpp>
#include <core/net/testing/BackendMatrix.hpp>
#include <core/net/testing/ScriptedBackend.hpp>
#include <core/platform/Clock.hpp>
#include <core/platform/SignalHandler.hpp>
#include <core/platform/SystemPipe.hpp>
#include <core/platform/Types.hpp>
#include <core/platform/Wakeup.hpp>
#include <core/tui/InputEvent.hpp>
#include <core/tui/runtime/TuiRuntime.hpp>
#include <core/tui/runtime/testing/ScriptedInputSource.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using namespace std::chrono_literals;

using core::async::OperationCancelled;
using core::async::Task;
using core::net::EventLoop;
using core::net::testing::HandlerId;
using core::net::testing::ScriptedBackend;
using core::platform::ManualClock;
using core::platform::SystemPipe;
using core::tui::InputEvent;
using core::tui::KeyEvent;
using core::tui::runtime::ActivityKind;
using core::tui::runtime::TuiRuntime;
using core::tui::runtime::TuiRuntimeOptions;
using core::tui::runtime::testing::ScriptedInputSource;

// On registration ids: the runtime's input flow is the first thing to park, and a source with no
// resize channel starts no second flow, so the input registration is HandlerId{1}. It re-registers
// on every re-park, so the SECOND read of a scripted case is HandlerId{2}.

namespace
{

KeyEvent keyOf(char32_t codepoint)
{
    return KeyEvent { .codepoint = codepoint };
}

/// @param pipes The pipes to open.
/// @return @p pipes open channels, or an empty vector if the OS refused one.
[[nodiscard]] std::vector<std::unique_ptr<SystemPipe>> openPipes(std::size_t pipes)
{
    auto opened = std::vector<std::unique_ptr<SystemPipe>> {};
    opened.reserve(pipes);
    for ([[maybe_unused]] auto const index: std::views::iota(std::size_t { 0 }, pipes))
    {
        auto pipe = core::platform::createSystemPipe();
        if (!pipe)
            return {};
        opened.push_back(std::move(*pipe));
    }
    return opened;
}

/// Returns the codepoint of the next key event the runtime delivers.
Task<char32_t> awaitOneKeyCodepoint(TuiRuntime* runtime)
{
    auto const event = co_await runtime->nextEvent();
    co_return std::get<KeyEvent>(event).codepoint;
}

/// Returns the next key's codepoint and records the thread its flow resumed on.
Task<char32_t> awaitOneKeyOnThread(TuiRuntime* runtime, std::thread::id* resumedOn)
{
    auto const event = co_await runtime->nextEvent();
    *resumedOn = std::this_thread::get_id();
    co_return std::get<KeyEvent>(event).codepoint;
}

/// Sums the codepoints of @p count key events, in arrival order.
Task<int> sumKeyCodepoints(TuiRuntime* runtime, int count)
{
    auto sum = 0;
    for ([[maybe_unused]] auto const index: std::views::iota(0, count))
    {
        auto const event = co_await runtime->nextEvent();
        sum += static_cast<int>(std::get<KeyEvent>(event).codepoint);
    }
    co_return sum;
}

/// Returns 1 if nextEventFor yielded an event, 0 if it resolved with none.
Task<int> awaitEventForResult(TuiRuntime* runtime, int timeoutMs)
{
    auto const event = co_await runtime->nextEventFor(std::chrono::milliseconds { timeoutMs });
    co_return event.has_value() ? 1 : 0;
}

/// Returns the kind of the first activity nextActivity observes (as its enum value).
Task<int> awaitActivityKind(TuiRuntime* runtime, int timeoutMs)
{
    auto const activity = co_await runtime->nextActivity(std::chrono::milliseconds { timeoutMs });
    co_return static_cast<int>(activity.kind);
}

/// Returns true once an agent message is observed.
Task<bool> awaitAgentReady(TuiRuntime* runtime)
{
    co_await runtime->nextAgentReady();
    co_return true;
}

/// Awaits input; returns a sentinel if cancelled instead.
Task<int> awaitKeyOrCancel(TuiRuntime* runtime, int cancelSentinel)
{
    try
    {
        auto const event = co_await runtime->nextEvent();
        co_return static_cast<int>(std::get<KeyEvent>(event).codepoint);
    }
    catch (OperationCancelled const&)
    {
        co_return cancelSentinel;
    }
}

/// Resumes immediately when the delay has already elapsed (it declines to park).
Task<int> awaitZeroDelay(TuiRuntime* runtime)
{
    co_await runtime->delay(0ms);
    co_return 7;
}

/// Parks on a delay of @p delayMs, then sets *fired and returns it.
Task<int> awaitDelayThenFire(TuiRuntime* runtime, int delayMs, bool* fired)
{
    co_await runtime->delay(std::chrono::milliseconds { delayMs });
    *fired = true;
    co_return delayMs;
}

/// Waits for @p handle to become readable; returns 1 on readiness or @p cancelSentinel if
/// cancelled while parked.
Task<int> awaitReadableOrCancel(TuiRuntime* runtime, core::platform::NativeHandle handle, int cancelSentinel)
{
    try
    {
        co_await runtime->waitReadable(handle);
        co_return 1;
    }
    catch (OperationCancelled const&)
    {
        co_return cancelSentinel;
    }
}

/// Waits for @p handle to become readable and counts the resolution in *resolved.
Task<void> countReadable(TuiRuntime* runtime, core::platform::NativeHandle handle, std::size_t* resolved)
{
    co_await runtime->waitReadable(handle);
    ++*resolved;
}

/// A value-producing task that completes synchronously — the "work wins" arm.
Task<int> produceValue(int value)
{
    co_return value;
}

/// Parks on a never-ready handle forever (until cancelled) — the "timeout wins" arm.
Task<int> parkOnHandleForever(TuiRuntime* runtime, core::platform::NativeHandle handle)
{
    co_await runtime->waitReadable(handle);
    co_return 0;
}

/// The whenAny WINNER: completes without ever suspending.
Task<void> completeNow()
{
    co_return;
}

/// The whenAny LOSER for core-cpp#18: parks on input and must unwind cancelled, which it records.
/// The record is the point -- a race that resolves because its loser was never really parked
/// would look the same from outside.
Task<void> parkOnInputUntilCancelled(TuiRuntime* runtime, bool* cancelled)
{
    try
    {
        auto const event = co_await runtime->nextEvent();
        static_cast<void>(event);
    }
    catch (OperationCancelled const&)
    {
        *cancelled = true;
    }
}

/// The whenAny LOSER for core-cpp#16: parks on a delay and must unwind cancelled.
Task<void> parkOnDelayUntilCancelled(TuiRuntime* runtime, bool* cancelled)
{
    try
    {
        co_await runtime->delay(50ms);
    }
    catch (OperationCancelled const&)
    {
        *cancelled = true;
    }
}

/// Races a flow parked on input against one that finishes at once, and reports the winner.
/// @param loserCancelled Set when the parked arm unwinds cancelled.
Task<int> raceOnInput(TuiRuntime* runtime, bool* loserCancelled)
{
    auto tasks = std::vector<Task<void>> {};
    tasks.reserve(2);
    tasks.push_back(parkOnInputUntilCancelled(runtime, loserCancelled));
    tasks.push_back(completeNow());
    auto const winner = co_await core::async::whenAny(std::move(tasks));
    co_return winner.has_value() ? static_cast<int>(*winner) : -1;
}

/// Races a flow parked on a delay against one that finishes at once, lets the loser's frame go,
/// and then outlives the deadline that loser was parked on.
Task<int> raceOnDelayThenOutliveIt(TuiRuntime* runtime, bool* loserCancelled)
{
    {
        auto tasks = std::vector<Task<void>> {};
        tasks.reserve(2);
        tasks.push_back(parkOnDelayUntilCancelled(runtime, loserCancelled));
        tasks.push_back(completeNow());
        static_cast<void>(co_await core::async::whenAny(std::move(tasks)));
    } // the loser's frame is destroyed here, with the race state

    co_await runtime->delay(120ms); // past the 50ms the cancelled delay was parked on
    co_return 1;
}

/// Sets *destroyed = true when its frame unwinds (RAII), so a case can prove a parked flow was
/// cancelled-and-unwound rather than raw-destroyed.
struct UnwindFlag
{
    bool* destroyed;

    ~UnwindFlag() { *destroyed = true; }
};

/// Parks forever on input with an RAII guard; the guard must still run if the flow is cancelled.
Task<void> parkForeverWithGuard(TuiRuntime* runtime, bool* destroyed)
{
    *destroyed = false;
    auto guard = UnwindFlag { destroyed };
    try
    {
        auto const event = co_await runtime->nextEvent();
        static_cast<void>(event);
    }
    catch (OperationCancelled const&)
    {
        static_cast<void>(destroyed);
    }
}

/// A ScriptedBackend that advances an injected ManualClock by a fixed step on every wait, so a
/// deadline is crossed deterministically with no real time passing.
class ClockAdvancingBackend: public ScriptedBackend
{
  public:
    /// @param clock The clock each wait advances.
    /// @param step How much time one wait stands for.
    ClockAdvancingBackend(ManualClock& clock, std::chrono::milliseconds step) noexcept:
        _clock(clock), _step(step)
    {
    }

    [[nodiscard]] core::net::WaitResult wait(std::optional<core::platform::SteadyDuration> timeout) override
    {
        _clock.advance(_step);
        return ScriptedBackend::wait(timeout);
    }

  private:
    ManualClock& _clock;
    std::chrono::milliseconds _step;
};

} // namespace

// ---------------------------------------------------------------------------------------------
// Input delivery, over a real backend and a real readiness channel.
// ---------------------------------------------------------------------------------------------

TEST_CASE("Runtime delivers an input event to a waiting flow", "[TuiRuntime]")
{
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    source.pushEvents({ InputEvent { keyOf(U'x') } });
    auto runtime = TuiRuntime { loop, source };

    REQUIRE(runtime.blockOn(awaitOneKeyCodepoint(&runtime)) == U'x');
}

TEST_CASE("Runtime delivers buffered events in arrival order", "[TuiRuntime]")
{
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    // Two events decoded from one read are buffered and consumed in order.
    source.pushEvents({ InputEvent { keyOf(U'a') }, InputEvent { keyOf(U'b') } });
    auto runtime = TuiRuntime { loop, source };

    REQUIRE(runtime.blockOn(sumKeyCodepoints(&runtime, 2))
            == static_cast<int>(U'a') + static_cast<int>(U'b'));
}

TEST_CASE("Protocol reports never surface as input events", "[TuiRuntime]")
{
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    // A focus report (internal) followed by a real key: the flow must see only the key.
    source.pushEvents(
        { InputEvent { core::tui::FocusEvent { .focused = true } }, InputEvent { keyOf(U'z') } });
    auto runtime = TuiRuntime { loop, source };

    REQUIRE(runtime.blockOn(awaitOneKeyCodepoint(&runtime)) == U'z');
}

TEST_CASE("Events a terminal query handed back are delivered without waiting", "[TuiRuntime]")
{
    // They were read off the input handle BEFORE anything still on it, so nothing is going to
    // become readable on their account: if the runtime only looked at them when a source flow
    // woke, a key typed during a DA1 probe would be stranded until the next keystroke.
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource {}; // no pipe at all: nothing can ever become ready
    source.pushPending({ InputEvent { keyOf(U'q') } });
    auto runtime = TuiRuntime { loop, source };

    REQUIRE(runtime.blockOn(awaitOneKeyCodepoint(&runtime)) == U'q');
}

TEST_CASE("A resize arrives as an input event on its own channel", "[TuiRuntime]")
{
    auto const pipes = openPipes(2);
    REQUIRE(pipes.size() == 2);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get(), pipes[1].get() };
    source.pushResize(core::tui::ResizeEvent { .columns = 120, .rows = 40 });
    auto runtime = TuiRuntime { loop, source };

    auto awaitResize = [](TuiRuntime* rt) -> Task<int> {
        auto const event = co_await rt->nextEvent();
        co_return std::get<core::tui::ResizeEvent>(event).columns;
    };

    REQUIRE(runtime.blockOn(awaitResize(&runtime)) == 120);
}

TEST_CASE("nextEventFor yields an event that arrives before the timeout", "[TuiRuntime]")
{
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    source.pushEvents({ InputEvent { keyOf(U'e') } });
    auto runtime = TuiRuntime { loop, source };

    REQUIRE(runtime.blockOn(awaitEventForResult(&runtime, 5000)) == 1);
}

TEST_CASE("nextEventFor resolves with nothing when its deadline elapses", "[TuiRuntime]")
{
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() }; // nothing scripted: nothing arrives
    auto runtime = TuiRuntime { loop, source };

    REQUIRE(runtime.blockOn(awaitEventForResult(&runtime, 1)) == 0);
    // The deadline was retired by the resume, not left on the loop to fire into an empty slot.
    REQUIRE(loop.pendingTimerCount() == 0);
}

TEST_CASE("nextActivity reports an input event, an agent message, or a timeout", "[TuiRuntime]")
{
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();

    SECTION("input event")
    {
        auto loop = EventLoop { *backend };
        auto source = ScriptedInputSource { pipes[0].get() };
        source.pushEvents({ InputEvent { keyOf(U'a') } });
        auto runtime = TuiRuntime { loop, source };
        REQUIRE(runtime.blockOn(awaitActivityKind(&runtime, 5000)) == static_cast<int>(ActivityKind::Event));
    }
    SECTION("agent message, already pending when the flow asks")
    {
        auto loop = EventLoop { *backend };
        auto source = ScriptedInputSource { pipes[0].get() };
        auto runtime = TuiRuntime { loop, source };
        loop.post([&runtime] { runtime.notifyAgentReady(); });
        REQUIRE(runtime.blockOn(awaitActivityKind(&runtime, 5000))
                == static_cast<int>(ActivityKind::AgentReady));
    }
    SECTION("timeout")
    {
        auto loop = EventLoop { *backend };
        auto source = ScriptedInputSource { pipes[0].get() };
        auto runtime = TuiRuntime { loop, source };
        REQUIRE(runtime.blockOn(awaitActivityKind(&runtime, 1)) == static_cast<int>(ActivityKind::Timeout));
    }
}

TEST_CASE("An agent message posted while a flow is parked resumes it", "[TuiRuntime][agent]")
{
    // The interesting order: the waiter parks FIRST and the message arrives afterwards, from the
    // loop's cross-thread surface, which is what replaced the agent wakeup handle.
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    auto runtime = TuiRuntime { loop, source };

    auto worker = std::jthread { [&loop, &runtime] {
        std::this_thread::sleep_for(20ms);
        loop.post([&runtime] { runtime.notifyAgentReady(); });
    } };

    REQUIRE(runtime.blockOn(awaitAgentReady(&runtime)));
}

TEST_CASE("Non-input activity resolves a timed waiter but never a plain nextEvent", "[TuiRuntime]")
{
    // A focus change is activity, not input. `nextEventFor` can say "nothing happened" and so is
    // told; `nextEvent` cannot -- its await_resume yields an event or throws -- so resuming it
    // would report a focus change as a cancellation, which used to close an open modal.
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();

    SECTION("a timed waiter resolves with no event")
    {
        auto loop = EventLoop { *backend };
        auto source = ScriptedInputSource { pipes[0].get() };
        source.pushEvents({ InputEvent { core::tui::FocusEvent { .focused = true } } });
        auto runtime = TuiRuntime { loop, source };
        REQUIRE(runtime.blockOn(awaitEventForResult(&runtime, 5000)) == 0);
    }
    SECTION("a plain nextEvent waits past it for real input")
    {
        auto loop = EventLoop { *backend };
        auto source = ScriptedInputSource { pipes[0].get() };
        source.pushEvents({ InputEvent { core::tui::FocusEvent { .focused = true } } });
        source.pushEvents({ InputEvent { keyOf(U'm') } });
        auto runtime = TuiRuntime { loop, source };
        REQUIRE(runtime.blockOn(awaitOneKeyCodepoint(&runtime)) == U'm');
    }
}

// ---------------------------------------------------------------------------------------------
// The backend matrix: the same scenario over every readiness mechanism this platform builds.
// ---------------------------------------------------------------------------------------------

TEST_CASE("Input arriving while the loop waits resumes the flow on the loop thread", "[TuiRuntime][backend]")
{
    // Nothing is ready when the flow parks, so the loop is inside its backend's wait when the
    // other thread scripts the input -- the case the runtime exists for, and the one no scripted
    // backend can stand in for. Run over every backend this platform builds: on Windows that is
    // WFMO and IOCP, and the terminal input handle is the reason the IOCP waitable-HANDLE bridge
    // exists at all.
    for (auto const& entry: core::net::testing::BackendMatrix)
    {
        auto backend = core::net::makeBackend(entry.kind);
        if (!backend)
            continue; // not built on this platform

        DYNAMIC_SECTION("backend=" << entry.name)
        {
            auto const pipes = openPipes(1);
            REQUIRE(pipes.size() == 1);
            auto loop = EventLoop { *backend };
            auto source = ScriptedInputSource { pipes[0].get() };
            auto runtime = TuiRuntime { loop, source };

            auto const driver = std::this_thread::get_id();
            auto resumedOn = std::thread::id {};
            auto writer = std::jthread { [&source] {
                std::this_thread::sleep_for(30ms);
                source.pushEvents({ InputEvent { keyOf(U'w') } });
            } };

            REQUIRE(runtime.blockOn(awaitOneKeyOnThread(&runtime, &resumedOn)) == U'w');
            REQUIRE(resumedOn == driver);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Deadlines, which are the loop's.
// ---------------------------------------------------------------------------------------------

TEST_CASE("delay(0) resumes without waiting", "[TuiRuntime][clock]")
{
    auto backend = ScriptedBackend {};
    auto loop = EventLoop { backend };
    auto source = ScriptedInputSource {};
    auto runtime = TuiRuntime { loop, source };

    REQUIRE(runtime.blockOn(awaitZeroDelay(&runtime)) == 7);
    REQUIRE(backend.waitCount() == 0); // the ready path never reaches a wait
}

TEST_CASE("A pending delay bounds the wait and fires when the clock crosses it", "[TuiRuntime][clock]")
{
    // Each wait stands for 40ms of clock time, so a 100ms delay is still pending after two and
    // due on the third -- deterministic, with no real time passing.
    auto clock = ManualClock {};
    auto backend = ClockAdvancingBackend { clock, 40ms };
    auto loop = EventLoop { backend, clock };
    auto source = ScriptedInputSource {};
    auto runtime = TuiRuntime { loop, source };
    for ([[maybe_unused]] auto const step: std::views::iota(0, 6))
        backend.pushTimeout();

    auto fired = false;
    REQUIRE(runtime.blockOn(awaitDelayThenFire(&runtime, 100, &fired)) == 100);

    REQUIRE(fired);
    // The ARGUMENT, not just the outcome: the first wait was bounded by the pending deadline
    // rather than left indefinite or forced to zero.
    REQUIRE(backend.recordedTimeouts().front().has_value());
    REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(*backend.recordedTimeouts().front())
            == 100ms);
    REQUIRE(backend.waitCount() == 3);
    REQUIRE(loop.pendingTimerCount() == 0);
}

// ---------------------------------------------------------------------------------------------
// Handle waits, which are the loop's too.
// ---------------------------------------------------------------------------------------------

TEST_CASE("waitReadable resolves over a real SystemPipe", "[TuiRuntime][fd]")
{
    auto const pipes = openPipes(2);
    REQUIRE(pipes.size() == 2);
    char const payload = 'Z';
    REQUIRE(pipes[1]->write(&payload, 1).has_value());

    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    auto runtime = TuiRuntime { loop, source };

    auto readByte = [](TuiRuntime* rt, SystemPipe* pipe) -> Task<char> {
        co_await rt->waitReadable(pipe->waitHandle());
        char byte = 0;
        auto const got = pipe->read(&byte, 1);
        co_return (got.has_value() && got->bytesRead() == 1) ? byte : '\0';
    };

    REQUIRE(runtime.blockOn(readByte(&runtime, pipes[1].get())) == 'Z');
}

TEST_CASE("waitReadable on an invalid handle resolves immediately as cancelled", "[TuiRuntime][fd]")
{
    auto backend = ScriptedBackend {};
    auto loop = EventLoop { backend };
    auto source = ScriptedInputSource {};
    auto runtime = TuiRuntime { loop, source };

    constexpr auto Cancelled = -3;
    REQUIRE(runtime.blockOn(awaitReadableOrCancel(&runtime, core::platform::InvalidHandle, Cancelled))
            == Cancelled);
    REQUIRE(backend.waitCount() == 0); // an unwaitable handle resolves inline
}

TEST_CASE("More concurrent handle waits than one native wait accepts all resolve",
          "[TuiRuntime][fd][chunking]")
{
    // core-cpp#19: the Windows event source called WaitForMultipleObjects with an unchecked count
    // and mapped its refusal past 64 handles onto "interrupted", so a consumer watching ~60
    // descriptors saw the whole TUI unwind as if Ctrl+C had been pressed. The loop's Windows
    // backend sweeps its set in chunks instead, and this runtime inherits that by being composed
    // on it -- so the number below is deliberately past MAXIMUM_WAIT_OBJECTS.
    constexpr auto Watched = std::size_t { 70 };
    auto const pipes = openPipes(Watched + 1);
    if (pipes.size() != Watched + 1)
        SKIP("the OS refused " << (Watched + 1) << " pipes");

    char const payload = 'r';
    for (auto const index: std::views::iota(std::size_t { 1 }, pipes.size()))
        REQUIRE(pipes[index]->write(&payload, 1).has_value());

    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    auto runtime = TuiRuntime { loop, source };

    auto resolved = std::size_t { 0 };
    auto watchAll = [](TuiRuntime* rt,
                       std::vector<std::unique_ptr<SystemPipe>> const* channels,
                       std::size_t* count) -> Task<void> {
        auto waiters = std::vector<Task<void>> {};
        waiters.reserve(channels->size() - 1);
        for (auto const index: std::views::iota(std::size_t { 1 }, channels->size()))
            waiters.push_back(countReadable(rt, (*channels)[index]->waitHandle(), count));
        co_await core::async::whenAll(std::move(waiters));
    };

    runtime.blockOn(watchAll(&runtime, &pipes, &resolved));
    REQUIRE(resolved == Watched);
}

// ---------------------------------------------------------------------------------------------
// Interrupts.
// ---------------------------------------------------------------------------------------------

TEST_CASE("An interrupt cancels a flow parked on input", "[TuiRuntime][interrupt]")
{
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    auto wakeup = core::platform::Wakeup {};
    auto runtime = TuiRuntime { loop, source, TuiRuntimeOptions { .interruptWakeup = &wakeup } };

    // What SignalHandler does on Ctrl+C: record the signal, then signal the wakeup the loop is
    // already watching. The interrupt flow parked there is what runs the policy.
    core::platform::SignalHandler::simulateSigint();
    wakeup.signal();

    constexpr auto Sentinel = -99;
    REQUIRE(runtime.blockOn(awaitKeyOrCancel(&runtime, Sentinel)) == Sentinel);
    // The flow observed it, so the flag is consumed: a second wait must not see this one again.
    REQUIRE_FALSE(core::platform::SignalHandler::hasPendingSigint());
}

TEST_CASE("An installed interrupt handler replaces the default stop", "[TuiRuntime][interrupt]")
{
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    auto wakeup = core::platform::Wakeup {};
    auto runtime = TuiRuntime { loop, source, TuiRuntimeOptions { .interruptWakeup = &wakeup } };

    // A consumer that scopes cancellation -- closing the open modal rather than the program --
    // delivers a key instead of stopping the loop, so the parked flow resumes normally.
    auto interrupts = 0;
    runtime.setInterruptHandler([&interrupts, &source] {
        ++interrupts;
        source.pushEvents({ InputEvent { keyOf(U'i') } });
    });

    core::platform::SignalHandler::simulateSigint();
    wakeup.signal();

    constexpr auto Sentinel = -99;
    REQUIRE(runtime.blockOn(awaitKeyOrCancel(&runtime, Sentinel)) == static_cast<int>(U'i'));
    REQUIRE(interrupts == 1);
    REQUIRE_FALSE(core::platform::SignalHandler::hasPendingSigint());
}

// ---------------------------------------------------------------------------------------------
// withTimeout, which is core::net's and takes the loop.
// ---------------------------------------------------------------------------------------------

TEST_CASE("withTimeout returns the work's value when it finishes first", "[TuiRuntime][timeout]")
{
    auto backend = ScriptedBackend {};
    auto loop = EventLoop { backend };
    auto source = ScriptedInputSource {};
    auto runtime = TuiRuntime { loop, source };

    auto result = runtime.blockOn(
        core::net::withTimeout(&runtime.loop(), produceValue(42), std::chrono::seconds { 10 }));

    REQUIRE(result.has_value());
    REQUIRE(*result == 42);
    REQUIRE(loop.pendingTimerCount() == 0);
}

TEST_CASE("withTimeout cancels the work and leaves nothing parked when its deadline fires",
          "[TuiRuntime][timeout][fd]")
{
    auto const pipes = openPipes(2);
    REQUIRE(pipes.size() == 2);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    auto runtime = TuiRuntime { loop, source };

    // The work parks on a pipe nothing will ever write to, so only the timeout arm can win.
    auto result = runtime.blockOn(
        core::net::withTimeout(&runtime.loop(), parkOnHandleForever(&runtime, pipes[1]->waitHandle()), 20ms));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(loop.pendingTimerCount() == 0);
    // Only the runtime's own input flow is still parked; the cancelled work took its park with it.
    REQUIRE(loop.parkedWaiterCount() == 1);
}

// ---------------------------------------------------------------------------------------------
// The four deferred defects, each asserted on what distinguishes it.
// ---------------------------------------------------------------------------------------------

TEST_CASE("core-cpp#16: a cancelled delay leaves nothing behind for a later turn to touch",
          "[TuiRuntime][regression]")
{
    // The old runtime's DelayAwaiter reset its stop-callback and left its entry in the timer heap.
    // Crossing that deadline after the loser's frame was destroyed called done() on freed storage
    // -- a SIGSEGV in a plain debug build, not a subtle wrong answer. The loop's own awaitable
    // unregisters its park on every resume, ready or cancelled, which is what this asserts.
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    auto runtime = TuiRuntime { loop, source };

    auto loserCancelled = false;
    REQUIRE(runtime.blockOn(raceOnDelayThenOutliveIt(&runtime, &loserCancelled)) == 1);
    REQUIRE(loserCancelled);
    REQUIRE(loop.pendingTimerCount() == 0);
    REQUIRE(loop.pendingTimerSlotCount() == 0);
}

TEST_CASE("core-cpp#17: a turn with nothing ready waits rather than spinning", "[TuiRuntime][regression]")
{
    // The old runtime's pumpOnce returned without waiting whenever nothing it knew about was
    // parked, and blockOn looped on it unconditionally, so a flow waiting on a source that
    // registered no fd burned a core in silence. An outcome test cannot tell a loop that slept
    // from one that spun -- both deliver the key -- so the assertion is on the ARGUMENT the first
    // wait was given: indefinite, because nothing bounds it.
    auto backend = ScriptedBackend {};
    auto loop = EventLoop { backend };
    auto source = ScriptedInputSource {};
    source.pushEvents({ InputEvent { keyOf(U'q') } });
    auto runtime = TuiRuntime { loop, source };

    backend.pushTimeout();                 // the idle wait, whose argument is the assertion
    backend.pushReadable(HandlerId { 1 }); // then the input flow's registration becomes ready

    REQUIRE(runtime.blockOn(awaitOneKeyCodepoint(&runtime)) == U'q');

    // THREE waits for two scripted steps, and the extra one is not slack: the runtime is
    // constructed outside a turn, so its `submit` of the input flow goes through the inbound queue
    // and wakes the backend, and the first wait consumes that wake without spending a step.
    REQUIRE(backend.waitCount() == 3);
    // Every one of them was INDEFINITE. That is the whole assertion: with no deadline pending
    // there is nothing to bound a wait, so a loop that meant to sleep asks for no timeout, and one
    // that was about to spin asks for zero.
    for (auto const& timeout: backend.recordedTimeouts())
        CHECK_FALSE(timeout.has_value());
}

TEST_CASE("core-cpp#18: a whenAny loser parked on input unwinds without waiting for input",
          "[TuiRuntime][regression]")
{
    // The old input awaiters armed no stop-callback, so a cancelled one stayed in the runtime's
    // single waiter slot until input happened to arrive -- which, after EOF on stdin, is never.
    // The race could then not resolve at all, and the next flow to ask for input found the slot
    // taken. So the ARGUMENT again: this race must need no wait whatsoever.
    auto backend = ScriptedBackend {};
    auto loop = EventLoop { backend };
    auto source = ScriptedInputSource {}; // nothing scripted: no input will ever arrive
    auto runtime = TuiRuntime { loop, source };

    auto loserCancelled = false;
    REQUIRE(runtime.blockOn(raceOnInput(&runtime, &loserCancelled)) == 1);
    REQUIRE(loserCancelled);
    REQUIRE(backend.waitCount() == 0);
}

TEST_CASE("core-cpp#18: the input slot is free again for the next flow", "[TuiRuntime][regression]")
{
    // The other half of the same defect: having released the slot, the runtime must serve the
    // flow that asks next. A runtime that left it occupied trips its own single-waiter assertion
    // in a debug build and silently strands the first flow in a release one.
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    source.pushEvents({ InputEvent { keyOf(U'k') } });
    auto runtime = TuiRuntime { loop, source };

    auto loserCancelled = false;
    auto raceThenRead = [](TuiRuntime* rt, bool* cancelled) -> Task<char32_t> {
        static_cast<void>(co_await raceOnInput(rt, cancelled));
        auto const event = co_await rt->nextEvent();
        co_return std::get<KeyEvent>(event).codepoint;
    };

    REQUIRE(runtime.blockOn(raceThenRead(&runtime, &loserCancelled)) == U'k');
    REQUIRE(loserCancelled);
}

// ---------------------------------------------------------------------------------------------
// Teardown.
// ---------------------------------------------------------------------------------------------

TEST_CASE("Destroying the runtime leaves the loop holding nothing of it", "[TuiRuntime][teardown]")
{
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    {
        auto runtime = TuiRuntime { loop, source };
        // One bounded turn, so the input flow reaches its park. `runOnce` with a zero bound never
        // waits, which is what makes this safe to do with nothing ready.
        std::ignore = loop.runOnce(core::platform::SteadyDuration::zero());
        REQUIRE(loop.parkedWaiterCount() == 1);
    } // ~TuiRuntime: the source flow is taken back off the loop and finished

    REQUIRE(loop.parkedWaiterCount() == 0);
    REQUIRE(loop.pendingTimerCount() == 0);
    REQUIRE(loop.readyCount() == 0);
}

TEST_CASE("Destroying the runtime with input already dispatched holds nothing either",
          "[TuiRuntime][teardown]")
{
    // The other teardown case, and the one that is easy to get wrong: a source flow the loop has
    // already QUEUED still owns a live backend registration, because only its await_resume
    // unregisters. A destructor that merely dropped the frame would leave the loop watching a
    // terminal handle for an object that no longer exists.
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    {
        auto runtime = TuiRuntime { loop, source };
        std::ignore = loop.runOnce(core::platform::SteadyDuration::zero()); // the flow parks
        source.pushEvents({ InputEvent { keyOf(U'd') } });
        std::ignore = loop.runOnce(core::platform::SteadyDuration::zero()); // readiness dispatched
    }

    REQUIRE(loop.parkedWaiterCount() == 0);
    REQUIRE(loop.pendingTimerCount() == 0);
}

TEST_CASE("A flow parked on the runtime unwinds while the runtime is still alive", "[TuiRuntime][teardown]")
{
    // **The timing is the assertion.** A flow awaiting this runtime holds an awaiter that reads it
    // on resume -- the slot, the deadline, the buffer -- and a cancellation callback that names
    // it, registered on a token the LOOP owns. So the unwind cannot be left to ~EventLoop: by then
    // the runtime is gone, and the loop's own request_stop() calls that callback against storage
    // that no longer exists. AddressSanitizer reported exactly that, as a stack-use-after-scope
    // read of the runtime from one scope further out, and a plain debug build did not notice.
    //
    // So the guard has to run at ~TuiRuntime, not merely by the end of the block: waiting for the
    // loop would be the defect, and "it unwound eventually" cannot tell the two apart.
    auto const pipes = openPipes(1);
    REQUIRE(pipes.size() == 1);
    auto const backend = core::net::makeDefaultBackend();
    auto loop = EventLoop { *backend };
    auto source = ScriptedInputSource { pipes[0].get() };
    auto destroyed = false;
    {
        auto runtime = TuiRuntime { loop, source };
        runtime.spawn(parkForeverWithGuard(&runtime, &destroyed));
        runtime.blockOn(awaitZeroDelay(&runtime)); // drive the spawned flow to its park
        REQUIRE_FALSE(destroyed);                  // still parked on input
    } // ~TuiRuntime: it resumes what it parked, so the frame unwinds and the guard runs

    REQUIRE(destroyed);
    // And nothing of it is left on the loop, which outlives it here exactly as it does in a
    // program: the parked flow was taken back, not abandoned to the loop's own teardown.
    REQUIRE(loop.parkedWaiterCount() == 0);
    REQUIRE(loop.readyCount() == 0);
}
