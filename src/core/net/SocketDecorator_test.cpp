// SPDX-License-Identifier: Apache-2.0
//
// The ordering rule every transport is held to: **retiring a parked operation completes it LAST.**
//
// Detach the operation from the socket first, complete it last, and touch no member afterwards.
// That is not tidiness. Completing RESUMES the parked coroutine, and a coroutine that OWNS the
// socket — a connection handler holding it in a by-value `unique_ptr` parameter — runs to its end
// and destroys it before the completion returns. A transport that read one of its own members
// afterwards would write through a freed object, which only a sanitizer build reports.
//
// The cases below are the shape that makes the rule bite: the coroutine the completion resumes is
// the socket's only owner, and it drops it from inside the resumption. Each watches the
// destruction through a `weak_ptr`, so the assertion is a deterministic value on every platform
// rather than an ASan report once in N runs.
//
// It is named for decorators because they are where the rule is easiest to lose: a decorator
// forwards the verb and may hold state of its own around it. `SplitSocket` is exercised here for
// exactly that reason.
#include <core/async/DetachedTask.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/SplitSocket.hpp>
#include <core/net/testing/BackendMatrix.hpp>
#include <core/net/testing/InMemoryTransport.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

using core::async::DetachedTask;
using core::net::EventLoop;
using core::net::ISocket;
using core::net::testing::BackendMatrix;

namespace
{

/// Awaits a readability watch on a socket this flow OWNS, and drops it the moment the watch
/// resumes — what a connection handler does when its watch says the peer is gone.
/// @param owner The socket's only owning reference, moved in; reset from inside the resumption.
DetachedTask watchThenDrop(std::shared_ptr<ISocket> owner)
{
    std::ignore = co_await owner->waitReadable();
    owner.reset();
}

/// Writes to a socket this flow OWNS, and drops it the moment the write resumes.
/// @param owner The socket's only owning reference, moved in.
/// @param payload The bytes; must outlive the flow.
DetachedTask writeThenDrop(std::shared_ptr<ISocket> owner, std::vector<std::byte> const* payload)
{
    std::ignore = co_await owner->write(std::span<std::byte const> { *payload });
    owner.reset();
}

/// Big enough that no platform's send buffer takes it, so the write parks.
constexpr std::size_t UnsendablePayload = std::size_t { 8 } * 1024 * 1024;

} // namespace

TEST_CASE("A retired operation may destroy the socket it belonged to", "[net][socket][decorator]")
{
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            SECTION("a readability watch retired by close()")
            {
                auto loop = EventLoop { *source };
                auto pair = core::net::testing::makeSocketPair(loop);
                REQUIRE(pair.has_value());
                auto keepPeerAlive = std::move(pair->second);

                auto owner = std::shared_ptr<ISocket> { std::move(pair->first) };
                auto* const socket = owner.get();
                auto const watched = std::weak_ptr<ISocket> { owner };
                watchThenDrop(std::move(owner));
                // No turn is needed and none may be taken: a `DetachedTask` starts eagerly,
                // so the watch has already reached its park -- and driving a turn here would
                // BLOCK in the backend's wait with nothing left to wake it, turning this
                // case's red into a hang.

                REQUIRE(loop.parkedWaiterCount() > 0);
                REQUIRE_FALSE(watched.expired());

                socket->close();
                CHECK(watched.expired()); // the resumption destroyed it, from inside close()
            }

            SECTION("a readability watch retired by cancelRead()")
            {
                auto loop = EventLoop { *source };
                auto pair = core::net::testing::makeSocketPair(loop);
                REQUIRE(pair.has_value());
                auto keepPeerAlive = std::move(pair->second);

                auto owner = std::shared_ptr<ISocket> { std::move(pair->first) };
                auto* const socket = owner.get();
                auto const watched = std::weak_ptr<ISocket> { owner };
                watchThenDrop(std::move(owner));

                REQUIRE(loop.parkedWaiterCount() > 0);
                REQUIRE_FALSE(watched.expired());

                socket->cancelRead();
                CHECK(watched.expired());
            }

            SECTION("a parked write retired by close()")
            {
                auto loop = EventLoop { *source };
                auto pair = core::net::testing::makeSocketPair(loop);
                REQUIRE(pair.has_value());
                auto keepPeerAlive = std::move(pair->second);

                auto const payload = std::vector<std::byte>(UnsendablePayload, std::byte { 0xA5 });
                auto owner = std::shared_ptr<ISocket> { std::move(pair->first) };
                auto* const socket = owner.get();
                auto const watched = std::weak_ptr<ISocket> { owner };
                writeThenDrop(std::move(owner), &payload);

                REQUIRE(loop.parkedWaiterCount() > 0);
                REQUIRE_FALSE(watched.expired());

                socket->close();
                CHECK(watched.expired());
            }
        }
    }
}

TEST_CASE("A decorator forwards the rule along with the verb", "[net][socket][decorator]")
{
    // A `SplitSocket` adds no operation of its own: it hands the inner half's awaitable straight
    // out by value, so the slot being claimed is the half's and the retirement is the half's. What
    // could go wrong is a decorator that wrapped the operation in something of its own and then
    // touched that after completing — which is the same rule one layer up.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto readPair = core::net::testing::makeSocketPair(loop);
            auto writePair = core::net::testing::makeSocketPair(loop);
            REQUIRE(readPair.has_value());
            REQUIRE(writePair.has_value());
            auto keepReadPeer = std::move(readPair->second);
            auto keepWritePeer = std::move(writePair->second);

            auto owner = std::shared_ptr<ISocket> { core::net::combineHalves(std::move(readPair->first),
                                                                             std::move(writePair->first)) };
            auto* const socket = owner.get();
            auto const watched = std::weak_ptr<ISocket> { owner };
            watchThenDrop(std::move(owner));

            REQUIRE(loop.parkedWaiterCount() > 0);
            REQUIRE_FALSE(watched.expired());

            socket->cancelRead(); // forwarded to the read half, which retires and resumes
            CHECK(watched.expired());
        }
    }
}
