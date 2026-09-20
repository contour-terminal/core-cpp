// SPDX-License-Identifier: Apache-2.0
//
// The halves of the consumer smoke programs that are the same for every way core-cpp is consumed:
// a loopback echo driven by two coroutine flows on one event loop, and a line through a core::log
// sink of the consumer's own. tests/consumer-cpm and tests/consumer-vendored each add what is
// theirs alone -- core::tui there, core::net_tls and the offline configure here -- and neither
// carries a second copy of this.
//
// It is a header of the CONSUMER's, not of core-cpp's: it is compiled with the consumer's warning
// set (-Wall -Wextra -Werror, or /W4 /WX) rather than behind SYSTEM, which is what makes it prove
// that core-cpp's headers compile under someone else's options.
//
// The vendored consumer's container mounts this directory next to its own, so the relative include
// resolves there as it does in a checkout.
#pragma once

#include <core/async/Task.hpp>
#include <core/async/WhenAll.hpp>
#include <core/log/LogStore.hpp>
#include <core/net/DefaultEventSource.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/IListener.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/Sockets.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace consumer::smoke
{

/// The bytes either flow moves at once. Both halves accumulate up to the greeting's length, so
/// this only has to be big enough for a greeting.
inline constexpr std::size_t BufferSize = 64;

/// What a program checked, so a failure names the step instead of only setting an exit code.
class Checks
{
  public:
    void expect(bool ok, std::string_view what)
    {
        std::cout << (ok ? "ok   " : "FAIL ") << what << '\n';
        if (!ok)
            ++_failed;
    }

    [[nodiscard]] int failed() const noexcept { return _failed; }

  private:
    int _failed = 0;
};

/// Bytes of @p text, as a socket write takes them.
[[nodiscard]] inline std::span<std::byte const> bytesOf(std::string_view text) noexcept
{
    return { reinterpret_cast<std::byte const*>(text.data()), text.size() };
}

/// Accepts one connection, reads @p expected bytes and writes them back.
///
/// The read is a loop, not one read(): a read() returns what one segment carried, so a greeting
/// that arrives split would otherwise be echoed as its first half, the client would wait for the
/// rest until the timeout, and the leg would fail with nothing to read in its output. Loopback
/// usually delivers a short write in one segment, which is what would make that an intermittent
/// red in a gating job rather than an obvious one.
inline core::async::Task<void> echoServer(core::net::IListener* listener, std::size_t expected, bool* served)
{
    auto accepted = co_await listener->accept();
    if (!accepted.has_value())
        co_return;
    auto connection = std::move(*accepted);

    auto buffer = std::array<std::byte, BufferSize> {};
    std::size_t total = 0;
    while (total < expected)
    {
        auto const read = co_await connection->read(std::span<std::byte> { buffer }.subspan(total));
        if (!read.has_value() || *read == 0)
            co_return;
        total += *read;
    }
    auto const written = co_await connection->write(std::span<std::byte const> { buffer }.subspan(0, total));
    *served = written.has_value() && *written == total;
}

/// Connects to the listener, sends @p greeting and compares what comes back.
inline core::async::Task<void> echoClient(core::net::EventLoop* loop,
                                          std::uint16_t port,
                                          std::string_view greeting,
                                          bool* matched)
{
    auto connected = co_await core::net::connect(loop, "127.0.0.1", port);
    if (!connected.has_value())
        co_return;
    auto socket = std::move(*connected);

    auto const written = co_await socket->write(bytesOf(greeting));
    if (!written.has_value() || *written != greeting.size())
        co_return;

    auto buffer = std::array<std::byte, BufferSize> {};
    std::size_t total = 0;
    while (total < greeting.size())
    {
        auto const read = co_await socket->read(std::span<std::byte> { buffer }.subspan(total));
        if (!read.has_value() || *read == 0)
            break;
        total += *read;
    }
    *matched = total == greeting.size() && std::memcmp(buffer.data(), greeting.data(), greeting.size()) == 0;
}

/// Runs both flows on the one loop, which is what makes this a round trip rather than two halves.
inline core::async::Task<void> loopbackEcho(core::net::EventLoop* loop,
                                            core::net::IListener* listener,
                                            std::string_view greeting,
                                            bool* served,
                                            bool* matched)
{
    co_await core::async::whenAll(echoServer(listener, greeting.size(), served),
                                  echoClient(loop, listener->localPort(), greeting, matched));
}

/// core::net and core::async: a real loopback socket, driven by two coroutine flows.
inline void checkLoopbackEcho(Checks& checks, std::string_view greeting)
{
    checks.expect(greeting.size() <= BufferSize, "the greeting fits one buffer of either flow");
    if (greeting.size() > BufferSize)
        return;

    auto source = core::net::makeDefaultEventSource();
    auto loop = core::net::EventLoop { *source };
    auto listener = core::net::listen(loop, "127.0.0.1", 0);
    checks.expect(listener.has_value(), "core::net::listen() bound an ephemeral loopback port");
    if (!listener.has_value())
        return;

    auto served = false;
    auto matched = false;
    loop.blockOn(loopbackEcho(&loop, listener->get(), greeting, &served, &matched));
    checks.expect(served, "the server flow echoed the whole request");
    checks.expect(matched, "the client flow read its own bytes back");
    (*listener)->close();
}

/// core::log: a category of the consumer's own, writing through a sink of the consumer's own.
inline void checkLogging(Checks& checks, std::string_view who)
{
    auto captured = std::string {};
    {
        auto sink = core::log::Sink { true, [&captured](std::string_view const& line) { captured += line; } };
        auto category = core::log::Category { "consumer",
                                              "The consumer's own category",
                                              core::log::Category::State::Enabled };
        category.setSink(sink);
        category()("a line from {}", who);
    }
    checks.expect(captured.find(std::string { "a line from " } + std::string { who }) != std::string::npos,
                  "core::log wrote the line through the consumer's sink");
}

} // namespace consumer::smoke
