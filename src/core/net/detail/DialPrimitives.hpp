// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// The handful of OS calls an outbound dial needs, behind one platform-free declaration.
///
/// **Two implementations chosen by the source list, not one body with an `#ifdef` in it**
/// (`.agent/rules/platform.md`): `posix/DialPrimitives.cpp` and `windows/DialPrimitives.cpp`. The
/// difference is real rather than cosmetic — on POSIX the thing the loop watches IS the socket,
/// while on Windows readiness for a socket arrives through a `WSAEVENT` associated with it — and
/// @c DialHandles is the shape that lets the dial itself stay written once.

#include <core/net/ISocket.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/KeepAlive.hpp>
#include <core/net/NetError.hpp>
#include <core/net/SocketAddress.hpp>
#include <core/platform/Types.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace core::net
{

class EventLoop;

namespace detail
{

    /// The two handles one outstanding dial holds.
    ///
    /// They are the SAME value on POSIX and different on Windows, and that is the whole reason
    /// this struct exists: a dial that assumed one handle would have to be written twice.
    struct DialHandles
    {
        /// The connecting socket, as the platform spells one.
        platform::NativeHandle socket = platform::InvalidHandle;

        /// What the loop watches for this dial's readiness.
        platform::NativeHandle readiness = platform::InvalidHandle;

        /// What @c readiness is, for the backend that has to tell a socket from an event.
        HandleKind kind = DefaultHandleKind;

        /// @return True once @c openDialSocket has filled this in.
        [[nodiscard]] bool valid() const noexcept { return socket != platform::InvalidHandle; }
    };

    /// Whether a `::connect` answered at once or left the dial outstanding.
    enum class ConnectProgress : std::uint8_t
    {
        Completed, ///< The connection is established; no wait is needed. The loopback case.
        Pending,   ///< The kernel accepted the request; readiness will say how it ended.
    };

    /// Creates the non-blocking, close-on-exec socket a dial to @p endpoint needs, and whatever
    /// readiness object the platform requires alongside it.
    ///
    /// Close-on-exec matters here and not only for tidiness: a dialled socket is created by a
    /// plain `::socket` and so is NOT close-on-exec, unlike an accepted one — and a process that
    /// dials and also spawns children would otherwise hand every child an open peer connection.
    /// @param endpoint The candidate being dialled; its family and protocol choose the socket.
    /// @return The handles, or why none could be made.
    [[nodiscard]] std::expected<DialHandles, NetError> openDialSocket(ResolvedEndpoint const& endpoint);

    /// Closes what @c openDialSocket made, announcing the readiness handle to @p loop first.
    ///
    /// The announcement is unconditional even when nothing is parked, because the loop's contract
    /// is that a handle which MAY be registered is announced before it closes — epoll and kqueue
    /// can report neither a closed descriptor nor its former waiters.
    /// @param loop The loop the readiness handle may be registered with; may be null.
    /// @param handles What to close; left invalid.
    void closeDialSocket(EventLoop* loop, DialHandles& handles) noexcept;

    /// Issues the non-blocking `::connect`.
    /// @param handles What @c openDialSocket produced.
    /// @param endpoint The address to dial.
    /// @return Whether it completed or is outstanding, or why it failed outright.
    [[nodiscard]] std::expected<ConnectProgress, NetError> beginConnect(DialHandles const& handles,
                                                                        ResolvedEndpoint const& endpoint);

    /// Reads the socket's pending error — `getsockopt(SO_ERROR)` — which is the ONLY thing that
    /// says whether a dial resolved into a connection or into a refusal.
    ///
    /// **Ruling R101.** Readiness is not success: a refused connect also makes the socket ready,
    /// and which callback the kernel picks to say so is not portable. On Linux a failure can
    /// arrive with neither direction set; on macOS the write filter fires with `EV_EOF` and the
    /// backend reports `Writable`. A dial that believed the callback would hand its caller a
    /// socket whose first write fails.
    /// @param handles The dialling socket.
    /// @return Nothing when the connection is up, or the classified failure.
    [[nodiscard]] std::expected<void, NetError> pendingSocketError(DialHandles const& handles);

    /// Applies the options a connected socket is expected to carry, and — only if asked —
    /// keepalive.
    ///
    /// Keepalive is applied AFTER and separately from the rest, which is the point: the others
    /// are what every socket this library hands out carries, and this one is asked for by ONE
    /// dial. Best-effort by contract, so a socket that would not take it is still handed over
    /// rather than failing a connection over a tuning option.
    /// @param handles The connected socket.
    /// @param keepAlive Whether to arm probes.
    void applyDialledSocketOptions(DialHandles const& handles, KeepAlive keepAlive) noexcept;

    /// Wraps the connected handle as the platform's @c ISocket, transferring ownership.
    /// @param loop The loop the socket is pinned to.
    /// @param handles The connected handles; left invalid, because the socket owns them now.
    /// @param peer The printable peer address to report from @c ISocket::peerAddress.
    /// @return The socket.
    [[nodiscard]] std::unique_ptr<ISocket> adoptDialled(EventLoop& loop,
                                                        DialHandles& handles,
                                                        std::string peer);

} // namespace detail

} // namespace core::net
