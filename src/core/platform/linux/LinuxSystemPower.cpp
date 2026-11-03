// SPDX-License-Identifier: Apache-2.0
#include <core/platform/SystemPower.hpp>
#include <core/platform/detail/LogindPlan.hpp>

#include <sys/eventfd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <expected>
#include <memory>
#include <thread>
#include <utility>

#include <poll.h>
#include <unistd.h>

#include <systemd/sd-bus.h>

namespace core::platform
{

namespace
{
    [[nodiscard]] std::uint64_t monotonicMicroseconds() noexcept
    {
        auto now = timespec {};
        ::clock_gettime(CLOCK_MONOTONIC, &now);
        return (static_cast<std::uint64_t>(now.tv_sec) * 1'000'000U)
               + (static_cast<std::uint64_t>(now.tv_nsec) / 1'000U);
    }

    /// System sleep/wake notifications from logind's PrepareForSleep signal, read over sd-bus on a
    /// private thread.
    class LinuxSystemPowerMonitor final: public ISystemPowerMonitor
    {
      public:
        LinuxSystemPowerMonitor() = default;
        LinuxSystemPowerMonitor(LinuxSystemPowerMonitor const&) = delete;
        LinuxSystemPowerMonitor(LinuxSystemPowerMonitor&&) = delete;
        LinuxSystemPowerMonitor& operator=(LinuxSystemPowerMonitor const&) = delete;
        LinuxSystemPowerMonitor& operator=(LinuxSystemPowerMonitor&&) = delete;

        /// Stops and joins the thread first, so no callback runs once the bus is closed or the
        /// registry is destroyed.
        ~LinuxSystemPowerMonitor() override
        {
            if (_thread.joinable())
            {
                auto const one = std::uint64_t { 1 };
                while (::write(_shutdown, &one, sizeof(one)) == -1 && errno == EINTR)
                    ;
                _thread.join();
            }
            if (_bus != nullptr)
                sd_bus_flush_close_unref(_bus);
            if (_shutdown != -1)
                ::close(_shutdown);
        }

        /// Connects to the system bus, matches PrepareForSleep and starts the reading thread.
        /// @return Whether the registration succeeded.
        [[nodiscard]] std::expected<void, PlatformError> start()
        {
            _shutdown = ::eventfd(0, EFD_CLOEXEC);
            if (_shutdown == -1 || sd_bus_open_system(&_bus) < 0)
                return std::unexpected(PlatformError::NotificationRegistrationFailed);
            auto const matched = sd_bus_match_signal(_bus,
                                                     nullptr,
                                                     "org.freedesktop.login1",
                                                     "/org/freedesktop/login1",
                                                     "org.freedesktop.login1.Manager",
                                                     "PrepareForSleep",
                                                     &onPrepareForSleep,
                                                     this);
            if (matched < 0)
                return std::unexpected(PlatformError::NotificationRegistrationFailed);
            _thread = std::jthread { [this] { run(); } };
            return {};
        }

        [[nodiscard]] PowerSubscription subscribe(Handler handler) override
        {
            return _registry.subscribe(std::move(handler));
        }

      private:
        static int onPrepareForSleep(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/)
        {
            auto sleeping = 0;
            if (sd_bus_message_read(message, "b", &sleeping) < 0)
                return 0;
            auto& self = *static_cast<LinuxSystemPowerMonitor*>(userdata);
            auto const state =
                sleeping != 0 ? detail::LogindSleepState::Sleeping : detail::LogindSleepState::Waking;
            self._registry.dispatch(detail::eventForPrepareForSleep(state));
            return 0;
        }

        /// Serves the bus until the shutdown eventfd is signalled or the connection fails.
        void run()
        {
            auto serving = true;
            while (serving)
            {
                auto processed = 0;
                do
                    processed = sd_bus_process(_bus, nullptr);
                while (processed > 0);
                auto deadline = std::uint64_t { 0 };
                if (processed < 0 || sd_bus_get_timeout(_bus, &deadline) < 0)
                    break;

                auto fds = std::array {
                    pollfd { .fd = sd_bus_get_fd(_bus),
                             .events = static_cast<short>(sd_bus_get_events(_bus)),
                             .revents = 0 },
                    pollfd { .fd = _shutdown, .events = POLLIN, .revents = 0 },
                };
                auto const timeout = detail::pollTimeoutMilliseconds(deadline, monotonicMicroseconds());
                auto const ready = ::poll(fds.data(), fds.size(), timeout);
                serving = (ready >= 0 || errno == EINTR) && (fds[1].revents & POLLIN) == 0;
            }
        }

        // Declared first, so it outlives the bus and the thread that dispatch into it.
        detail::PowerHandlerRegistry _registry;
        int _shutdown = -1;
        sd_bus* _bus = nullptr;
        std::jthread _thread;
    };
} // namespace

std::expected<std::unique_ptr<ISystemPowerMonitor>, PlatformError> makeNativeSystemPowerMonitor()
{
    auto monitor = std::make_unique<LinuxSystemPowerMonitor>();
    if (auto started = monitor->start(); !started)
        return std::unexpected(started.error());
    return monitor;
}

} // namespace core::platform
