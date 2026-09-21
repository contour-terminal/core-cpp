// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// A scripted @c InputSource for deterministic @c TuiRuntime unit tests.
///
/// It is half of what `MockEventSource` was, and the half that is missing is the point. That
/// double had to impersonate a scheduler — it owned `wait(timeoutMs)`, handed out fd registration
/// tokens and decided when a park resolved — so a case written against it proved things about the
/// double. Scheduling is now @c core::net::EventLoop's, and a case scripts READINESS at
/// @c core::net::testing::ScriptedBackend or gets it from a real kernel, while this scripts only
/// DECODING: which events the bytes behind a ready handle turn into.
///
/// Point it at a @c core::platform::SystemPipe and readiness becomes real on every platform, so
/// the same case runs over epoll, poll, kqueue, IOCP and WFMO unchanged. Leave the pipe out and
/// the handle is inert, which is what a scripted backend wants — it names registrations by attach
/// order and never looks at the handle.

#include <core/platform/SystemPipe.hpp>
#include <core/platform/Types.hpp>
#include <core/tui/InputEvent.hpp>
#include <core/tui/runtime/InputSource.hpp>

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace core::tui::runtime::testing
{

/// An @c InputSource that returns a pre-written sequence of decoded events.
class ScriptedInputSource: public InputSource
{
  public:
    /// @param input The pipe whose readiness stands in for terminal input, or null for a case
    ///        whose readiness is scripted at the backend instead. Not owned; it must outlive this
    ///        source, and the runtime reads its handle once.
    /// @param resize The pipe whose readiness stands in for a terminal resize, or null for a
    ///        source with no resize channel. Not owned.
    explicit ScriptedInputSource(platform::SystemPipe* input = nullptr,
                                 platform::SystemPipe* resize = nullptr) noexcept:
        _input(input), _resize(resize)
    {
    }

    /// @name Scripting
    /// @{

    /// Appends one read's worth of events, and makes the input handle ready if this source has a
    /// real pipe behind it.
    ///
    /// **Callable from another thread**, which is what a case about input arriving WHILE the loop
    /// waits needs: nothing is ready when the flow parks, and the wait has to be ended by
    /// something outside it. The lock is what makes that legitimate rather than a data race the
    /// pipe's own ordering happens to hide.
    /// @param events What the next @c readReady returns.
    void pushEvents(std::vector<InputEvent> events)
    {
        {
            auto const lock = std::scoped_lock { _mutex };
            _reads.push_back(std::move(events));
        }
        signal(_input);
    }

    /// Appends events as a terminal query hands them back: delivered without waiting, because
    /// they were read before anything still on the handle.
    /// @param events The handed-back events.
    void pushPending(std::vector<InputEvent> events)
    {
        auto const lock = std::scoped_lock { _mutex };
        for (auto& event: events)
            _pending.push_back(std::move(event));
    }

    /// Appends a resize notification, and makes the resize handle ready.
    /// @param resize The size change the next @c readResize reports.
    void pushResize(ResizeEvent resize)
    {
        {
            auto const lock = std::scoped_lock { _mutex };
            _resizes.push_back(resize);
        }
        signal(_resize);
    }

    /// Appends what a partial-escape flush completes to — what a lone ESC decodes to once its
    /// continuation has not arrived.
    /// @param events The events the next @c flushPartial returns.
    void pushFlush(std::vector<InputEvent> events)
    {
        auto const lock = std::scoped_lock { _mutex };
        _flushes.push_back(std::move(events));
    }

    /// @}

    /// @name Observation
    /// @{

    /// @return How many times @c readReady was called.
    [[nodiscard]] std::size_t readCount() const
    {
        auto const lock = std::scoped_lock { _mutex };
        return _readCount;
    }

    /// @return How many times @c flushPartial was called.
    [[nodiscard]] std::size_t flushCount() const
    {
        auto const lock = std::scoped_lock { _mutex };
        return _flushCount;
    }

    /// @}

    [[nodiscard]] platform::NativeHandle inputHandle() const noexcept override
    {
        return _input != nullptr ? _input->waitHandle() : platform::standardInput();
    }

    [[nodiscard]] platform::NativeHandle resizeHandle() const noexcept override
    {
        return _resize != nullptr ? _resize->waitHandle() : platform::InvalidHandle;
    }

    [[nodiscard]] std::vector<InputEvent> takePending() override
    {
        auto const lock = std::scoped_lock { _mutex };
        return std::exchange(_pending, {});
    }

    [[nodiscard]] std::vector<InputEvent> readReady() override
    {
        drain(_input);
        auto const lock = std::scoped_lock { _mutex };
        ++_readCount;
        if (_reads.empty())
            return {};
        auto events = std::move(_reads.front());
        _reads.pop_front();
        return events;
    }

    [[nodiscard]] std::optional<InputEvent> readResize() override
    {
        drain(_resize);
        auto const lock = std::scoped_lock { _mutex };
        if (_resizes.empty())
            return std::nullopt;
        auto const resize = _resizes.front();
        _resizes.pop_front();
        return InputEvent { resize };
    }

    [[nodiscard]] std::vector<InputEvent> flushPartial() override
    {
        auto const lock = std::scoped_lock { _mutex };
        ++_flushCount;
        if (_flushes.empty())
            return {};
        auto events = std::move(_flushes.front());
        _flushes.pop_front();
        return events;
    }

    /// Dispatches nothing and only reports, which is all a case needs: the runtime drops every
    /// protocol response itself, and what a real terminal additionally does with a focus report
    /// (notify its handlers) has no observable here.
    /// @param events The decoded events, filtered in place.
    /// @return Whether a focus report was among them.
    [[nodiscard]] bool consumeReports(std::vector<InputEvent>& events) override
    {
        auto focusChanged = false;
        auto kept = std::vector<InputEvent> {};
        kept.reserve(events.size());
        for (auto& event: events)
        {
            if (!isProtocolReport(event))
            {
                kept.push_back(std::move(event));
                continue;
            }
            if (std::holds_alternative<FocusEvent>(event))
                focusChanged = true;
        }
        events = std::move(kept);
        return focusChanged;
    }

  private:
    /// Makes @p pipe readable, so a real backend reports the readiness a script is standing in
    /// for. A source with no pipe has its readiness scripted at the backend instead.
    /// @param pipe The channel to signal, or null.
    static void signal(platform::SystemPipe* pipe)
    {
        if (pipe == nullptr)
            return;
        char const token = 'x';
        std::ignore = pipe->write(&token, 1);
    }

    /// Consumes one byte of @p pipe's readiness, so a level-triggered backend stops reporting it
    /// once every scripted batch has been read.
    /// @param pipe The channel to drain, or null.
    static void drain(platform::SystemPipe* pipe)
    {
        if (pipe == nullptr)
            return;
        char token = 0;
        std::ignore = pipe->read(&token, 1);
    }

    /// Guards every scripted container below, because @c pushEvents is the one member a case
    /// legitimately calls from a thread that is not the loop's.
    mutable std::mutex _mutex;

    platform::SystemPipe* _input;  ///< Input readiness, or null for a scripted backend.
    platform::SystemPipe* _resize; ///< Resize readiness, or null for no resize channel.

    std::deque<std::vector<InputEvent>> _reads;   ///< One entry per scripted read.
    std::deque<std::vector<InputEvent>> _flushes; ///< One entry per scripted partial-escape flush.
    std::deque<ResizeEvent> _resizes;             ///< One entry per scripted resize.
    std::vector<InputEvent> _pending;             ///< What a terminal query handed back.

    std::size_t _readCount = 0;  ///< How many reads happened.
    std::size_t _flushCount = 0; ///< How many flushes happened.
};

} // namespace core::tui::runtime::testing
