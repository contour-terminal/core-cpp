// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// The operating system's random source, behind an interface so that a test can replace it.

#include <cstddef>
#include <span>

namespace core
{

/// A source of random bytes.
///
/// Whatever draws nonces, salts or keys takes an @c IRandom rather than calling the system, so a test
/// can hand it a deterministic one. The production implementation is @c systemRandom().
class IRandom
{
  public:
    IRandom() = default;
    IRandom(IRandom const&) = delete;
    IRandom& operator=(IRandom const&) = delete;
    IRandom(IRandom&&) = delete;
    IRandom& operator=(IRandom&&) = delete;
    virtual ~IRandom() = default;

    /// Overwrites @p out entirely with random bytes; an empty span is fine and writes nothing.
    /// @param out The bytes to fill.
    virtual void fill(std::span<std::byte> out) = 0;
};

/// The operating system's cryptographically secure random source: `getrandom()` on Linux,
/// `arc4random_buf()` on macOS and the BSDs, `BCryptGenRandom()` on Windows.
///
/// A system that cannot supply random bytes has no safe way to go on, so a failure ends the process
/// (see @c Require) rather than hand back bytes that are not random. Safe to call from any thread.
/// @return The process-wide source, valid for the life of the process.
[[nodiscard]] IRandom& systemRandom();

} // namespace core
