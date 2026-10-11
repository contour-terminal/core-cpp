// SPDX-License-Identifier: Apache-2.0
#include <core/WideString.hpp>

#include <cstddef>
#include <string>
#include <string_view>

#include <windows.h>

namespace core::detail
{

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

} // namespace core::detail
