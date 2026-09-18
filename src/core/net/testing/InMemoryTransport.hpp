// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// A connected pair of in-process @c ISockets for deterministic tests — no TCP,
/// no listener, no real network. Backed by a bidirectional socket pair (AF_UNIX
/// socketpair on POSIX, a loopback TCP pair on Windows) so it goes through the
/// same reactor-driven non-blocking read/write path as a real socket, and so it
/// parks/cancels exactly like production I/O.

#include <core/net/EventLoop.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/IoResult.hpp>

#include <expected>
#include <memory>

namespace core::net::testing
{

/// A connected pair of endpoints: bytes written to @c first are readable on
/// @c second and vice versa.
struct SocketPair
{
    std::unique_ptr<ISocket> first;  ///< One endpoint.
    std::unique_ptr<ISocket> second; ///< The peer endpoint.
};

/// Creates a connected in-process @c ISocket pair driven by @p loop.
/// @param loop The loop whose reactor drives readiness (not owned).
/// @return The connected pair, or a @c NetError if the underlying pair could not
///         be created.
[[nodiscard]] std::expected<SocketPair, NetError> makeSocketPair(EventLoop& loop);

} // namespace core::net::testing
