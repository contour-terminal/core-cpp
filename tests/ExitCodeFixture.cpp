// SPDX-License-Identifier: Apache-2.0
//
// Not a test on its own: tests/cmake/check-exit-codes.cmake runs this binary once per tag and asserts
// the exit status core::testing_main maps each outcome to. Run without a tag, it fails on purpose.

#include <catch2/catch_test_macros.hpp>

TEST_CASE("every assertion passes", "[pass]")
{
    auto const value = 1;
    CHECK(value == 1);
}

TEST_CASE("four assertions fail", "[fail4]")
{
    auto const value = 1;
    CHECK(value == 2);
    CHECK(value == 3);
    CHECK(value == 4);
    CHECK(value == 5);
}

TEST_CASE("the only test case skips", "[skipall]")
{
    SKIP("x");
}

TEST_CASE("one section skips and another fails", "[mixed]")
{
    auto const value = 1;
    SECTION("skips")
    {
        SKIP("x");
    }
    SECTION("fails")
    {
        CHECK(value == 2);
    }
}
