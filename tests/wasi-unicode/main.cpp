// SPDX-License-Identifier: Apache-2.0
//
// core::tui's width and grapheme tables, asked from inside a WebAssembly guest: the text is split
// into grapheme clusters by libunicode and measured by core::tui::stringWidth() -- the same source
// (src/core/tui/Unicode.cpp) and the same libunicode tables a guest's TUI measures with. Each row
// says what a terminal shows, so a table built wrong for the guest (or not at all) reads as a row
// that disagrees, not as a crash.
//
// No test framework: this must build with -fno-exceptions -fno-rtti for wasm32-wasip1, and run
// under a WASI runtime that offers nothing but standard output and an exit code.

#include <core/tui/Unicode.hpp>

#include <libunicode/utf8_grapheme_segmenter.h>

#include <array>
#include <cstdio>
#include <string_view>

namespace
{

/// One row: a UTF-8 text, the grapheme clusters it is, and the columns it takes.
struct Row
{
    std::string_view name;
    std::string_view text;
    int clusters;
    int width;
};

// clang-format off
constexpr auto Rows = std::array {
    Row { .name = "ASCII letter",                  .text = "A",                         .clusters = 1, .width = 1 },
    Row { .name = "ASCII with a space",            .text = "a b",                       .clusters = 3, .width = 3 },
    Row { .name = "CJK ideograph U+4E2D",          .text = "中",                    .clusters = 1, .width = 2 },
    Row { .name = "Hangul syllable U+D55C",        .text = "한",                    .clusters = 1, .width = 2 },
    Row { .name = "halfwidth katakana U+FF71",     .text = "ｱ",                    .clusters = 1, .width = 1 },
    Row { .name = "emoji U+1F600",                 .text = "\U0001F600",                .clusters = 1, .width = 2 },
    Row { .name = "e + combining acute U+0301",    .text = "é",                   .clusters = 1, .width = 1 },
    Row { .name = "regional-indicator flag DE",    .text = "\U0001F1E9\U0001F1EA",      .clusters = 1, .width = 2 },
    Row { .name = "ZWJ family U+1F468 U+1F469 U+1F467",
          .text = "\U0001F468‍\U0001F469‍\U0001F467",                         .clusters = 1, .width = 2 },
    Row { .name = "emoji with skin tone U+1F44D U+1F3FD",
          .text = "\U0001F44D\U0001F3FD",                                               .clusters = 1, .width = 2 },
    Row { .name = "mixed",                         .text = "a中é\U0001F600",  .clusters = 4, .width = 6 },
};
// clang-format on

/// @return How many grapheme clusters libunicode splits @p text into.
[[nodiscard]] int countClusters(std::string_view text) noexcept
{
    auto count = 0;
    for ([[maybe_unused]] auto const& cluster: unicode::utf8_grapheme_segmenter(text))
        ++count;
    return count;
}

} // namespace

int main()
{
    auto failures = 0;
    for (auto const& row: Rows)
    {
        auto const clusters = countClusters(row.text);
        auto const width = core::tui::stringWidth(row.text);
        auto const ok = clusters == row.clusters && width == row.width;
        if (!ok)
            ++failures;
        std::printf("%s %-40.*s clusters %d (want %d), width %d (want %d)\n",
                    ok ? "ok  " : "FAIL",
                    static_cast<int>(row.name.size()),
                    row.name.data(),
                    clusters,
                    row.clusters,
                    width,
                    row.width);
    }
    std::printf("%d of %zu rows as expected\n", static_cast<int>(Rows.size()) - failures, Rows.size());
    return failures == 0 ? 0 : 1;
}
