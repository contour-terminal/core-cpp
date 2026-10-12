// SPDX-License-Identifier: Apache-2.0
//
// core::pathFromUtf8() and core::pathToUtf8() carry a path between core-cpp's UTF-8 strings and
// std::filesystem::path. What each case tells them apart from is the narrow constructor and
// path::string(), which on Windows read and write the ANSI code page: those agree with UTF-8 on
// ASCII and on nothing else, so every case that can fail spells a name beyond it. A path built from
// a u8 literal is UTF-8 by type on every platform, which is what the cases compare against.
#include <core/Utf8Path.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string_view>
#include <system_error>

using namespace std::string_view_literals;

namespace
{

/// "Jürgen": U+00FC is two bytes in UTF-8, which a CP-1252 host reads as two characters.
constexpr auto Juergen = "J\xC3\xBCrgen"sv;

/// "Ω-日本-😀": a Greek letter, two CJK ideographs and a character beyond the BMP, which UTF-16
/// spells as a surrogate pair. No ANSI code page spells all of them.
constexpr auto Mixed = "\xCE\xA9-\xE6\x97\xA5\xE6\x9C\xAC-\xF0\x9F\x98\x80"sv;

} // namespace

TEST_CASE("pathFromUtf8 reads its text as UTF-8", "[Utf8Path]")
{
    CHECK(core::pathFromUtf8(Juergen) == std::filesystem::path { u8"Jürgen" });
    CHECK(core::pathFromUtf8(Mixed) == std::filesystem::path { u8"Ω-日本-😀" });
    CHECK(core::pathFromUtf8("C:/Users/J\xC3\xBCrgen/state")
          == std::filesystem::path { u8"C:/Users/Jürgen/state" });
}

TEST_CASE("pathFromUtf8 keeps ASCII and the empty path as they are", "[Utf8Path]")
{
    CHECK(core::pathFromUtf8("").empty());
    CHECK(core::pathFromUtf8("logs/daemon.log") == std::filesystem::path { "logs/daemon.log" });
}

TEST_CASE("pathToUtf8 spells a path in UTF-8", "[Utf8Path]")
{
    CHECK(core::pathToUtf8(std::filesystem::path { u8"Jürgen" }) == Juergen);
    CHECK(core::pathToUtf8(std::filesystem::path { u8"Ω-日本-😀" }) == Mixed);
    CHECK(core::pathToUtf8(std::filesystem::path {}).empty());
}

TEST_CASE("pathFromUtf8 and pathToUtf8 are each other's inverse", "[Utf8Path]")
{
    // Separators included: neither function rewrites one, so a path keeps its own.
    for (auto const text: { "a/b"sv, "a\\b"sv, Juergen, Mixed })
        CHECK(core::pathToUtf8(core::pathFromUtf8(text)) == text);
}

TEST_CASE("pathFromUtf8 on text that is not UTF-8", "[Utf8Path]")
{
    // "Jürgen" in CP-1252: 0xFC starts no UTF-8 sequence.
    constexpr auto Legacy = "J\xFCrgen"sv;
#ifdef _WIN32
    // A Windows path is UTF-16, so the bytes must be converted, and the conversion refuses them.
    CHECK_THROWS_AS(core::pathFromUtf8(Legacy), std::system_error);
#else
    // A POSIX path is bytes, so they are kept as they are.
    CHECK(core::pathFromUtf8(Legacy).native() == Legacy);
#endif
}
