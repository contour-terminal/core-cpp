// SPDX-License-Identifier: Apache-2.0
#include <core/Environment.hpp>
#include <core/platform/EnvironmentProvider.hpp>
#include <core/platform/testing/TestEnvironmentProvider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <tuple>

#ifdef _WIN32
    #include <core/platform/windows/WindowsEnvironmentProvider.hpp>
#else
    #include <core/platform/posix/PosixEnvironmentProvider.hpp>
#endif

using namespace core::platform;

// Spelled the way AGENT.md's namespace-equals-directory rule says a header in platform/testing/
// must be reachable, and the way its two neighbours there already are. A consumer writing the
// qualified name by analogy gets exactly this line.
using core::platform::testing::TestEnvironmentProvider;

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

TEST_CASE("EnvironmentProvider.homeDirectory_prefers_HOME", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("HOME", "/home/alice");
    env.set("USERPROFILE", "C:/Users/alice");
    CHECK(env.homeDirectory() == std::filesystem::path("/home/alice"));
}

TEST_CASE("EnvironmentProvider.homeDirectory_falls_back_to_USERPROFILE", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("USERPROFILE", "C:/Users/alice");
    CHECK(env.homeDirectory() == std::filesystem::path("C:/Users/alice"));
}

TEST_CASE("EnvironmentProvider.homeDirectory_missing", "[platform]")
{
    TestEnvironmentProvider const env;
    CHECK(!env.homeDirectory().has_value());
}

TEST_CASE("EnvironmentProvider.configHome_prefers_XDG_CONFIG_HOME", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("XDG_CONFIG_HOME", "/xdg");
    env.set("APPDATA", "C:/AppData");
    env.set("HOME", "/home/alice");
    CHECK(env.configHome() == std::filesystem::path("/xdg"));
}

TEST_CASE("EnvironmentProvider.configHome_falls_back_to_APPDATA", "[platform]")
{
    // An empty XDG_CONFIG_HOME counts as unset, as the XDG specification says.
    TestEnvironmentProvider env;
    env.set("XDG_CONFIG_HOME", "");
    env.set("APPDATA", "C:/AppData");
    env.set("HOME", "/home/alice");
    CHECK(env.configHome() == std::filesystem::path("C:/AppData"));
}

TEST_CASE("EnvironmentProvider.configHome_falls_back_to_home_dot_config", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("HOME", "/home/alice");
    CHECK(env.configHome() == std::filesystem::path("/home/alice") / ".config");
}

TEST_CASE("EnvironmentProvider.configHome_missing", "[platform]")
{
    TestEnvironmentProvider const env;
    CHECK(!env.configHome().has_value());
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

TEST_CASE("nativeEnvironmentProvider is this platform's own provider", "[platform]")
{
    // The implementations are private (posix/, windows/); a composition root reaches them only
    // through the factory.
    auto const provider = nativeEnvironmentProvider();
    REQUIRE(provider != nullptr);
    CHECK(dynamic_cast<NativeEnvironmentProvider const*>(provider.get()) != nullptr);
}

TEST_CASE("the native EnvironmentProvider exports what it was set to", "[platform]")
{
    // The one provider that writes the process environment, the one a child process inherits
    // and LiveEnvironment reads. A variable set but not exported stays the provider's own.
    constexpr auto Name = "CORE_CPP_NATIVE_PROVIDER_TEST_VARIABLE";
    auto const provider = nativeEnvironmentProvider();
    REQUIRE(provider != nullptr);
    auto const live = core::LiveEnvironment {};

    provider->set(Name, "local");
    CHECK(provider->get(Name) == "local");
    CHECK(!live.get(Name).has_value());

    provider->exportVariable(Name);
    CHECK(live.get(Name) == "local");
    auto const keys = provider->keys();
    CHECK(std::ranges::find(keys, std::string { Name }) != keys.end());

    provider->unset(Name);
    CHECK(!provider->get(Name).has_value());
    CHECK(!live.get(Name).has_value());
}

TEST_CASE("the native EnvironmentProvider reads what the process environment holds", "[platform]")
{
    // PATH exists on every platform this builds for, and the provider was never told it.
    auto const provider = nativeEnvironmentProvider();
    REQUIRE(provider != nullptr);
    CHECK(provider->get("PATH") == core::LiveEnvironment {}.get("PATH"));
}

namespace
{
/// Removes a variable this file wrote, however the test case leaves.
///
/// The process environment is global to the binary, so a REQUIRE that throws past the cleanup
/// would leave the variable set for every test that runs afterwards.
struct WrittenVariable
{
    char const* name;
    ~WrittenVariable() { std::ignore = core::unsetProcessEnvironmentVariable(name); }
};
} // namespace

TEST_CASE("the native EnvironmentProvider reads an empty variable as set, not as absent", "[platform]")
{
    // An empty value is a variable that is set, which is what core::LiveEnvironment answers and
    // what Environment_test asserts of the writer. The Windows provider hand-rolled its own
    // GetEnvironmentVariableA call instead of delegating, and a return of 0 there cannot tell an
    // empty value from a missing name -- so two readers of the same Win32 block disagreed.
    constexpr auto Name = "CORE_CPP_NATIVE_PROVIDER_EMPTY_TEST_VARIABLE";
    auto const cleanup = WrittenVariable { .name = Name };
    auto const provider = nativeEnvironmentProvider();
    REQUIRE(provider != nullptr);

    REQUIRE(core::setProcessEnvironmentVariable(Name, "").has_value());
    REQUIRE(core::LiveEnvironment {}.get(Name) == "");
    CHECK(provider->get(Name) == "");

    REQUIRE(core::unsetProcessEnvironmentVariable(Name).has_value());
    CHECK(!provider->get(Name).has_value());
}

TEST_CASE("the native EnvironmentProvider reads a value longer than a page", "[platform]")
{
    // A reader that sizes a fixed buffer and ignores the "too small" return hands back a string
    // of NUL bytes instead of the value.
    constexpr auto Name = "CORE_CPP_NATIVE_PROVIDER_LONG_TEST_VARIABLE";
    auto const cleanup = WrittenVariable { .name = Name };
    auto const provider = nativeEnvironmentProvider();
    REQUIRE(provider != nullptr);

    auto const value = std::string(8192, 'v');
    REQUIRE(core::setProcessEnvironmentVariable(Name, value).has_value());
    CHECK(provider->get(Name) == value);
}
