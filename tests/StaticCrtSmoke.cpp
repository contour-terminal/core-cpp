// SPDX-License-Identifier: Apache-2.0
//
// A program built against the static C runtime (/MT), linking core-cpp's static-CRT twins
// (CORE_CPP_MSVC_STATIC_RUNTIME_VARIANTS): core::net_mt, and through it platform_mt and base_mt,
// plus core::log_mt. fastcached's fastcache-cc is such a program, in the same build as a /MD daemon.
//
// Built twice by tests/CMakeLists.txt: as core-cpp-static-crt-smoke, which links the twins and must
// link and run -- a loopback echo and a log line -- and as core-cpp-static-crt-mismatch, which links
// the ordinary /MD libraries and must FAIL to link. The second is what the twins exist for, and it is
// checked rather than assumed, so a build that stopped recording its C runtime would be noticed.

#include <iostream>
#include <string_view>

#include "consumer-shared/ConsumerSmoke.hpp"

int main()
{
    auto checks = consumer::smoke::Checks {};
    consumer::smoke::checkLoopbackEcho(checks, std::string_view { "hello from a /MT program" });
    consumer::smoke::checkLogging(checks, std::string_view { "a /MT program" });

    if (checks.failed() != 0)
    {
        std::cout << checks.failed() << " check(s) failed\n";
        return 1;
    }
    std::cout << "static-crt-smoke: every check passed\n";
    return 0;
}
