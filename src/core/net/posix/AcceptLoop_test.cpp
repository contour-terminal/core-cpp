// SPDX-License-Identifier: Apache-2.0
//
// What the POSIX accept loop does with each error `accept(2)` can leave: park, take the next
// connection at once, or report -- and, when it reports, in which category. The loop used to retry
// only EINTR and ECONNABORTED and hand every other errno over as `SystemError`, which a caller must
// read as exhaustion and back off on: so a packet filter's `EPERM` for ONE connection, or any of the
// pending-connection errors Linux's accept(2) says to treat like EAGAIN, cost a listener its backoff.
// Nothing in the suite can provoke most of these from a real kernel, so the decision is asserted
// row by row, together with the table that classifies what it reports.
#include <core/net/EventLoop.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/NetError.hpp>
#include <core/net/posix/AcceptLoop.hpp>
#include <core/net/testing/ScriptedBackend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

using core::net::AcceptAttempt;
using core::net::EventLoop;
using core::net::IAcceptCall;
using core::net::NetErrorCode;
using core::net::detail::acceptFailureOf;
using core::net::detail::AcceptStep;

namespace
{

/// One `errno` a failed `accept(2)` can leave, what the loop does, and -- when it reports -- what
/// the caller is told.
struct Row
{
    int systemCode;    ///< The `errno` value.
    AcceptStep step;   ///< What the accept loop does.
    NetErrorCode code; ///< The category reported; `Ok` where the loop does not report.
};

constexpr auto Rows = std::array {
    // Nothing pending.
    Row { .systemCode = EAGAIN, .step = AcceptStep::Park, .code = NetErrorCode::Ok },
    Row { .systemCode = EWOULDBLOCK, .step = AcceptStep::Park, .code = NetErrorCode::Ok },
    // Taken again at once: an interrupted call, and the pending-connection errors no category names.
    Row { .systemCode = EINTR, .step = AcceptStep::Retry, .code = NetErrorCode::Ok },
    Row { .systemCode = ECONNABORTED, .step = AcceptStep::Retry, .code = NetErrorCode::Ok },
    Row { .systemCode = EPROTO, .step = AcceptStep::Retry, .code = NetErrorCode::Ok },
    Row { .systemCode = ENOPROTOOPT, .step = AcceptStep::Retry, .code = NetErrorCode::Ok },
    // Per-connection answers that HAVE a category, reported as it, never as SystemError.
    Row { .systemCode = EPERM, .step = AcceptStep::Report, .code = NetErrorCode::PermissionDenied },
    Row { .systemCode = EHOSTUNREACH, .step = AcceptStep::Report, .code = NetErrorCode::HostUnreach },
    Row { .systemCode = ENETUNREACH, .step = AcceptStep::Report, .code = NetErrorCode::HostUnreach },
    Row { .systemCode = EHOSTDOWN, .step = AcceptStep::Report, .code = NetErrorCode::HostUnreach },
    Row { .systemCode = ENETDOWN, .step = AcceptStep::Report, .code = NetErrorCode::HostUnreach },
#ifdef ENONET
    Row { .systemCode = ENONET, .step = AcceptStep::Report, .code = NetErrorCode::HostUnreach },
#endif
    // Also what a socket that is not a stream answers on every call, so reported rather than retried.
    Row { .systemCode = EOPNOTSUPP, .step = AcceptStep::Report, .code = NetErrorCode::Unsupported },
    // Exhaustion is ResourceExhausted, which is what tells a caller to back off: never SystemError,
    // which may be permanent and must not be backed off on forever (`AcceptPolicy.hpp`).
    Row { .systemCode = EMFILE, .step = AcceptStep::Report, .code = NetErrorCode::ResourceExhausted },
    Row { .systemCode = ENFILE, .step = AcceptStep::Report, .code = NetErrorCode::ResourceExhausted },
    Row { .systemCode = ENOBUFS, .step = AcceptStep::Report, .code = NetErrorCode::ResourceExhausted },
    Row { .systemCode = ENOMEM, .step = AcceptStep::Report, .code = NetErrorCode::ResourceExhausted },
    // A listener that is not one, whichever errno says so: `EINVAL` is a socket that is not listening,
    // accept's own row, and must not read as exhaustion (see `AcceptOwnClassifications`).
    Row { .systemCode = EBADF, .step = AcceptStep::Report, .code = NetErrorCode::BadHandle },
    Row { .systemCode = ENOTSOCK, .step = AcceptStep::Report, .code = NetErrorCode::BadHandle },
    Row { .systemCode = EINVAL, .step = AcceptStep::Report, .code = NetErrorCode::BadHandle },
    // A pending timeout on one new connection is that connection's failure, not the loop's poll
    // ticking: accept's own row, because the shared table answers `ETIMEDOUT` as a deadline.
    Row { .systemCode = ETIMEDOUT, .step = AcceptStep::Report, .code = NetErrorCode::HostUnreach },
#ifdef ENOSR
    Row { .systemCode = ENOSR, .step = AcceptStep::Report, .code = NetErrorCode::ResourceExhausted },
#endif
};

} // namespace

TEST_CASE("Every error accept(2) can leave is parked on, retried or reported as its category",
          "[net][errors]")
{
    for (auto const& row: Rows)
    {
        INFO("errno " << row.systemCode);
        auto const failure = acceptFailureOf(row.systemCode);
        CHECK(failure.step == row.step);
        CHECK(failure.code == row.code);
    }
}

namespace
{

/// A script entry that stands for "a connection arrives": the scripted call hands out a fresh
/// connected descriptor in its place. Any other entry is a failure with that `errno`.
constexpr int Connects = 0;

/// `IAcceptCall` answering from a script, one entry per call: a failure `errno`, or `Connects`.
/// A script that has run out answers `EBADF`, which the loop reports, so a loop that retries what
/// it should have reported ends instead of spinning -- and the call count says so.
class ScriptedAcceptCall final: public IAcceptCall
{
  public:
    explicit ScriptedAcceptCall(std::vector<int> script): _script { std::move(script) } {}

    ScriptedAcceptCall(ScriptedAcceptCall const&) = delete;
    ScriptedAcceptCall& operator=(ScriptedAcceptCall const&) = delete;
    ScriptedAcceptCall(ScriptedAcceptCall&&) = delete;
    ScriptedAcceptCall& operator=(ScriptedAcceptCall&&) = delete;

    ~ScriptedAcceptCall() override
    {
        for (auto const fd: _peers)
            ::close(fd);
    }

    [[nodiscard]] AcceptAttempt accept(int /*listenFd*/,
                                       sockaddr_storage& /*peer*/,
                                       socklen_t& /*peerLen*/) noexcept override
    {
        auto const entry = _calls < _script.size() ? _script[_calls] : EBADF;
        ++_calls;
        if (entry != Connects)
            return AcceptAttempt { .fd = -1, .error = entry };
        auto pair = std::array<int, 2> { -1, -1 };
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair.data()) != 0)
            return AcceptAttempt { .fd = -1, .error = errno };
        ::fcntl(pair[0], F_SETFL, ::fcntl(pair[0], F_GETFL, 0) | O_NONBLOCK);
        _peers.push_back(pair[1]);
        return AcceptAttempt { .fd = pair[0], .error = 0 };
    }

    /// @return How many times the loop called accept.
    [[nodiscard]] std::size_t calls() const noexcept { return _calls; }

  private:
    std::vector<int> _script;
    std::vector<int> _peers;
    std::size_t _calls = 0;
};

/// One path through the accept loop: what accept(2) answers, call by call, and what the loop
/// returns and after how many calls.
struct LoopCase
{
    std::string_view name;             ///< What the case is.
    std::vector<int> script;           ///< The answers, in order.
    std::optional<NetErrorCode> error; ///< The reported category, or nothing for an accepted socket.
    std::size_t calls;                 ///< How many accepts the loop made.
};

} // namespace

TEST_CASE("The accept loop retries, reports and parks as the decision says, through the system call",
          "[net][errors]")
{
    auto const cases = std::array {
        // Retried: each dequeued a connection that had already failed; the next one is served.
        LoopCase { .name = "retried errors are taken again at once",
                   .script = { EPROTO, ECONNABORTED, ENOPROTOOPT, EINTR, Connects },
                   .error = std::nullopt,
                   .calls = 5 },
        // Reported at once, in its category, with no second call.
        LoopCase { .name = "a filtered connection is reported, not retried",
                   .script = { EPERM, Connects },
                   .error = NetErrorCode::PermissionDenied,
                   .calls = 1 },
        LoopCase { .name = "a pending network error with a category is reported",
                   .script = { ENETUNREACH, Connects },
                   .error = NetErrorCode::HostUnreach,
                   .calls = 1 },
        LoopCase { .name = "a socket that is not listening is a bad handle",
                   .script = { EINVAL, Connects },
                   .error = NetErrorCode::BadHandle,
                   .calls = 1 },
        LoopCase { .name = "exhaustion is reported for the caller to back off on",
                   .script = { EMFILE, Connects },
                   .error = NetErrorCode::ResourceExhausted,
                   .calls = 1 },
        // Parked: nothing pending; the listener is readable, so the park returns and accepts.
        LoopCase { .name = "nothing pending parks, then accepts",
                   .script = { EAGAIN, Connects },
                   .error = std::nullopt,
                   .calls = 2 },
    };

    for (auto const& loopCase: cases)
    {
        INFO(loopCase.name);
        auto source = core::net::makeBackend(core::net::preferredBackendKind());
        REQUIRE(source != nullptr);
        auto loop = EventLoop { *source };

        // The "listener": a descriptor the park can wait on, made readable up front so a park
        // returns at once rather than waiting for a connection nobody will make.
        auto listenPair = std::array<int, 2> { -1, -1 };
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, listenPair.data()) == 0);
        REQUIRE(::fcntl(listenPair[0], F_SETFL, ::fcntl(listenPair[0], F_GETFL, 0) | O_NONBLOCK) == 0);
        auto const byte = std::byte { 1 };
        REQUIRE(::write(listenPair[1], &byte, 1) == 1);

        auto scripted = ScriptedAcceptCall { loopCase.script };
        auto const lifetime = std::make_shared<int>(0);
        auto const closed = false;
        auto const listenFd = listenPair[0];
        auto result = loop.blockOn(core::net::acceptOne(
            &loop, &listenFd, &closed, std::weak_ptr<void const> { lifetime }, &scripted));

        auto const calls = scripted.calls();
        auto const accepted = result.has_value();
        auto const reported =
            accepted ? std::optional<NetErrorCode> {} : std::optional { result.error().code };
        ::close(listenPair[0]);
        ::close(listenPair[1]);

        CHECK(calls == loopCase.calls);
        CHECK(accepted == !loopCase.error.has_value());
        // `Ok` stands for "nothing reported" on both sides, so a mismatch prints two categories
        // rather than two unprintable optionals.
        CHECK(reported.value_or(NetErrorCode::Ok) == loopCase.error.value_or(NetErrorCode::Ok));
    }
}

TEST_CASE("A listener the loop refuses to watch is reported dead, not thrown", "[net][errors][accept-policy]")
{
    // `waitReadable` throws `FdRegistrationFailed` when the backend refuses the registration. Let
    // out of `accept()`, it ended an accept loop as an exception nobody reported -- the one way out
    // of `serve` that was neither a closed listener nor a dead one. Reported as `BadHandle`, it is
    // a dead listener: the loop gives up on it, closes it and says so.
    auto source = core::net::testing::ScriptedBackend {};
    source.refuseNextAttach();
    auto loop = EventLoop { source };
    auto listenPair = std::array<int, 2> { -1, -1 };
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, listenPair.data()) == 0);
    REQUIRE(::fcntl(listenPair[0], F_SETFL, ::fcntl(listenPair[0], F_GETFL, 0) | O_NONBLOCK) == 0);

    // Nothing pending, so the accept parks -- and the park is what the backend refuses.
    auto scripted = ScriptedAcceptCall { { EAGAIN } };
    auto const lifetime = std::make_shared<int>(0);
    auto const closed = false;
    auto const listenFd = listenPair[0];
    auto const accept = [](EventLoop* l,
                           int const* fd,
                           bool const* isClosed,
                           std::weak_ptr<void const> alive,
                           IAcceptCall* call) -> core::async::Task<std::optional<NetErrorCode>> {
        try
        {
            auto result = co_await core::net::acceptOne(l, fd, isClosed, std::move(alive), call);
            co_return result.has_value() ? std::optional<NetErrorCode> {}
                                         : std::optional { result.error().code };
        }
        catch (core::net::FdRegistrationFailed const&)
        {
            co_return std::optional { NetErrorCode::Last }; // escaped: stands for "thrown"
        }
    };
    auto const reported =
        loop.blockOn(accept(&loop, &listenFd, &closed, std::weak_ptr<void const> { lifetime }, &scripted));
    ::close(listenPair[0]);
    ::close(listenPair[1]);

    REQUIRE(reported.has_value());
    CHECK(*reported == NetErrorCode::BadHandle);
}
