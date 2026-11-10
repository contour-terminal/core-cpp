// SPDX-License-Identifier: Apache-2.0
//
// connectNamedPipe against a pipe server made here with the Win32 API.

#include <core/async/Task.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/windows/NamedPipe.hpp>

#include <catch2/catch_test_macros.hpp>

// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

#include <array>
#include <cstddef>
#include <string>
#include <thread>
#include <tuple>

using core::net::NetErrorCode;

TEST_CASE("connectNamedPipe: a pipe nobody serves is refused")
{
    auto backend = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(backend != nullptr);
    auto loop = core::net::EventLoop { *backend };

    auto const socket = core::net::detail::connectNamedPipe(loop, LR"(\\.\pipe\core-cpp-no-such-pipe)");
    REQUIRE_FALSE(socket.has_value());
    CHECK(socket.error().code == NetErrorCode::ConnRefused);
}

TEST_CASE("connectNamedPipe: bytes written reach the server, and its reply and hang-up come back")
{
    auto backend = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(backend != nullptr);
    auto loop = core::net::EventLoop { *backend };

    auto const name =
        std::wstring { LR"(\\.\pipe\core-cpp-named-pipe-test-)" } + std::to_wstring(::GetCurrentProcessId());
    auto const server = ::CreateNamedPipeW(
        name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 256, 256, 0, nullptr);
    REQUIRE(server != INVALID_HANDLE_VALUE);

    auto serving = std::thread { [server] {
        std::ignore = ::ConnectNamedPipe(server, nullptr);
        auto request = std::array<char, 4> {};
        auto got = DWORD { 0 };
        std::ignore = ::ReadFile(server, request.data(), 4, &got, nullptr);
        auto wrote = DWORD { 0 };
        std::ignore = ::WriteFile(server, "pong", 4, &wrote, nullptr);
        std::ignore = ::FlushFileBuffers(server);
        ::DisconnectNamedPipe(server);
    } };

    auto socket = core::net::detail::connectNamedPipe(loop, name);
    REQUIRE(socket.has_value());
    auto const ping =
        std::array { std::byte { 'p' }, std::byte { 'i' }, std::byte { 'n' }, std::byte { 'g' } };
    auto reply = std::array<std::byte, 8> {};
    auto const task = [&]() -> core::async::Task<void> {
        auto const wrote = co_await (*socket)->write(ping);
        REQUIRE(wrote.has_value());
        auto const got = co_await (*socket)->read(reply);
        REQUIRE(got.has_value());
        CHECK(*got == 4);
        auto const eof = co_await (*socket)->read(reply);
        REQUIRE(eof.has_value());
        CHECK(*eof == 0);
    };
    loop.blockOn(task());
    serving.join();
    ::CloseHandle(server);
}
