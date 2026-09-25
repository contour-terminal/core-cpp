// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `KeyedStrands` — one @c Strand per key, made when a key gets work and reclaimed when it runs
/// out.

#include <core/async/ExecutorContext.hpp>
#include <core/async/IExecutor.hpp>
#include <core/async/ParkedWork.hpp>
#include <core/async/Strand.hpp>

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if CORE_CPP_ASYNC_HAS_THREADS
    #include <condition_variable>
#endif

namespace core::async
{

/// A hook `KeyedStrands` calls around every task any key's strand runs, with the key: to install
/// the ambient context that belongs to that key -- the session of the action the key is running --
/// for exactly the length of each task. What `AroundTask` says holds for it.
/// @tparam Key The key type.
template <typename Key>
struct KeyedAroundTask
{
    /// Called with @c context, the key and the task; must call the task (see @c RunTask).
    void (*call)(void* context, Key const& key, RunTask run) = nullptr;
    /// Passed to @c call.
    void* context = nullptr;

    /// @tparam Hook A callable taking the key and a @c RunTask.
    /// @param hook The hook; referenced, so it must outlive the `KeyedStrands` given the result.
    /// @return A hook that calls @p hook.
    template <typename Hook>
        requires std::invocable<Hook&, Key const&, RunTask>
    [[nodiscard]] static KeyedAroundTask of(Hook& hook) noexcept
    {
        return KeyedAroundTask {
            .call =
                [](void* context, Key const& key, RunTask run) { (*static_cast<Hook*>(context))(key, run); },
            .context = std::addressof(hook),
        };
    }

    /// @return Whether a hook is set.
    [[nodiscard]] explicit operator bool() const noexcept { return call != nullptr; }
};

namespace detail
{

    template <typename Key, typename Hash, typename KeyEqual>
    class KeyedStrandsRegistry;

    /// One key's strand. It is the executor its tasks see as current, and what a @c ResumeTarget
    /// taken in one of them keeps alive, so a coroutine that parked while its key went idle can
    /// still find the key: a retired strand hands what it is given to the registry, which gives it
    /// to the key's current strand, or makes one.
    template <typename Key, typename Hash, typename KeyEqual>
    class KeyStrand final: public StrandCore
    {
      public:
        using Registry = KeyedStrandsRegistry<Key, Hash, KeyEqual>;

        /// @param registry The strands this one belongs to.
        /// @param key The key it serves.
        /// @param base Where its pump runs.
        /// @param options How it shares the base.
        KeyStrand(std::shared_ptr<Registry> registry, Key key, IExecutor& base, StrandOptions options):
            StrandCore(base,
                       withKeyedHook(options, registry->hooked(), this),
                       registry.get(),
                       StrandReclaim::WhenIdle),
            _registry(std::move(registry)),
            _key(std::move(key))
        {
        }

        /// @return The key this strand serves.
        [[nodiscard]] Key const& key() const noexcept { return _key; }

      protected:
        [[nodiscard]] bool tryRetire() override { return _registry->retire(*this); }

        void reroute(ParkedWork work) override { _registry->submit(_key, std::move(work)); }

      private:
        /// @return @p options, with an around-task hook that hands this strand's key to the
        ///         registry's keyed hook where the registry has one.
        [[nodiscard]] static StrandOptions withKeyedHook(StrandOptions options,
                                                         bool hooked,
                                                         KeyStrand* self) noexcept
        {
            if (hooked)
                options.aroundTask = AroundTask {
                    .call =
                        [](void* context, RunTask run) {
                            auto const& strand = *static_cast<KeyStrand const*>(context);
                            strand._registry->aroundTask(strand._key, run);
                        },
                    .context = self,
                };
            return options;
        }

        std::shared_ptr<Registry> _registry;
        Key _key;
    };

    /// The keys and their strands, shared by the @c KeyedStrands that owns them and by each key's
    /// strand, so a strand a parked coroutine still holds can reach it after the owner is gone.
    template <typename Key, typename Hash, typename KeyEqual>
    class KeyedStrandsRegistry final:
        public std::enable_shared_from_this<KeyedStrandsRegistry<Key, Hash, KeyEqual>>
    {
      public:
        using KeyStrandType = KeyStrand<Key, Hash, KeyEqual>;

        /// @param base Where every key's pump runs.
        /// @param options How each key's strand shares the base.
        /// @param aroundTask Called around every task, with its key; may be unset.
        KeyedStrandsRegistry(IExecutor& base, StrandOptions options, KeyedAroundTask<Key> aroundTask) noexcept
            :
            _base(base), _options(options), _aroundTask(aroundTask)
        {
        }

        /// Queues @p work on @p key's strand, making the strand if the key has none. After the
        /// registry closed, the work is dropped, which frees what nobody owns.
        /// @param key The key.
        /// @param work The coroutine to resume on it.
        void submit(Key const& key, ParkedWork work)
        {
            try
            {
                // `work` drops when this returns, if it was refused, freeing what nobody owns.
                std::ignore = offer(key, [&work] { return StrandTask { Parked { std::move(work) } }; });
            }
            catch (...)
            {
                // The caller resumes the coroutine with this, so its claim is given back rather than
                // released.
                work.abandon.disarm();
                throw;
            }
        }

        /// Queues @p work on @p key's strand unless the registry is closed.
        /// @return Whether it was queued; where not, @p work is as it was.
        [[nodiscard]] bool trySubmit(Key const& key, ParkedWork& work)
        {
            return offer(key, [&work] { return StrandTask { Parked { std::move(work) } }; });
        }

        /// Queues a call made from @p fn on @p key's strand unless the registry is closed. The call
        /// is allocated before any lock is taken and made under it.
        /// @return Whether it was queued; where not, @p fn is as it was.
        template <typename Arg>
        [[nodiscard]] bool offerCall(Key const& key, Arg&& fn)
        {
            auto storage = CallStorage<PostedCallOf<std::decay_t<Arg>>> {};
            return offer(key,
                         [&storage, &fn] { return StrandTask { storage.construct(std::forward<Arg>(fn)) }; });
        }

        /// Calls the keyed around-task hook. @pre @c hooked.
        /// @param key The key whose task @p run is.
        /// @param run The task.
        void aroundTask(Key const& key, RunTask run) const
        {
            _aroundTask.call(_aroundTask.context, key, run);
        }

        /// @return Whether a keyed around-task hook is set.
        [[nodiscard]] bool hooked() const noexcept { return static_cast<bool>(_aroundTask); }

        /// Retires @p strand if it is still @p strand's key's strand and has nothing queued.
        /// @param strand A strand whose pump ran out of work.
        /// @return Whether it was retired, and removed.
        [[nodiscard]] bool retire(KeyStrandType& strand)
        {
            auto const lock = std::scoped_lock { _mutex };
            auto const slot = _strands.find(strand.key());
            if (slot == _strands.end() || slot->second.get() != &strand || !strand.retireIfEmpty())
                return false;
            _strands.erase(slot);
            notifyIfIdleLocked();
            return true;
        }

        /// Closes every strand and refuses what arrives later. See `~KeyedStrands`.
        void close()
        {
            auto strands = std::unordered_map<Key, std::shared_ptr<KeyStrandType>, Hash, KeyEqual> {};
            {
                auto const lock = std::scoped_lock { _mutex };
                _closed = true;
                strands.swap(_strands);
#if CORE_CPP_ASYNC_HAS_THREADS
                _idle.notify_all();
#endif
            }
            for (auto& [key, strand]: strands)
                strand->close();
        }

        /// @return How many keys have a strand right now.
        [[nodiscard]] std::size_t size() const
        {
            auto const lock = std::scoped_lock { _mutex };
            return _strands.size();
        }

        /// @return Whether no key has a strand: nothing queued or running on any.
        [[nodiscard]] bool idle() const
        {
            auto const lock = std::scoped_lock { _mutex };
            return _strands.empty();
        }

#if CORE_CPP_ASYNC_HAS_THREADS
        /// Blocks until no key has a strand.
        void waitIdle()
        {
            auto lock = std::unique_lock { _mutex };
            _idle.wait(lock, [this] { return _strands.empty() || _closed; });
        }
#endif

      private:
        /// Queues the task @p make makes on @p key's strand, making the strand if the key has none.
        ///
        /// Touches @p key only until the task is queued: it may be a member of the awaiter of the
        /// very coroutine being queued, which another thread can resume, and so destroy, the moment
        /// the strand's lock is released.
        /// @param key The key.
        /// @param make Makes the task; not called where this returns false.
        /// @return False where the registry is closed, or this thread is freeing work one of its
        ///         strands abandoned (see `detail::FreeingAbandoned`).
        /// @throws What the base's `submit` throws, what @p make throws, `std::bad_alloc`. A strand
        ///         made for the key that could not take the task is removed again.
        template <typename Make>
        [[nodiscard]] bool offer(Key const& key, Make make)
        {
            if (FreeingAbandoned::active(this))
                return false;
            auto enqueued = StrandCore::Enqueued {};
            // Held, not borrowed: once the pump is queued the strand can run, retire and let go of
            // itself before the hand-off below has been told how the base answered.
            auto strand = std::shared_ptr<KeyStrandType> {};
            // A strand made here that could not take the task: removed under the lock, closed
            // outside it, which frees the pump it may already have made.
            auto discarded = std::shared_ptr<KeyStrandType> {};
            try
            {
                auto const lock = std::scoped_lock { _mutex };
                if (_closed)
                    return false;
                auto slot = _strands.find(key);
                auto const made = slot == _strands.end();
                if (made)
                    slot = _strands
                               .emplace(key,
                                        std::make_shared<KeyStrandType>(
                                            this->shared_from_this(), key, _base, _options))
                               .first;
                // Under the registry's lock, so a retirement cannot slip between the lookup and the
                // queueing: `retire` takes this lock first, then the strand's.
                strand = slot->second;
                try
                {
                    enqueued = strand->enqueue(std::move(make));
                }
                catch (...)
                {
                    if (made)
                    {
                        discarded = strand;
                        _strands.erase(slot);
                        notifyIfIdleLocked();
                    }
                    throw;
                }
            }
            catch (...)
            {
                if (discarded)
                    discarded->close();
                throw;
            }
            // Outside every lock: a base that resumes inline runs the strand's tasks in this call.
            if (enqueued.pump)
                strand->queueOnBase(enqueued.pump, enqueued.identity);
            return true;
        }

        /// Wakes `waitIdle` if no key has a strand. Holds the lock.
        void notifyIfIdleLocked() noexcept
        {
#if CORE_CPP_ASYNC_HAS_THREADS
            if (_strands.empty())
                _idle.notify_all();
#endif
        }

        IExecutor& _base;
        StrandOptions _options;
        KeyedAroundTask<Key> _aroundTask;

        mutable std::mutex _mutex; ///< Guards everything below; taken before any strand's own.
#if CORE_CPP_ASYNC_HAS_THREADS
        std::condition_variable _idle; ///< Signalled when the last strand retires.
#endif
        std::unordered_map<Key, std::shared_ptr<KeyStrandType>, Hash, KeyEqual> _strands;
        bool _closed { false };
    };

} // namespace detail

/// One @c Strand per key over a shared base executor: work for one key runs serially and in
/// order, and work for different keys runs concurrently where the base has the threads.
///
/// For state partitioned by a key -- a model instance, a connection, a session -- where one strand
/// would serialise everything and a strand per object would have to be created, owned and
/// destroyed by hand.
///
/// **Made lazily, reclaimed when idle.** A key has a strand only while it has work queued or
/// running: the submit that finds none makes one, and the strand retires itself when its queue
/// runs dry, so a program keyed by connection holds strands for its busy connections, not for every
/// connection it ever had. A coroutine that parked on something another thread completes while
/// its key was reclaimed comes back to the key -- to its current strand, or a new one -- never to a
/// second strand beside it.
///
/// Every member is callable from any thread. What `Strand` says about a task, the current executor,
/// a throw out of `resume()`, posted calls, the `try` members and destruction holds for each key's
/// strand; `runningHere(key)` and `runningAnyHere()` are its queries. The around-task hook is a
/// `KeyedAroundTask`, given the key. The base must outlive this object and run what it queues.
///
/// **Single-threaded WebAssembly.** `waitIdle()` does not exist there: nothing else can finish the
/// work, and a blocking wait is not allowed. Destroying or closing these strands drops what is
/// queued without waiting, since nothing else can be running; a host that wants the work run
/// first pumps its base until `idle()`.
///
/// @tparam Key The key type: copyable, hashable by @p Hash, compared by @p KeyEqual.
/// @tparam Hash Hashes a key.
/// @tparam KeyEqual Compares two keys; default-constructed wherever it is used.
template <typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class KeyedStrands final
{
  public:
    /// @param base Where every key's strand runs. Must outlive this object.
    /// @param options How each key's strand shares the base. Its `aroundTask` must be unset.
    /// @param aroundTask Called around every task, with its key.
    explicit KeyedStrands(IExecutor& base, StrandOptions options = {}, KeyedAroundTask<Key> aroundTask = {}):
        _registry(std::make_shared<Registry>(base, options, aroundTask))
    {
        assert(!options.aroundTask
               && "KeyedStrands takes its around-task hook as a KeyedAroundTask, which is given the key");
    }

    KeyedStrands(KeyedStrands const&) = delete;
    KeyedStrands(KeyedStrands&&) = delete;
    KeyedStrands& operator=(KeyedStrands const&) = delete;
    KeyedStrands& operator=(KeyedStrands&&) = delete;

    /// Closes every key's strand, as `~Strand` does: queued work is dropped, freeing what nobody
    /// owns, and a task running on another thread is waited for. Work that arrives afterwards --
    /// a coroutine that parked on one of these strands and is resumed later -- is dropped too. Called
    /// from inside a task of one of these strands, it does not wait for that task, as `~Strand` does not.
    ~KeyedStrands() { _registry->close(); }

    /// Awaitable that continues the awaiting coroutine on one key's strand.
    class ResumeOnKey final
    {
      public:
        /// @param strands The strands. @param key The key whose strand to continue on.
        ResumeOnKey(KeyedStrands& strands, Key key): _strands(&strands), _key(std::move(key)) {}

        /// @return False: always suspend, so the resumption happens on the strand.
        [[nodiscard]] constexpr bool await_ready() const noexcept { return false; }

        /// Queues the awaiting coroutine on the key's strand.
        /// @tparam Promise The awaiting coroutine's promise type.
        /// @param handle The suspended coroutine.
        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> handle)
        {
            _strands->submit(_key, detail::parkedWorkFor(handle));
        }

        void await_resume() const noexcept {}

      private:
        KeyedStrands* _strands;
        Key _key;
    };

    /// Queues @p handle, borrowed, on @p key's strand.
    /// @param key The key. @param handle The coroutine to resume there.
    void submit(Key const& key, std::coroutine_handle<> handle)
    {
        submit(key, ParkedWork { .resume = handle });
    }

    /// Queues @p work on @p key's strand, holding its claim until it runs.
    /// @param key The key. @param work The coroutine to resume there, and its claim on the chain.
    void submit(Key const& key, ParkedWork work) { _registry->submit(key, std::move(work)); }

    /// @param key The key whose strand to continue on.
    /// @return An awaitable: `co_await strands.resumeOn(key)` hops onto that key's strand.
    [[nodiscard]] ResumeOnKey resumeOn(Key key) { return ResumeOnKey { *this, std::move(key) }; }

    /// @param key The key to ask about.
    /// @return Whether the calling thread is inside a task of @p key's strand.
    [[nodiscard]] bool runningHere(Key const& key) const noexcept
    {
        return ExecutorScope::anyInForce([this, &key](ExecutorScope const& scope) noexcept {
            return scope.family() == _registry.get()
                   && KeyEqual {}(static_cast<KeyStrand const&>(scope.executor()).key(), key);
        });
    }

    /// @return Whether the calling thread is inside a task of any key's strand.
    [[nodiscard]] bool runningAnyHere() const noexcept
    {
        return ExecutorScope::anyInForce(
            [this](ExecutorScope const& scope) noexcept { return scope.family() == _registry.get(); });
    }

    /// @return How many keys have a strand right now: those with work queued or running. Racy by
    ///         nature; for tests and metrics.
    [[nodiscard]] std::size_t size() const { return _registry->size(); }

    /// Queues @p fn, a callable, on @p key's strand to run as one task. Callable from any thread.
    ///
    /// As `Strand::post`: held by value in one allocation -- plus, for a key that has no strand
    /// right now, what making its strand costs -- and dropped uncalled once these strands are
    /// closed.
    /// @param key The key. @param fn The callable, called with no arguments.
    template <typename F>
        requires std::invocable<std::decay_t<F>&> && std::constructible_from<std::decay_t<F>, F>
    void post(Key const& key, F&& fn)
    {
        std::ignore = _registry->offerCall(key, std::forward<F>(fn));
    }

    /// Queues @p fn on @p key's strand, unless these strands are closed. As `Strand::tryPost`.
    /// @param key The key. @param fn The callable; moved from only where this returns true.
    /// @return Whether it was queued.
    template <typename F>
        requires std::invocable<F&> && std::move_constructible<F>
    [[nodiscard]] bool tryPost(Key const& key, F& fn)
    {
        return _registry->offerCall(key, std::move(fn));
    }

    /// Queues @p handle, borrowed, on @p key's strand, unless these strands are closed.
    /// @param key The key. @param handle The coroutine to resume there.
    /// @return Whether it was queued.
    [[nodiscard]] bool trySubmit(Key const& key, std::coroutine_handle<> handle)
    {
        auto work = ParkedWork { .resume = handle };
        return _registry->trySubmit(key, work);
    }

    /// Queues @p work on @p key's strand, unless these strands are closed.
    /// @param key The key. @param work The coroutine and its claim; moved from only where this
    ///        returns true.
    /// @return Whether it was queued.
    [[nodiscard]] bool trySubmit(Key const& key, ParkedWork& work) { return _registry->trySubmit(key, work); }

    /// Closes every key's strand, as the destructor does: queued work is dropped, a task running on
    /// another thread is waited for, and whatever arrives later -- a coroutine that parked on one
    /// of these strands included -- is dropped, or refused by the `try` members. Idempotent; the
    /// destructor calls it.
    void close() { _registry->close(); }

    /// @return Whether no key has work queued or running: what a single-threaded host pumps its
    ///         base until before it destroys these strands. Racy by nature where other threads
    ///         submit.
    [[nodiscard]] bool idle() const { return _registry->idle(); }

#if CORE_CPP_ASYNC_HAS_THREADS
    /// Blocks until no key has work queued or running, including work submitted while it waits.
    ///
    /// Not from inside a task of these strands, which would wait for itself: asserted. Not in the
    /// single-threaded WebAssembly build either, where there is no other thread to finish the work
    /// and a blocking wait is not allowed; that build does not declare it.
    void waitIdle()
    {
        assert(!runningAnyHere()
               && "KeyedStrands::waitIdle called from one of its own tasks would wait for itself");
        _registry->waitIdle();
    }
#endif

  private:
    using Registry = detail::KeyedStrandsRegistry<Key, Hash, KeyEqual>;
    using KeyStrand = detail::KeyStrand<Key, Hash, KeyEqual>;

    std::shared_ptr<Registry> _registry;
};

} // namespace core::async
