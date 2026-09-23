// SPDX-License-Identifier: Apache-2.0

// winsock2.h MUST precede windows.h / ws2tcpip.h (which project headers pull in),
// so this block leads every Win32 net translation unit.
// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

#include <core/async/Cancellation.hpp>
#include <core/async/Task.hpp>
#include <core/async/WhenAll.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/windows/WindowsLoopback.hpp>
#include <core/net/windows/WindowsSocket.hpp>
#include <core/platform/WinsockInit.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>

namespace
{

using core::async::Task;
using core::net::EventLoop;
using core::net::WindowsSocket;

/// Every byte of a destroyed socket's storage is set to this, and must still be when the flow
/// that was parked on it has finished unwinding.
constexpr auto Poison = std::byte { 0xA5 };

/// Storage a @c WindowsSocket is constructed into and destroyed in, so the case can read what
/// happens to it after the destructor has run -- which a heap allocation would hand back to the
/// allocator, where only a sanitizer could see a write.
struct SocketStorage
{
    alignas(WindowsSocket) std::array<std::byte, sizeof(WindowsSocket)> bytes {};

    [[nodiscard]] WindowsSocket* socket() noexcept { return reinterpret_cast<WindowsSocket*>(bytes.data()); }
};

/// Parks reading the socket and records that the read unwound through `OperationCancelled`.
/// @param socket The socket to park on; destroyed under this flow.
/// @param cancelled Set when the read unwound as cancelled.
Task<void> parkThenRecordCancellation(WindowsSocket* socket, bool* cancelled)
{
    auto buffer = std::array<std::byte, 16> {};
    try
    {
        static_cast<void>(co_await socket->read(buffer));
    }
    catch (core::async::OperationCancelled const&)
    {
        *cancelled = true;
    }
}

/// Destroys the socket while the reader is parked on it, then poisons its storage.
/// @param loop The loop the reader is parked on.
/// @param storage Where the socket lives.
/// @param parkedAtDestruction Set to the loop's parked-waiter count just before the destructor.
Task<void> destroyThenPoison(EventLoop* loop, SocketStorage* storage, std::size_t* parkedAtDestruction)
{
    // The reader ran first and parked on WSAEWOULDBLOCK before this child started.
    *parkedAtDestruction = loop->parkedWaiterCount();
    std::destroy_at(storage->socket()); // ~WindowsSocket -> close(FdWakePolicy::Cancel)
    std::ranges::fill(storage->bytes, Poison);
    co_return;
}

/// Runs both on one loop: the reader's cancellation is delivered a turn after the destructor.
Task<void> destroyWhileParked(EventLoop* loop, SocketStorage* storage, bool* cancelled, std::size_t* parked)
{
    co_await core::async::whenAll(parkThenRecordCancellation(storage->socket(), cancelled),
                                  destroyThenPoison(loop, storage, parked));
}

} // namespace

TEST_CASE("A read parked on a destroyed WindowsSocket unwinds without writing to it",
          "[net][windows][wfmo][closehang]")
{
    // The destructor closes with `FdWakePolicy::Cancel`, and the `OperationCancelled` that unwinds
    // the parked read arrives a turn later, when the socket is gone. Nothing on that path may
    // write to the socket. A write there -- 670a7ed cleared `_readWaiter` from a scope guard --
    // is a use-after-free that every gate without a sanitizer passes, because a freed heap block
    // absorbs it; here the block is poisoned storage the case still owns, so the write is a
    // changed byte.
    core::platform::ensureWinsockInitialized();
    auto const backend = core::net::makeBackend(core::net::BackendKind::Wfmo);
    REQUIRE(backend != nullptr);
    auto loop = EventLoop { *backend };

    auto pair = std::array<SOCKET, 2> {};
    REQUIRE(core::net::makeLoopbackPair(pair));
    auto peer = WindowsSocket { loop, pair[1] }; // owns the other end, and says nothing
    auto storage = SocketStorage {};
    std::construct_at(storage.socket(), loop, pair[0]);

    auto cancelled = false;
    auto parkedAtDestruction = std::size_t { 0 };
    loop.blockOn(destroyWhileParked(&loop, &storage, &cancelled, &parkedAtDestruction));

    CHECK(parkedAtDestruction == 1); // the reader was parked, so the path under test ran
    CHECK(cancelled);                // it unwound as cancelled, not resumed into a dead object
    auto const touched = std::ranges::count_if(storage.bytes, [](std::byte b) { return b != Poison; });
    CHECK(touched == 0);
}
