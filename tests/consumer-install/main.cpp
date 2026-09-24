// SPDX-License-Identifier: Apache-2.0
//
// A program built against an installed core-cpp: the generated header came from the install, and
// the event loop, the host-driven backend and the platform's default backend are core::net's
// compiled code, linked from the installed archive. The host-driven loop is used because it runs
// the same on every platform, WebAssembly included, and needs no socket.

#include <core/Config.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/HostDrivenBackend.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/testing/ManualHostScheduler.hpp>

#include <cstdio>
#include <string_view>

int main()
{
    auto failed = 0;
    auto const expect = [&failed](bool condition, std::string_view what) {
        std::printf("%s: %.*s\n", condition ? "ok" : "FAILED", static_cast<int>(what.size()), what.data());
        failed += condition ? 0 : 1;
    };

    expect(std::string_view { CORE_CPP_VERSION_STRING } == CONSUMER_EXPECTED_VERSION,
           "the installed core/Config.hpp is the package's version");

    auto host = core::net::testing::ManualHostScheduler {};
    auto backend = core::net::HostDrivenBackend { host };
    auto loop = core::net::EventLoop { backend };
    auto ran = 0;
    loop.post([&ran] { ++ran; });
    host.pump();
    expect(ran == 1, "a posted callback ran on the host's pump");

    expect(core::net::makeDefaultBackend() != nullptr, "the platform's default backend was made");

    return failed == 0 ? 0 : 1;
}
