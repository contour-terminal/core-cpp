// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// The @c IoBackend that reports nothing, ever.

#include <core/net/IoBackend.hpp>

#include <cstddef>
#include <expected>
#include <optional>
#include <unordered_set>

namespace core::net::testing
{

/// An @c IoBackend with no I/O behind it: registrations are accepted and never become
/// ready, and a wait returns at once.
///
/// What it is for is a loop with no I/O at all — one driven entirely by `post`,
/// `spawn` and timers, which is most of what a scheduler test needs. Because it never
/// blocks, a loop driven by it must not be asked to: its wait ignores the timeout
/// rather than sleeping through it, since sleeping is exactly what the caller is
/// trying to avoid, and the single-threaded WebAssembly subset has nothing to sleep
/// with. Task B4's `testing::TestLoop` pairs it with `IdlePolicy::Return` for that
/// reason.
class NullBackend final: public IoBackend
{
  public:
    [[nodiscard]] BackendKind kind() const noexcept override { return BackendKind::Null; }

    [[nodiscard]] std::expected<void, NetError> attach(ReadinessHandler& handler) override
    {
        _attached.insert(&handler);
        return {};
    }

    [[nodiscard]] std::expected<void, NetError> setInterest(ReadinessHandler& handler,
                                                            Interest /*interest*/) override
    {
        if (!_attached.contains(&handler))
            return std::unexpected { makeNetError(
                NetErrorCode::BadHandle, 0, "NullBackend::setInterest: handler is not attached") };
        return {};
    }

    void detach(ReadinessHandler& handler) noexcept override { _attached.erase(&handler); }

    /// @param timeout Ignored: see the class documentation.
    /// @return Always nothing dispatched.
    [[nodiscard]] WaitResult wait(std::optional<platform::SteadyDuration> /*timeout*/) override
    {
        ++_waitCount;
        return WaitResult {};
    }

    void wake() noexcept override { ++_wakeCount; }

    /// @return How many times `wait()` was called.
    [[nodiscard]] std::size_t waitCount() const noexcept { return _waitCount; }

    /// @return How many times `wake()` was called.
    [[nodiscard]] std::size_t wakeCount() const noexcept { return _wakeCount; }

    /// @return The number of handlers currently attached.
    [[nodiscard]] std::size_t attachedCount() const noexcept { return _attached.size(); }

  private:
    std::unordered_set<ReadinessHandler const*> _attached;
    std::size_t _waitCount = 0;
    std::size_t _wakeCount = 0;
};

} // namespace core::net::testing
