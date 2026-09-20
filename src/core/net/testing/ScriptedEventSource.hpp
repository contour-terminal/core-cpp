// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// A scripted @c EventSource for deterministic @c EventLoop unit tests.

#include <core/net/EventSource.hpp>
#include <core/platform/Types.hpp>

#include <cstdint>
#include <deque>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace core::net::testing
{

/// An @c EventSource that returns a pre-scripted sequence of wait outcomes.
///
/// Each `wait()` pops and returns the next scripted outcome and records the
/// timeout it was called with. When the script is exhausted it THROWS: upstream
/// Endo returned an interrupt outcome as a backstop, but this port's WaitOutcome
/// has no interrupt channel, and an empty outcome would let a parked flow spin
/// the pump forever — a thrown error surfaces the test bug immediately instead.
///
/// The fd registry is modelled too: @c attach hands back synthetic, monotonically
/// increasing tokens (no real fds), so tests can script readiness on a given
/// token via @c pushReadable / @c pushWritable and drive the readiness awaitables
/// deterministically.
class ScriptedEventSource: public EventSource
{
  public:
    /// Appends a bare timeout outcome (nothing happened).
    void pushTimeout() { _scripted.push_back(WaitOutcome {}); }

    /// Appends an outcome marking @p token readable.
    /// @param token A token previously handed out by @c attach.
    void pushReadable(FdToken token)
    {
        _scripted.push_back(WaitOutcome { .readyRead = { token }, .readyWrite = {} });
    }

    /// Appends an outcome marking @p token writable.
    /// @param token A token previously handed out by @c attach.
    void pushWritable(FdToken token)
    {
        _scripted.push_back(WaitOutcome { .readyRead = {}, .readyWrite = { token } });
    }

    /// @return The timeouts passed to each `wait()` call, in order.
    [[nodiscard]] std::vector<int> const& recordedTimeouts() const noexcept { return _timeouts; }

    /// @return How many times `wait()` was invoked.
    [[nodiscard]] std::size_t waitCount() const noexcept { return _timeouts.size(); }

    /// @return The number of fds currently attached (after attach/detach).
    [[nodiscard]] std::size_t attachedCount() const noexcept { return _live.size(); }

    /// @return The token most recently handed out by @c attach (invalid if none).
    [[nodiscard]] FdToken lastToken() const noexcept { return FdToken { _nextToken }; }

    WaitOutcome wait(int timeoutMs) override
    {
        _timeouts.push_back(timeoutMs);
        if (_scripted.empty())
            throw std::runtime_error("ScriptedEventSource: script exhausted while a flow is still parked");
        auto outcome = std::move(_scripted.front());
        _scripted.pop_front();
        return outcome;
    }

    FdToken attach(platform::NativeHandle /*fd*/, FdInterest /*interest*/) override
    {
        auto const token = FdToken { ++_nextToken };
        _live.insert(token.value);
        return token;
    }

    /// Drops @p token's registration. IDEMPOTENT, as @c EventSource documents and every real
    /// backend behaves.
    ///
    /// Tracked as a SET of live tokens rather than a count, because the loop genuinely
    /// detaches twice on normal paths — `notifyHandleClosing` then `unregisterFdWaiter`;
    /// `requeueForCancellation` and `wakeAllWaiters` before `await_resume`. A counter
    /// decremented per CALL therefore reported fewer registrations than were live, so a leak
    /// assertion against this source would have passed on one that never went away.
    /// @param token The registration to drop; unknown and repeated tokens are no-ops.
    void detach(FdToken token) override { _live.erase(token.value); }

  private:
    std::deque<WaitOutcome> _scripted;
    std::vector<int> _timeouts;
    std::uint64_t _nextToken = 0;            ///< Source of synthetic, never-zero tokens.
    std::unordered_set<std::uint64_t> _live; ///< Tokens attached and not yet detached.
};

} // namespace core::net::testing
