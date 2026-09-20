// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `whenAny` — run several `Task<void>`s concurrently on one runtime and complete
/// as soon as the FIRST of them completes, cancelling the rest.
///
/// This is the select-style counterpart to @c whenAll: where `whenAll` waits for
/// every child and never cancels siblings, `whenAny` resumes the awaiting
/// coroutine with the index of the first child to finish and requests stop on a
/// shared child @c StopSource so the losing children unwind via
/// @c OperationCancelled. The losers must therefore be cancellation-safe (RAII
/// cleanup on @c OperationCancelled) — every runtime awaitable already is.
///
/// Cancellation propagates both ways: the children observe the shared child token
/// (so the winner cancels the losers), and a @c StopCallback on the awaiting
/// coroutine's own token chains into the child source (so cancelling the parent
/// cancels every child). The first child to reach its final suspension latches the
/// result and tail-transfers to the awaiting coroutine; later finishers are
/// no-ops, so the parent is resumed exactly once.
///
/// **The race state is reference-counted, and every call into it that can run
/// foreign code holds a reference for the duration of that call.** Requesting stop
/// on the child source runs the children's stop callbacks, and a runtime awaitable
/// resumes its coroutine from inside one: the losers then unwind inline, the last
/// of them transfers to the awaiting coroutine, @c await_resume() throws, and the
/// awaiting frame unwinds — destroying the @c WhenAnyAwaiter, and with it the
/// child source whose @c request_stop() is still on the stack. A state the awaiter
/// merely held as a member would be freed there, and the rest of that
/// @c request_stop() would run on freed memory. Holding it by @c shared_ptr makes
/// that impossible whichever @c StopToken this build takes: it is the caller's job
/// to keep a stop state alive across its own @c request_stop(), and neither
/// `std::stop_source` nor the fallback promises to do it for us.

#include <core/async/Awaitable.hpp>
#include <core/async/Cancellation.hpp>
#include <core/async/Task.hpp>
#include <core/async/UniqueCoroHandle.hpp>

#include <coroutine>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace core::async
{

namespace detail
{

    /// Shared join state for a `whenAny`. The first child to finish latches the
    /// winner index and requests stop so the losers unwind; the awaiting coroutine
    /// is resumed only once EVERY child has finished (winner completed + losers
    /// unwound), so the awaiter — which owns the child frames — outlives them all.
    /// @c remaining counts live children plus one start-phase guard.
    ///
    /// It is held by @c shared_ptr — by the awaiter, by every runner promise and by
    /// the parent→child cancel bridge — so that it outlives the awaiter wherever a
    /// stop callback of its own brings the race to an end (see the file comment).
    struct WhenAnyState
    {
        std::size_t remaining = 0;            ///< Live children plus the start-phase guard.
        std::optional<std::size_t> winner;    ///< The first child to complete; the latch as well.
        std::coroutine_handle<> continuation; ///< The `whenAny` awaiter's coroutine.
        std::exception_ptr exception;         ///< Winner's exception, rethrown to the awaiter.
        StopSource childStop;                 ///< request_stop() cancels the losing children.
    };

    /// A child wrapper coroutine. It awaits one task, records the winner index and
    /// any exception at its final suspension, and — if it is the first to finish —
    /// cancels its siblings and tail-transfers to the awaiting coroutine.
    class WhenAnyRunner
    {
      public:
        struct PromiseType
        {
            std::shared_ptr<WhenAnyState> state; ///< Shared, so the state outlives every call on it.
            std::size_t index = 0;               ///< This runner's position in the input list.
            StopToken token;                     ///< The shared child token (cancels losers).
            std::exception_ptr failure;          ///< This child's failure, if its task threw.
            bool cancelled = false;              ///< Its task unwound on OperationCancelled: a loser.

            WhenAnyRunner get_return_object() noexcept
            {
                return WhenAnyRunner { std::coroutine_handle<PromiseType>::from_promise(*this) };
            }

            [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }

            /// Final awaiter: the first child to COMPLETE claims the win, propagates its
            /// failure (if any) to the shared state, and requests stop so the losers
            /// unwind. A child that unwound cancelled claims nothing: it is a loser,
            /// whether the winner cancelled it or the awaiting flow did. The LAST child
            /// to finish (winner or unwound loser) tail-transfers to the awaiting
            /// coroutine — so the awaiter, which owns every child frame, is not
            /// destroyed until no child is still parked.
            struct FinalAwaiter
            {
                [[nodiscard]] bool await_ready() const noexcept { return false; }

                [[nodiscard]] std::coroutine_handle<> await_suspend(
                    std::coroutine_handle<PromiseType> self) const noexcept
                {
                    auto& promise = self.promise();
                    // A copy, not a reference to the promise's: request_stop() below can unwind
                    // this whole race and destroy this frame's owner, and the state has to outlive
                    // the rest of this function.
                    auto const race = promise.state;
                    if (!race->winner.has_value() && !promise.cancelled)
                    {
                        race->winner = promise.index;
                        race->exception = promise.failure; // surface the winner's failure, if any
                        race->childStop.request_stop();    // unwind the losing siblings
                    }
                    if (--race->remaining == 0 && race->continuation)
                        return race->continuation;
                    return std::noop_coroutine();
                }

                void await_resume() const noexcept {}
            };

            [[nodiscard]] FinalAwaiter final_suspend() const noexcept { return {}; }

            /// Sorts what escaped the child's task into the two things it can be: a
            /// cancellation, which makes this child a loser, or a failure, which the
            /// final awaiter surfaces if this child is the winner. Classifying here
            /// rather than in the runner body is what tells the two apart at all: a
            /// body that swallowed its @c OperationCancelled would reach the final
            /// awaiter looking exactly like a child that ran to completion.
            void unhandled_exception() noexcept
            {
                try
                {
                    throw;
                }
                catch (OperationCancelled const&)
                {
                    cancelled = true;
                }
                catch (...)
                {
                    failure = std::current_exception();
                }
            }

            void return_void() const noexcept {}

            /// @return The cancellation token observed by this runner (and its task).
            [[nodiscard]] StopToken const& stopToken() const noexcept { return token; }
        };

        using promise_type = PromiseType;
        using HandleType = std::coroutine_handle<PromiseType>;

        explicit WhenAnyRunner(HandleType handle) noexcept: _handle(handle) {}

        WhenAnyRunner(WhenAnyRunner&&) noexcept = default;
        WhenAnyRunner& operator=(WhenAnyRunner&&) noexcept = default;
        WhenAnyRunner(WhenAnyRunner const&) = delete;
        WhenAnyRunner& operator=(WhenAnyRunner const&) = delete;
        ~WhenAnyRunner() = default;

        [[nodiscard]] HandleType handle() const noexcept { return _handle.get(); }

      private:
        UniqueCoroHandle<PromiseType> _handle;
    };

    /// Wraps one task so it participates in the race. Whatever the task throws goes
    /// to the runner promise's @c unhandled_exception, which tells a cancellation
    /// (this child lost) from a failure (this child's, to surface if it wins); either
    /// way the runner reaches its final awaiter, so the join counter is always
    /// decremented.
    /// @param task The work to run.
    inline WhenAnyRunner makeWhenAnyRunner(Task<void> task)
    {
        co_await std::move(task);
    }

    /// The parent→child cancellation bridge: the callback registered on the awaiting coroutine's
    /// own token, which requests stop on the shared child source.
    ///
    /// A named functor rather than a lambda in a `StopCallback<std::function<void()>>`: one
    /// pointer of state needs neither an allocation nor an indirect call. It holds the race state
    /// by @c shared_ptr and takes a copy of that pointer before it requests stop, because the very
    /// request can end the race, unwind the awaiting coroutine and destroy this callback — the
    /// copy on this stack frame is then all that keeps the child source alive until
    /// @c request_stop() returns.
    class WhenAnyCancelBridge
    {
      public:
        /// @param state The race state to request stop on.
        explicit WhenAnyCancelBridge(std::shared_ptr<WhenAnyState> state) noexcept: _state(std::move(state))
        {
        }

        /// Requests stop on the child source, holding the state alive across the call.
        void operator()() const noexcept
        {
            auto const held = _state;
            held->childStop.request_stop();
        }

      private:
        std::shared_ptr<WhenAnyState> _state;
    };

    /// Awaitable that starts every runner and resumes the awaiting coroutine once
    /// the first child completes, returning that child's index.
    class WhenAnyAwaiter
    {
      public:
        explicit WhenAnyAwaiter(std::vector<Task<void>> tasks):
            _tasks(std::move(tasks)), _state(std::make_shared<WhenAnyState>())
        {
        }

        [[nodiscard]] bool await_ready() const noexcept { return _tasks.empty(); }

        /// Builds and starts a runner per task; keeps the awaiting coroutine
        /// suspended unless a child completes synchronously during start.
        /// @param awaiting The coroutine performing `co_await whenAny(...)`.
        /// @return False if a child already won synchronously (resume immediately).
        template <typename Promise>
        [[nodiscard]] bool await_suspend(std::coroutine_handle<Promise> awaiting)
        {
            _state->continuation = awaiting;
            _state->remaining = _tasks.size() + 1; // +1 start-phase guard

            // Chain parent cancellation into the child source so cancelling the
            // awaiting flow cancels every child.
            if constexpr (HasStopToken<Promise>)
            {
                _parentToken = awaiting.promise().stopToken();
                _parentReg.emplace(_parentToken, WhenAnyCancelBridge { _state });
            }

            _runners.reserve(_tasks.size());
            for (auto& task: _tasks)
                _runners.push_back(makeWhenAnyRunner(std::move(task)));

            auto const childToken = _state->childStop.get_token();
            for (auto const i: std::views::iota(std::size_t { 0 }, _runners.size()))
            {
                auto& promise = _runners[i].handle().promise();
                promise.state = _state;
                promise.index = i;
                promise.token = childToken;
                _runners[i].handle().resume();
            }

            // Release the start-phase guard. If every child already finished
            // synchronously (the winner ran and the losers saw the stop and unwound
            // immediately), remaining hits zero here and we resume the parent inline.
            return --_state->remaining != 0;
        }

        /// @return The index of the first task to complete, or nothing where none did:
        ///         an empty input, or every child unwound cancelled.
        /// @throws The winner's exception, if it failed; @c OperationCancelled if the
        ///         awaiting flow itself was cancelled and no child completed.
        [[nodiscard]] std::optional<std::size_t> await_resume() const
        {
            // Only where nothing won: a child that completed did so, and a cancellation
            // that arrives after it cannot undo it. `whenAny(readSocket(), timeout())`
            // whose read consumed bytes has nowhere to put them back, and
            // .agent/rules/async-and-net.md is explicit that the data wins. Where no
            // child completed, `winner` is empty -- a cancelled loser latches nothing --
            // and a stopped parent token is what says why.
            if (!_state->winner.has_value() && _parentToken.stop_requested())
                throw OperationCancelled {};
            if (_state->exception)
                std::rethrow_exception(_state->exception);
            return _state->winner;
        }

      private:
        std::vector<Task<void>> _tasks;       ///< Moved into runners on suspend.
        std::vector<WhenAnyRunner> _runners;  ///< Kept alive until the race completes.
        std::shared_ptr<WhenAnyState> _state; ///< Shared with the runners and the cancel bridge.
        StopToken _parentToken;               ///< The awaiting flow's own token (empty when it has none).
        std::optional<StopCallback<WhenAnyCancelBridge>> _parentReg; ///< Parent→child cancel bridge.
    };

} // namespace detail

/// Runs all given tasks concurrently and completes when the FIRST completes,
/// cancelling the rest.
/// @param tasks The tasks to race (moved in). Losers are cancelled via a shared
///        child stop source, so each must unwind cleanly on @c OperationCancelled.
/// @return An awaitable; `co_await` it to suspend until the first task finishes.
///         It resolves to the winner's index, or to @c std::nullopt where no child
///         completed at all (an empty input, or every child unwound cancelled).
[[nodiscard]] inline auto whenAny(std::vector<Task<void>> tasks) -> detail::WhenAnyAwaiter
{
    return detail::WhenAnyAwaiter { std::move(tasks) };
}

/// Convenience overload: races the given tasks.
/// @param tasks The tasks to race (moved in).
/// @return An awaitable resolving to the index of the first task to complete, or to
///         @c std::nullopt where none did.
template <typename... Tasks>
    requires(sizeof...(Tasks) > 0 && (std::is_same_v<Tasks, Task<void>> && ...))
[[nodiscard]] auto whenAny(Tasks&&... tasks) -> detail::WhenAnyAwaiter
{
    auto vec = std::vector<Task<void>> {};
    vec.reserve(sizeof...(Tasks));
    (vec.push_back(std::forward<Tasks>(tasks)), ...);
    return detail::WhenAnyAwaiter { std::move(vec) };
}

} // namespace core::async
