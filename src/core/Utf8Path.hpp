// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// A path from the UTF-8 text core-cpp's strings are spelled in, and that text back from a path.

#include <filesystem>
#include <string>
#include <string_view>

namespace core
{

/// @brief The path that UTF-8 @p text spells.
///
/// The one way a narrow string becomes a path in core-cpp: a command-line argument, an environment
/// value, a configuration value, a file name. A `char` is UTF-8 here, but
/// `std::filesystem::path`'s narrow constructor reads it in the platform's native narrow encoding,
/// which on Windows is the process's ANSI code page: on a CP-1252 host the two bytes of the `ü` in
/// `C:\Users\Jürgen` arrive as two characters, naming a directory that does not exist. This reads
/// @p text as UTF-8 whatever the process code page is. On POSIX a path is the bytes it is given, so
/// they pass through unchanged.
///
/// @param text The path, in UTF-8.
/// @return The path @p text spells.
/// @throws std::system_error on Windows, where a path is UTF-16, if @p text is not UTF-8 -- the
///         standard library's own conversion refuses it, as the narrow constructor does in a
///         process whose code page is UTF-8.
[[nodiscard]] inline std::filesystem::path pathFromUtf8(std::string_view text)
{
    // Through a std::u8string, which is UTF-8 by type. Not by reinterpret_cast to char8_t const*:
    // char8_t is not among the types that may alias a char.
    return std::filesystem::path { std::u8string(text.begin(), text.end()) };
}

/// @brief @p path in UTF-8, in its native format: the inverse of pathFromUtf8().
///
/// For a message, a log line or anything else that carries a path as text. Never
/// `path::string()`, which on Windows narrows through the ANSI code page: a name the code page can
/// spell comes out in that code page rather than in UTF-8, and one it cannot spell throws, so an
/// error that names the file throws instead of being reported (core-cpp#26).
///
/// @param path The path.
/// @return @p path in UTF-8, with the platform's own separators.
[[nodiscard]] inline std::string pathToUtf8(std::filesystem::path const& path)
{
    auto const spelled = path.u8string();
    return { spelled.begin(), spelled.end() };
}

} // namespace core
