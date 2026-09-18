// SPDX-License-Identifier: Apache-2.0
#include <core/Environment.hpp>
#include <core/platform/testing/TestEnvironmentProvider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#ifdef _WIN32
    #include <core/platform/windows/WindowsEnvironmentProvider.hpp>
#else
    #include <core/platform/posix/PosixEnvironmentProvider.hpp>
#endif

using namespace core::platform;

namespace
{
#ifdef _WIN32
using NativeEnvironmentProvider = WindowsEnvironmentProvider;
#else
using NativeEnvironmentProvider = PosixEnvironmentProvider;
#endif
} // namespace

TEST_CASE("TestEnvironmentProvider.set_and_get", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("FOO", "bar");
    auto const val = env.get("FOO");
    REQUIRE(val.has_value());
    CHECK(*val == "bar");
}

TEST_CASE("TestEnvironmentProvider.get_missing", "[platform]")
{
    TestEnvironmentProvider const env;
    CHECK(!env.get("MISSING").has_value());
}

TEST_CASE("TestEnvironmentProvider.unset", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("KEY", "value");
    env.unset("KEY");
    CHECK(!env.get("KEY").has_value());
}

TEST_CASE("TestEnvironmentProvider.keys", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("A", "1");
    env.set("B", "2");
    auto const keys = env.keys();
    CHECK(keys.size() == 2);
}

TEST_CASE("EnvironmentProvider.userName_prefers_USER", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("USER", "alice");
    env.set("LOGNAME", "bob");
    env.set("USERNAME", "carol");
    auto const name = env.userName();
    REQUIRE(name.has_value());
    CHECK(*name == "alice");
}

TEST_CASE("EnvironmentProvider.userName_falls_back_to_LOGNAME", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("LOGNAME", "bob");
    env.set("USERNAME", "carol");
    auto const name = env.userName();
    REQUIRE(name.has_value());
    CHECK(*name == "bob");
}

TEST_CASE("EnvironmentProvider.userName_falls_back_to_USERNAME", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("USERNAME", "carol");
    auto const name = env.userName();
    REQUIRE(name.has_value());
    CHECK(*name == "carol");
}

TEST_CASE("EnvironmentProvider.userName_skips_empty_values", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("USER", "");
    env.set("USERNAME", "carol");
    auto const name = env.userName();
    REQUIRE(name.has_value());
    CHECK(*name == "carol");
}

TEST_CASE("EnvironmentProvider.userName_missing", "[platform]")
{
    TestEnvironmentProvider const env;
    CHECK(!env.userName().has_value());
}

TEST_CASE("TestEnvironmentProvider.changeDirectory", "[platform]")
{
    TestEnvironmentProvider env("/home/user");
    CHECK(env.currentDirectory() == "/home/user");

    auto const result = env.changeDirectory("/tmp");
    CHECK(result.has_value());
    CHECK(env.currentDirectory() == "/tmp");
}

TEST_CASE("TestEnvironmentProvider.changeDirectory_invalid", "[platform]")
{
    TestEnvironmentProvider env("/home/user");
    env.addValidPath("/allowed");

    auto const result = env.changeDirectory("/forbidden");
    CHECK(!result.has_value());
    CHECK(result.error() == PlatformError::FileNotFound);
    CHECK(env.currentDirectory() == "/home/user");
}

TEST_CASE("the native EnvironmentProvider exports what it was set to", "[platform]")
{
    // The one provider that writes the process environment, the one a child process inherits
    // and LiveEnvironment reads. A variable set but not exported stays the provider's own.
    constexpr auto Name = "CORE_CPP_NATIVE_PROVIDER_TEST_VARIABLE";
    auto provider = NativeEnvironmentProvider {};
    auto const live = core::LiveEnvironment {};

    provider.set(Name, "local");
    CHECK(provider.get(Name) == "local");
    CHECK(!live.get(Name).has_value());

    provider.exportVariable(Name);
    CHECK(live.get(Name) == "local");
    auto const keys = provider.keys();
    CHECK(std::ranges::find(keys, std::string { Name }) != keys.end());

    provider.unset(Name);
    CHECK(!provider.get(Name).has_value());
    CHECK(!live.get(Name).has_value());
}

TEST_CASE("the native EnvironmentProvider reads what the process environment holds", "[platform]")
{
    // PATH exists on every platform this builds for, and the provider was never told it.
    auto const provider = NativeEnvironmentProvider {};
    CHECK(provider.get("PATH") == core::LiveEnvironment {}.get("PATH"));
}
