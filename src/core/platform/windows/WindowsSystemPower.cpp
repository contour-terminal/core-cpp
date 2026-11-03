// SPDX-License-Identifier: Apache-2.0
#include <core/platform/SystemPower.hpp>
#include <core/platform/detail/PowerBroadcastPlan.hpp>

#include <condition_variable>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <utility>

#include <powrprof.h>
#include <windows.h>

namespace core::platform
{

namespace
{
    /// System sleep/wake notifications from the Windows power manager, delivered on a thread it owns.
    class WindowsSystemPowerMonitor final: public ISystemPowerMonitor
    {
      public:
        WindowsSystemPowerMonitor() = default;
        WindowsSystemPowerMonitor(WindowsSystemPowerMonitor const&) = delete;
        WindowsSystemPowerMonitor(WindowsSystemPowerMonitor&&) = delete;
        WindowsSystemPowerMonitor& operator=(WindowsSystemPowerMonitor const&) = delete;
        WindowsSystemPowerMonitor& operator=(WindowsSystemPowerMonitor&&) = delete;

        /// Cancels the registration, then waits for a callback still running, so nothing touches the
        /// registry once it is destroyed.
        ///
        /// The documentation of PowerUnregisterSuspendResumeNotification does not say whether it waits
        /// for a callback in flight. This relies on two things instead: that no callback *starts* once
        /// the call has returned (what cancelling a registration means), and the in-flight count below,
        /// which covers one that started earlier.
        ~WindowsSystemPowerMonitor() override
        {
            if (_registration != nullptr)
                PowerUnregisterSuspendResumeNotification(_registration);
            auto lock = std::unique_lock { _mutex };
            _idle.wait(lock, [this] { return _inFlight == 0; });
        }

        /// Registers with the power manager.
        /// @return Whether the registration succeeded.
        [[nodiscard]] std::expected<void, PlatformError> start()
        {
            _parameters.Callback = &onBroadcast;
            _parameters.Context = this;
            if (PowerRegisterSuspendResumeNotification(
                    DEVICE_NOTIFY_CALLBACK, reinterpret_cast<HANDLE>(&_parameters), &_registration)
                != ERROR_SUCCESS)
            {
                _registration = nullptr;
                return std::unexpected(PlatformError::NotificationRegistrationFailed);
            }
            return {};
        }

        [[nodiscard]] PowerSubscription subscribe(Handler handler) override
        {
            return _registry.subscribe(std::move(handler));
        }

      private:
        /// Counts a running callback, so the destructor can wait for it.
        struct InFlight
        {
            explicit InFlight(WindowsSystemPowerMonitor& owner): _owner { owner }
            {
                auto const lock = std::scoped_lock { _owner._mutex };
                ++_owner._inFlight;
            }
            InFlight(InFlight const&) = delete;
            InFlight(InFlight&&) = delete;
            InFlight& operator=(InFlight const&) = delete;
            InFlight& operator=(InFlight&&) = delete;
            ~InFlight()
            {
                // Notified under the lock, so the destructor cannot return, and free the condition
                // variable, before this has finished with it.
                auto const lock = std::scoped_lock { _owner._mutex };
                if (--_owner._inFlight == 0)
                    _owner._idle.notify_all();
            }

          private:
            WindowsSystemPowerMonitor& _owner;
        };

        static ULONG CALLBACK onBroadcast(PVOID context, ULONG type, PVOID /*setting*/)
        {
            auto& self = *static_cast<WindowsSystemPowerMonitor*>(context);
            auto const inFlight = InFlight { self };
            if (auto const event = detail::eventForPowerBroadcast(type))
            {
                // An exception must not unwind into the power manager; a failing handler is dropped.
                try
                {
                    self._registry.dispatch(*event);
                }
                catch (...)
                {
                    return ERROR_SUCCESS;
                }
            }
            return ERROR_SUCCESS;
        }

        // Declared first, so it outlives the registration that dispatches into it.
        detail::PowerHandlerRegistry _registry;
        std::mutex _mutex;
        std::condition_variable _idle;
        unsigned _inFlight = 0;
        DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS _parameters {};
        HPOWERNOTIFY _registration = nullptr;
    };
} // namespace

std::expected<std::unique_ptr<ISystemPowerMonitor>, PlatformError> makeNativeSystemPowerMonitor()
{
    auto monitor = std::make_unique<WindowsSystemPowerMonitor>();
    if (auto started = monitor->start(); !started)
        return std::unexpected(started.error());
    return monitor;
}

} // namespace core::platform
