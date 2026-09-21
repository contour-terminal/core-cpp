// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Cross-platform factory functions for the async socket layer. Consumers use
/// these instead of including the per-platform implementation headers directly;
/// each resolves to the right backend (PosixSocket/Listener or
/// WindowsSocket/Listener) at compile time.

#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/IAsyncAddressResolver.hpp>
#include <core/net/IConnector.hpp>
#include <core/net/IListener.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/IoResult.hpp>
#include <core/platform/Types.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <vector>

namespace core::net
{

/// What one `listen` asks for.
///
/// **A named descriptor rather than positional parameters**, for the reason @c DialOptions gives:
/// the bind side is already three values, two of them integers, and a caller that transposes
/// `port` and `backlog` gets a listener on a port it did not choose with no diagnostic at all.
/// It is also the shape the design spec's rename map asks for, in place of each platform
/// listener's own `Bind`.
struct ListenOptions
{
    /// The bind address: "127.0.0.1", "0.0.0.0", "::". Empty means the wildcard address.
    std::string_view host = {};

    /// The bind port; 0 requests an OS-assigned ephemeral one, which
    /// @c IListener::boundPort then reports.
    std::uint16_t port = 0;

    /// The `::listen` backlog.
    int backlog = 128;
};

/// Binds a TCP listener as @p options asks, driven by @p loop's backend.
/// @param loop The loop whose backend drives accept readiness (not owned).
/// @param options The bind address, port and backlog.
/// @return The bound listener, or a @c NetError on failure.
[[nodiscard]] std::expected<std::unique_ptr<IListener>, NetError> listen(EventLoop& loop,
                                                                         ListenOptions options);

/// Binds a TCP listener on @p host : @p port.
///
/// The positional spelling, kept because it is what contour's callers write. It forwards to the
/// @c ListenOptions overload, which is the one a new caller should use.
/// @param loop The loop whose backend drives accept readiness (not owned).
/// @param host The bind address ("127.0.0.1", "0.0.0.0", "::").
/// @param port The bind port; 0 requests an OS-assigned ephemeral port.
/// @param backlog The listen backlog.
/// @return The bound listener, or a @c NetError on failure.
[[nodiscard]] std::expected<std::unique_ptr<IListener>, NetError> listen(EventLoop& loop,
                                                                         std::string_view host,
                                                                         std::uint16_t port,
                                                                         int backlog = 128);

/// Adopts an already-bound, already-listening handle as an @c IListener driven by @p loop.
///
/// For a socket this process did not create: one inherited from a supervisor (systemd socket
/// activation passes descriptor 3), or one a test bound for itself. Ownership of @p handle
/// transfers to the returned listener, which closes it.
///
/// **It does not bind and does not listen.** A handle that is merely open, or open and bound but
/// not listening, is adopted successfully and then accepts nothing — the kernel is the only thing
/// that knows, and neither platform offers a portable way to ask.
/// @param loop The loop whose backend drives accept readiness (not owned).
/// @param handle The listening socket handle (a descriptor on POSIX, a `SOCKET` on Windows).
/// @return The adopted listener, or a @c NetError if the handle could not be prepared.
[[nodiscard]] std::expected<std::unique_ptr<IListener>, NetError> adoptListener(
    EventLoop& loop, platform::NativeHandle handle);

/// Connects a TCP client socket to @p host : @p port, parking the caller until the connection
/// completes.
///
/// **Name resolution does not run on @p loop's thread.** It goes to @c defaultAsyncResolver,
/// whose pool starts on first use and is never touched by a dial to a literal address. This is
/// the behaviour change contour's callers inherit: the call looks the same and no longer stalls
/// every other coroutine on the loop for the length of a DNS lookup.
/// @param loop The loop whose backend drives connect readiness (not owned; a pointer, since
///        coroutine reference parameters can dangle).
/// @param host The remote host ("127.0.0.1", a hostname), unbracketed.
/// @param port The remote port.
/// @return A task resolving to the connected socket, or a @c NetError on failure.
[[nodiscard]] async::Task<SocketResult> connect(EventLoop* loop, std::string_view host, std::uint16_t port);

/// Connects through an injected resolver, with a per-call budget and keepalive.
///
/// The form to use where the process resolver is not what you want: a test that must observe
/// which thread resolved, a consumer with its own cache, a caller that needs the dial bounded.
/// @param loop The loop whose backend drives connect readiness (not owned).
/// @param host The remote host, unbracketed.
/// @param port The remote port.
/// @param resolver The name-resolution seam (not owned; a pointer, since coroutine reference
///        parameters can dangle — the same reason @p loop is one). Must outlive the returned task.
/// @param options The budget, and whether the connection carries keepalive.
/// @return A task resolving to the connected socket, or a @c NetError on failure.
[[nodiscard]] async::Task<SocketResult> connect(EventLoop* loop,
                                                std::string_view host,
                                                std::uint16_t port,
                                                IAsyncAddressResolver* resolver,
                                                DialOptions options);

/// Binds an AF_UNIX listener on the socket file @p path, hardening its parent
/// directory first (see UnixListener::bind for the exact policy).
/// @param loop The loop whose reactor drives accept readiness (not owned).
/// @param path The socket file path.
/// @param backlog The listen backlog.
/// @return The bound listener; @c NetErrorCode::Unsupported on Windows (for now).
[[nodiscard]] std::expected<std::unique_ptr<IListener>, NetError> listenUnix(EventLoop& loop,
                                                                             std::string_view path,
                                                                             int backlog = 128);

/// Connects to the AF_UNIX socket file @p path.
/// @param loop The loop whose reactor drives connect readiness (not owned; a
///        pointer, since coroutine reference parameters can dangle).
/// @param path The socket file path.
/// @return A task resolving to the connected socket; a @c NetError on failure,
///         @c NetErrorCode::Unsupported on Windows (for now).
[[nodiscard]] async::Task<std::expected<std::unique_ptr<ISocket>, NetError>> connectUnix(
    EventLoop* loop, std::string_view path);

/// Adopts an already-open stream file descriptor (a socketpair end, a PTY
/// master) as an @c ISocket driven by @p loop's reactor. Ownership of the fd
/// transfers to the returned socket.
/// @param loop The loop whose reactor drives readiness (not owned).
/// @param fd The open, stream-capable descriptor.
/// @return The adopted socket; @c NetErrorCode::Unsupported on Windows.
[[nodiscard]] std::expected<std::unique_ptr<ISocket>, NetError> adoptFd(EventLoop& loop, int fd);

/// Appends one read chunk from @p socket to @p buffer — the accumulate step of
/// every binary-framed decode loop.
///
/// Reports EOF and failure DISTINCTLY: a decode loop that cannot tell "the peer hung up" from
/// "the transport broke" cannot say why it dropped a connection, which is the whole content of
/// the resulting diagnostic.
///
/// @param socket The transport to read from (not owned; a pointer, since
///        coroutine reference parameters can dangle).
/// @param buffer Receives the read bytes.
/// @return The number of bytes appended, 0 on a clean EOF, or the transport error.
[[nodiscard]] async::Task<IoResult> appendReadChunk(ISocket* socket, std::vector<std::byte>* buffer);

} // namespace core::net
