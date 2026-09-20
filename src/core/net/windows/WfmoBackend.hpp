// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// The Windows @c IoBackend: `WSAEventSelect` + `WaitForMultipleObjects`.
///
/// A readiness backend on a platform whose scalable primitive is a COMPLETION port:
/// sockets publish their network events as waitable objects and this waits on those,
/// which is the shape the rest of the layer is written in. It is contour's Windows
/// event source behind the new interface — a port, not a rewrite.
///
/// It is the default for one release only. Task B7 makes IOCP the Windows default and
/// keeps this as its fallback; [core-cpp#6](https://github.com/contour-terminal/core-cpp/issues/6)
/// removes it a release after that. Until then it is in the parity matrix like every
/// other backend, because a fallback nobody exercises is not one.
///
/// `WaitForMultipleObjects` rejects a set larger than `MAXIMUM_WAIT_OBJECTS` (64), so
/// a larger set is swept in chunks with a rotating start, which @c detail::WaitChunk
/// computes free of `windows.h` and every platform tests.

#include <core/net/IoBackend.hpp>
#include <core/net/detail/ReadyBatch.hpp>
#include <core/net/detail/WakeupChannel.hpp>

#include <cstddef>
#include <expected>
#include <optional>
#include <vector>

namespace core::net
{

/// An @c IoBackend whose wait set is the registered waitable handles.
class WfmoBackend final: public IoBackend
{
  public:
    /// Creates the backend and its wakeup channel.
    /// @throws std::runtime_error under handle exhaustion (@c detail::WakeupChannel).
    WfmoBackend();

    [[nodiscard]] BackendKind kind() const noexcept override { return BackendKind::Wfmo; }

    [[nodiscard]] std::expected<void, NetError> attach(ReadinessHandler& handler) override;

    [[nodiscard]] std::expected<void, NetError> setInterest(ReadinessHandler& handler,
                                                            Interest interest) override;

    void detach(ReadinessHandler& handler) noexcept override;

    [[nodiscard]] WaitResult wait(std::optional<platform::SteadyDuration> timeout) override;

    void wake() noexcept override { _wakeup.signal(); }

  private:
    /// One registered handler and what it is currently watched for.
    struct Registration
    {
        ReadinessHandler* handler = nullptr; ///< The caller's handler (not owned).
        Interest interest = Interest::None;  ///< What it is watched for; None keeps it out of the wait set.
    };

    /// @param handler The handler to look for.
    /// @return Its registration, or nullptr if it is not attached.
    [[nodiscard]] Registration* find(ReadinessHandler const& handler) noexcept;

    /// Adds every currently-signalled registration to the batch.
    ///
    /// A full rescan rather than trusting the index one wait returned, so the fast and
    /// chunked paths report identically and every handle ready this round is picked
    /// up, not merely the first the OS named.
    void collectSignalled();

    /// The registrations, in registration order. The wakeup channel's is element 0,
    /// placed there by the constructor and never removed, so the wait set is never
    /// empty and a wait can always be broken.
    std::vector<Registration> _registrations;
    detail::WakeupChannel _wakeup; ///< How another thread breaks a wait in flight.
    detail::ReadyBatch _batch;     ///< What this wait found ready, and what `detach` withdraws from.

    /// Fair-rotation cursor over the handle chunks of the >64-handle wait, so a
    /// 0-timeout sweep (which always finds low-index chunks first) cannot starve
    /// handles past `MAXIMUM_WAIT_OBJECTS`.
    std::size_t _waitRotation = 0;
};

} // namespace core::net
