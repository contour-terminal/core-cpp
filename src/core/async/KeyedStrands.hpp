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
#include <coroutine>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#if CORE_ASYNC_STRAND_HAS_THREADS
    #include <condition_variable>
#endif

namespace core::async
{

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
            StrandCore(base, options, nullptr, registry.get(), StrandReclaim::WhenIdle),
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
        KeyedStrandsRegistry(IExecutor& base, StrandOptions options) noexcept: _base(base), _options(options)
        {
        }

        /// Queues @p work on @p key's strand, making the strand if the key has none. After the
        /// registry closed, the work is dropped.
        ///
        /// Touches @p key only until the work is queued: it may be a member of the awaiter of the
        /// very coroutine being queued, which another thread can resume, and so destroy, the moment
        /// the strand's lock is released.
        /// @param key The key.
        /// @param work The coroutine to resume on it.
        void submit(Key const& key, ParkedWork work)
        {
            auto pump = std::coroutine_handle<> {};
            {
                auto const lock = std::scoped_lock { _mutex };
                if (_closed)
                    return; // `work` drops outside the lock, freeing what nobody owns.
                auto slot = _strands.find(key);
                if (slot == _strands.end())
                    slot = _strands
                               .emplace(key,
                                        std::make_shared<KeyStrandType>(
                                            this->shared_from_this(), key, _base, _options))
                               .first;
                // Under the registry's lock, so a retirement cannot slip between the lookup and the
                // queueing: `retire` takes this lock first, then the strand's.
                pump = slot->second->enqueue(std::move(work));
            }
            // Outside every lock: a base that resumes inline runs the strand's tasks in this call.
            if (pump)
                _base.submit(pump);
        }

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
#if CORE_ASYNC_STRAND_HAS_THREADS
            if (_strands.empty())
                _idle.notify_all();
#endif
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
#if CORE_ASYNC_STRAND_HAS_THREADS
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

#if CORE_ASYNC_STRAND_HAS_THREADS
        /// Blocks until no key has a strand.
        void waitIdle()
        {
            auto lock = std::unique_lock { _mutex };
            _idle.wait(lock, [this] { return _strands.empty() || _closed; });
        }
#endif

      private:
        IExecutor& _base;
        StrandOptions _options;

        mutable std::mutex _mutex; ///< Guards everything below; taken before any strand's own.
#if CORE_ASYNC_STRAND_HAS_THREADS
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
/// a throw out of `resume()` and destruction holds for each key's strand; `runningHere(key)` and
/// `runningAnyHere()` are its queries. The base must outlive this object and run what it queues.
///
/// @tparam Key The key type: copyable, hashable by @p Hash, compared by @p KeyEqual.
/// @tparam Hash Hashes a key.
/// @tparam KeyEqual Compares two keys; default-constructed wherever it is used.
template <typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class KeyedStrands final
{
  public:
    /// @param base Where every key's strand runs. Must outlive this object.
    /// @param options How each key's strand shares the base.
    explicit KeyedStrands(IExecutor& base, StrandOptions options = {}):
        _registry(std::make_shared<Registry>(base, options))
    {
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

#if CORE_ASYNC_STRAND_HAS_THREADS
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
