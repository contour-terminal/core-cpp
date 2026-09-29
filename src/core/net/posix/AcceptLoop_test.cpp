// SPDX-License-Identifier: Apache-2.0
//
// What the POSIX accept loop does with each error `accept(2)` can leave: park, take the next
// connection at once, or report -- and, when it reports, in which category. The loop used to retry
// only EINTR and ECONNABORTED and hand every other errno over as `SystemError`, which a caller must
// read as exhaustion and back off on: so a packet filter's `EPERM` for ONE connection, or any of the
// pending-connection errors Linux's accept(2) says to treat like EAGAIN, cost a listener its backoff.
// Nothing in the suite can provoke most of these from a real kernel, so the decision is asserted
// row by row, together with the table that classifies what it reports.
#include <core/net/NetError.hpp>
#include <core/net/posix/AcceptLoop.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cerrno>

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
    // Exhaustion stays SystemError, which is what tells a caller to back off.
    Row { .systemCode = EMFILE, .step = AcceptStep::Report, .code = NetErrorCode::SystemError },
    Row { .systemCode = ENFILE, .step = AcceptStep::Report, .code = NetErrorCode::SystemError },
    Row { .systemCode = ENOBUFS, .step = AcceptStep::Report, .code = NetErrorCode::SystemError },
    Row { .systemCode = ENOMEM, .step = AcceptStep::Report, .code = NetErrorCode::SystemError },
    // A listener that is not one, whichever errno says so: `EINVAL` is a socket that is not listening,
    // accept's own row, and must not read as exhaustion (see `AcceptOwnClassifications`).
    Row { .systemCode = EBADF, .step = AcceptStep::Report, .code = NetErrorCode::BadHandle },
    Row { .systemCode = ENOTSOCK, .step = AcceptStep::Report, .code = NetErrorCode::BadHandle },
    Row { .systemCode = EINVAL, .step = AcceptStep::Report, .code = NetErrorCode::BadHandle },
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
