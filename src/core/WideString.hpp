// SPDX-License-Identifier: Apache-2.0
#pragma once

// Private to core-cpp, and Windows only. This header is in no file set, so it is neither installed
// nor public API, and WideString.cpp, which defines what it declares, is in base's SOURCES_WINDOWS.
// Sources of core::base, core::platform and core::testing include it; no public header may.
// It sits directly in src/core/ rather than in a windows/ directory because base can have no
// directory of its own: the vendoring tool reads every directory under src/core/ as a module
// (cmake/CoreCppVendor.cmake), so a copy would leave one out.

#include <optional>
#include <string>
#include <string_view>

namespace core::detail
{

/// @brief Converts UTF-16, as a wide Win32 API answers, to the UTF-8 core-cpp's strings are spelled in.
///
/// The one conversion behind every wide read in core-cpp, so that no name or value goes through
/// the ANSI code page, which cannot spell most of Unicode (core-cpp#7). A surrogate pair becomes
/// one four-byte sequence; a lone surrogate, which UTF-8 cannot spell, becomes U+FFFD, and the
/// rest of @p text is still converted. All of @p text is converted, an embedded NUL included.
///
/// @param text UTF-16 text.
/// @return @p text in UTF-8; empty when @p text is.
[[nodiscard]] std::string toUtf8(std::wstring_view text);

/// @brief Converts UTF-8, as core-cpp's strings are spelled, to the UTF-16 a wide Win32 API takes.
///
/// The one conversion behind every wide Win32 write and every path built from a string in
/// core-cpp. Strict, unlike toUtf8(): text that is not UTF-8 -- a stray continuation byte, a
/// truncated sequence, an overlong form, an encoded surrogate, a code point past U+10FFFF -- fails
/// the whole conversion, because read as U+FFFD it would name another file, variable or value than
/// the one asked for. All of @p text is converted, an embedded NUL included.
///
/// @param text UTF-8 text.
/// @return @p text in UTF-16, empty when @p text is; std::nullopt where it is not UTF-8.
[[nodiscard]] std::optional<std::wstring> toUtf16(std::string_view text);

} // namespace core::detail
