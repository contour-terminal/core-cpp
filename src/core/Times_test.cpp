// SPDX-License-Identifier: Apache-2.0
#include <core/Times.hpp>

#include <catch2/catch_test_macros.hpp>

#include <tuple>

TEST_CASE("times.count-simple")
{
    using namespace core;
    std::string s;
    times(5) | [&]() {
        s += 'A';
    };
    REQUIRE(s == "AAAAA");
}

TEST_CASE("times.count")
{
    using namespace core;
    std::string s;
    times(5) | [&](auto i) {
        s += std::to_string(i);
    };
    REQUIRE(s == "01234");
}

TEST_CASE("times.start_count")
{
    using namespace core;
    std::string s;
    times(5, 2) | [&](auto i) {
        s += std::to_string(i);
    };
    REQUIRE(s == "56");
}

TEST_CASE("times.start_count_step")
{
    using namespace core;
    std::string s;
    times(5, 3, 2) | [&](auto i) {
        s += std::to_string(i);
    };
    REQUIRE(s == "579");
}

TEST_CASE("times.iterator.post_increment_answers_the_prior_position")
{
    auto it = core::times(5).begin();
    auto const prior = it++;
    CHECK(*prior == 0);
    CHECK(*it == 1);
}

TEST_CASE("times.iterator.post_decrement_steps_back_and_answers_the_prior_position")
{
    auto it = core::times(5).begin();
    ++it;
    ++it;
    auto const prior = it--;
    CHECK(*prior == 2);
    CHECK(*it == 1);
}

TEST_CASE("times2D.iterator.post_increment_answers_the_prior_position")
{
    auto const grid = core::times(2) * core::times(3);
    auto it = grid.begin();
    auto const prior = it++;
    CHECK(*prior == std::tuple { 0, 0 });
    CHECK(*it == std::tuple { 0, 1 });
}
