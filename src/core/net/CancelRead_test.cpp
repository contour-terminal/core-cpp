// SPDX-License-Identifier: Apache-2.0
//
// `ISocket::cancelRead` — retiring a parked read-side operation WITHOUT closing the socket.
//
// Until it existed, a parked read could only be retrieved by `close()`, which made the
// one-read-operation rule unfollowable: a caller holding a parked `waitReadable` must resolve or
// abandon it before it reads, and *abandon* had no spelling short of tearing the connection down
// ([fastcached#710](https://github.com/LASTRADA-Software/fastcached/issues/710)).
//
// Two promises, and they are not the same promise. The SLOT is free when it returns — that is what
// lets a caller arm the next read in the same turn. The WAITER is resolved inline here, because a
// readiness transport consumes nothing, so a retired read can lose nothing. The completion-based
// transport, `IocpSocket`, keeps both promises for what these cases park -- a `waitReadable`
// probe, a zero-byte receive with nothing in it to lose -- and so runs them too; for a REAL read
// it keeps only the first and settles the waiter with whatever its receive did
// (fastcached#884), which `windows/IocpSocket_test.cpp` holds.
#include <core/async/Cancellation.hpp>
#include <core/async/Task.hpp>
#include <core/async/WhenAll.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/NetError.hpp>
#include <core/net/testing/BackendMatrix.hpp>
#include <core/net/testing/InMemoryTransport.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

using core::async::OperationCancelled;
using core::async::Task;
using core::net::EventLoop;
using core::net::ISocket;
using core::net::NetErrorCode;
using core::net::testing::BackendMatrix;

namespace
{

/// SKIPs the backend whose sockets do not implement `cancelRead`: WFMO's `WindowsSocket`, whose
/// parked read is an ordinary coroutine awaiting the loop and so has nothing to retire (its own
/// header says so). Said out loud rather than `continue`d past, because a section that ran nothing
/// must not read as a pass. It goes with WFMO, one release after IOCP became the Windows default
/// ([core-cpp#6](https://github.com/contour-terminal/core-cpp/issues/6)).
/// @param kind The backend the section is about to run over.
void skipWhereCancelReadIsNotImplemented(core::net::BackendKind kind)
{
    if (kind == core::net::BackendKind::Wfmo)
        SKIP(
            "WindowsSocket (the WFMO backend's socket) does not implement cancelRead; see WindowsSocket.hpp");
}

/// How a watch ended.
struct WatchOutcome
{
    bool resolved = false;                ///< Whether it finished at all.
    bool threw = false;                   ///< Whether it unwound through OperationCancelled.
    bool hasValue = false;                ///< Whether it answered with a count.
    NetErrorCode code = NetErrorCode::Ok; ///< The code, where it answered with an error.
    std::size_t count = 0;                ///< The count, where it answered with one.
};

/// Parks a readability watch and records how it ended.
Task<void> watchOnce(ISocket* sock, WatchOutcome* out)
{
    try
    {
        auto const watched = co_await sock->waitReadable();
        out->resolved = true;
        out->hasValue = watched.has_value();
        if (watched.has_value())
            out->count = *watched;
        else
            out->code = watched.error().code;
    }
    catch (OperationCancelled const&)
    {
        out->resolved = true;
        out->threw = true;
    }
}

/// Parks a watch, and when it is retired arms a SECOND one from inside the resumption.
///
/// The shape that makes `cancelRead` non-idempotent: the first retirement resumes this flow before
/// it returns, so the caller's next statement is not the next thing that happens — a second call
/// then retires the watch this resumption armed.
Task<void> watchThenRearm(ISocket* sock, WatchOutcome* first, WatchOutcome* second)
{
    co_await watchOnce(sock, first);
    co_await watchOnce(sock, second);
}

/// Retires the parked read once the watcher has parked, then records that the socket still works.
/// @param loop The loop both ends belong to.
/// @param sock The socket whose read to retire.
/// @param peer The other end, used to prove the socket is still usable afterwards.
/// @param parkedFirst Where to record that something really was parked.
/// @param reusable Where to record that a read after the retirement still works.
Task<void> retireThenReuse(EventLoop* loop, ISocket* sock, ISocket* peer, bool* parkedFirst, bool* reusable)
{
    *parkedFirst = loop->parkedWaiterCount() > 0;
    sock->cancelRead();

    // Not a close: the socket stays open, and a later read works. Asserted by moving a byte through
    // it, because "isClosed() is still false" would also hold for a socket whose descriptor had
    // been quietly broken.
    auto const payload = std::array<std::byte, 1> { std::byte { 0x5A } };
    std::ignore = co_await peer->write(std::span<std::byte const> { payload });
    auto buffer = std::array<std::byte, 4> {};
    auto const got = co_await sock->read(buffer);
    *reusable = got.has_value() && *got == 1;
}

/// Runs a watcher and a retirement concurrently on one loop.
Task<void> watchAndRetire(
    EventLoop* loop, ISocket* sock, ISocket* peer, WatchOutcome* out, bool* parkedFirst, bool* reusable)
{
    co_await core::async::whenAll(watchOnce(sock, out),
                                  retireThenReuse(loop, sock, peer, parkedFirst, reusable));
}

/// Retires twice in a row, which is what takes the watch the first retirement's resumption armed.
Task<void> retireTwice(EventLoop* loop, ISocket* sock, bool* parkedFirst)
{
    *parkedFirst = loop->parkedWaiterCount() > 0;
    sock->cancelRead();
    sock->cancelRead();
    co_return; // a coroutine, so `whenAll` composes it; both retirements are synchronous
}

/// Runs the re-arming watcher against two retirements.
Task<void> watchTwiceAndRetireTwice(
    EventLoop* loop, ISocket* sock, WatchOutcome* first, WatchOutcome* second, bool* parkedFirst)
{
    co_await core::async::whenAll(watchThenRearm(sock, first, second), retireTwice(loop, sock, parkedFirst));
}

} // namespace

TEST_CASE("cancelRead retrieves a parked read and leaves the socket usable", "[net][socket][cancelread]")
{
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            skipWhereCancelReadIsNotImplemented(backend.kind);
            auto loop = EventLoop { *source };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());

            auto outcome = WatchOutcome {};
            auto parkedFirst = false;
            auto reusable = false;
            loop.blockOn(watchAndRetire(
                &loop, pair->first.get(), pair->second.get(), &outcome, &parkedFirst, &reusable));

            CHECK(parkedFirst); // or nothing was retired and the rest of this case is vacuous
            REQUIRE(outcome.resolved);

            // **A VALUE, not a throw.** The cancel came from the RESOURCE, not from the flow's own
            // token: the flow is alive and asked a question about a read that has been taken away
            // from it. A merge that collapsed the two would make a retired read indistinguishable
            // from a cancelled connection.
            CHECK_FALSE(outcome.threw);
            REQUIRE_FALSE(outcome.hasValue);
            CHECK(outcome.code == NetErrorCode::Cancelled);

            CHECK(reusable); // not a close: a later read still works
            CHECK_FALSE(pair->first->isClosed());
        }
    }
}

TEST_CASE("cancelRead with nothing parked disturbs nothing", "[net][socket][cancelread]")
{
    // The property a caller with a RAII watch relies on: its destructor may retire a watch that has
    // already resolved, and that must not reach into whatever came after it.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            skipWhereCancelReadIsNotImplemented(backend.kind);
            auto loop = EventLoop { *source };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());

            pair->first->cancelRead();
            pair->first->cancelRead();
            CHECK_FALSE(pair->first->isClosed());
            CHECK(loop.parkedWaiterCount() == 0);

            // And the socket still moves bytes.
            auto wrote = false;
            auto got = std::size_t { 0 };
            loop.blockOn([](ISocket* from, ISocket* to, bool* w, std::size_t* g) -> Task<void> {
                auto const payload =
                    std::array<std::byte, 3> { std::byte { 1 }, std::byte { 2 }, std::byte { 3 } };
                auto const written = co_await from->write(std::span<std::byte const> { payload });
                *w = written.has_value() && *written == 3;
                auto buffer = std::array<std::byte, 8> {};
                auto const read = co_await to->read(buffer);
                if (read.has_value())
                    *g = *read;
            }(pair->second.get(), pair->first.get(), &wrote, &got));
            CHECK(wrote);
            CHECK(got == 3);
        }
    }
}

TEST_CASE("A second cancelRead takes the watch the first one's resumption armed", "[net][socket][cancelread]")
{
    // **It retires whatever is parked NOW, which is not the same as being idempotent**
    // ([fastcached#1233](https://github.com/LASTRADA-Software/fastcached/issues/1233)). The first
    // retirement resumes its victim inline, and a flow that arms its next read there leaves a NEW
    // watch in the slot — so the second call retires that one, and the caller sees `Cancelled` on
    // an operation nobody meant to cancel.
    //
    // Asserted rather than merely documented, because the declaration used to claim the stronger
    // word and nothing here would have caught it.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            skipWhereCancelReadIsNotImplemented(backend.kind);
            auto loop = EventLoop { *source };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());

            auto first = WatchOutcome {};
            auto second = WatchOutcome {};
            auto parkedFirst = false;
            loop.blockOn(watchTwiceAndRetireTwice(&loop, pair->first.get(), &first, &second, &parkedFirst));

            CHECK(parkedFirst);
            REQUIRE(first.resolved);
            REQUIRE_FALSE(first.hasValue);
            CHECK(first.code == NetErrorCode::Cancelled);

            // The second watch was armed by the first one's resumption and retired by the second
            // call. Both resolved with Cancelled, and the second one is the surprising half.
            REQUIRE(second.resolved);
            REQUIRE_FALSE(second.hasValue);
            CHECK(second.code == NetErrorCode::Cancelled);
        }
    }
}
