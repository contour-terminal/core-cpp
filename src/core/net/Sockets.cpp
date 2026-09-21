// SPDX-License-Identifier: Apache-2.0
#include <core/net/Sockets.hpp>

#include <core/net/EventLoop.hpp>
#include <core/net/IConnector.hpp>
#include <core/net/ThreadedAddressResolver.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace core::net
{

/// **contour's `connect()`, re-implemented over `makeConnector` rather than beside it.**
///
/// The body it replaces called `getaddrinfo` inline on whichever thread awaited it, which on an
/// event loop is a stall of unbounded length: a DNS lookup with a dead resolver is seconds, and
/// every coroutine on that loop waits for it, including the ones with nothing to do with the
/// network. What the caller sees is unchanged — same signature, same result type, same errors —
/// and what changed is whose thread pays for the lookup.
///
/// The connector is built per call rather than kept, and that is cheap by construction: it holds
/// two references and allocates nothing else. Keeping one would mean caching it per loop, which
/// is a lifetime question this function has no way to answer.
async::Task<SocketResult> connect(EventLoop* loop, std::string_view host, std::uint16_t port)
{
    co_return co_await connect(loop, host, port, &defaultAsyncResolver(), DialOptions {});
}

async::Task<SocketResult> connect(EventLoop* loop,
                                  std::string_view host,
                                  std::uint16_t port,
                                  IAsyncAddressResolver* resolver,
                                  DialOptions options)
{
    // The host is copied HERE rather than deeper down, because this is the frame that outlives
    // the call expression: `IConnector::connect` takes a `std::string` by value for exactly this
    // reason, and a `string_view` parameter names storage the caller may destroy before the first
    // suspend.
    auto const connector = makeConnector(*loop, *resolver);
    co_return co_await connector->connect(std::string { host }, port, options);
}

} // namespace core::net
