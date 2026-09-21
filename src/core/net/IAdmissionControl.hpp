// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `IAdmissionControl` — the policy that caps how many connections a server holds at once.
///
/// An accept loop consults it between accepts; a denied accept means the just-accepted socket is
/// closed immediately rather than queued, because a server that accepts what it cannot serve has
/// only moved the queue somewhere the client cannot see.
///
/// Imported from fastcached's `Net/IAdmissionControl.hpp` at
/// `0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`.

#include <atomic>
#include <cstddef>

namespace core::net
{

/// Whether another connection may be admitted right now.
class IAdmissionControl
{
  public:
    IAdmissionControl() = default;
    virtual ~IAdmissionControl() = default;

    IAdmissionControl(IAdmissionControl const&) = delete;
    IAdmissionControl& operator=(IAdmissionControl const&) = delete;
    IAdmissionControl(IAdmissionControl&&) = delete;
    IAdmissionControl& operator=(IAdmissionControl&&) = delete;

    /// @return True if a new connection may be admitted right now.
    [[nodiscard]] virtual bool allowAccept() noexcept = 0;

    /// Tells the policy a connection has been admitted — handed to a worker — so @c allowAccept
    /// can refuse the next call once the cap is reached.
    virtual void onConnectionStarted() noexcept = 0;

    /// Tells the policy a connection has ended, cleanly or not.
    virtual void onConnectionEnded() noexcept = 0;
};

/// The default policy: a cap on concurrent connections. Thread-safe.
class CountingAdmissionControl final: public IAdmissionControl
{
  public:
    /// @param maxConcurrent The connection cap; 0 means unlimited.
    explicit CountingAdmissionControl(std::size_t maxConcurrent = 0) noexcept: _max(maxConcurrent) {}

    /// @copydoc IAdmissionControl::allowAccept
    [[nodiscard]] bool allowAccept() noexcept override
    {
        if (_max == 0)
            return true;
        return _inFlight.load(std::memory_order_acquire) < _max;
    }

    /// @copydoc IAdmissionControl::onConnectionStarted
    void onConnectionStarted() noexcept override { _inFlight.fetch_add(1, std::memory_order_acq_rel); }

    /// @copydoc IAdmissionControl::onConnectionEnded
    void onConnectionEnded() noexcept override { _inFlight.fetch_sub(1, std::memory_order_acq_rel); }

    /// @return How many connections are in flight.
    [[nodiscard]] std::size_t inFlight() const noexcept { return _inFlight.load(std::memory_order_acquire); }

    /// Adjusts the cap at runtime — a configuration reload.
    /// @param newMax The new cap; 0 means unlimited.
    void setMax(std::size_t newMax) noexcept { _max = newMax; }

  private:
    std::size_t _max;
    std::atomic<std::size_t> _inFlight { 0 };
};

} // namespace core::net
