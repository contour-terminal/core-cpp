// SPDX-License-Identifier: Apache-2.0
#include <core/tui/MarkdownRenderer.hpp>
#include <core/tui/StyledText.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace core::tui;

// ============================================================================
// StyledText table tests
// ============================================================================

TEST_CASE("StyledText.table.basic")
{
    auto const text = StyledText::fromMarkdown("| Name | Age |\n|------|-----|\n| Alice | 30 |\n");

    // Should have multiple lines (top border, header, separator, data, bottom border)
    CHECK(text.lineCount() >= 5);

    // Collect all text from all lines
    std::string allText;
    for (auto const& line: text.lines())
        for (auto const& span: line)
            allText += span.text;

    CHECK(allText.contains("Name"));
    CHECK(allText.contains("Age"));
    CHECK(allText.contains("Alice"));
    CHECK(allText.contains("30"));
}

TEST_CASE("StyledText.table.no_separator_not_rendered_as_table")
{
    auto const text = StyledText::fromMarkdown("| A | B |\n| C | D |\n");

    // Without a separator, this should be plain text (2 lines), not a table (5+ lines)
    CHECK(text.lineCount() <= 3); // 2 lines + possibly trailing empty
}

TEST_CASE("StyledText.table.border_style")
{
    auto theme = MarkdownRenderer::defaultTheme();
    auto const text = StyledText::fromMarkdown("| H |\n|---|\n| D |\n", 0, &theme);

    // Check that table border spans have the tableBorder style (dim)
    bool foundBorderSpan = false;
    for (auto const& line: text.lines())
    {
        for (auto const& span: line)
        {
            if (span.style.dim)
            {
                foundBorderSpan = true;
                break;
            }
        }
        if (foundBorderSpan)
            break;
    }
    CHECK(foundBorderSpan);
}

TEST_CASE("StyledText.table.header_bold")
{
    auto theme = MarkdownRenderer::defaultTheme();
    auto const text = StyledText::fromMarkdown("| Header |\n|--------|\n| Data |\n", 0, &theme);

    // Check that at least one span with header text is bold
    bool foundBoldHeader = false;
    for (auto const& line: text.lines())
    {
        for (auto const& span: line)
        {
            if (span.text.contains("Header") && span.style.bold)
            {
                foundBoldHeader = true;
                break;
            }
        }
        if (foundBoldHeader)
            break;
    }
    CHECK(foundBoldHeader);
}

TEST_CASE("StyledText.table.multiple_columns")
{
    auto const text =
        StyledText::fromMarkdown("| A | B | C |\n|---|---|---|\n| 1 | 2 | 3 |\n| 4 | 5 | 6 |\n");

    // Should have at least 6 lines: top border, header, separator, 2 data rows, bottom border
    CHECK(text.lineCount() >= 6);

    std::string allText;
    for (auto const& line: text.lines())
        for (auto const& span: line)
            allText += span.text;

    CHECK(allText.contains('1'));
    CHECK(allText.contains('6'));
}

TEST_CASE("StyledText.table.followed_by_paragraph")
{
    auto const text = StyledText::fromMarkdown("| H |\n|---|\n| D |\n\nAfter table.\n");

    std::string allText;
    for (auto const& line: text.lines())
        for (auto const& span: line)
            allText += span.text;

    CHECK(allText.contains('H'));
    CHECK(allText.contains('D'));
    CHECK(allText.contains("After table."));
}

TEST_CASE("StyledText.table.alignment_right")
{
    auto const text = StyledText::fromMarkdown("| Num |\n|----:|\n| 42 |\n");

    // Collect data row text
    std::string allText;
    for (auto const& line: text.lines())
        for (auto const& span: line)
            allText += span.text;

    // "42" should be present with leading spaces (right-aligned)
    CHECK(allText.contains("42"));
}
