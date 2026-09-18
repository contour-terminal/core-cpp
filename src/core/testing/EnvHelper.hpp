// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Environment.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#ifdef _WIN32
    #include <stdlib.h> // _putenv_s
#endif

namespace core::testing
{

/// @brief Cross-platform setenv for tests.
///
/// Prefer injecting a @c core::testing::FakeEnvironment or a
/// @c core::platform::testing::TestEnvironmentProvider where the code under test takes one: the
/// process environment is shared by every thread and every test in the binary. This is for code
/// that reads the process environment itself, or passes it on to a child process.
///
/// On Windows it is `_putenv_s()`, which updates both the CRT's copy of the environment that
/// `getenv()` reads and the operating system's block that @c core::LiveEnvironment reads and a child
/// process inherits; there an empty @p value removes the variable. Elsewhere it is
/// @c core::setProcessEnvironmentVariable().
///
/// @param name Environment variable name.
/// @param value Environment variable value.
inline void setTestEnv(char const* name, char const* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    std::ignore = core::setProcessEnvironmentVariable(name, value);
#endif
}

/// @brief Cross-platform unsetenv for tests.
/// @param name Environment variable name to unset.
inline void unsetTestEnv(char const* name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    std::ignore = core::unsetProcessEnvironmentVariable(name);
#endif
}

/// @brief Sets an environment variable for a scope, restoring the previous value after.
///
/// The environment is process-global, so a test that sets a variable and restores it at the
/// end of the test body leaks that change whenever an assertion throws first. $HOME is the
/// one that bites here: other fixtures read it while constructing a shell, so a leaked
/// value silently redirects a later test's history or config to the wrong place.
class ScopedEnv
{
  public:
    /// @brief Sets @p name to @p value until this object goes out of scope.
    /// @param name  Environment variable name.
    /// @param value Value to set for the duration of the scope.
    ScopedEnv(std::string_view name, std::string_view value):
        _name { name }, _previous { core::LiveEnvironment {}.get(name) }
    {
        setTestEnv(_name.c_str(), std::string { value }.c_str());
    }

    ~ScopedEnv()
    {
        if (_previous)
            setTestEnv(_name.c_str(), _previous->c_str());
        else
            unsetTestEnv(_name.c_str());
    }

    ScopedEnv(ScopedEnv const&) = delete;
    ScopedEnv& operator=(ScopedEnv const&) = delete;
    ScopedEnv(ScopedEnv&&) = delete;
    ScopedEnv& operator=(ScopedEnv&&) = delete;

  private:
    std::string _name;
    std::optional<std::string> _previous;
};

} // namespace core::testing
