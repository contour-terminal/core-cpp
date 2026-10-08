// SPDX-License-Identifier: Apache-2.0
#include <core/tui/ClipboardProtocol.hpp>

#include <core/Base64.hpp>

#include <algorithm>
#include <array>
#include <ranges>

namespace core::tui
{

namespace
{
    using namespace std::string_view_literals;

    /// @brief How each selection is named in the two protocols.
    struct TargetRow
    {
        ClipboardTarget target;
        std::string_view osc52Selector;   ///< OSC 52's selection parameter.
        std::string_view osc5522Location; ///< The metadata OSC 5522's write header carries.
    };

    constexpr auto TargetTable = std::array {
        TargetRow { .target = ClipboardTarget::Clipboard, .osc52Selector = "c"sv, .osc5522Location = ""sv },
        TargetRow {
            .target = ClipboardTarget::Primary, .osc52Selector = "p"sv, .osc5522Location = ":loc=primary"sv },
    };

    /// @brief One error: the OSC 5522 status code that reports it, if any, and its description.
    struct ErrorRow
    {
        ClipboardWriteError error;
        std::string_view wireCode; ///< Empty for an error no terminal reports.
        std::string_view message;
    };

    constexpr auto ErrorTable = std::array {
        ErrorRow { .error = ClipboardWriteError::NoTerminal,
                   .wireCode = ""sv,
                   .message = "no controlling terminal to send the clipboard sequence to"sv },
        ErrorRow { .error = ClipboardWriteError::UnsupportedMimeType,
                   .wireCode = ""sv,
                   .message = "terminal only supports OSC 52 (plain text); cannot copy this MIME type"sv },
        ErrorRow { .error = ClipboardWriteError::PermissionDenied,
                   .wireCode = "EPERM"sv,
                   .message = "terminal refused clipboard access (EPERM)"sv },
        ErrorRow { .error = ClipboardWriteError::TooLarge,
                   .wireCode = "EFBIG"sv,
                   .message = "data too large for the terminal's clipboard (EFBIG)"sv },
        ErrorRow { .error = ClipboardWriteError::Busy,
                   .wireCode = "EBUSY"sv,
                   .message = "terminal clipboard busy (EBUSY)"sv },
        ErrorRow { .error = ClipboardWriteError::InvalidData,
                   .wireCode = "EINVAL"sv,
                   .message = "terminal rejected the clipboard data (EINVAL)"sv },
        ErrorRow { .error = ClipboardWriteError::IoError,
                   .wireCode = "EIO"sv,
                   .message = "terminal reported an I/O error, or writing to it failed"sv },
        ErrorRow { .error = ClipboardWriteError::PrimaryUnavailable,
                   .wireCode = "ENOSYS"sv,
                   .message = "terminal has no primary selection (ENOSYS)"sv },
        ErrorRow { .error = ClipboardWriteError::NoConfirmation,
                   .wireCode = ""sv,
                   .message = "terminal did not confirm the copy in time"sv },
    };

    constexpr auto OscIntroducer = "\033]"sv;
    constexpr auto StringTerminator = "\033\\"sv;
    constexpr auto Osc5522Prefix = "5522;"sv;

    auto targetRow(ClipboardTarget target) noexcept -> TargetRow const&
    {
        return *std::ranges::find(TargetTable, target, &TargetRow::target);
    }

    /// @brief The value of @p key in OSC 5522 metadata (`key=value:key=value`), if present.
    auto metadataValue(std::string_view metadata, std::string_view key) -> std::optional<std::string_view>
    {
        for (auto const field: metadata | std::views::split(':'))
        {
            auto const pair = std::string_view { field.begin(), field.end() };
            auto const eq = pair.find('=');
            if (eq != std::string_view::npos && pair.substr(0, eq) == key)
                return pair.substr(eq + 1);
        }
        return std::nullopt;
    }

    auto appendOsc5522Packet(std::string& out, std::string_view metadata, std::string_view payload) -> void
    {
        out += OscIntroducer;
        out += Osc5522Prefix;
        out += metadata;
        if (!payload.empty() || metadata.contains("mime="))
        {
            out += ';';
            out += payload;
        }
        out += StringTerminator;
    }

} // namespace

auto encodeOsc52(std::string_view data, ClipboardTarget target) -> std::string
{
    auto out = std::string { OscIntroducer };
    out += "52;";
    out += targetRow(target).osc52Selector;
    out += ';';
    out += base64::encode(data);
    out += StringTerminator;
    return out;
}

auto encodeOsc5522Write(std::string_view data, std::string_view mime, ClipboardTarget target) -> std::string
{
    auto out = std::string {};
    appendOsc5522Packet(out, std::string { "type=write" }.append(targetRow(target).osc5522Location), {});

    auto const dataHeader = std::string { "type=wdata:mime=" }.append(base64::encode(mime));
    if (data.empty())
        appendOsc5522Packet(out, dataHeader, {});
    for (auto const chunk: data | std::views::chunk(Osc5522ChunkSize))
        appendOsc5522Packet(out, dataHeader, base64::encode(std::string_view { chunk.begin(), chunk.end() }));

    appendOsc5522Packet(out, "type=wdata", {});
    return out;
}

auto parseOsc5522WriteStatus(std::string_view oscPayload)
    -> std::optional<std::expected<void, ClipboardWriteError>>
{
    if (!oscPayload.starts_with(Osc5522Prefix))
        return std::nullopt;

    auto const metadata = oscPayload.substr(Osc5522Prefix.size());
    auto const status = metadataValue(metadata, "status");
    if (metadataValue(metadata, "type") != "write" || !status)
        return std::nullopt;

    if (*status == "DONE")
        return std::expected<void, ClipboardWriteError> {};

    auto const row = std::ranges::find(ErrorTable, *status, &ErrorRow::wireCode);
    auto const error = row != ErrorTable.end() ? row->error : ClipboardWriteError::IoError;
    return std::expected<void, ClipboardWriteError> { std::unexpect, error };
}

auto describe(ClipboardWriteError error) noexcept -> std::string_view
{
    return std::ranges::find(ErrorTable, error, &ErrorRow::error)->message;
}

auto isPlainTextMimeType(std::string_view mime) noexcept -> bool
{
    constexpr auto PlainText = "text/plain"sv;
    return mime == PlainText
           || (mime.starts_with(PlainText) && mime.substr(PlainText.size()).starts_with(';'));
}

} // namespace core::tui
