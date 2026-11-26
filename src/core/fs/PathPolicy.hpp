// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// The name rules a file system applies to a relative path: one `constexpr` row per operating
/// system, so the walker reads data rather than asking which platform it runs on.

#include <core/fs/FsExport.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace core::fs
{

/// How a name reaches the operating system.
enum class PathEncoding : std::uint8_t
{
    Utf8Bytes, ///< As the UTF-8 bytes it is spelled in (POSIX).
    Utf16,     ///< Converted to UTF-16 (Windows).
};

/// Whether two names that differ only in case name one object.
enum class CaseRule : std::uint8_t
{
    Sensitive,   ///< They name two (POSIX).
    Insensitive, ///< They name one (Windows; ASCII letters are folded).
};

/// Whether a name may end in a dot or a space, which Windows strips from a name it opens, so that
/// `x.` and `x` would name one file.
enum class TrailingDotsAndSpaces : std::uint8_t
{
    Allowed, ///< An ordinary character at the end, too (POSIX).
    Refused, ///< `Invalid` (Windows).
};

/// The name rules of one file system.
struct PathPolicy
{
    std::string_view separators;       ///< What separates components of a path argument.
    std::string_view targetSeparators; ///< What separates components of a symbolic link's target.
    std::span<std::string_view const> reservedNames; ///< Device names refused as a name's stem, in any case.
    std::string_view volumeSeparators;               ///< What marks a symbolic link's target as naming a
                                                     ///< volume (`C:`), and so absolute, in its first
                                                     ///< component.
    std::string_view forbiddenCharacters;            ///< Characters a name may not hold, NUL aside, which
                                                     ///< no policy allows.
    PathEncoding encoding;                           ///< How a name reaches the system.
    CaseRule caseRule;                               ///< Whether case distinguishes names.
    TrailingDotsAndSpaces trailing;                  ///< Whether a name may end in `.` or a space.
};

/// @return The POSIX rules: `/` separates, every other byte but NUL is a name's, names are case
///         sensitive.
[[nodiscard]] CORE_FS_API PathPolicy const& posixPathPolicy() noexcept;

/// @return The Windows rules: `/` and `\` separate; `<>:"|?*` and the control characters are
///         forbidden; `CON PRN AUX NUL COM0-9 LPT0-9 COM¹²³ LPT¹²³ CONIN$ CONOUT$` are reserved as a
///         name's stem; a trailing dot or space is refused; names are case insensitive.
[[nodiscard]] CORE_FS_API PathPolicy const& windowsPathPolicy() noexcept;

/// @return The rules of the operating system this was built for, chosen by the build.
[[nodiscard]] CORE_FS_API PathPolicy const& nativePathPolicy() noexcept;

} // namespace core::fs
