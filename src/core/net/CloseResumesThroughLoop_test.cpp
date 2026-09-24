// SPDX-License-Identifier: Apache-2.0
//
// A resource settles a parked operation at once and resumes its waiter through the loop: never
// inline, inside `close()` or `cancelRead()` (guarantee G2, `.agent/rules/async-and-net.md`: every
// resumption happens in turn step 2, and a resource never resumes its consumer inline).
//
// The defect this pins was found by contour. `NativeClient::detach` is
// `_writer.close(); _connection->close();`, and the first close resumed the client's parked read
// INSIDE `close()`; that flow ran to its end and destroyed the client, and the second statement
// then called through a destroyed `_connection`. Deterministic, and an ASan report in the loop's
// own guarantees rather than in contour's code.
#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/NetError.hpp>
#include <core/net/testing/BackendMatrix.hpp>
#include <core/net/testing/InMemoryTransport.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <tuple>
#include <utility>

using core::async::Task;
using core::net::EventLoop;
using core::net::ISocket;
using core::net::NetErrorCode;
using core::net::testing::BackendMatrix;

namespace
{

/// How a parked read ended.
struct ReadOutcome
{
    bool resolved = false;                ///< Whether the read's flow has run past its `co_await`.
    bool hasValue = false;                ///< Whether it answered with a count.
    NetErrorCode code = NetErrorCode::Ok; ///< The code, where it answered with an error.
};

/// Parks one read and records how it ended.
Task<void> readOnce(ISocket* sock, ReadOutcome* out)
{
    auto buffer = std::array<std::byte, 8> {};
    auto const got = co_await sock->read(buffer);
    out->resolved = true;
    out->hasValue = got.has_value();
    if (!got.has_value())
        out->code = got.error().code;
}

/// Which verb retires the read.
enum class Retire : std::uint8_t
{
    Close,
    CancelRead,
};

/// Retires the parked read and records whether its waiter had already run when the verb returned.
Task<void> retireAndLook(
    ISocket* sock, Retire verb, ReadOutcome const* reader, bool* ranInside, bool* returned)
{
    if (verb == Retire::Close)
        sock->close();
    else
        sock->cancelRead();
    *ranInside = reader->resolved;
    *returned = true;
    co_return;
}

/// Runs turns until @p done or @p bound turns have run; each turn waits at most 10ms.
/// @return How many turns ran.
std::size_t turnUntil(EventLoop& loop, bool const& done, std::size_t bound)
{
    auto turns = std::size_t { 0 };
    while (!done && turns < bound)
    {
        std::ignore = loop.runOnce(std::chrono::milliseconds { 10 });
        ++turns;
    }
    return turns;
}

/// Runs turns until something is parked on the loop, at most @p bound of them.
/// @return Whether something parked.
bool turnUntilParked(EventLoop& loop, std::size_t bound)
{
    auto turns = std::size_t { 0 };
    while (turns < bound && loop.parkedWaiterCount() == 0)
    {
        std::ignore = loop.runOnce(std::chrono::milliseconds { 0 });
        ++turns;
    }
    return loop.parkedWaiterCount() > 0;
}

/// What contour's client was: an object owning two sockets, destroyed by the flow reading one.
struct Client
{
    std::unique_ptr<ISocket> reader;
    std::unique_ptr<ISocket> writer;
    bool* destroyed;

    Client(std::unique_ptr<ISocket> readSide, std::unique_ptr<ISocket> writeSide, bool* flag) noexcept:
        reader(std::move(readSide)), writer(std::move(writeSide)), destroyed(flag)
    {
    }
    Client(Client const&) = delete;
    Client& operator=(Client const&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;
    ~Client() { *destroyed = true; }
};

/// contour's `runClient`: reads until the read fails, then destroys the client that owns it.
Task<void> runClient(std::unique_ptr<Client>* client, ReadOutcome* out)
{
    co_await readOnce((*client)->reader.get(), out);
    client->reset();
}

/// contour's `NativeClient::detach`: close one socket, then the other, through the client. Records
/// whether the client was already gone between the two -- which, with an inline resume, it was, and
/// the second close would have called through freed storage.
Task<void> detach(std::unique_ptr<Client>* client, bool const* destroyed, bool* goneBetween)
{
    auto* const owner = client->get();
    owner->reader->close();
    *goneBetween = *destroyed;
    if (!*goneBetween)
        owner->writer->close();
    co_return;
}

} // namespace

TEST_CASE("close() and cancelRead() settle a parked read at once and resume it through the loop",
          "[net][socket][resume]")
{
    for (auto const& backend: BackendMatrix)
    {
        auto source = core::net::makeBackend(backend.kind);
        if (!source)
            continue;
        for (auto const verb: { Retire::Close, Retire::CancelRead })
        {
            DYNAMIC_SECTION("backend=" << backend.name
                                       << " verb=" << (verb == Retire::Close ? "close" : "cancelRead"))
            {
                auto loop = EventLoop { *source };
                auto pair = core::net::testing::makeSocketPair(loop);
                REQUIRE(pair.has_value());

                auto reader = ReadOutcome {};
                loop.spawn(readOnce(pair->first.get(), &reader));
                REQUIRE(turnUntilParked(loop, 8));
                REQUIRE_FALSE(reader.resolved);

                auto ranInside = false;
                auto returned = false;
                loop.spawn(retireAndLook(pair->first.get(), verb, &reader, &ranInside, &returned));
                auto const turns = turnUntil(loop, reader.resolved, 8);

                INFO("turns: " << turns);
                REQUIRE(returned);
                // The waiter had NOT run when the verb returned: it is the loop's to resume ...
                CHECK_FALSE(ranInside);
                // ... and the loop did: in the turn that ran the verb on the reactors, one turn later
                // where the wait needs one more dispatch -- WFMO's closed event is reported on the
                // next turn, and IOCP settles a retired real read when the kernel hands it back.
                CHECK(reader.resolved);
                CHECK(turns <= 2);
                // What it resolved to is unchanged: a Cancelled VALUE, because the flow is alive and
                // asked about a read the resource took away. (WindowsSocket's closed-socket read
                // answers BadHandle after a close, as ClosedParkIdle_test records.)
                CHECK_FALSE(reader.hasValue);
                if (verb == Retire::CancelRead || backend.name != "wfmo")
                    CHECK(reader.code == NetErrorCode::Cancelled);
            }
        }
    }
}

TEST_CASE("A flow resumed by close() may destroy the socket's owner without close() touching it",
          "[net][socket][resume]")
{
    // contour's crash, as a case. The reading flow destroys the client that owns both sockets once
    // its read ends; the detaching flow closes the read socket and then the write socket through
    // that same client. Resumed inline, the reader destroyed the client INSIDE the first close.
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

            auto destroyed = false;
            auto client =
                std::make_unique<Client>(std::move(readPair->first), std::move(writePair->first), &destroyed);
            auto reader = ReadOutcome {};
            loop.spawn(runClient(&client, &reader));
            REQUIRE(turnUntilParked(loop, 8));

            auto goneBetween = false;
            loop.spawn(detach(&client, &destroyed, &goneBetween));
            auto const turns = turnUntil(loop, destroyed, 8);

            INFO("turns: " << turns);
            CHECK_FALSE(goneBetween); // the client outlived both closes
            CHECK(destroyed);         // and then its reader destroyed it, on the loop
            CHECK(reader.resolved);
            CHECK(client == nullptr);
        }
    }
}
