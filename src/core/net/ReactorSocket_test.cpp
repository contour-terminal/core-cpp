// SPDX-License-Identifier: Apache-2.0
//
// `PosixSocket` over every readiness backend this platform builds — fastcached's `EpollSocket_test`
// and `KqueueSocket_test` merged into one suite.
//
// **Two suites becoming one is the point of the task.** Upstream shipped two socket
// implementations that were byte-identical but for the reactor type they named, and therefore two
// test files that were near-identical but for the backend they hardcoded. What actually varies per
// platform is the multiplexer, which is `IoBackend`'s job — so there is one socket, and its cases
// are asked of every backend through `testing::BackendMatrix`. A case that only makes sense for one
// backend would be a finding about the contract, not a reason for a second suite; there are none
// here.
//
// What this file covers that `Socket_test` does not: the PARKED arms. Every case below drives a
// payload or a lifetime event that cannot be satisfied by the synchronous fast path, so the
// readiness callback and the retry loop are what answer it.
#include <core/async/Cancellation.hpp>
#include <core/async/Task.hpp>
#include <core/async/WhenAll.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/NetError.hpp>
#include <core/net/posix/PosixSocket.hpp>
#include <core/net/testing/BackendMatrix.hpp>
#include <core/net/testing/CoroTestSupport.hpp>
#include <core/net/testing/InMemoryTransport.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

using core::async::OperationCancelled;
using core::async::Task;
using core::net::EventLoop;
using core::net::ISocket;
using core::net::NetErrorCode;
using core::net::testing::BackendMatrix;

namespace
{

/// Big enough that no platform's send buffer takes it in one syscall, so `write` MUST park on
/// writability and resume from the readiness callback. One mebibyte rather than something larger:
/// the property is "more than one syscall", and past that every extra byte buys only runtime.
constexpr std::size_t LargePayload = std::size_t { 1024 } * 1024;

/// How an operation ended.
struct Outcome
{
    bool resolved = false;                ///< Whether it finished at all.
    bool threw = false;                   ///< Whether it unwound through OperationCancelled.
    bool hasValue = false;                ///< Whether it answered with a value.
    std::size_t count = 0;                ///< The byte count, where it answered with one.
    NetErrorCode code = NetErrorCode::Ok; ///< The code, where it answered with an error.
};

/// @param size How many bytes.
/// @return A payload whose every byte is a function of its index, so a reader can tell a correct
///         stream from a plausible one — a buffer of zeroes would pass a length check after a
///         cursor bug had reordered it.
[[nodiscard]] std::vector<std::byte> makePayload(std::size_t size)
{
    auto payload = std::vector<std::byte> {};
    payload.reserve(size);
    for (auto const index: std::views::iota(std::size_t { 0 }, size))
        payload.push_back(static_cast<std::byte>(((index * 31) + 7) & 0xFF));
    return payload;
}

/// Writes every byte of @p payload, recording how the operation ended.
Task<void> writeAll(ISocket* sock, std::vector<std::byte> const* payload, Outcome* out)
{
    try
    {
        auto const written = co_await sock->write(std::span<std::byte const> { *payload });
        out->resolved = true;
        out->hasValue = written.has_value();
        if (written.has_value())
            out->count = *written;
        else
            out->code = written.error().code;
    }
    catch (OperationCancelled const&)
    {
        out->resolved = true;
        out->threw = true;
    }
}

/// Writes @p segments as one gathered write.
Task<void> writeGathered(ISocket* sock, std::vector<std::span<std::byte const>> const* segments, Outcome* out)
{
    auto const written =
        co_await sock->writeVectored(std::span<std::span<std::byte const> const> { *segments });
    out->resolved = true;
    out->hasValue = written.has_value();
    if (written.has_value())
        out->count = *written;
    else
        out->code = written.error().code;
}

/// Drains exactly @p expected.size() bytes and records whether they matched byte for byte.
Task<void> drainAndCompare(ISocket* sock, std::vector<std::byte> const* expected, bool* matched)
{
    auto received = std::vector<std::byte> {};
    received.reserve(expected->size());
    auto chunk = std::array<std::byte, std::size_t { 64 } * 1024> {};
    while (received.size() < expected->size())
    {
        auto const got = co_await sock->read(chunk);
        if (!got.has_value() || *got == 0)
            break;
        received.insert(received.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(*got));
    }
    *matched = received == *expected;
}

/// Parks a read and records how it ended.
Task<void> readOnce(ISocket* sock, Outcome* out)
{
    try
    {
        auto buffer = std::array<std::byte, 64> {};
        auto const got = co_await sock->read(buffer);
        out->resolved = true;
        out->hasValue = got.has_value();
        if (got.has_value())
            out->count = *got;
        else
            out->code = got.error().code;
    }
    catch (OperationCancelled const&)
    {
        out->resolved = true;
        out->threw = true;
    }
}

/// Closes @p sock once the reader has parked on it.
Task<void> closeOnceParked(EventLoop* loop, ISocket* sock, bool* parkedFirst)
{
    *parkedFirst = loop->parkedWaiterCount() > 0;
    sock->close();
    co_return;
}

/// Destroys @p owner once the reader has parked on it — the destructor path, which must ABANDON
/// rather than resolve: by the time the flow runs there is no socket left for it to look at.
Task<void> destroyOnceParked(EventLoop* loop, std::unique_ptr<ISocket>* owner, bool* parkedFirst)
{
    *parkedFirst = loop->parkedWaiterCount() > 0;
    owner->reset();
    co_return;
}

/// Reads with the loop's timer count sampled while the read is parked, which is what says the
/// receive deadline is a timer on the loop's ONE deadline heap rather than a wake of the socket's
/// own.
Task<void> readUnderDeadline(EventLoop* loop, ISocket* sock, Outcome* out, std::size_t* timersWhileParked)
{
    co_await core::async::whenAll(readOnce(sock, out), [](EventLoop* l, std::size_t* t) -> Task<void> {
        *t = l->pendingTimerCount();
        co_return;
    }(loop, timersWhileParked));
}

} // namespace

TEST_CASE("write completes a payload larger than the send buffer", "[net][socket][reactor]")
{
    // The parked-write path, which is the whole reason the retry loop lives at the socket: one
    // mebibyte does not fit any platform's send buffer, so this takes as many writable edges as the
    // kernel gives it. A `write` that resolved on the first partial send would report a short count
    // here and silently truncate every large reply this library ever sends.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());

            auto const payload = makePayload(LargePayload);
            auto wrote = Outcome {};
            auto matched = false;
            loop.blockOn([](ISocket* from, ISocket* to, std::vector<std::byte> const* p, Outcome* w, bool* m)
                             -> Task<void> {
                co_await core::async::whenAll(writeAll(from, p, w), drainAndCompare(to, p, m));
            }(pair->first.get(), pair->second.get(), &payload, &wrote, &matched));

            REQUIRE(wrote.resolved);
            REQUIRE(wrote.hasValue);
            CHECK(wrote.count == LargePayload); // every byte, not just the first send's worth
            CHECK(matched);                     // and in the right order
        }
    }
}

TEST_CASE("writeVectored round-trips a small gathered reply", "[net][socket][reactor]")
{
    // The canonical shape: a reply assembled as [header][body][trailer] where the body points
    // straight into a payload nobody wants to copy.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());

            auto const header = makePayload(5);
            auto const body = makePayload(11);
            auto const trailer = makePayload(3);
            auto const segments = std::vector<std::span<std::byte const>> {
                std::span<std::byte const> { header },
                std::span<std::byte const> { body },
                std::span<std::byte const> { trailer },
            };
            auto expected = std::vector<std::byte> {};
            expected.insert(expected.end(), header.begin(), header.end());
            expected.insert(expected.end(), body.begin(), body.end());
            expected.insert(expected.end(), trailer.begin(), trailer.end());

            auto wrote = Outcome {};
            auto matched = false;
            loop.blockOn([](ISocket* from,
                            ISocket* to,
                            std::vector<std::span<std::byte const>> const* s,
                            std::vector<std::byte> const* e,
                            Outcome* w,
                            bool* m) -> Task<void> {
                co_await core::async::whenAll(writeGathered(from, s, w), drainAndCompare(to, e, m));
            }(pair->first.get(), pair->second.get(), &segments, &expected, &wrote, &matched));

            REQUIRE(wrote.resolved);
            REQUIRE(wrote.hasValue);
            CHECK(wrote.count == expected.size());
            CHECK(matched); // the ORDER, which a count alone would not discriminate
        }
    }
}

TEST_CASE("writeVectored streams a large value across partial writes", "[net][socket][reactor]")
{
    // The vectored cursor's real exercise: one mebibyte in the middle segment, so the send is torn
    // across many `sendmsg` calls and the cursor has to resume mid-segment. A cursor that advanced
    // by whole segments would send the same bytes twice and the comparison below would fail.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());

            auto const header = makePayload(9);
            auto const body = makePayload(LargePayload);
            auto const segments = std::vector<std::span<std::byte const>> {
                std::span<std::byte const> { header },
                std::span<std::byte const> { body },
            };
            auto expected = std::vector<std::byte> {};
            expected.insert(expected.end(), header.begin(), header.end());
            expected.insert(expected.end(), body.begin(), body.end());

            auto wrote = Outcome {};
            auto matched = false;
            loop.blockOn([](ISocket* from,
                            ISocket* to,
                            std::vector<std::span<std::byte const>> const* s,
                            std::vector<std::byte> const* e,
                            Outcome* w,
                            bool* m) -> Task<void> {
                co_await core::async::whenAll(writeGathered(from, s, w), drainAndCompare(to, e, m));
            }(pair->first.get(), pair->second.get(), &segments, &expected, &wrote, &matched));

            REQUIRE(wrote.resolved);
            REQUIRE(wrote.hasValue);
            CHECK(wrote.count == expected.size());
            CHECK(matched);
        }
    }
}

TEST_CASE("close() resolves a parked read with a Cancelled VALUE", "[net][socket][reactor][cancel]")
{
    // Spec §2 item 5, second rule, at the socket. The socket is still there for the resumed flow to
    // look at — `close()` is idempotent and `isClosed()` answers — so the flow is handed a value and
    // carries on. A throw here would make a closed socket indistinguishable from a cancelled flow.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());

            auto outcome = Outcome {};
            auto parkedFirst = false;
            loop.blockOn([](EventLoop* l, ISocket* s, Outcome* o, bool* p) -> Task<void> {
                co_await core::async::whenAll(readOnce(s, o), closeOnceParked(l, s, p));
            }(&loop, pair->first.get(), &outcome, &parkedFirst));

            CHECK(parkedFirst);
            REQUIRE(outcome.resolved);
            CHECK_FALSE(outcome.threw);
            REQUIRE_FALSE(outcome.hasValue);
            CHECK(outcome.code == NetErrorCode::Cancelled);
        }
    }
}

TEST_CASE("The DESTRUCTOR unwinds a parked read rather than resolving it", "[net][socket][reactor][cancel]")
{
    // The other half of the same rule, and the one that decides whether a resumed flow reads freed
    // storage. `close()` can hand the flow a value because the socket is still there; a DESTRUCTOR
    // cannot, so the operation is abandoned and `await_resume` throws whatever the flow's own token
    // says. Unwinding never re-enters the body, which is the only safe thing to do with a frame
    // whose socket has gone.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());
            auto owner = std::move(pair->first);

            auto outcome = Outcome {};
            auto parkedFirst = false;
            loop.blockOn([](EventLoop* l, std::unique_ptr<ISocket>* o, Outcome* out, bool* p) -> Task<void> {
                co_await core::async::whenAll(readOnce(o->get(), out), destroyOnceParked(l, o, p));
            }(&loop, &owner, &outcome, &parkedFirst));

            CHECK(parkedFirst);
            REQUIRE(outcome.resolved);
            CHECK(outcome.threw); // unwound, not resolved
            CHECK(owner == nullptr);
        }
    }
}

TEST_CASE("A stop on the flow's own token unwinds a parked read", "[net][socket][reactor][cancel]")
{
    // Spec §2 item 5, FIRST rule, and the routing that makes it work: the stop callback cannot
    // touch the socket (it may run on any thread), so it names the loop park instead, and the loop
    // resolves it on its own thread and tells the socket. This case is what proves that route
    // exists — without it the read stays parked and the case HANGS rather than failing.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());

            auto outcome = Outcome {};
            loop.blockOn([](EventLoop* l, ISocket* s, Outcome* o) -> Task<void> {
                // The sleep wins; `whenAny` then stops the reader's token, which is a cancel from
                // the FLOW and must therefore throw rather than resolve.
                co_await core::net::testing::anyOf(
                    readOnce(s, o), core::net::testing::sleepFor(l, std::chrono::milliseconds { 20 }));
            }(&loop, pair->first.get(), &outcome));

            REQUIRE(outcome.resolved);
            CHECK(outcome.threw);
            CHECK_FALSE(outcome.hasValue);
        }
    }
}

TEST_CASE("A receive deadline bounds one read, on the loop's own timer heap",
          "[net][socket][reactor][deadline]")
{
    // `setReceiveDeadline` is a CONSUMER of the loop's timers, not an inventor of a second
    // deadline mechanism. Both halves are asserted: the read is bounded (it reports Timeout on an
    // idle socket rather than parking forever), and while it is parked the loop has exactly one
    // pending timer — which is what says the deadline went onto the one heap `computeTimeout` and
    // `armHostWake` already read. A socket that computed a wake of its own would show zero here,
    // and its symptom would be a wait that is too long: a hang, not a failure.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());
            pair->first->setReceiveDeadline(std::chrono::milliseconds { 20 });

            auto outcome = Outcome {};
            auto timersWhileParked = std::size_t { 0 };
            loop.blockOn(readUnderDeadline(&loop, pair->first.get(), &outcome, &timersWhileParked));

            CHECK(timersWhileParked == 1);
            REQUIRE(outcome.resolved);
            CHECK_FALSE(outcome.threw);
            REQUIRE_FALSE(outcome.hasValue);
            CHECK(outcome.code == NetErrorCode::Timeout);
        }
    }
}

TEST_CASE("A read that answers cancels the deadline it armed", "[net][socket][reactor][deadline]")
{
    // The other direction, and the one whose absence is a slow leak rather than a failure: a
    // deadline left armed after its read answered would fire into a socket with nothing parked, and
    // a connection doing one read per request would accumulate one stale timer per request.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());
            pair->first->setReceiveDeadline(std::chrono::seconds { 60 });

            auto outcome = Outcome {};
            loop.blockOn([](ISocket* reader, ISocket* writer, Outcome* o) -> Task<void> {
                auto const payload = std::array<std::byte, 2> { std::byte { 1 }, std::byte { 2 } };
                co_await core::async::whenAll(
                    readOnce(reader, o), [](ISocket* w, std::array<std::byte, 2> const* p) -> Task<void> {
                        std::ignore = co_await w->write(std::span<std::byte const> { *p });
                    }(writer, &payload));
            }(pair->first.get(), pair->second.get(), &outcome));

            REQUIRE(outcome.resolved);
            REQUIRE(outcome.hasValue);
            CHECK(outcome.count == 2);
            CHECK(loop.pendingTimerCount() == 0); // the minute-long deadline did not survive the read
        }
    }
}

TEST_CASE("A socket operation created and never awaited leaves the socket safe to close",
          "[net][socket][reactor]")
{
    // **Found by the read-slot canary, and it is not about the canary.** The verb records the kind
    // and the buffer when it is CALLED; the awaitable records itself at the owner when it is
    // AWAITED. `[[nodiscard]]` makes dropping one in between a warning rather than an
    // impossibility — and a warning is exactly what a consumer building with different flags does
    // not get. The slot was then left naming an operation with no frame behind it, and the next
    // `close()` or destructor dereferenced null.
    //
    // Reachable without anything exotic: `auto op = sock->read(buf);` in a scope that returns
    // early, or a `co_await` guarded by a condition that turns out false. The guard that would
    // have caught the empty-buffer spelling is Debug-only, so a Release consumer met the crash and
    // not the assertion.
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto loop = EventLoop { *source };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());

            SECTION("dropped before close()")
            {
                auto buffer = std::array<std::byte, 16> {};
                {
                    auto const dropped = pair->first->read(buffer);
                    CHECK_FALSE(dropped.await_ready()); // it really did want to park
                }
                pair->first->close();
                CHECK(pair->first->isClosed());
            }

            SECTION("dropped before the destructor")
            {
                auto buffer = std::array<std::byte, 16> {};
                {
                    auto const dropped = pair->first->read(buffer);
                    CHECK_FALSE(dropped.await_ready());
                }
                pair->first.reset();
                CHECK(pair->first == nullptr);
            }

            SECTION("a dropped write, then close()")
            {
                auto const payload = makePayload(LargePayload);
                {
                    auto const dropped = pair->first->write(std::span<std::byte const> { payload });
                    CHECK_FALSE(dropped.await_ready());
                }
                pair->first->close();
                CHECK(pair->first->isClosed());
            }

            SECTION("a dropped read, then another read that works")
            {
                auto buffer = std::array<std::byte, 16> {};
                {
                    auto const dropped = pair->first->read(buffer);
                    CHECK_FALSE(dropped.await_ready());
                }
                // The slot is free again, so the socket is still usable — a dropped operation must
                // not cost the connection.
                auto outcome = Outcome {};
                loop.blockOn([](ISocket* reader, ISocket* writer, Outcome* o) -> Task<void> {
                    auto const payload = std::array<std::byte, 1> { std::byte { 9 } };
                    co_await core::async::whenAll(
                        readOnce(reader, o), [](ISocket* w, std::array<std::byte, 1> const* p) -> Task<void> {
                            std::ignore = co_await w->write(std::span<std::byte const> { *p });
                        }(writer, &payload));
                }(pair->first.get(), pair->second.get(), &outcome));
                REQUIRE(outcome.resolved);
                REQUIRE(outcome.hasValue);
                CHECK(outcome.count == 1);
            }
        }
    }
}
