// SPDX-License-Identifier: Apache-2.0
#include <core/WideString.hpp>

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#include <windows.h>

namespace core
{

std::expected<std::wstring, std::error_code> toUtf16(std::string_view text)
{
    // MultiByteToWideChar() fails for an empty input, whose answer needs no call.
    if (text.empty())
        return std::wstring {};
    // MB_ERR_INVALID_CHARS fails the conversion where the text is not UTF-8, rather than putting
    // U+FFFD there. The view is not NUL-terminated, so each call is told its size.
    auto const length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
        return std::unexpected(std::make_error_code(std::errc::illegal_byte_sequence));
    auto utf16 = std::wstring(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), utf16.data(), length);
    return utf16;
}

std::string toUtf8(std::wstring_view text)
{
    if (text.empty())
        return {};
    // No flags: with WC_ERR_INVALID_CHARS a lone surrogate would fail the whole conversion, where
    // without it the surrogate reads as U+FFFD and the rest of the text survives. The view is not
    // NUL-terminated, so each call is told its size.
    auto const length = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    auto utf8 = std::string(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

} // namespace core
