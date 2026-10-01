// SPDX-License-Identifier: Apache-2.0
//
// What an accept loop does about each failed accept: `AcceptErrorPolicy`, driven by a manual clock.
// Every case that walks several codes or several failures collects the wrong answers and asserts
// them ONCE, so a regression reads as one list rather than as a count of failed assertions.
#include <core/net/AcceptPolicy.hpp>
#include <core/net/NetError.hpp>
#include <core/platform/Clock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using core::net::AcceptAction;
using core::net::AcceptDisposition;
using core::net::acceptDispositionOf;
using core::net::AcceptErrorPolicy;
using core::net::AcceptVerdict;
using core::net::NetErrorCode;

namespace
{

/// @return Every code in the enumeration, in declaration order.
[[nodiscard]] auto allCodes()
{
    return std::views::iota(0, static_cast<int>(NetErrorCode::Last))
           | std::views::transform([](int index) { return static_cast<NetErrorCode>(index); });
}

/// @param action An action.
/// @return Its name, for a message.
[[nodiscard]] std::string_view nameOf(AcceptAction action)
{
    switch (action)
    {
        case AcceptAction::AcceptAgain: return "accept again";
        case AcceptAction::Stop: return "stop";
        case AcceptAction::GiveUp: return "give up";
    }
    return "?";
}

/// @param verdict A verdict.
/// @return It in words, for a message.
[[nodiscard]] std::string describe(AcceptVerdict const& verdict)
{
    return std::format("{} after {} ms{}",
                       nameOf(verdict.action),
                       verdict.delay.count(),
                       verdict.warning.has_value() ? ", warning" : "");
}

} // namespace

TEST_CASE("Only a closed or dead listener ends an accept loop at once", "[net][accept-policy]")
{
    // The whole of the defect this policy exists for: a loop that ended on ANY failed accept. So
    // the set that ends a loop is asserted whole, rather than one member at a time -- a code added
    // to it is a server that can be stopped from outside.
    auto ending = std::vector<NetErrorCode> {};
    for (auto const code: allCodes())
    {
        auto const disposition = acceptDispositionOf(code);
        if (disposition == AcceptDisposition::Closed || disposition == AcceptDisposition::Dead)
            ending.push_back(code);
    }
    CHECK(ending == std::vector { NetErrorCode::Cancelled, NetErrorCode::BadHandle });

    // Exhaustion backs off without end; what nothing classifies backs off within a bound. Keying
    // the backoff on `SystemError` instead would back off on a permanently failing listener forever.
    auto wrong = std::string {};
    auto const expect = [&wrong](NetErrorCode code, AcceptDisposition want) {
        if (acceptDispositionOf(code) != want)
            wrong += std::format("{}; ", core::net::toString(code));
    };
    expect(NetErrorCode::ResourceExhausted, AcceptDisposition::Exhausted);
    expect(NetErrorCode::SystemError, AcceptDisposition::Unclassified);
    expect(NetErrorCode::ConnReset, AcceptDisposition::PeerFailed);
    // A poll timeout is one event under two names, and neither is a failure.
    expect(NetErrorCode::Timeout, AcceptDisposition::PollTick);
    expect(NetErrorCode::WouldBlock, AcceptDisposition::PollTick);
    // A value that is no code is unclassified: backed off on within a bound, never spun on.
    expect(NetErrorCode::Last, AcceptDisposition::Unclassified);
    INFO("classified wrongly: " << wrong);
    CHECK(wrong.empty());
}

TEST_CASE("An error pending on one new connection is stepped past without a backoff", "[net][accept-policy]")
{
    // Each of these is one connection's failure, so none may cost the loop a backoff, and none may
    // end it.
    auto const clock = core::platform::ManualClock {};
    auto wrong = std::string {};
    for (auto const code: { NetErrorCode::Eof,
                            NetErrorCode::ConnReset,
                            NetErrorCode::ConnRefused,
                            NetErrorCode::HostUnreach,
                            NetErrorCode::PermissionDenied,
                            NetErrorCode::Unsupported,
                            NetErrorCode::MessageTooLarge })
    {
        auto policy = AcceptErrorPolicy {};
        auto const verdict = policy.onError(code, clock.now());
        if (verdict.action != AcceptAction::AcceptAgain || verdict.delay != std::chrono::milliseconds {})
            wrong += std::format("{}: {}; ", core::net::toString(code), describe(verdict));
    }
    INFO("answered wrongly: " << wrong);
    CHECK(wrong.empty());
}

TEST_CASE("A failed connection is accepted past with a rate-limited warning", "[net][accept-policy]")
{
    auto clock = core::platform::ManualClock {};
    auto policy = AcceptErrorPolicy {};

    auto const first = policy.onError(NetErrorCode::ConnReset, clock.now());
    CHECK(first.action == AcceptAction::AcceptAgain);
    REQUIRE(first.warning.has_value());
    CHECK(first.warning->unreported == 0);

    // Inside the interval: accepted past, and not said.
    clock.advance(std::chrono::seconds { 1 });
    auto said = 0;
    for ([[maybe_unused]] auto const i: std::views::iota(0, 3))
        if (policy.onError(NetErrorCode::ConnReset, clock.now()).warning.has_value())
            ++said;
    CHECK(said == 0);

    // Past it: said again, carrying what it stands for.
    clock.advance(AcceptErrorPolicy::WarnInterval);
    auto const later = policy.onError(NetErrorCode::ConnReset, clock.now());
    REQUIRE(later.warning.has_value());
    CHECK(later.warning->unreported == 3);
    CHECK(core::net::describeAcceptFailure(
              "http", core::net::makeNetError(NetErrorCode::ConnReset, 0, "AcceptEx"), later)
          == "http: an accept failed (connection reset (AcceptEx)); accepting again (3 more since the last "
             "warning)");
}

TEST_CASE("An exhausted accept backs off within a bound, without end, and an accept resets it",
          "[net][accept-policy]")
{
    auto const clock = core::platform::ManualClock {};
    auto policy = AcceptErrorPolicy {};

    auto delays = std::vector<std::int64_t> {};
    for ([[maybe_unused]] auto const i: std::views::iota(0, 9))
        delays.push_back(policy.onError(NetErrorCode::ResourceExhausted, clock.now()).delay.count());
    CHECK(delays == std::vector<std::int64_t> { 10, 20, 40, 80, 160, 320, 640, 1000, 1000 });

    // Exhaustion is transient by definition, so it never gives up, however long it lasts: well past
    // the bound an unclassified failure gives up at.
    auto gaveUpAt = -1;
    for (auto const i: std::views::iota(0, static_cast<int>(AcceptErrorPolicy::UnclassifiedBeforeGiveUp) * 4))
        if (gaveUpAt < 0
            && policy.onError(NetErrorCode::ResourceExhausted, clock.now()).action
                   != AcceptAction::AcceptAgain)
            gaveUpAt = i;
    CHECK(gaveUpAt == -1);

    policy.onAccepted();
    CHECK(policy.onError(NetErrorCode::ResourceExhausted, clock.now()).delay
          == AcceptErrorPolicy::FirstBackoff);

    // A per-connection failure never waits, whatever an exhaustion before it did.
    policy.onAccepted();
    CHECK(policy.onError(NetErrorCode::ConnReset, clock.now()).delay == std::chrono::milliseconds {});
}

TEST_CASE("An unclassified failure backs off, and the loop gives up if nothing else happens",
          "[net][accept-policy]")
{
    // `SystemError` may be permanent -- a listener the network subsystem took down with it -- and a
    // loop backing off on it forever looks alive while it serves nothing. So it backs off as
    // exhaustion does, for a bounded run, and then gives up.
    auto const clock = core::platform::ManualClock {};
    auto policy = AcceptErrorPolicy {};
    auto const bound = AcceptErrorPolicy::UnclassifiedBeforeGiveUp;

    auto wrong = std::string {};
    for (auto const failure: std::views::iota(std::uint32_t { 1 }, bound))
    {
        auto const verdict = policy.onError(NetErrorCode::SystemError, clock.now());
        if (verdict.action != AcceptAction::AcceptAgain || verdict.delay == std::chrono::milliseconds {})
            wrong += std::format("failure {}: {}; ", failure, describe(verdict));
    }
    auto const last = policy.onError(NetErrorCode::SystemError, clock.now());
    if (last.action != AcceptAction::GiveUp)
        wrong += std::format("failure {}: {}, expected to give up; ", bound, describe(last));
    INFO(wrong);
    CHECK(wrong.empty());

    // The count is of failures IN A ROW: an accept, or a connection the listener dequeued and that
    // failed, says the listener is alive, and starts it again.
    for (auto const evidence: { 0, 1 })
    {
        auto alive = AcceptErrorPolicy {};
        for ([[maybe_unused]] auto const i: std::views::iota(std::uint32_t { 1 }, bound))
            std::ignore = alive.onError(NetErrorCode::SystemError, clock.now());
        if (evidence == 0)
            alive.onAccepted();
        else
            std::ignore = alive.onError(NetErrorCode::ConnReset, clock.now());
        INFO((evidence == 0 ? "after an accept" : "after a failed connection"));
        CHECK(alive.onError(NetErrorCode::SystemError, clock.now()).action == AcceptAction::AcceptAgain);
    }
}

TEST_CASE("Failed connections without end make the loop yield and never back off", "[net][accept-policy]")
{
    // A listener answering a per-connection code forever would spin its loop, and one whose accept
    // fails without suspending would never yield it. But each such failure consumed a queued
    // connection, so the answer is a constant YIELD: a backoff that doubled held a port refusing for
    // as long as a flood's backlog of dead connections took to drain at one a second.
    auto const clock = core::platform::ManualClock {};
    auto policy = AcceptErrorPolicy {};
    auto wrong = std::string {};
    auto const expect = [&](int round, std::uint32_t failure, std::chrono::milliseconds want) {
        auto const verdict = policy.onError(NetErrorCode::ConnReset, clock.now());
        if (verdict.delay != want || verdict.action != AcceptAction::AcceptAgain)
            wrong += std::format(
                "round {} failure {}: {}, expected {} ms; ", round, failure, describe(verdict), want.count());
    };
    for (auto const round: { 1, 2, 3 })
    {
        for (auto const failure:
             std::views::iota(std::uint32_t { 1 }, AcceptErrorPolicy::FailuresBeforeYield))
            expect(round, failure, std::chrono::milliseconds {});
        // The same short yield every time: it does not grow.
        expect(round, AcceptErrorPolicy::FailuresBeforeYield, AcceptErrorPolicy::FirstBackoff);
    }
    INFO(wrong);
    CHECK(wrong.empty());

    // One accept and the count starts again.
    policy.onAccepted();
    CHECK(policy.onError(NetErrorCode::ConnReset, clock.now()).delay == std::chrono::milliseconds {});
}

TEST_CASE("A closed listener stops quietly, a dead one gives up, and a poll tick says nothing",
          "[net][accept-policy]")
{
    auto const clock = core::platform::ManualClock {};
    auto policy = AcceptErrorPolicy {};

    auto wrong = std::string {};
    auto const expect = [&](NetErrorCode code, AcceptAction want) {
        auto const verdict = policy.onError(code, clock.now());
        if (verdict.action != want || verdict.delay != std::chrono::milliseconds {}
            || verdict.warning.has_value())
            wrong += std::format("{}: {}, expected {} at once and quietly; ",
                                 core::net::toString(code),
                                 describe(verdict),
                                 nameOf(want));
    };
    expect(NetErrorCode::Cancelled, AcceptAction::Stop);
    expect(NetErrorCode::BadHandle, AcceptAction::GiveUp);
    expect(NetErrorCode::WouldBlock, AcceptAction::AcceptAgain);
    expect(NetErrorCode::Timeout, AcceptAction::AcceptAgain);
    INFO(wrong);
    CHECK(wrong.empty());

    CHECK(core::net::describeAcceptLoopEnded("http",
                                             core::net::makeNetError(NetErrorCode::BadHandle, 0, "accept"))
          == "http: accept loop ended (bad handle (accept)) on a listener that was not closed; it accepts no "
             "more "
             "connections");
}
