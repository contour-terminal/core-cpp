// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Environment.hpp>

#include <filesystem>
#include <optional>

namespace core::platform
{

/// @brief Returns the user's home directory.
///
/// Tries HOME (Unix), then USERPROFILE (Windows).
///
/// @param environment The environment to read the variables from.
/// @return The home directory path, or std::nullopt if neither variable is set.
[[nodiscard]] inline auto homeDirectory(core::Environment const& environment)
    -> std::optional<std::filesystem::path>
{
    if (auto const home = environment.get("HOME"))
        return std::filesystem::path(*home);
    if (auto const home = environment.get("USERPROFILE"))
        return std::filesystem::path(*home);
    return std::nullopt;
}

/// @brief Returns the user's home directory, from the process environment as it is now.
/// @return The home directory path, or std::nullopt if neither HOME nor USERPROFILE is set.
[[nodiscard]] inline auto homeDirectory() -> std::optional<std::filesystem::path>
{
    return homeDirectory(core::LiveEnvironment {});
}

/// @brief Returns the user's configuration base directory.
///
/// On Unix: $XDG_CONFIG_HOME, or ~/.config if not set.
/// On Windows: $APPDATA (typically ~/AppData/Roaming).
///
/// @param environment The environment to read the variables from.
/// @return The configuration directory path, or std::nullopt if it cannot be determined.
[[nodiscard]] inline auto configHome(core::Environment const& environment)
    -> std::optional<std::filesystem::path>
{
    if (auto const xdg = environment.get("XDG_CONFIG_HOME"); xdg && !xdg->empty())
        return std::filesystem::path(*xdg);
    if (auto const appdata = environment.get("APPDATA"); appdata && !appdata->empty())
        return std::filesystem::path(*appdata);
    if (auto home = homeDirectory(environment))
        return *home / ".config";
    return std::nullopt;
}

/// @brief Returns the user's configuration base directory, from the process environment as it
/// is now.
/// @return The configuration directory path, or std::nullopt if it cannot be determined.
[[nodiscard]] inline auto configHome() -> std::optional<std::filesystem::path>
{
    return configHome(core::LiveEnvironment {});
}

} // namespace core::platform
