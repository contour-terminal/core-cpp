// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `ParkedWork` — a coroutine handed to an executor, and what that executor may free if it
/// never resumes it.
///
/// An executor that queues coroutines has to answer one question its interface does not ask:
/// what happens to work still queued when the executor goes away. Resuming it is not an
/// alternative — a bounded wait re-parks and the drain spins — and freeing it wholesale is a
/// double free for every caller whose `Task` object still owns the frame. So the answer travels
/// with the park, as @c ParkedWork::abandon, which `core::async::detail::parkedWorkFor` fills in
/// exactly where the await chain bottoms out in a @c DetachedTask: the one coroutine shape in
/// this module that nobody owns.
///
/// Origin: [fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025),
/// [fastcached#1054](https://github.com/LASTRADA-Software/fastcached/issues/1054).

#include <core/async/Awaitable.hpp>
#include <core/async/DetachedTask.hpp>

#include <coroutine>
#include <type_traits>
#include <utility>

namespace core::async
{

/// A coroutine parked on an executor: the handle to resume, and — only when this chain belongs
/// to nobody — the frame that executor may free if it never resumes it.
///
/// **The two halves are different questions, and the second has a default that is safe.**
/// `IExecutor::submit(std::coroutine_handle<>)` BORROWS: the contract is "resume this", and the
/// caller guarantees the frame stays alive until it does. That is why an executor may not simply
/// destroy what it holds at teardown — a caller parking a handle whose frame a live `Task` owns
/// would be double-freed rather than have a leak fixed.
///
/// **It is the chain's ROOT, never the parked frame itself, and that distinction is the whole
/// point.** Destroying the parked frame runs its own destructors and stops there: whoever awaits
/// it through a `Task::Awaiter` is left holding a dangling handle, is itself unreachable, and the
/// chain goes on leaking — which is how fastcached measured a four-allocation leak going to three
/// with LeakSanitizer still red. Destroying the root frees all of it, because ownership in a
/// `Task` chain runs downward: each frame's awaiter owns the frame it awaits.
struct ParkedWork
{
    std::coroutine_handle<> resume {}; ///< The coroutine to resume. Never owned by the executor.
    std::coroutine_handle<>
        abandon {}; ///< The chain root to free if it is never resumed; empty when owned elsewhere.
};

namespace detail
{

    /// The frame an executor may free if it never resumes @p handle, or an empty handle where
    /// something else owns this chain.
    ///
    /// Three answers, and the third is what makes this safe to ask at all:
    ///
    /// - a @c DetachedTask is owned by nobody, so it is its own root;
    /// - a promise that carries an @c unownedRoot answers with it — the root of the chain rather
    ///   than this frame, because freeing the frame an executor happens to hold would leave
    ///   whoever awaits it unreachable and still leaked;
    /// - anything else, including the type-erased `std::coroutine_handle<>` (@p Promise deduces
    ///   to `void`), answers *not mine*, which is the borrowing behaviour.
    ///
    /// @tparam Promise The parking coroutine's promise type, as the compiler passes it to
    ///         `await_suspend`.
    /// @param handle The coroutine about to park.
    /// @return The chain root to free on abandonment, or an empty handle.
    template <typename Promise>
    [[nodiscard]] std::coroutine_handle<> unownedRootOf(std::coroutine_handle<Promise> handle) noexcept
    {
        if constexpr (std::is_same_v<Promise, DetachedTask::promise_type>)
            return handle;
        else if constexpr (CarriesUnownedRoot<Promise>)
            return handle.promise().unownedRoot;
        else
            return {};
    }

    /// How a coroutine parks itself on an @c IExecutor.
    ///
    /// The one place the ownership question is answered, so no awaitable has to decide it and
    /// none can get it wrong by omission.
    /// @tparam Promise The parking coroutine's promise type.
    /// @param handle The coroutine about to park.
    /// @return The handle to resume, paired with the chain root to free if it is not.
    template <typename Promise>
    [[nodiscard]] ParkedWork parkedWorkFor(std::coroutine_handle<Promise> handle) noexcept
    {
        return ParkedWork { .resume = handle, .abandon = unownedRootOf(handle) };
    }

    /// One entry in an executor's parked-work container, owning @c ParkedWork::abandon for as
    /// long as it sits there.
    ///
    /// **The ownership is folded into the operation rather than called beside it**: `resume()`
    /// disowns and resumes in one expression, so there is no line an executor can forget the
    /// release on, and a container that is simply cleared — at teardown, or by its own destructor
    /// — frees exactly the chains nothing else can.
    class Parked
    {
      public:
        Parked() noexcept = default;

        /// @param work The handle to resume, and the chain root to free if it is not.
        explicit Parked(ParkedWork work) noexcept: _work(work) {}

        Parked(Parked const&) = delete;
        Parked& operator=(Parked const&) = delete;

        Parked(Parked&& other) noexcept: _work(std::exchange(other._work, ParkedWork {})) {}

        Parked& operator=(Parked&& other) noexcept
        {
            if (this != &other)
            {
                abandon();
                _work = std::exchange(other._work, ParkedWork {});
            }
            return *this;
        }

        ~Parked() { abandon(); }

        /// @return The handle this entry would resume; empty once it has been taken.
        [[nodiscard]] std::coroutine_handle<> handle() const noexcept { return _work.resume; }

        /// @return True while this entry still holds work.
        explicit operator bool() const noexcept
        {
            return static_cast<bool>(_work.resume) || static_cast<bool>(_work.abandon);
        }

        /// Resumes the parked coroutine, giving the chain back to whoever it belongs to.
        ///
        /// Disowns first and resumes last, so a body that runs to its end and frees its own frame
        /// cannot be freed a second time by this entry going out of scope.
        ///
        /// **A handle that cannot be resumed is FREED here, not dropped.** Taking the work out and
        /// then declining to resume it would discard an owned chain root without destroying it — a
        /// silent leak on the one path this type exists to close. The contract is *resumed or
        /// freed, never neither*.
        void resume()
        {
            auto const work = std::exchange(_work, ParkedWork {});
            if (work.resume && !work.resume.done())
            {
                work.resume.resume();
                return;
            }
            if (work.abandon)
                work.abandon.destroy();
        }

        /// Hands the parked work to a caller taking it off this executor.
        ///
        /// After this the caller is the only one who may resume or destroy it, so this entry must
        /// do neither.
        /// @return What was parked here; empty afterwards.
        [[nodiscard]] ParkedWork take() noexcept { return std::exchange(_work, ParkedWork {}); }

      private:
        /// Frees the chain root, if this entry still holds one.
        void abandon() noexcept
        {
            auto const work = std::exchange(_work, ParkedWork {});
            if (work.abandon)
                work.abandon.destroy();
        }

        ParkedWork _work {};
    };

} // namespace detail

} // namespace core::async
