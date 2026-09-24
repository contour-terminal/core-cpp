// SPDX-License-Identifier: Apache-2.0
//
// A test binary of a consumer's, on an installed core::testing_main: core-cpp's Catch2 main() with its
// exit-code contract, linked from the package rather than built beside it.

#include <core/testing/ExitCode.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("a consumer's case runs under core-cpp's installed main", "[consumer-install]")
{
    CHECK(core::testing::SkipExitCode == CORE_CPP_SKIP_EXIT_CODE);
}
