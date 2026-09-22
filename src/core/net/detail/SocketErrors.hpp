// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `detail::classifySocketError` — a platform socket error, as this library's vocabulary.
///
/// One declaration, one table per platform (`posix/SocketErrors.cpp`, `windows/SocketErrors.cpp`),
/// chosen by the source list. **Not yet the only table in the module, and that is a known debt
/// rather than a design**: `PosixSocket`, `WindowsSocket` and the dial primitives each still carry a
/// private switch, and a private switch is the shape that lacked a row and turned a firewall's
/// `EACCES` into an unclassified `SystemError` upstream. The datagram and blocking transports use
/// this one; moving the other three onto it is a change to files other tasks own. Private to
/// `core::net`.
///
/// Origin: fastcached `Detail::TranslateSocketError` in `src/FastCache/Net/BlockingSocket.cpp`
/// (`0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`), plus `MessageTooLarge`.

#include <core/net/NetError.hpp>

#include <string>

namespace core::net::detail
{

/// Maps a platform socket error onto @c NetErrorCode.
///
/// A code with no row is @c NetErrorCode::SystemError, and the caller still has the number in
/// @c NetError::systemCode. **`EPIPE` has no row on purpose**: it is not a reset (see
/// `SocketClosedStates_test.cpp`, which pins the in-memory fake to this answer against a real pair).
/// @param systemCode An `errno` value on POSIX, a `WSAGetLastError()` value on Windows.
/// @return The category it belongs to.
[[nodiscard]] NetErrorCode classifySocketError(int systemCode) noexcept;

/// @param systemCode As for @c classifySocketError.
/// @param context What was being attempted.
/// @return The classified error, carrying @p systemCode.
[[nodiscard]] inline NetError socketError(int systemCode, std::string context)
{
    return makeNetError(classifySocketError(systemCode), systemCode, std::move(context));
}

/// @return The error the last socket call on this thread reported: `errno`, or `WSAGetLastError()`.
[[nodiscard]] int lastSocketError() noexcept;

} // namespace core::net::detail
