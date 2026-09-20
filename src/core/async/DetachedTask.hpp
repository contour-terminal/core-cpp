// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `DetachedTask` — a fire-and-forget coroutine whose frame nobody owns.
///
/// The body runs to its first suspension on construction and the frame frees itself on final
/// return, so the caller keeps no handle: a server spawns one per connection and the chain of
/// I/O awaitables drives it from there.
///
/// It is a header of its own, although fastcached keeps it in `Task.hpp`, because every other
/// piece of this module's vocabulary is (`Awaitable.hpp`, `Cancellation.hpp`,
/// `UniqueCoroHandle.hpp`) and because it is what makes an await chain UNOWNED — the one fact
/// `core::async::detail::unownedRootOf` has to be able to name. Every other coroutine frame here
/// is owned by something: a `Task` value, or the awaiter of whatever awaits it.

#include <core/async/StopToken.hpp>

#include <coroutine>
#include <exception>

namespace core::async
{

/// A coroutine started for its effects, owned by nobody, freeing its own frame when it ends.
///
/// An exception escaping the body terminates the process: there is no caller to hand it to, and
/// no frame left to unwind into. A detached flow catches what it can act on — a connection error
/// becomes a response — before reaching its final suspension.
///
/// It is the one shape an executor may free at teardown, because nothing else can
/// ([`ParkedWork`](ParkedWork.hpp)).
struct DetachedTask
{
    /// The coroutine promise; the standard looks up `DetachedTask::promise_type`.
    struct promise_type
    {
        /// @return The (empty) handle-less object that represents this coroutine.
        [[nodiscard]] DetachedTask get_return_object() noexcept { return {}; }

        /// Runs the body at once: a detached flow has no caller to attach a continuation.
        [[nodiscard]] std::suspend_never initial_suspend() const noexcept { return {}; }

        /// Frees the frame at the end: there is no owner to do it.
        [[nodiscard]] std::suspend_never final_suspend() const noexcept { return {}; }

        void return_void() const noexcept {}

        /// Ends the process: there is nowhere for an exception to go from here.
        void unhandled_exception() const noexcept { std::terminate(); }

        /// @return A token that never reports a stop.
        ///
        /// A detached flow has no awaiting coroutine to inherit cancellation from, and it carries
        /// no source of its own: what cancels it is whatever it awaits, through that awaitable's
        /// own token. Answering with an empty token rather than not answering at all is what lets
        /// a `Task` awaited from here take the ordinary inheritance path (@c HasStopToken) instead
        /// of a second, silent one.
        [[nodiscard]] StopToken stopToken() const noexcept { return StopToken {}; }
    };
};

} // namespace core::async
