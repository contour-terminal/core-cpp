// SPDX-License-Identifier: Apache-2.0
//
// What a parked socket operation costs the backend: the guard for the registration a socket keeps
// for its life (`RegistrationLifetime::UntilClosed`).
//
// **These cases count, because the defect they guard against is a cost and not a wrong answer.**
// Until the registration was kept, every parked read attached a registration, armed it and
// detached it again -- on epoll an `EPOLL_CTL_ADD` and an `EPOLL_CTL_DEL` per request, which a
// loopback echo measured at a third of the loop's cost per request -- and every case in the suite
// was green, because a registration per park is exactly as CORRECT as one per socket. A case that
// asserts only the bytes cannot see it; one that counts the calls reaching the backend can. Against
// the per-park registration, the first case below reports 2x its round count of attaches.
//
// POSIX-only because the property is `PosixSocket`'s: it is the transport that announces its close
// to the loop, which is the promise the kept registration rests on.
#include <core/async/Task.hpp>
#include <core/async/WhenAll.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/posix/PosixSocket.hpp>
#include <core/net/testing/BackendMatrix.hpp>
#include <core/net/testing/InMemoryTransport.hpp>
#include <core/net/testing/ScriptedBackend.hpp>
#include <core/platform/Clock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <sys/socket.h>

#include <array>
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

using core::async::Task;
using core::net::EventLoop;
using core::net::Interest;
using core::net::ISocket;
using core::net::testing::BackendMatrix;

namespace
{

/// An @c IoBackend that forwards to a real one and counts what the loop asked of it.
///
/// A decorator rather than a double, because the question is what a REAL socket on a REAL backend
/// costs: a scripted backend would report whatever the case scripted, and the reads here have to
/// park on the kernel's own readiness to count as parked at all.
class CountingBackend final: public core::net::IoBackend
{
  public:
    /// @param inner The backend every call is forwarded to (owned).
    explicit CountingBackend(std::unique_ptr<core::net::IoBackend> inner): _inner(std::move(inner)) {}

    [[nodiscard]] core::net::BackendKind kind() const noexcept override { return _inner->kind(); }

    [[nodiscard]] std::expected<void, core::net::NetError> attach(
        core::net::ReadinessHandler& handler) override
    {
        ++attaches;
        return _inner->attach(handler);
    }

    [[nodiscard]] std::expected<void, core::net::NetError> setInterest(core::net::ReadinessHandler& handler,
                                                                       Interest interest) override
    {
        ++interestChanges;
        if (core::net::hasInterest(interest, Interest::Write))
            ++writeArmings;
        auto result = _inner->setInterest(handler, interest);
        if (result)
            armed[&handler] = interest;
        return result;
    }

    void detach(core::net::ReadinessHandler& handler) noexcept override
    {
        ++detaches;
        armed.erase(&handler);
        _inner->detach(handler);
    }

    [[nodiscard]] core::net::WaitResult wait(std::optional<core::platform::SteadyDuration> timeout) override
    {
        auto const result = _inner->wait(timeout);
        dispatched += result.dispatched;
        return result;
    }

    void wake() noexcept override { _inner->wake(); }

    std::size_t attaches = 0;        ///< Calls to @c attach.
    std::size_t interestChanges = 0; ///< Calls to @c setInterest.
    std::size_t writeArmings = 0;    ///< Calls to @c setInterest whose mask includes writability.
    std::size_t detaches = 0;        ///< Calls to @c detach.
    std::size_t dispatched = 0;      ///< Readiness callbacks the inner backend ran.

    /// What each still-attached registration was last armed for.
    std::unordered_map<core::net::ReadinessHandler const*, Interest> armed;

  private:
    std::unique_ptr<core::net::IoBackend> _inner;
};

/// How many request/response rounds a case runs. Enough that a per-park cost is unmistakable
/// against the one-per-socket cost, few enough that a loopback run of it is instant.
constexpr std::size_t Rounds = 16;

/// Writes one byte and then reads one, @p rounds times: the client of a request/response exchange.
Task<void> ask(ISocket* sock, std::size_t rounds, std::size_t* completed)
{
    auto byte = std::array<std::byte, 1> { std::byte { 0x2a } };
    for ([[maybe_unused]] auto const round: std::views::iota(std::size_t { 0 }, rounds))
    {
        auto const wrote = co_await sock->write(std::span<std::byte const> { byte });
        if (!wrote.has_value())
            co_return;
        auto const got = co_await sock->read(byte);
        if (!got.has_value() || *got == 0)
            co_return;
        ++*completed;
    }
}

/// Reads one byte and then writes it back, @p rounds times: the server of the same exchange.
Task<void> answer(ISocket* sock, std::size_t rounds)
{
    auto byte = std::array<std::byte, 1> {};
    for ([[maybe_unused]] auto const round: std::views::iota(std::size_t { 0 }, rounds))
    {
        auto const got = co_await sock->read(byte);
        if (!got.has_value() || *got == 0)
            co_return;
        auto const wrote = co_await sock->write(std::span<std::byte const> { byte });
        if (!wrote.has_value())
            co_return;
    }
}

/// Writes every byte of @p payload and records whether it all went.
Task<void> writeAll(ISocket* sock, std::vector<std::byte> const* payload, bool* done)
{
    auto const wrote = co_await sock->write(std::span<std::byte const> { *payload });
    *done = wrote.has_value() && *wrote == payload->size();
}

/// Reads one byte at a time until the socket closes, counting each: a reader that is parked again
/// the moment it has taken what arrived.
Task<void> readForever(ISocket* sock, std::size_t* received)
{
    auto byte = std::array<std::byte, 1> {};
    while (true)
    {
        auto const got = co_await sock->read(byte);
        if (!got.has_value() || *got == 0)
            co_return;
        ++*received;
    }
}

/// Reads until @p expected bytes have arrived.
Task<void> drain(ISocket* sock, std::size_t expected)
{
    auto chunk = std::array<std::byte, std::size_t { 64 } * 1024> {};
    auto received = std::size_t { 0 };
    while (received < expected)
    {
        auto const got = co_await sock->read(chunk);
        if (!got.has_value() || *got == 0)
            co_return;
        received += *got;
    }
}

} // namespace

TEST_CASE("a socket's parked reads share one backend registration for its life",
          "[net][socket][registration]")
{
    for (auto const& backend: BackendMatrix)
    {
        auto inner = core::net::makeBackend(backend.kind);
        if (!inner)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto counting = CountingBackend { std::move(inner) };
            auto loop = EventLoop { counting };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());

            auto completed = std::size_t { 0 };
            loop.blockOn([](ISocket* client, ISocket* server, std::size_t* done) -> Task<void> {
                co_await core::async::whenAll(ask(client, Rounds, done), answer(server, Rounds));
            }(pair->first.get(), pair->second.get(), &completed));

            REQUIRE(completed == Rounds);
            // What makes the counts below mean anything: the reads PARKED. One flow runs at a time
            // on this loop, so each side's read finds nothing yet and waits for the kernel to say
            // the other side wrote -- a read answered inline would cost the backend nothing under
            // either design, and the case would pass for the wrong reason.
            REQUIRE(counting.dispatched >= Rounds);

            // One registration per socket, made the first time it parks and armed for reading once.
            // Per park this was 2 * Rounds of each, one attach, one arm and one detach a request.
            CHECK(counting.attaches == 2);
            CHECK(counting.interestChanges == 2);
            CHECK(counting.detaches == 0);

            // And the registration ends with the socket: announced before the descriptor closes,
            // so the backend drops it while the number still names this socket.
            pair->first->close();
            pair->second->close();
            CHECK(counting.detaches == 2);
            CHECK(counting.armed.empty());
        }
    }
}

TEST_CASE("a socket's registration is not left armed for writing once its write is taken",
          "[net][socket][registration]")
{
    // The other half of keeping the registration: readability is kept armed after its read, which
    // is what saves the kernel call, and WRITABILITY must not be. A socket with room in its send
    // buffer is writable on every wait, so a registration left armed for it after the write that
    // wanted it would report on every turn for the rest of the connection's life -- a loop that
    // never sleeps, and never fails a case either.
    for (auto const& backend: BackendMatrix)
    {
        auto inner = core::net::makeBackend(backend.kind);
        if (!inner)
            continue;
        DYNAMIC_SECTION("backend=" << backend.name)
        {
            auto counting = CountingBackend { std::move(inner) };
            auto loop = EventLoop { counting };
            auto pair = core::net::testing::makeSocketPair(loop);
            REQUIRE(pair.has_value());

            // More than any send buffer takes at once, so the write parks on writability.
            auto const payload = std::vector<std::byte>(std::size_t { 4 } * 1024 * 1024, std::byte { 0x5a });
            auto done = false;
            loop.blockOn(
                [](ISocket* from, ISocket* to, std::vector<std::byte> const* p, bool* d) -> Task<void> {
                    co_await core::async::whenAll(writeAll(from, p, d), drain(to, p->size()));
                }(pair->first.get(), pair->second.get(), &payload, &done));

            REQUIRE(done);
            // The write parked, or nothing here was armed for writing to begin with.
            auto const armedForWriting = [&counting] {
                return std::ranges::count_if(counting.armed, [](auto const& entry) {
                    return core::net::hasInterest(entry.second, Interest::Write);
                });
            };
            REQUIRE(counting.writeArmings > 0);
            CHECK(armedForWriting() == 0);

            pair->first->close();
            pair->second->close();
        }
    }
}

TEST_CASE("a socket that stays readable does not starve its parked write", "[net][socket][registration]")
{
    // The reader and the writer share ONE registration, and a backend services one callback per
    // registration per wait, preferring readability. So on a socket that is readable on every wait
    // -- a peer that keeps sending while this side has a large reply to deliver -- the writable
    // callback is never chosen, and a writer woken only by it never moves. The loop therefore wakes
    // a parked writer on a readable report too.
    //
    // Scripted, because the property is about which callback a wait chooses, and a real kernel
    // would decide the timing: a peer thread flooding the socket leaves some waits readable-only
    // and some not, and the case would pass or fail by luck. Here every wait reports the socket
    // readable AND writable, and the descriptors are real, so every `recv` and `send` is the
    // kernel's own answer.
    auto fds = std::array<int, 2> { -1, -1 };
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) == 0);
    for (auto const fd: fds)
        REQUIRE(::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK) == 0);
    auto const peer = fds[1];

    auto clock = core::platform::ManualClock {};
    auto backend = core::net::testing::ScriptedBackend {};
    auto loop =
        EventLoop { backend, clock, core::net::EventLoopOptions { .idle = core::net::IdlePolicy::Return } };
    {
        auto socket = core::net::PosixSocket { loop, fds[0] };

        // Larger than the socket pair's buffers, so the write parks; the peer drains it below.
        auto const payload = std::vector<std::byte>(std::size_t { 2 } * 1024 * 1024, std::byte { 0x5a });
        auto written = false;
        auto received = std::size_t { 0 };
        loop.spawn(writeAll(&socket, &payload, &written));
        loop.spawn(readForever(&socket, &received));
        backend.pushTimeout();
        std::ignore = loop.runOnce(core::platform::SteadyDuration::zero());
        REQUIRE_FALSE(written);
        auto const watch = backend.lastHandlerId();
        REQUIRE(watch);

        // Every round: the peer sends a byte and makes room, so the socket is genuinely readable
        // and writable, and the wait says both. Bounded -- a starved writer is a case that fails,
        // not one that hangs.
        auto sink = std::array<std::byte, std::size_t { 64 } * 1024> {};
        constexpr auto MaxRounds = std::size_t { 256 };
        for ([[maybe_unused]] auto const round: std::views::iota(std::size_t { 0 }, MaxRounds))
        {
            if (written)
                break;
            auto const one = std::byte { 1 };
            REQUIRE(::write(peer, &one, 1) == 1);
            while (::read(peer, sink.data(), sink.size()) > 0)
            {
            }
            backend.pushReadiness(watch, core::net::Readiness::Readable | core::net::Readiness::Writable);
            std::ignore = loop.runOnce(core::platform::SteadyDuration::zero());
        }

        // The reads were served -- the readable side of each report was taken, which is what
        // would starve the writer if it were all a report could wake.
        CHECK(received > 0);
        CHECK(written);

        // Close with a step in hand: the closed reader is resumed inline, but a turn still waits.
        backend.pushTimeout();
        socket.close();
        std::ignore = loop.runOnce(core::platform::SteadyDuration::zero());
    }
    ::close(peer);
}
