// SPDX-License-Identifier: Apache-2.0
#include <core/platform/SystemPower.hpp>
#include <core/platform/detail/PowerMessagePlan.hpp>
#include <core/platform/testing/FakeSystemPowerMonitor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <latch>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using core::platform::PlatformError;
using core::platform::PowerSubscription;
using core::platform::SystemPowerEvent;
using core::platform::detail::PowerHandlerRegistry;
using core::platform::testing::FakeSystemPowerMonitor;

TEST_CASE("a subscriber receives each power event", "[platform][power]")
{
    auto monitor = FakeSystemPowerMonitor {};
    auto received = std::vector<SystemPowerEvent> {};
    auto const subscription = monitor.subscribe([&](SystemPowerEvent event) { received.push_back(event); });

    monitor.emit(SystemPowerEvent::WillSleep);
    monitor.emit(SystemPowerEvent::DidWake);

    CHECK(received == std::vector { SystemPowerEvent::WillSleep, SystemPowerEvent::DidWake });
}

TEST_CASE("a destroyed subscription receives nothing", "[platform][power]")
{
    auto monitor = FakeSystemPowerMonitor {};
    auto received = std::vector<SystemPowerEvent> {};
    auto subscription = monitor.subscribe([&](SystemPowerEvent event) { received.push_back(event); });
    REQUIRE(monitor.subscriberCount() == 1);

    subscription = PowerSubscription {};
    monitor.emit(SystemPowerEvent::DidWake);

    CHECK(received.empty());
    CHECK(monitor.subscriberCount() == 0);
}

TEST_CASE("a moved subscription stays subscribed", "[platform][power]")
{
    auto monitor = FakeSystemPowerMonitor {};
    auto received = std::vector<SystemPowerEvent> {};
    auto first = monitor.subscribe([&](SystemPowerEvent event) { received.push_back(event); });
    auto const second = PowerSubscription { std::move(first) };

    monitor.emit(SystemPowerEvent::DidWake);

    CHECK(received.size() == 1);
    CHECK(monitor.subscriberCount() == 1);
}

TEST_CASE("unsubscribing waits out a running handler", "[platform][power]")
{
    auto registry = PowerHandlerRegistry {};
    auto sequence = std::atomic<int> { 0 };
    auto handlerDone = std::atomic<int> { 0 };
    auto destructorDone = std::atomic<int> { 0 };
    auto entered = std::latch { 1 };
    auto release = std::latch { 1 };

    auto subscription = registry.subscribe([&](SystemPowerEvent) {
        entered.count_down();
        release.wait();
        handlerDone = ++sequence;
    });

    {
        auto dispatcher = std::jthread { [&] { registry.dispatch(SystemPowerEvent::DidWake); } };
        entered.wait();
        auto destroyer = std::jthread { [&] {
            subscription = PowerSubscription {};
            destructorDone = ++sequence;
        } };

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(destructorDone == 0);
        release.count_down();
    }

    CHECK(handlerDone != 0);
    CHECK(handlerDone < destructorDone);
}

TEST_CASE("a handler may drop its own subscription", "[platform][power]")
{
    auto monitor = FakeSystemPowerMonitor {};
    auto calls = 0;
    auto subscription = PowerSubscription {};
    subscription = monitor.subscribe([&](SystemPowerEvent) {
        ++calls;
        subscription = PowerSubscription {};
    });

    monitor.emit(SystemPowerEvent::DidWake);
    monitor.emit(SystemPowerEvent::DidWake);

    CHECK(calls == 1);
    CHECK(monitor.subscriberCount() == 0);
}

TEST_CASE("the native monitor constructs or says why not", "[platform][power]")
{
    auto monitor = core::platform::makeNativeSystemPowerMonitor();
#ifdef __APPLE__
    REQUIRE(monitor.has_value());
#endif
    if (monitor.has_value())
    {
        auto const subscription = (*monitor)->subscribe([](SystemPowerEvent) {});
        SUCCEED();
    }
    else
        CHECK(monitor.error() == PlatformError::NotImplemented);
}

namespace
{
/// Owns a second subscription, as an object captured by another handler would.
struct SubscriptionOwner
{
    PowerSubscription subscription;
};
} // namespace

TEST_CASE("destroying a handler may unsubscribe another token", "[platform][power]")
{
    auto monitor = FakeSystemPowerMonitor {};
    auto owner = std::make_shared<SubscriptionOwner>();
    owner->subscription = monitor.subscribe([](SystemPowerEvent) {});
    auto first = monitor.subscribe([captured = std::move(owner)](SystemPowerEvent) {});
    REQUIRE(monitor.subscriberCount() == 2);

    first = PowerSubscription {}; // destroys the handler, hence the owner, hence the second token

    CHECK(monitor.subscriberCount() == 0);
}

TEST_CASE("destroying an earlier handler may unsubscribe a later token", "[platform][power]")
{
    auto monitor = FakeSystemPowerMonitor {};
    auto owner = std::make_shared<SubscriptionOwner>();
    auto first = monitor.subscribe([captured = owner](SystemPowerEvent) {});
    owner->subscription = monitor.subscribe([](SystemPowerEvent) {});
    owner.reset();
    REQUIRE(monitor.subscriberCount() == 2);

    first = PowerSubscription {}; // the owner dies with the handler; its token sits after the erased entry

    CHECK(monitor.subscriberCount() == 0);
}

TEST_CASE("power messages map to events and acknowledgements", "[platform][power]")
{
    using core::platform::detail::planPowerMessage;
    using core::platform::detail::PowerAcknowledgement;
    using core::platform::detail::PowerMessage;
    using core::platform::detail::PowerMessageAction;

    CHECK(planPowerMessage(PowerMessage::CanSystemSleep)
          == PowerMessageAction { std::nullopt, PowerAcknowledgement::Yes });
    CHECK(planPowerMessage(PowerMessage::WillSleep)
          == PowerMessageAction { SystemPowerEvent::WillSleep, PowerAcknowledgement::Yes });
    CHECK(planPowerMessage(PowerMessage::WillNotSleep)
          == PowerMessageAction { SystemPowerEvent::DidWake, PowerAcknowledgement::No });
    CHECK(planPowerMessage(PowerMessage::HasPoweredOn)
          == PowerMessageAction { SystemPowerEvent::DidWake, PowerAcknowledgement::No });
    CHECK(planPowerMessage(PowerMessage::Other) == PowerMessageAction {});
}
