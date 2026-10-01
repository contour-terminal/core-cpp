// SPDX-License-Identifier: Apache-2.0
#include <core/net/AcceptLoopHealth.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

using core::net::AcceptLoopHealth;
using core::net::StoppedSurface;

TEST_CASE("A stopped surface is recorded in order and its listener told once", "[net][accept-policy]")
{
    auto health = AcceptLoopHealth {};
    CHECK(health.snapshot().empty());

    auto told = std::vector<std::string> {};
    health.subscribe(
        [&told](StoppedSurface const& stopped) { told.push_back(stopped.surface + ": " + stopped.reason); });

    health.stopped("http", "bad handle (accept)");
    health.stopped("admin", "system error (AcceptEx)");

    auto const snapshot = health.snapshot();
    REQUIRE(snapshot.size() == 2);
    CHECK(snapshot[0].surface == "http");
    CHECK(snapshot[1].surface == "admin");
    CHECK(told == std::vector<std::string> { "http: bad handle (accept)", "admin: system error (AcceptEx)" });
}

TEST_CASE("A forwarded registry passes on its stops, the earlier ones included", "[net][accept-policy]")
{
    // A component with a loop and a registry of its own; the process still reads one. A stop
    // recorded before the forward was set up must not be lost to it.
    auto process = AcceptLoopHealth {};
    auto component = AcceptLoopHealth {};
    component.stopped("raft", "bad handle");
    component.forward(process);
    component.stopped("raft-again", "system error");

    auto const seen = process.snapshot();
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].surface == "raft");
    CHECK(seen[1].surface == "raft-again");
    CHECK(component.snapshot().size() == 2);
}

TEST_CASE("A listener may read the registry back without deadlocking", "[net][accept-policy]")
{
    // The listener runs outside the lock, which is what lets a probe answered from inside one see
    // the stop it was just told about. A regression here does not fail, it hangs, and the binary's
    // TIMEOUT is what reports it.
    auto health = AcceptLoopHealth {};
    auto seen = std::size_t { 0 };
    health.subscribe([&health, &seen](StoppedSurface const&) { seen = health.snapshot().size(); });
    health.stopped("admin", "bad handle");
    CHECK(seen == 1);
}
