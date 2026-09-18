// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Config.hpp>

#include <catch2/catch_totals.hpp>

namespace core::testing
{

/// The exit status of a test binary whose every test case was skipped. Every core-cpp test is
/// registered with it as ctest's SKIP_RETURN_CODE, so such a run reads as skipped, not passed.
inline constexpr int SkipExitCode = CORE_CPP_SKIP_EXIT_CODE;

/// @brief Maps the outcome of a Catch2 run to core-cpp's exit-code contract.
///
/// | Outcome                                   | Exit status  |
/// |-------------------------------------------|--------------|
/// | any assertion or test case failed         | 1            |
/// | every test case that ran was skipped      | SkipExitCode |
/// | no test case ran and Catch2 said so       | 2            |
/// | otherwise                                 | 0            |
///
/// Catch2 3.8's own main returns 42 when anything failed and 4 when every test case was
/// skipped, and ctest reads both as a failure: a binary whose tests all skip is reported as
/// broken rather than skipped (LASTRADA-Software/fastcached#1128 and #1152).
///
/// @param totals        What the run counted, as the test-run-ended event reports it.
/// @param rawExitCode   What Catch::Session::run() returned.
[[nodiscard]] int normalisedExitCode(Catch::Totals const& totals, int rawExitCode) noexcept;

} // namespace core::testing
