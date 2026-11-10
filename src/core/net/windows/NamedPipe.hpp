// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Connecting to a Windows named pipe. Private to this module's Windows sources and its ssh-agent
/// client: the Win32-OpenSSH agent listens on `\\.\pipe\openssh-ssh-agent`, not on a socket.

#include <core/net/EventLoop.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/NetError.hpp>

#include <expected>
#include <memory>
#include <string_view>

namespace core::net::detail
{

/// Opens the named pipe @p name as a client and presents it as an @c ISocket.
///
/// **The socket's reads and writes are synchronous**, on a handle opened without overlapped I/O:
/// a named pipe is not a SOCKET, so the loop's completion port cannot serve it. That suits what it
/// is for, a request/reply exchange with a local agent that answers at once; it is not for a peer
/// that may stall.
/// @param loop The loop the caller runs on (unused: nothing is registered with it).
/// @param name The pipe name, e.g. `\\.\pipe\openssh-ssh-agent`.
/// @return The connected socket; @c NetErrorCode::ConnRefused when no such pipe exists,
///         @c NetErrorCode::PermissionDenied when the OS refuses access, @c NetErrorCode::SystemError
///         for anything else.
[[nodiscard]] std::expected<std::unique_ptr<ISocket>, NetError> connectNamedPipe(EventLoop& loop,
                                                                                 std::wstring_view name);

} // namespace core::net::detail
