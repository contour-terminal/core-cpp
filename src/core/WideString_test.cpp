// SPDX-License-Identifier: Apache-2.0
//
// core::detail::toUtf8() is the one UTF-16 to UTF-8 conversion behind every wide Win32 read in
// core-cpp: an environment value, the environment's names, a thread's description, the host name.
// core::detail::toUtf16() is the other direction, behind every wide write and every path built from
// a string. Each case below tells one apart from a conversion that is wrong in one particular way.
#include <core/WideString.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>

using core::detail::toUtf16;
using core::detail::toUtf8;

namespace
{

/// The two halves of U+1F600. A lone surrogate has no `\u` spelling, so the cases build text from
/// code units.
constexpr auto HighSurrogate = wchar_t { 0xD83D };
constexpr auto LowSurrogate = wchar_t { 0xDE00 };

/// U+FFFD, the replacement character, in UTF-8.
constexpr auto Replacement = std::string_view { "\xEF\xBF\xBD" };

} // namespace

TEST_CASE("toUtf8 converts empty text to empty text", "[WideString][windows]")
{
    CHECK(toUtf8(L"").empty());
    CHECK(toUtf8(std::wstring_view {}).empty());
}

TEST_CASE("toUtf8 keeps ASCII as it is", "[WideString][windows]")
{
    CHECK(toUtf8(L"build-agent-07.example") == "build-agent-07.example");
}

TEST_CASE("toUtf8 encodes the BMP at two and three bytes", "[WideString][windows]")
{
    CHECK(toUtf8(L"Jürgen") == "J\xC3\xBCrgen");
    CHECK(toUtf8(L"Ω") == "\xCE\xA9");
    CHECK(toUtf8(L"€") == "\xE2\x82\xAC");
}

TEST_CASE("toUtf8 encodes a surrogate pair as one four-byte sequence", "[WideString][windows]")
{
    // Not CESU-8's two three-byte halves, which a conversion one code unit at a time produces.
    CHECK(toUtf8(std::wstring { HighSurrogate, LowSurrogate }) == "\xF0\x9F\x98\x80");
}

TEST_CASE("toUtf8 reads a lone surrogate as U+FFFD and converts the rest", "[WideString][windows]")
{
    auto const replaced = "a" + std::string { Replacement } + "z";
    CHECK(toUtf8(std::wstring { L'a', HighSurrogate, L'z' }) == replaced);
    CHECK(toUtf8(std::wstring { L'a', LowSurrogate, L'z' }) == replaced);
    // A pair in the wrong order is two lone surrogates.
    CHECK(toUtf8(std::wstring { LowSurrogate, HighSurrogate })
          == std::string { Replacement } + std::string { Replacement });
}

TEST_CASE("toUtf8 converts the whole view and an embedded NUL with it", "[WideString][windows]")
{
    CHECK(toUtf8(std::wstring_view { L"a\0b", 3 }) == std::string_view { "a\0b", 3 });
}

TEST_CASE("toUtf16 converts UTF-8 of every sequence length", "[WideString][windows]")
{
    CHECK(toUtf16("") == std::wstring {});
    CHECK(toUtf16("build-agent-07.example") == L"build-agent-07.example");
    CHECK(toUtf16("J\xC3\xBCrgen") == L"Jürgen");
    CHECK(toUtf16("\xE2\x82\xAC") == L"€");
    CHECK(toUtf16("\xF0\x9F\x98\x80") == std::wstring { HighSurrogate, LowSurrogate });
    CHECK(toUtf16(std::string_view { "a\0b", 3 }) == std::wstring_view { L"a\0b", 3 });
}

TEST_CASE("toUtf16 refuses text that is not UTF-8", "[WideString][windows]")
{
    // Strict, unlike toUtf8(): read as U+FFFD, such text would name another file or variable.
    CHECK_FALSE(toUtf16("J\xFCrgen").has_value());          // "Jürgen" in CP-1252
    CHECK_FALSE(toUtf16("a\x80z").has_value());             // a lone continuation byte
    CHECK_FALSE(toUtf16("a\xE2\x82").has_value());          // a sequence cut short
    CHECK_FALSE(toUtf16("a\xED\xA0\x80z").has_value());     // an encoded surrogate
    CHECK_FALSE(toUtf16("a\xC0\xAFz").has_value());         // an overlong form
    CHECK_FALSE(toUtf16("a\xF4\x90\x80\x80z").has_value()); // past U+10FFFF
}
