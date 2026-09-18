// SPDX-License-Identifier: Apache-2.0
#include <core/FNV.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>

using Fnv32 = core::FNV<char, std::uint32_t>;

TEST_CASE("FNV hashes a string as FNV-1a", "[base][fnv]")
{
    CHECK(Fnv32 {}(std::string { "a" }) == 0xE40C'292CU);
    CHECK(Fnv32 {}(std::string { "abcd" }) == 0xCE34'79BDU);
}

TEST_CASE("FNV hashes a trivially copyable value byte-wise", "[base][fnv]")
{
    // Hashing each byte must not pick this overload again for the byte itself: for FNV<char>
    // an `unsigned char` byte matched `V const&` exactly, and the overload called itself forever.
    auto const fnv = Fnv32 {};
    auto const bytes = std::array<char, 4> { 'a', 'b', 'c', 'd' };
    CHECK(fnv(fnv.basis(), bytes) == 0xCE34'79BDU);
    CHECK(fnv(fnv.basis(), bytes) == fnv(std::string { "abcd" }));
}
