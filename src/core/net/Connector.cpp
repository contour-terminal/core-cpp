// SPDX-License-Identifier: Apache-2.0
#include <core/net/ConnectFlow.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/IConnector.hpp>
#include <core/net/ReadinessDial.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace core::net
{

namespace
{
    /// The one connector this library ships.
    ///
    /// **One rather than one per backend.** fastcached had an `EpollConnector` and a
    /// `KqueueConnector` whose bodies were identical but for the reactor type they named, held
    /// together by a template; core-cpp has one @c EventLoop over an @c IoBackend, so the
    /// platform difference is four OS calls in `detail/DialPrimitives.hpp` and the connector has
    /// nothing left to vary over.
    ///
    /// The loop is a CONSTRUCTOR parameter rather than a per-call one: the socket handed back is
    /// pinned to one loop, so which loop is a property of the connector. Passing one per call
    /// would let a caller obtain a socket wired to a loop other than the one its coroutine runs
    /// on — a data race with no symptom until load.
    class ReadinessConnector final: public IConnector
    {
      public:
        /// @param loop The loop the returned sockets are pinned to; not owned.
        /// @param resolver The name-resolution seam; not owned.
        ReadinessConnector(EventLoop& loop, IAsyncAddressResolver& resolver) noexcept:
            _loop(loop), _resolver(resolver)
        {
        }

        /// @copydoc IConnector::connect
        [[nodiscard]] async::Task<SocketResult> connect(std::string host,
                                                        std::uint16_t port,
                                                        DialOptions options) override
        {
            co_return co_await detail::runConnectFlow(
                &_resolver, &_loop, &_loop.clock(), std::move(host), port, options, &dialStep, &_loop);
        }

      private:
        /// The @c detail::DialStep over the readiness dial.
        /// @param state The loop, as a `void*`.
        /// @param endpoint The candidate to dial.
        /// @param deadline When to give up on it.
        /// @param keepAlive Whether the connected socket probes a silent peer.
        /// @return The connected socket, or why this candidate did not produce one.
        static async::Task<SocketResult> dialStep(void* state,
                                                  ResolvedEndpoint endpoint,
                                                  platform::SteadyTimePoint deadline,
                                                  KeepAlive keepAlive)
        {
            co_return co_await detail::dialReadiness(
                static_cast<EventLoop*>(state), endpoint, deadline, keepAlive);
        }

        EventLoop& _loop;
        IAsyncAddressResolver& _resolver;
    };

} // namespace

std::unique_ptr<IConnector> makeConnector(EventLoop& loop, IAsyncAddressResolver& resolver)
{
    return std::make_unique<ReadinessConnector>(loop, resolver);
}

} // namespace core::net
