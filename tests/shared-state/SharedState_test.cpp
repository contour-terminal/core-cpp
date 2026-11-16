// SPDX-License-Identifier: Apache-2.0

// What a shared core-cpp promises: one copy of every piece of process and thread state, however many
// images (a DLL, the executable that loads it) compile its headers. Each case sets the state in one
// image and reads it in the other, in both directions where the state is writable.
//
// "The other image" is a shared library built apart from this executable with hidden visibility, so
// anything core-cpp's headers define inline exists once per image, exactly as in a program whose DLL
// and executable both compile them. The cases are only registered when core-cpp itself is shared
// (CORE_CPP_BUILD_SHARED): a static core-cpp is linked into each image and has one copy per image by
// construction.

#include <core/Assert.hpp>
#include <core/async/Strand.hpp>
#include <core/async/testing/ManualExecutor.hpp>
#include <core/net/detail/ReadyBatch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Probe.hpp"

namespace probe = core::shared_state_probe;

namespace
{

struct Observed
{
    core::async::ExecutorScope const* innermost = nullptr;
    core::async::ExecutorScope const* previous = nullptr;
    core::async::IExecutor* current = nullptr;
};

void observeHere(void* context)
{
    auto& observed = *static_cast<Observed*>(context);
    observed.innermost = core::async::ExecutorScope::innermost();
    observed.previous = observed.innermost->previous();
    observed.current = core::async::currentExecutor();
}

} // namespace

TEST_CASE("an ExecutorScope entered in the library is the current one in the executable")
{
    auto executor = core::async::testing::ManualExecutor {};
    auto observed = Observed {};

    probe::enterScope(executor, &observeHere, &observed);

    CHECK(observed.innermost != nullptr);
    CHECK(observed.current == &executor);
}

TEST_CASE("an ExecutorScope entered in the executable is the current one in the library")
{
    auto executor = core::async::testing::ManualExecutor {};

    CHECK(probe::innermostScope() == nullptr);
    {
        auto const scope = core::async::ExecutorScope { executor };
        CHECK(probe::innermostScope() == &scope);
    }
    CHECK(probe::innermostScope() == nullptr);
}

TEST_CASE("scopes nest across the boundary and restore what they replaced")
{
    auto outer = core::async::testing::ManualExecutor {};
    auto inner = core::async::testing::ManualExecutor {};
    auto const outerScope = core::async::ExecutorScope { outer };
    auto observed = Observed {};

    probe::enterScope(inner, &observeHere, &observed);

    CHECK(observed.current == &inner);
    CHECK(observed.previous == &outerScope);
    CHECK(core::async::ExecutorScope::innermost() == &outerScope);
}

TEST_CASE("the error category is one object in both images")
{
    CHECK(probe::errorCategory() == &core::log::errorLog);
    CHECK(probe::categoryNamed("error") == &core::log::errorLog);
}

TEST_CASE("the category registry is one list in both images")
{
    auto const category = core::log::Category { "shared-state-test", "registered here, found there" };

    CHECK(probe::categoryNamed("shared-state-test") == &category);
}

TEST_CASE("the default clocks are one object in both images")
{
    CHECK(probe::steadyClock() == &core::defaultSteadyClock());
    CHECK(probe::wallClock() == &core::platform::defaultSystemWallClock());
}

TEST_CASE("a fail handler installed in the executable is seen by the library")
{
    CHECK_FALSE(probe::failHandlerInstalled());
    core::setFailHandler([](std::string_view, std::string_view, std::string_view, int) {});
    CHECK(probe::failHandlerInstalled());
    core::setFailHandler({});
    CHECK_FALSE(probe::failHandlerInstalled());
}

TEST_CASE("a readiness dispatch begun in the executable is seen by the library")
{
    CHECK_FALSE(probe::readinessDispatchInFlight());
    {
        auto const guard = core::net::detail::ReadinessDispatchGuard {};
        CHECK(probe::readinessDispatchInFlight());
    }
    CHECK_FALSE(probe::readinessDispatchInFlight());
}

TEST_CASE("work abandoned by a strand freed in the executable is seen by the library")
{
    auto owner = 0;

    CHECK_FALSE(probe::freeingAbandonedFor(&owner));
    {
        auto const freeing = core::async::detail::FreeingAbandoned { &owner };
        CHECK(probe::freeingAbandonedFor(&owner));
    }
    CHECK_FALSE(probe::freeingAbandonedFor(&owner));
}
