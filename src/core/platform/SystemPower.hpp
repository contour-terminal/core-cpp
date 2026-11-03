// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/platform/PlatformError.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>

namespace core::platform
{

/// A change in the power state of the machine.
enum class SystemPowerEvent : std::uint8_t
{
    WillSleep = 0, ///< The machine is about to sleep.
    DidWake = 1,   ///< The machine has woken up.
};

namespace detail
{
    class PowerHandlerRegistryState;
} // namespace detail

/// A live subscription to an ISystemPowerMonitor.
///
/// Move-only. Destroying (or resetting) it unsubscribes, and waits out a handler that is running for it on
/// another thread, so the handler is no longer running once the destructor returned. Captures by reference
/// are then safe to destroy; captures by value may be destroyed a moment later, on the monitor's thread, when
/// a dispatch still holds the handler. A handler may drop its own subscription: that does not wait.
class PowerSubscription
{
  public:
    /// An empty token, subscribed to nothing.
    PowerSubscription() noexcept = default;
    PowerSubscription(PowerSubscription const&) = delete;
    PowerSubscription& operator=(PowerSubscription const&) = delete;

    /// Takes over the subscription of another token, leaving it empty.
    PowerSubscription(PowerSubscription&& other) noexcept;

    /// Unsubscribes the current subscription, then takes over the one of another token.
    PowerSubscription& operator=(PowerSubscription&& other) noexcept;

    /// Unsubscribes.
    ~PowerSubscription();

  private:
    friend class detail::PowerHandlerRegistryState;

    PowerSubscription(std::shared_ptr<detail::PowerHandlerRegistryState> state, std::uint64_t id) noexcept;

    void release() noexcept;

    std::shared_ptr<detail::PowerHandlerRegistryState> _state;
    std::uint64_t _id = 0;
};

/// Where system sleep/wake notifications come from: the operating system, or a test double.
class ISystemPowerMonitor
{
  public:
    /// Receives each power event.
    using Handler = std::function<void(SystemPowerEvent)>;

    ISystemPowerMonitor() = default;
    ISystemPowerMonitor(ISystemPowerMonitor const&) = delete;
    ISystemPowerMonitor(ISystemPowerMonitor&&) = delete;
    ISystemPowerMonitor& operator=(ISystemPowerMonitor const&) = delete;
    ISystemPowerMonitor& operator=(ISystemPowerMonitor&&) = delete;
    virtual ~ISystemPowerMonitor() = default;

    /// Registers a handler for power events.
    /// Handlers run on a thread the monitor owns; marshal to your own loop.
    /// @param handler Called for every event until the returned token is destroyed.
    /// @return The token that keeps the handler subscribed.
    [[nodiscard]] virtual PowerSubscription subscribe(Handler handler) = 0;
};

namespace detail
{
    /// Thread-safe handler table every ISystemPowerMonitor implementation dispatches through.
    /// Tokens it hands out keep its state alive, so they may safely outlive the registry.
    class PowerHandlerRegistry
    {
      public:
        PowerHandlerRegistry();

        /// Adds a handler.
        /// @param handler Called by dispatch() until the returned token is destroyed.
        /// @return The token that removes the handler again.
        [[nodiscard]] PowerSubscription subscribe(ISystemPowerMonitor::Handler handler);

        /// Calls every live handler, in subscription order, on the calling thread.
        /// A handler may subscribe or unsubscribe, its own subscription included.
        /// @param event What happened.
        void dispatch(SystemPowerEvent event);

        /// The number of live subscriptions.
        [[nodiscard]] std::size_t size() const;

      private:
        std::shared_ptr<PowerHandlerRegistryState> _state;
    };
} // namespace detail

/// Creates the platform's system power monitor.
/// @return The monitor, or the reason this platform (or session) cannot report power events;
///         PlatformError::NotImplemented where there is no backend.
[[nodiscard]] std::expected<std::unique_ptr<ISystemPowerMonitor>, PlatformError>
makeNativeSystemPowerMonitor();

} // namespace core::platform
