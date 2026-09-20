// SPDX-License-Identifier: Apache-2.0
//
// A consumer's own code, using four core-cpp modules and compiled with the consumer's warning set
// (-Wall -Wextra -Werror, or /W4 /WX): a coroutine flow on core::net's event loop, a loopback echo
// over a real socket, a line through a core::log sink of this program's own, and a screen composed
// onto core::tui's MockTerminalOutput.
//
// core-cpp is added with SYSTEM YES, so its headers are outside those flags; this file is not, and
// a core-cpp header that only compiles with core-cpp's own options would fail here.
//
// Every step reports through Checks, so a failure names the step rather than only an exit code.

#include <core/async/Task.hpp>
#include <core/async/WhenAll.hpp>
#include <core/log/LogStore.hpp>
#include <core/net/DefaultEventSource.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/IListener.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/Sockets.hpp>
#include <core/tui/MockTerminalOutput.hpp>
#include <core/tui/TerminalOutput.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

using core::async::Task;
using core::net::EventLoop;
using core::net::IListener;

namespace
{

/// What this program checked, so a failure names the step instead of only setting an exit code.
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
[[nodiscard]] std::span<std::byte const> bytesOf(std::string_view text) noexcept
{
    return { reinterpret_cast<std::byte const*>(text.data()), text.size() };
}

/// Accepts one connection and writes back what it read.
Task<void> echoServer(IListener* listener, bool* served)
{
    auto accepted = co_await listener->accept();
    if (!accepted.has_value())
        co_return;
    auto connection = std::move(*accepted);

    auto buffer = std::array<std::byte, 64> {};
    auto const read = co_await connection->read(buffer);
    if (!read.has_value() || *read == 0)
        co_return;
    auto const written = co_await connection->write(std::span<std::byte const> { buffer }.subspan(0, *read));
    *served = written.has_value() && *written == *read;
}

/// Connects to the listener, sends a greeting and compares what comes back.
Task<void> echoClient(EventLoop* loop, std::uint16_t port, bool* matched)
{
    static constexpr auto Greeting = std::string_view { "hello from a consumer" };

    auto connected = co_await core::net::connect(loop, "127.0.0.1", port);
    if (!connected.has_value())
        co_return;
    auto socket = std::move(*connected);

    auto const written = co_await socket->write(bytesOf(Greeting));
    if (!written.has_value() || *written != Greeting.size())
        co_return;

    auto buffer = std::array<std::byte, 64> {};
    std::size_t total = 0;
    while (total < Greeting.size())
    {
        auto const read = co_await socket->read(std::span<std::byte> { buffer }.subspan(total));
        if (!read.has_value() || *read == 0)
            break;
        total += *read;
    }
    *matched = total == Greeting.size() && std::memcmp(buffer.data(), Greeting.data(), Greeting.size()) == 0;
}

/// Runs both flows on the one loop, which is what makes this a round trip rather than two halves.
Task<void> loopbackEcho(EventLoop* loop, IListener* listener, bool* served, bool* matched)
{
    co_await core::async::whenAll(echoServer(listener, served),
                                  echoClient(loop, listener->localPort(), matched));
}

/// core::net and core::async: a real loopback socket, driven by two coroutine flows.
void checkLoopbackEcho(Checks& checks)
{
    auto source = core::net::makeDefaultEventSource();
    auto loop = EventLoop { *source };
    auto listener = core::net::listen(loop, "127.0.0.1", 0);
    checks.expect(listener.has_value(), "core::net::listen() bound an ephemeral loopback port");
    if (!listener.has_value())
        return;

    auto served = false;
    auto matched = false;
    loop.blockOn(loopbackEcho(&loop, listener->get(), &served, &matched));
    checks.expect(served, "the server flow echoed the request");
    checks.expect(matched, "the client flow read its own bytes back");
    (*listener)->close();
}

/// core::log: a category of the consumer's own, writing through a sink of the consumer's own.
void checkLogging(Checks& checks)
{
    auto captured = std::string {};
    {
        auto sink = core::log::Sink { true, [&captured](std::string_view const& line) { captured += line; } };
        auto category = core::log::Category { "consumer",
                                              "The consumer's own category",
                                              core::log::Category::State::Enabled };
        category.setSink(sink);
        category()("a line from {}", "a consumer");
    }
    checks.expect(captured.find("a line from a consumer") != std::string::npos,
                  "core::log wrote the line through the consumer's sink");
}

/// core::tui: composing onto the mock output, which is what a consumer's own tests render against.
void checkTerminalOutput(Checks& checks)
{
    auto output = core::tui::MockTerminalOutput { 80, 24 };
    output.moveTo(4, 6); // moveTo is 1-based; the mock reports the cursor 0-based.
    checks.expect(output.cursorRow() == 3 && output.cursorCol() == 5,
                  "core::tui::MockTerminalOutput tracked the cursor");

    output.writeHyperlink("core-cpp", "https://example.invalid/core-cpp", core::tui::Style {});
    auto const runs = output.hyperlinkRuns();
    checks.expect(runs.size() == 1 && runs.front().text == "core-cpp"
                      && runs.front().url == "https://example.invalid/core-cpp",
                  "core::tui composed the hyperlink run");
}

} // namespace

int main()
{
    auto checks = Checks {};
    checkLoopbackEcho(checks);
    checkLogging(checks);
    checkTerminalOutput(checks);

    if (checks.failed() != 0)
    {
        std::cout << checks.failed() << " check(s) failed\n";
        return 1;
    }
    std::cout << "consumer-cpm: every check passed\n";
    return 0;
}
