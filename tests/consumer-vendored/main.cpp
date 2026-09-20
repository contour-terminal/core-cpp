// SPDX-License-Identifier: Apache-2.0
//
// What a consumer with a vendored copy builds: a coroutine flow on core::net's event loop, a
// loopback echo over a real socket, a line through a core::log sink of this program's own, and
// core::net_tls, which is the one part that needs a system dependency (OpenSSL) rather than a
// fetched one. There is no core::tui here: CORE_CPP_WITH_TUI is off, as it is for a build that
// wants no libunicode.
//
// The first two are in ../consumer-shared/ConsumerSmoke.hpp, which the CPM consumer uses too; what
// is here is what only this consumer does, which is core::net_tls. The container this runs in
// mounts that directory next to this one, so the include resolves there as it does in a checkout.
//
// Every step reports through Checks, so a failure names the step rather than only an exit code.

#include <core/net/Tls.hpp>

#include <iostream>
#include <string_view>

#include "../consumer-shared/ConsumerSmoke.hpp"

using consumer::smoke::Checks;

namespace
{

/// core::net_tls: OpenSSL comes from the system, never from a fetch, so this is what proves the
/// offline build found it and linked it.
void checkTls(Checks& checks)
{
    checks.expect(core::net::constantTimeEquals("a token", "a token")
                      && !core::net::constantTimeEquals("a token", "another"),
                  "core::net_tls compared two secrets through OpenSSL");

    auto const context = core::net::makeTlsClientContext();
    checks.expect(context.has_value() && *context != nullptr, "core::net_tls built a client context");
}

} // namespace

int main()
{
    auto checks = Checks {};
    consumer::smoke::checkLoopbackEcho(checks, std::string_view { "hello from a vendored copy" });
    consumer::smoke::checkLogging(checks, std::string_view { "a vendored consumer" });
    checkTls(checks);

    if (checks.failed() != 0)
    {
        std::cout << checks.failed() << " check(s) failed\n";
        return 1;
    }
    std::cout << "consumer-vendored: every check passed\n";
    return 0;
}
