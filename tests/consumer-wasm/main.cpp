// SPDX-License-Identifier: Apache-2.0
//
// What a WebAssembly consumer runs under node: a coroutine chain from core::async resumed by the
// program itself (there is no event loop in the subset yet), a value from core::base, and the
// error vocabulary of core::net_types, which is the part of core::net that builds everywhere.
//
// Task B5 replaces the hand-resumed root task here with a `delay` awaited on a host-driven loop
// under -sASYNCIFY, which is what a browser or node actually yields to.
//
// Every step reports through Checks, so a failure names the step rather than only an exit code.

#include <core/Base64.hpp>
#include <core/async/Task.hpp>
#include <core/net/NetError.hpp>

#include <iostream>
#include <string>
#include <string_view>

using core::async::Task;

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

/// The inner half of the chain: a task that produces a value.
Task<int> increment(int value)
{
    co_return value + 1;
}

/// The outer half: a task that awaits another task and works on what it produced.
Task<int> doubledIncrement(int value)
{
    auto const incremented = co_await increment(value);
    co_return incremented * 2;
}

/// core::async: a root task the program resumes itself, which is what a driver does.
void checkCoroutines(Checks& checks)
{
    auto task = doubledIncrement(20);
    task.handle().resume();
    checks.expect(task.done(), "the coroutine chain ran to completion");
    checks.expect(task.done() && task.result() == 42, "the coroutine chain produced its value");
}

/// core::base: code that has nothing to do with an operating system, and so builds everywhere.
void checkBase(Checks& checks)
{
    auto const encoded = core::base64::encode("core-cpp");
    checks.expect(core::base64::decode(encoded) == "core-cpp", "core::base64 round-tripped a string");
}

/// core::net_types: the error vocabulary, which is header-only and links nothing.
void checkNetTypes(Checks& checks)
{
    auto const error = core::net::makeNetError(core::net::NetErrorCode::ConnRefused, 0, "connect");
    checks.expect(error.toString().find("connection refused") != std::string::npos
                      && error.toString().find("connect") != std::string::npos,
                  "core::net_types named the error and its context");
}

} // namespace

int main()
{
    auto checks = Checks {};
    checkCoroutines(checks);
    checkBase(checks);
    checkNetTypes(checks);

    if (checks.failed() != 0)
    {
        std::cout << checks.failed() << " check(s) failed\n";
        return 1;
    }
    std::cout << "consumer-wasm: every check passed\n";
    return 0;
}
