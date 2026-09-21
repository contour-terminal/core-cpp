// SPDX-License-Identifier: Apache-2.0
#include <core/net/ConnectFlow.hpp>

#include <core/net/EventLoop.hpp>

#include <cstdint>
#include <format>
#include <utility>

namespace core::net::detail
{

namespace
{

    /// The deadline this whole call must finish inside.
    ///
    /// A non-positive budget means the caller did not ask for one, and `SteadyTimePoint::max()`
    /// is how "no deadline" is spelled everywhere here. A default-constructed time point would
    /// mean *already expired*, which turns an opt-out into an instant failure.
    /// @param clock The time source, or null for no budget.
    /// @param connectTimeout What the caller asked for.
    /// @return The absolute deadline, or `SteadyTimePoint::max()`.
    [[nodiscard]] platform::SteadyTimePoint budgetDeadline(platform::IClock* clock,
                                                           std::chrono::milliseconds connectTimeout) noexcept
    {
        if (clock == nullptr || connectTimeout <= std::chrono::milliseconds::zero())
            return platform::SteadyTimePoint::max();
        return clock->now() + connectTimeout;
    }

} // namespace

async::Task<SocketResult> runConnectFlow(IAsyncAddressResolver* resolver,
                                         EventLoop* loop,
                                         platform::IClock* clock,
                                         std::string host,
                                         std::uint16_t port,
                                         DialOptions options,
                                         DialStep dial,
                                         void* dialState)
{
    // Refused before the resolver is touched. An empty host resolves to the wildcard address,
    // which is a bind target and not a dial target — and connecting to it reaches localhost on
    // Linux rather than failing, so the mistake would be silent.
    if (host.empty())
        co_return std::unexpected(
            makeNetError(NetErrorCode::AddressNotAvail, 0, std::format("no host to dial for port {}", port)));

    auto const deadline = budgetDeadline(clock, options.connectTimeout);

    auto resolved = co_await resolver->resolve(host, port, loop);
    if (!resolved.has_value())
        co_return std::unexpected(resolved.error());
    if (resolved->empty())
        co_return std::unexpected(resolveFailure(host, port, "no usable address"));

    // Seeded so a resolver that hands back an empty list — which the guard above makes
    // unreachable, but which a future resolver could — still produces an error naming the
    // endpoint rather than a default-constructed one.
    auto failure = makeNetError(
        NetErrorCode::AddressNotAvail, 0, std::format("no usable address for {}:{}", host, port));

    auto remainingCandidates = resolved->size();
    for (auto const& candidate: *resolved)
    {
        // Checked PER CANDIDATE rather than once, which is what makes the budget a total:
        // without this the second candidate would start a fresh attempt after the first had
        // already consumed the whole allowance.
        if (clock != nullptr && clock->now() >= deadline)
        {
            failure = makeNetError(
                NetErrorCode::Timeout,
                0,
                std::format(
                    "connect to {}:{} timed out after {} ms", host, port, options.connectTimeout.count()));
            break;
        }

        // Each candidate gets an equal share of what is LEFT, not the whole of it. Both halves
        // matter and they pull against each other:
        //
        // - Handing every candidate the full budget means a caller asking for two seconds can
        //   wait four. A bound that multiplies by the number of addresses a name happens to have
        //   is not a bound.
        // - Handing the FIRST candidate the whole remaining budget defeats the fallback entirely
        //   whenever that candidate black-holes rather than refuses — the ordinary case for an
        //   AAAA on a machine with no IPv6 route, and the exact situation trying every candidate
        //   exists for.
        //
        // Dividing gives the caller the total it asked for and still leaves every candidate a
        // real chance. A candidate that finishes early hands what it did not use to the ones
        // after it, because the share is recomputed from the clock each time rather than fixed up
        // front.
        auto candidateDeadline = deadline;
        if (clock != nullptr && deadline != platform::SteadyTimePoint::max() && remainingCandidates > 1)
        {
            auto const left = deadline - clock->now();
            candidateDeadline = clock->now() + (left / static_cast<std::int64_t>(remainingCandidates));
        }
        --remainingCandidates;

        auto attempt = co_await dial(dialState, candidate, candidateDeadline, options.keepAlive);
        if (attempt.has_value())
            co_return std::move(*attempt);

        // The LAST failure wins: an AAAA that cannot be routed followed by an A that can is a
        // healthy host, and reporting the first would describe this machine's routing rather than
        // the peer.
        failure = std::move(attempt.error());
    }

    co_return std::unexpected(std::move(failure));
}

} // namespace core::net::detail
