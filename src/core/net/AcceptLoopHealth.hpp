// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Which of a process's accept loops have given up while they were meant to be serving.
///
/// **A listening port is not a serving one.** An accept loop that ends leaves its listening socket
/// open, so the port still shows `LISTENING` and the kernel goes on completing handshakes into a
/// backlog nobody drains -- and then refuses every connect. fastcached's health endpoint answered
/// `200` for nine hours over a node whose compile surface was in exactly that state, because nothing
/// it read could tell. A loop that gives up (@c AcceptAction::GiveUp) says so here, and a liveness
/// probe that answers from @c AcceptLoopHealth::snapshot answers the question it is asked.
///
/// Origin: fastcached `src/FastCache/Transport/AcceptLoopHealth.hpp`.

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace core::net
{

/// One accept loop that gave up while it was meant to be serving.
struct StoppedSurface
{
    std::string surface; ///< What the loop served, as its log lines name it.
    std::string reason;  ///< What the last accept answered, in words.
};

/// The accept loops of one process that have given up, and who to tell when one does.
///
/// Thread-safe: an accept loop reports from its own thread, a probe reads from another.
class AcceptLoopHealth
{
  public:
    /// Told once per stopped surface, outside the lock, on the stopping loop's thread.
    using Listener = std::function<void(StoppedSurface const&)>;

    AcceptLoopHealth() = default;
    AcceptLoopHealth(AcceptLoopHealth const&) = delete;
    AcceptLoopHealth(AcceptLoopHealth&&) = delete;
    AcceptLoopHealth& operator=(AcceptLoopHealth const&) = delete;
    AcceptLoopHealth& operator=(AcceptLoopHealth&&) = delete;
    ~AcceptLoopHealth() = default;

    /// Names who is told when a surface stops: a later call replaces the listener for the stops
    /// that follow it. Called while the process is being assembled, before any accept loop runs.
    /// @param listener Told of each stop.
    void subscribe(Listener listener);

    /// Passes every stop recorded here on to @p target as well, the ones already recorded included.
    ///
    /// For a component that runs a loop of its own and owns its own registry, so a process still has
    /// ONE registry its liveness probe reads. Called once, while the process is being assembled.
    /// @param target Where stops are passed on to; must outlive this registry.
    void forward(AcceptLoopHealth& target);

    /// An accept loop gave up while its surface was meant to be serving.
    /// @param surface What the loop served.
    /// @param reason What the last accept answered, in words.
    void stopped(std::string_view surface, std::string_view reason);

    /// @return Every surface that has stopped, in the order they stopped; empty while all serve.
    [[nodiscard]] std::vector<StoppedSurface> snapshot() const;

  private:
    mutable std::mutex _mutex;
    std::vector<StoppedSurface> _stopped; ///< Guarded by `_mutex`.
    Listener _listener;                   ///< Guarded by `_mutex`; copied out before it is called.
    AcceptLoopHealth* _forward {};        ///< Guarded by `_mutex`; see `forward`.
};

} // namespace core::net
