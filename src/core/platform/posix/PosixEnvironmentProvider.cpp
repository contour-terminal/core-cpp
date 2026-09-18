// SPDX-License-Identifier: Apache-2.0
#include <core/platform/posix/PosixEnvironmentProvider.hpp>

#include <core/Environment.hpp>
#include <core/platform/PathUtils.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <unistd.h>

// glibc's <unistd.h> declares `environ` (C++ builds define _GNU_SOURCE); macOS reaches it
// through _NSGetEnviron(), and the other systems want it declared here.
#ifdef __APPLE__
    #include <crt_externs.h>
#elifndef __GLIBC__
extern "C" char** environ;
#endif

namespace core::platform
{

namespace
{
    /// @return The process's environment block, as child processes inherit it.
    [[nodiscard]] char** processEnviron() noexcept
    {
#ifdef __APPLE__
        return *_NSGetEnviron();
#else
        return environ;
#endif
    }
} // namespace

PosixEnvironmentProvider& PosixEnvironmentProvider::instance()
{
    static PosixEnvironmentProvider env;
    return env;
}

std::unique_ptr<EnvironmentProvider> nativeEnvironmentProvider()
{
    return std::make_unique<PosixEnvironmentProvider>();
}

void PosixEnvironmentProvider::set(std::string_view name, std::string_view value)
{
    _values[std::string(name)] = std::string(value);
}

std::optional<std::string> PosixEnvironmentProvider::get(std::string_view name) const
{
    if (auto i = _values.find(std::string(name)); i != _values.end())
        return i->second;
    return core::LiveEnvironment {}.get(name);
}

void PosixEnvironmentProvider::unset(std::string_view name)
{
    _values.erase(std::string(name));
    // The interface has no error channel; a name the environment cannot hold is not set there.
    std::ignore = core::unsetProcessEnvironmentVariable(name);
}

void PosixEnvironmentProvider::exportVariable(std::string_view name)
{
    if (auto i = _values.find(std::string(name)); i != _values.end())
        // The interface has no error channel; a name the environment cannot hold stays local.
        std::ignore = core::setProcessEnvironmentVariable(name, i->second);
}

std::vector<std::string> PosixEnvironmentProvider::keys() const
{
    std::vector<std::string> result;

    // First, collect from system environment
    auto* const* env = processEnviron();
    while (env != nullptr && *env != nullptr)
    {
        std::string_view const entry(*env);
        if (auto const pos = entry.find('='); pos != std::string_view::npos)
            result.emplace_back(entry.substr(0, pos));
        ++env;
    }

    // Add locally-set variables that might not be exported yet
    for (auto const& [key, _]: _values)
    {
        if (std::ranges::find(result, key) == result.end())
            result.push_back(key);
    }

    return result;
}

std::expected<void, PlatformError> PosixEnvironmentProvider::changeDirectory(
    std::filesystem::path const& path)
{
    if (chdir(path.c_str()) != 0)
        return std::unexpected(PlatformError::FileNotFound);
    return {};
}

std::string PosixEnvironmentProvider::currentDirectory() const
{
    return normalizePath(std::filesystem::current_path());
}

} // namespace core::platform
