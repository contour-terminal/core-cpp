// SPDX-License-Identifier: Apache-2.0
#include <core/Random.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

TEST_CASE("systemRandom: two fills differ")
{
    auto first = std::array<std::byte, 32> {};
    auto second = std::array<std::byte, 32> {};
    core::systemRandom().fill(first);
    core::systemRandom().fill(second);
    CHECK(first != second);
    // 32 random bytes are all zero with probability 2^-256.
    CHECK(std::ranges::any_of(first, [](std::byte b) { return b != std::byte { 0 }; }));
}

TEST_CASE("systemRandom: a zero-length fill is fine")
{
    core::systemRandom().fill(std::span<std::byte> {});
    auto untouched = std::array { std::byte { 0x5A } };
    core::systemRandom().fill(std::span<std::byte> { untouched.data(), 0 });
    CHECK(untouched[0] == std::byte { 0x5A });
}

TEST_CASE("systemRandom: a request beyond one getrandom call fills every byte")
{
    // Past the 256 bytes getrandom() promises in one go, and past a short read's reach.
    auto big = std::vector<std::byte>(1 << 20, std::byte { 0 });
    core::systemRandom().fill(big);
    // The last kilobyte is all zero with probability 2^-8192 unless the tail was never filled.
    CHECK(
        std::ranges::any_of(std::span { big }.last(1024), [](std::byte b) { return b != std::byte { 0 }; }));
}

TEST_CASE("systemRandom: is one source for the process")
{
    CHECK(&core::systemRandom() == &core::systemRandom());
}
