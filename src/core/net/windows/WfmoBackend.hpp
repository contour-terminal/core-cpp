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
/// computes free of `windows.h` and every platform tests. `WfmoBackend_test` covers
/// this backend's USE of that arithmetic, which is the half a unit test of the
/// arithmetic cannot reach.
///
/// **Waiting on a registered handle must not change it.** A wait here is a detector
/// followed by a rescan: `WaitForMultipleObjects` says that *something* in a chunk
/// fired, and `WaitForSingleObject(h, 0)` on EVERY non-muted registration then says
/// *which*. So each registered handle is waited on repeatedly, by dispatches that have
/// nothing to do with it. A handle a wait CONSUMES is therefore drained by a rescan it
/// was not the subject of: its readiness is dispatched to nobody and the flow parked on
/// it hangs.
///
/// Handles satisfy this for different reasons; the two that arise here are a
/// MANUAL-RESET object, which stays signalled until something resets it —
/// `WaitForMultipleObjects` consumes an AUTO-reset event, which is the case that fails
/// — and a handle whose signal a wait does not consume: console input stays signalled
/// while its buffer holds records, and reading them, not waiting on them, is what
/// clears it. An auto-reset event, a semaphore, a mutex and a synchronization waitable
/// timer are each consumed by a wait, and none of them may be registered here.
///
/// Nothing enforces this, because nothing can: a HANDLE does not say which it is. **When
/// you register a new class of handle here, check it against the property above** — not
/// against the examples, which are only the handles that exist today: `WSACreateEvent`
/// (sockets and listeners), `platform::SystemPipe`'s wakeup event and
/// `platform::Wakeup`, all manual-reset; Task B12 adds `platform::standardInput()`,
/// which is the second kind.

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
