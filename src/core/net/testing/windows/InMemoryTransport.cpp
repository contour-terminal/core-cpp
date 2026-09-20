// SPDX-License-Identifier: Apache-2.0

// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
// clang-format on

#include <core/net/testing/InMemoryTransport.hpp>

#include <core/net/windows/WindowsLoopback.hpp>
#include <core/net/windows/WindowsSocket.hpp>
#include <core/platform/WinsockInit.hpp>

#include <array>

namespace core::net::testing
{

std::expected<SocketPair, NetError> makeSocketPair(EventLoop& loop)
{
    platform::ensureWinsockInitialized();
    auto pair = std::array<SOCKET, 2> {};
    if (!makeLoopbackPair(pair)) // the shared production helper (net/windows/WindowsLoopback)
        return std::unexpected(makeNetError(NetErrorCode::SystemError, WSAGetLastError(), "loopback pair"));

    return SocketPair {
        .first = std::unique_ptr<ISocket>(new WindowsSocket(loop, pair[0])),
        .second = std::unique_ptr<ISocket>(new WindowsSocket(loop, pair[1])),
    };
}

} // namespace core::net::testing
