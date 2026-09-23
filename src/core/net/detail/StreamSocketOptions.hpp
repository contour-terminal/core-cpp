// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `detail::applyStreamSocketOptions` -- the one place a connected stream socket gets its options,
/// dialled or accepted, on every platform.

#include <core/net/KeepAlive.hpp>
#include <core/net/SocketBuffers.hpp>
#include <core/platform/Types.hpp>

#include <cstddef>

namespace core::net::detail
{

/// What a connected stream socket is configured with beyond what every one of them carries.
///
/// Per socket: a dial takes it from its @c DialOptions, and an accept from the listener's
/// @c ListenOptions (which never asks for keepalive).
struct StreamSocketOptions
{
    KeepAlive keepAlive = KeepAlive::No; ///< Whether the socket probes a silent peer.
    SocketBufferSizes buffers {};        ///< Kernel buffer sizes; unset ones are left alone.
};

/// Applies the options every connected stream socket carries, and those @p options ask for.
///
/// **Every dial and every accept goes through this, on every platform.** Before it existed only
/// the dial set `TCP_NODELAY`, so a server's replies waited on Nagle for the client's delayed ACK
/// while the client's requests did not. What it sets:
///
/// - close-on-exec, where the platform has it (POSIX; a Windows socket is created
///   non-inheritable, and an accepted one inherits that from its listener);
/// - `TCP_NODELAY`, so a small write is not held back behind an unacknowledged one;
/// - `SO_SNDBUF` / `SO_RCVBUF`, only for a size @p options names;
/// - keepalive, only if @p options asks for it -- after and apart from the rest, because it is
///   asked for by one connection rather than carried by all.
///
/// Best-effort by contract: an AF_UNIX socket refuses `TCP_NODELAY`, a kernel may refuse a size,
/// and neither is a reason to fail a connection that is otherwise up.
/// @param socket The connected socket.
/// @param options What this socket asks for beyond the defaults.
void applyStreamSocketOptions(platform::NativeHandle socket, StreamSocketOptions const& options) noexcept;

/// What the kernel reports for the options @c applyStreamSocketOptions sets, for tests and
/// diagnostics: asked of the socket rather than trusted from the call.
struct StreamSocketReport
{
    bool noDelay = false;          ///< `TCP_NODELAY`.
    std::size_t sendBuffer = 0;    ///< `SO_SNDBUF`, as reported (Linux reports twice the value set).
    std::size_t receiveBuffer = 0; ///< `SO_RCVBUF`, likewise.
};

/// @param socket A stream socket.
/// @return What its options read back as; a zero for any option the kernel would not report.
[[nodiscard]] StreamSocketReport reportStreamSocketOptions(platform::NativeHandle socket) noexcept;

} // namespace core::net::detail
