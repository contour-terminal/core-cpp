// SPDX-License-Identifier: Apache-2.0
#include <core/tui/runtime/TuiRuntime.hpp>

#include <core/platform/SignalHandler.hpp>

#include <algorithm>
#include <cassert>
#include <utility>

namespace core::tui::runtime
{

TuiRuntime::TuiRuntime(net::EventLoop& loop, InputSource& input, TuiRuntimeOptions options):
    _loop(loop),
    _input(input),
    _options(options),
    _inputHandle(input.inputHandle()),
    _resizeHandle(input.resizeHandle())
{
    startSourceFlows();
}

TuiRuntime::TuiRuntime(net::EventLoop& loop, Terminal& terminal, TuiRuntimeOptions options):
    _loop(loop),
    _ownedInput(std::in_place, terminal),
    _input(*_ownedInput),
    _options(options),
    _inputHandle(_ownedInput->inputHandle()),
    _resizeHandle(_ownedInput->resizeHandle())
{
    startSourceFlows();
}

TuiRuntime::~TuiRuntime()
{
    // **Nothing may outlive this object still naming it**, and two different populations do: the
    // flows this runtime parked in its own slots, and the source flows it started. Both are
    // retired here, while the loop -- and this object -- are still alive.
    assert(_loop.teardownIsSerialisedWithDispatch()
           && "~TuiRuntime from a second thread while another is driving its loop: this clears "
              "scheduler state beside a turn that is reading it");
    _stopping = true;

    unwindParkedWaiters();
    retireTimer(_escapeFlush);

    // The source flows. Each frame names THIS object, and the loop is holding it, so a resumption
    // after this object is gone reaches freed storage. `cancelPending` is the retrieval, and its
    // answer is an ownership transfer rather than a status: true means the loop no longer has it
    // and this call may resume or destroy it.
    //
    // **A flow is in one of THREE states here, and `cancelPending` answers true for all three.**
    // They are enumerated rather than tested, because an earlier version of this comment named two
    // and the missing one was the state every flow passes through first:
    //
    //   1. SUBMITTED, NEVER STARTED. `async::Task` is lazy, so between the constructor and the
    //      first turn the frame sits at its initial suspend point in the inbound queue, body not
    //      entered. Resuming it runs that body FROM THE TOP.
    //   2. PARKED on a handle. `cancelPending` takes the park and detaches the registration, so
    //      nothing of it is left with the backend.
    //   3. QUEUED after readiness. The waiter was taken, but `await_resume` has not run, so the
    //      park is still filed AND still attached -- only `await_resume` unregisters it. That is
    //      [core-cpp#41](https://github.com/contour-terminal/core-cpp/issues/41), and the resume
    //      below is a WORKAROUND for it: when `cancelPending` learns to unregister the park on a
    //      ready-queue hit, state 3 stops needing a resume and this loop should be revisited.
    //
    // One action serves all three, and it works because of the flows' SHAPE rather than because of
    // a test on the state -- which is what makes it survive a fourth state being added. Resume
    // once, and let `_stopping` end the body at its first statement: state 1 enters its loop,
    // finds `_stopping` and returns without ever awaiting; state 3 runs `await_resume`, which
    // unregisters the park, then finds `_stopping`; state 2 runs `await_resume` against an id
    // already taken -- both calls are no-ops -- then finds `_stopping`.
    //
    // The shape that makes that true is `while (!_stopping)` in each flow, and it is load-bearing
    // for state 1 alone: the guard must precede the first `co_await`, because `EventLoop`'s
    // awaiter knows nothing about this runtime and parks a flow that starts during teardown. That
    // park would name a frame `_sources` is about to destroy, and `~EventLoop` would resume it.
    for (auto& source: _sources)
    {
        if (source.done())
            continue;
        if (_loop.cancelPending(source.handle()) && !source.done())
            source.handle().resume();
    }
}

void TuiRuntime::unwindParkedWaiters()
{
    // A flow awaiting this runtime is unwound HERE, from the destructor body, while every member
    // it is about to read is still alive. Handing it to the loop instead would have it resumed
    // afterwards, and its `await_resume` asks this object for the slot, the deadline and the
    // buffer. Worse, an awaiter that is never resumed never runs `_cancelReg.reset()`, so its
    // cancellation callback -- which names this runtime -- stays armed on a token the LOOP owns,
    // and `~EventLoop`'s own `request_stop()` then calls it against storage that is gone. That is
    // the failure AddressSanitizer reported: a stack-use-after-scope read of the runtime, from the
    // loop's teardown, one scope further out.
    //
    // The waiters the loop is already holding come back first, by the same retrieval the source
    // flows use. `cancelPending` answering false would mean the loop no longer has it, which
    // cannot happen while it is still listed here: a waiter leaves that list only through
    // `await_resume`, and `await_resume` only runs from a resumption.
    for (auto const waiter: std::exchange(_handedToLoop, {}))
        if (waiter && !waiter.done() && _loop.cancelPending(waiter))
            waiter.resume();

    // Then the parked ones. This terminates in at most two passes whatever the resumed bodies do:
    // `_stopping` is set, so `await_suspend` declines to park and `await_resume` throws, and a
    // flow that catches the cancellation and awaits again cannot get back into a slot.
    while (_inputWaiter || _agentWaiter)
    {
        auto const waiter = _inputWaiter ? takeInputWaiter() : std::exchange(_agentWaiter, {});
        if (waiter && !waiter.done())
            waiter.resume();
    }
}

void TuiRuntime::handToLoop(std::coroutine_handle<> waiter)
{
    if (!waiter || waiter.done())
        return;
    _handedToLoop.push_back(waiter);
    _loop.resumeSoon(async::ParkedWork { .resume = waiter });
}

void TuiRuntime::forgetHandedToLoop(std::coroutine_handle<> waiter) noexcept
{
    std::erase(_handedToLoop, waiter);
}

void TuiRuntime::startSourceFlows()
{
    if (_inputHandle != platform::InvalidHandle)
        startSourceFlow(inputFlow());
    if (_resizeHandle != platform::InvalidHandle)
        startSourceFlow(resizeFlow());
    if (_options.interruptWakeup != nullptr)
        startSourceFlow(interruptFlow());
    if (_options.signalFd != platform::InvalidHandle)
        startSourceFlow(signalFlow());
}

void TuiRuntime::startSourceFlow(async::Task<void> flow)
{
    assert(_sourceCount < _sources.size() && "TuiRuntime: more source flows than slots for them");
    // `submit` rather than `spawn`, and the difference is the whole of the teardown above: `spawn`
    // hands the frame to the loop, which offers no way to take one back, so a runtime destroyed
    // before its loop would leave the loop owning a frame that names freed storage. `submit`
    // BORROWS -- the frame stays here, in `_sources`, and the destructor is what makes that
    // borrow honest.
    _sources[_sourceCount] = std::move(flow);
    _loop.submit(_sources[_sourceCount].handle());
    ++_sourceCount;
}

async::Task<void> TuiRuntime::inputFlow()
{
    // `!_stopping` rather than `true`, and it is checked BEFORE the first `co_await`: the
    // destructor resumes a flow that may never have started, and this is what stops that
    // resumption from parking on the loop. See ~TuiRuntime.
    while (!_stopping)
    {
        try
        {
            co_await _loop.waitReadable(_inputHandle);
        }
        catch (async::OperationCancelled const&)
        {
            co_return;
        }
        catch (net::FdRegistrationFailed const&)
        {
            // The backend will not watch this handle -- it is closed, or the kernel refused the
            // filter. Returning is the only option that does not spin: re-parking would ask the
            // same refused question on every turn, forever.
            co_return;
        }
        if (_stopping)
            co_return;

        auto decoded = _input.readReady();
        if (decoded.empty())
            // Bytes arrived and decoded to nothing: either a partial escape sequence, or a console
            // record that is not input. Arm the flush so a lone ESC is eventually delivered as
            // Escape rather than waiting for a continuation nobody is going to type.
            armEscapeFlush();
        else
            retireTimer(_escapeFlush);
        routeDecoded(std::move(decoded));
    }
}

async::Task<void> TuiRuntime::resizeFlow()
{
    // `!_stopping` rather than `true`, and it is checked BEFORE the first `co_await`: the
    // destructor resumes a flow that may never have started, and this is what stops that
    // resumption from parking on the loop. See ~TuiRuntime.
    while (!_stopping)
    {
        try
        {
            co_await _loop.waitReadable(_resizeHandle);
        }
        catch (async::OperationCancelled const&)
        {
            co_return;
        }
        catch (net::FdRegistrationFailed const&)
        {
            co_return;
        }
        if (_stopping)
            co_return;

        if (auto resize = _input.readResize())
        {
            auto events = std::vector<InputEvent> {};
            events.push_back(std::move(*resize));
            routeDecoded(std::move(events));
        }
    }
}

async::Task<void> TuiRuntime::interruptFlow()
{
    // `!_stopping` rather than `true`, and it is checked BEFORE the first `co_await`: the
    // destructor resumes a flow that may never have started, and this is what stops that
    // resumption from parking on the loop. See ~TuiRuntime.
    while (!_stopping)
    {
        try
        {
            co_await _loop.waitReadable(_options.interruptWakeup->nativeHandle());
        }
        catch (async::OperationCancelled const&)
        {
            co_return;
        }
        catch (net::FdRegistrationFailed const&)
        {
            co_return;
        }
        if (_stopping)
            co_return;

        _options.interruptWakeup->reset();
        // The wakeup only says "look"; the flag is where the signal handler recorded WHAT. A
        // wakeup with no flag behind it is a spurious signal and is not an interrupt. Clearing it
        // here is what makes each interrupt observed exactly once.
        if (!platform::SignalHandler::hasPendingSigint())
            continue;
        platform::SignalHandler::clearPendingSigint();
        runInterruptPolicy();
    }
}

async::Task<void> TuiRuntime::signalFlow()
{
    // `!_stopping` rather than `true`, and it is checked BEFORE the first `co_await`: the
    // destructor resumes a flow that may never have started, and this is what stops that
    // resumption from parking on the loop. See ~TuiRuntime.
    while (!_stopping)
    {
        try
        {
            co_await _loop.waitReadable(_options.signalFd);
        }
        catch (async::OperationCancelled const&)
        {
            co_return;
        }
        catch (net::FdRegistrationFailed const&)
        {
            co_return;
        }
        if (_stopping)
            co_return;

        // A reaped job (SIGCHLD) is non-input activity: resume an idle flow so its owner reports
        // finished jobs promptly instead of at the next keypress.
        if (platform::SignalHandler::processSignalFd())
            notifyActivity();
    }
}

void TuiRuntime::runInterruptPolicy()
{
    if (_onInterrupt)
        _onInterrupt();
    else
        // Not `rootStopSource().request_stop()`, which cancels every flow and unparks none of
        // them: a flow parked on a socket would observe its cancellation only when that socket
        // next became readable. `requestStop` is the same request plus the unparking.
        _loop.requestStop();
}

void TuiRuntime::routeDecoded(std::vector<InputEvent> events)
{
    if (events.empty())
        return;

    // The source dispatches the terminal's own protocol responses (colour scheme, focus, cursor
    // position, cell size) and removes them. A focus change is reported back because it is
    // non-input activity an idle flow still wants: focus chrome has to redraw.
    auto const focusChanged = _input.consumeReports(events);

    for (auto& event: events)
        // A second filter over a source that is contracted to have applied the first. It is
        // deliberate: `isProtocolReport` is the policy, a source is only obliged to deliver
        // decoded events, and a report that reached an application as input would look like a
        // keystroke nobody typed. The cost is one predicate per event.
        if (!isProtocolReport(event))
            _inputBuffer.push_back(std::move(event));

    if (focusChanged)
        notifyActivity();
    deliverInput();
}

bool TuiRuntime::hasBufferedInput()
{
    if (_inputBuffer.empty())
        // A terminal query reads input while it waits for its reply and hands back whatever was
        // not the reply. Those events were read off the input handle BEFORE anything still on it,
        // so nothing is going to become readable on their account and no source flow will ever
        // notice them. Asking here is what delivers them, at the one moment it matters.
        for (auto& event: _input.takePending())
            if (!isProtocolReport(event))
                _inputBuffer.push_back(std::move(event));
    return !_inputBuffer.empty();
}

InputEvent TuiRuntime::popBufferedInput()
{
    assert(!_inputBuffer.empty() && "TuiRuntime::popBufferedInput with nothing buffered");
    auto event = std::move(_inputBuffer.front());
    _inputBuffer.pop_front();
    return event;
}

void TuiRuntime::parkOnInput(std::coroutine_handle<> waiter,
                             InputWake wake,
                             std::optional<platform::SteadyTimePoint> deadline)
{
    assert(!_stopping && "TuiRuntime::parkOnInput during teardown: the awaiters decline to park");
    assert(!_inputWaiter && "TuiRuntime: an input waiter is already parked");
    _inputWaiter = waiter;
    _inputWake = wake;
    if (deadline)
        _inputDeadline = _loop.addTimer(*deadline, &TuiRuntime::onInputDeadline, this);
}

void TuiRuntime::releaseInputWaiter(std::coroutine_handle<> waiter) noexcept
{
    // An empty handle is an awaiter that declined to park -- it was cancelled before it got the
    // chance -- and it owns none of this state. Without this the comparison below would find an
    // empty slot "equal" and retire a deadline belonging to nobody.
    if (!waiter)
        return;
    // Idempotent, and usually a no-op on the slot: the resumption that brought this flow back is
    // what emptied it.
    //
    // **The deadline is retired where the waiter LEAVES the slot, not here.** `takeInputWaiter` is
    // that place and every path goes through it, so by the time this runs `_inputDeadline` may
    // already belong to a DIFFERENT flow -- one that parked while this one was queued. Retiring it
    // here disarmed that flow's timeout without firing it, so a `nextEventFor` or `nextActivity`
    // waited forever on a deadline it had asked for and silently lost. Retired only in the branch
    // where the slot still holds us, which is the only case in which it is ours.
    if (_inputWaiter == waiter)
    {
        _inputWaiter = {};
        _inputWake = InputWake::EventOnly;
        retireTimer(_inputDeadline);
    }
    forgetHandedToLoop(waiter);
}

void TuiRuntime::parkOnAgent(std::coroutine_handle<> waiter)
{
    assert(!_stopping && "TuiRuntime::parkOnAgent during teardown: the awaiters decline to park");
    assert(!_agentWaiter && "TuiRuntime: an agent waiter is already parked");
    _agentWaiter = waiter;
}

void TuiRuntime::releaseAgentWaiter(std::coroutine_handle<> waiter) noexcept
{
    if (!waiter)
        return;
    if (_agentWaiter == waiter)
        _agentWaiter = {};
    forgetHandedToLoop(waiter);
}

void TuiRuntime::requestCancelWaiter(std::coroutine_handle<> waiter) noexcept
{
    if (!waiter)
        return;
    // These two slots are this runtime's state, and this runtime's state is the loop thread's --
    // the same rule `EventLoop::registerPark` asserts, for the same reason: a token stopped from
    // another thread would write them beside a turn reading them. Cancel a TUI flow on the loop's
    // thread, or `post()` a call to whatever does it.
    assert(_loop.teardownIsSerialisedWithDispatch()
           && "TuiRuntime: a TUI flow was cancelled from a second thread while another is driving "
              "its loop; post() the cancellation to the loop instead");

    if (_inputWaiter == waiter)
        std::ignore = takeInputWaiter();
    else if (_agentWaiter == waiter)
        _agentWaiter = {};
    else
        // Already resumed -- by an event, by a deadline, or by an earlier cancellation. Queueing
        // it again would resume a frame the first resumption may already have destroyed.
        return;

    // BORROWS, which is what this is: the frame belongs to whoever is awaiting, it is suspended
    // at the `co_await` this cancels, and its `await_resume` is what runs next.
    handToLoop(waiter);
}

std::coroutine_handle<> TuiRuntime::takeInputWaiter() noexcept
{
    retireTimer(_inputDeadline);
    _inputWake = InputWake::EventOnly;
    return std::exchange(_inputWaiter, {});
}

void TuiRuntime::deliverInput()
{
    if (!_inputWaiter || !hasBufferedInput())
        return;
    handToLoop(takeInputWaiter());
}

void TuiRuntime::notifyActivity()
{
    // A `nextEvent()` waiter is deliberately left alone: its `await_resume` can produce an event
    // or throw, and nothing else, so resuming it with no event would report a focus change as a
    // cancellation -- which is how a focus change used to close an open modal.
    if (!_inputWaiter || _inputWake == InputWake::EventOnly)
        return;
    handToLoop(takeInputWaiter());
}

void TuiRuntime::notifyAgentReady()
{
    _agentPending = true;
    if (_agentWaiter)
    {
        handToLoop(std::exchange(_agentWaiter, {}));
        return;
    }
    if (_inputWaiter && _inputWake == InputWake::OrAgent)
        handToLoop(takeInputWaiter());
}

void TuiRuntime::armEscapeFlush()
{
    if (_escapeFlush)
        return; // the earlier deadline is the one that matters
    _escapeFlush =
        _loop.addTimer(_loop.clock().now() + _options.escapeFlush, &TuiRuntime::onEscapeFlush, this);
}

void TuiRuntime::retireTimer(net::TimerId& timer) noexcept
{
    if (timer)
        std::ignore = _loop.cancelTimer(timer);
    timer = net::TimerId::invalid();
}

void TuiRuntime::onInputDeadline(void* state) noexcept
{
    auto& self = *static_cast<TuiRuntime*>(state);
    // The timer has fired, so it is gone from the loop's table; clearing the id here is what stops
    // `releaseInputWaiter` from trying to cancel one that already ran.
    self._inputDeadline = net::TimerId::invalid();
    self.notifyActivity();
}

void TuiRuntime::onEscapeFlush(void* state) noexcept
{
    auto& self = *static_cast<TuiRuntime*>(state);
    self._escapeFlush = net::TimerId::invalid();
    self.routeDecoded(self._input.flushPartial());
}

NextInputEventAwaiter TuiRuntime::nextEvent() noexcept
{
    return NextInputEventAwaiter { *this };
}

NextEventForAwaiter TuiRuntime::nextEventFor(std::chrono::milliseconds timeout) noexcept
{
    return NextEventForAwaiter { *this, _loop.clock().now() + timeout };
}

NextActivityAwaiter TuiRuntime::nextActivity(std::chrono::milliseconds timeout) noexcept
{
    return NextActivityAwaiter { *this, _loop.clock().now() + timeout };
}

NextAgentReadyAwaiter TuiRuntime::nextAgentReady() noexcept
{
    return NextAgentReadyAwaiter { *this };
}

} // namespace core::tui::runtime
