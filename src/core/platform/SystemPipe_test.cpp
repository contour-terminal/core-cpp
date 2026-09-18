// SPDX-License-Identifier: Apache-2.0
#include <core/platform/SystemPipe.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>

#ifndef _WIN32
    #include <ranges>

    #include <fcntl.h>
#endif

using core::platform::createSystemPipe;
using core::platform::InvalidHandle;

TEST_CASE("SystemPipe round-trips bytes between its ends", "[systempipe]")
{
    auto pipe = createSystemPipe();
    REQUIRE(pipe.has_value());
    REQUIRE((*pipe)->good());

    char const payload[] = "hello";
    auto const written = (*pipe)->write(payload, sizeof(payload));
    REQUIRE(written.has_value());
    REQUIRE(*written == sizeof(payload));

    auto buf = std::array<char, sizeof(payload)> {};
    auto const got = (*pipe)->read(buf.data(), buf.size());
    REQUIRE(got.has_value());
    REQUIRE(*got == sizeof(payload));
    REQUIRE(std::memcmp(buf.data(), payload, sizeof(payload)) == 0);
}

TEST_CASE("SystemPipe exposes a valid wait handle", "[systempipe]")
{
    auto pipe = createSystemPipe();
    REQUIRE(pipe.has_value());
    REQUIRE((*pipe)->waitHandle() != InvalidHandle);
    REQUIRE((*pipe)->readFd() != InvalidHandle);
    REQUIRE((*pipe)->writeFd() != InvalidHandle);
}

#ifndef _WIN32
TEST_CASE("SystemPipe ends are non-blocking and close-on-exec", "[systempipe]")
{
    // A producer must never stall on a full wakeup pipe, a loop's drain must never park on a
    // spurious readiness, and a child process must not inherit either end.
    auto pipe = createSystemPipe();
    REQUIRE(pipe.has_value());
    for (auto const fd: { (*pipe)->readFd(), (*pipe)->writeFd() })
    {
        CHECK((::fcntl(fd, F_GETFL) & O_NONBLOCK) != 0);
        CHECK((::fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0);
    }
}

TEST_CASE("SystemPipe reports a write into a full channel as done", "[systempipe]")
{
    // Nothing drains the channel here, so the socket buffer fills up. Each further byte would
    // only have signalled a wakeup that is already pending, so the write succeeds instead of
    // blocking the producer or failing it.
    auto pipe = createSystemPipe();
    REQUIRE(pipe.has_value());
    REQUIRE((::fcntl((*pipe)->writeFd(), F_GETFL) & O_NONBLOCK) != 0); // else this would block

    auto const chunk = std::array<char, 4096> {};
    for ([[maybe_unused]] auto const round: std::views::iota(0, 1024)) // 4 MiB, past any socket buffer
    {
        auto const written = (*pipe)->write(chunk.data(), chunk.size());
        REQUIRE(written.has_value());
        REQUIRE(*written > 0);
    }
}

TEST_CASE("SystemPipe read of an empty channel fails rather than blocking", "[systempipe]")
{
    auto pipe = createSystemPipe();
    REQUIRE(pipe.has_value());
    REQUIRE((::fcntl((*pipe)->readFd(), F_GETFL) & O_NONBLOCK) != 0); // else this would block

    auto buf = std::array<char, 16> {};
    auto const got = (*pipe)->read(buf.data(), buf.size());
    CHECK(!got.has_value());
}
#endif
