// SPDX-License-Identifier: Apache-2.0
#include <core/platform/SystemPower.hpp>

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

namespace core::platform::detail
{

/// The registry's shared state: handlers in subscription order, and who is running one.
class PowerHandlerRegistryState: public std::enable_shared_from_this<PowerHandlerRegistryState>
{
  public:
    [[nodiscard]] PowerSubscription subscribe(ISystemPowerMonitor::Handler handler)
    {
        auto const lock = std::scoped_lock { _mutex };
        auto const id = ++_lastId;
        _entries.push_back(Entry {
            .id = id, .handler = std::make_shared<ISystemPowerMonitor::Handler>(std::move(handler)) });
        return PowerSubscription { shared_from_this(), id };
    }

    void dispatch(SystemPowerEvent event)
    {
        auto snapshot = std::vector<Entry> {};
        {
            auto const lock = std::scoped_lock { _mutex };
            snapshot = _entries;
        }
        auto const self = std::this_thread::get_id();
        for (auto const& entry: snapshot)
        {
            {
                auto lock = std::unique_lock { _mutex };
                if (!isSubscribed(entry.id))
                    continue;
                _running.push_back(Running { .id = entry.id, .thread = self });
            }
            // A handler that throws must still be unmarked, or unsubscribe would wait forever.
            auto const unmark = Unmark { *this, entry.id, self };
            (*entry.handler)(event);
        }
    }

    [[nodiscard]] std::size_t size() const
    {
        auto const lock = std::scoped_lock { _mutex };
        return _entries.size();
    }

    void unsubscribe(std::uint64_t id) noexcept
    {
        // Declared before the lock, so the handler dies after it is released: destroying a handler can
        // unsubscribe another token of this very registry.
        auto erased = Entry {};
        auto lock = std::unique_lock { _mutex };
        if (auto const it =
                std::ranges::find_if(_entries, [id](Entry const& entry) { return entry.id == id; });
            it != _entries.end())
        {
            erased = std::move(*it);
            _entries.erase(it);
        }
        auto const self = std::this_thread::get_id();
        _idle.wait(lock, [&] {
            return std::ranges::none_of(
                _running, [&](Running const& run) { return run.id == id && run.thread != self; });
        });
    }

  private:
    struct Entry
    {
        std::uint64_t id;
        std::shared_ptr<ISystemPowerMonitor::Handler> handler;
    };

    struct Running
    {
        std::uint64_t id;
        std::thread::id thread;
    };

    /// Marks a handler as no longer running when it returns or throws.
    struct Unmark
    {
        PowerHandlerRegistryState& state;
        std::uint64_t id;
        std::thread::id thread;

        Unmark(PowerHandlerRegistryState& s, std::uint64_t i, std::thread::id t):
            state { s }, id { i }, thread { t }
        {
        }
        Unmark(Unmark const&) = delete;
        Unmark& operator=(Unmark const&) = delete;
        ~Unmark()
        {
            {
                auto const lock = std::scoped_lock { state._mutex };
                auto const it = std::ranges::find_if(
                    state._running, [&](Running const& run) { return run.id == id && run.thread == thread; });
                if (it != state._running.end())
                    state._running.erase(it);
            }
            state._idle.notify_all();
        }
    };

    [[nodiscard]] bool isSubscribed(std::uint64_t id) const
    {
        return std::ranges::any_of(_entries, [id](Entry const& entry) { return entry.id == id; });
    }

    mutable std::mutex _mutex;
    std::condition_variable _idle;
    std::vector<Entry> _entries;
    std::vector<Running> _running;
    std::uint64_t _lastId = 0;
};

PowerHandlerRegistry::PowerHandlerRegistry(): _state { std::make_shared<PowerHandlerRegistryState>() }
{
}

PowerSubscription PowerHandlerRegistry::subscribe(ISystemPowerMonitor::Handler handler)
{
    return _state->subscribe(std::move(handler));
}

void PowerHandlerRegistry::dispatch(SystemPowerEvent event)
{
    _state->dispatch(event);
}

std::size_t PowerHandlerRegistry::size() const
{
    return _state->size();
}

} // namespace core::platform::detail

namespace core::platform
{

PowerSubscription::PowerSubscription(std::shared_ptr<detail::PowerHandlerRegistryState> state,
                                     std::uint64_t id) noexcept:
    _state { std::move(state) }, _id { id }
{
}

PowerSubscription::PowerSubscription(PowerSubscription&& other) noexcept:
    _state { std::move(other._state) }, _id { std::exchange(other._id, 0) }
{
}

PowerSubscription& PowerSubscription::operator=(PowerSubscription&& other) noexcept
{
    if (this != &other)
    {
        release();
        _state = std::move(other._state);
        _id = std::exchange(other._id, 0);
    }
    return *this;
}

PowerSubscription::~PowerSubscription()
{
    release();
}

void PowerSubscription::release() noexcept
{
    if (auto const state = std::exchange(_state, nullptr))
        state->unsubscribe(_id);
    _id = 0;
}

} // namespace core::platform
