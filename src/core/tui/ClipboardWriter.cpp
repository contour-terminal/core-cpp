// SPDX-License-Identifier: Apache-2.0
#include <core/tui/ClipboardWriter.hpp>

#include <core/tui/Terminal.hpp>

#include <algorithm>
#include <string_view>
#include <utility>
#include <variant>

namespace core::tui
{

namespace
{
    /// DECRQM for mode 5522, then DA1: the DA1 reply ends the probe whatever the terminal is.
    constexpr auto Osc5522Probe = std::string_view { "\033[?5522$p\033[c" };

    /// @brief Whether a DECRQM answer says the terminal knows the mode.
    constexpr auto knowsMode(DecModeStatus status) noexcept -> bool
    {
        return status == DecModeStatus::Set || status == DecModeStatus::Reset
               || status == DecModeStatus::PermanentlySet;
    }
} // namespace

ClipboardWriter::ClipboardWriter(ChannelFactory openChannel,
                                 core::platform::IClock& clock,
                                 ClipboardWriterTimeouts timeouts):
    _openChannel(std::move(openChannel)), _clock(clock), _timeouts(timeouts)
{
}

auto ClipboardWriter::write(std::string_view data, std::string_view mime, ClipboardTarget target)
    -> std::expected<ClipboardTransport, ClipboardWriteError>
{
    return _openChannel().and_then([&](std::unique_ptr<TerminalChannel> const& channel) {
        return transportFor(*channel).and_then(
            [&](ClipboardTransport transport) { return send(*channel, transport, data, mime, target); });
    });
}

auto ClipboardWriter::transportFor(TerminalChannel& channel)
    -> std::expected<ClipboardTransport, ClipboardWriteError>
{
    // A background process may write but not read: no probe, and nothing learned to keep.
    if (channel.access() == ChannelAccess::WriteOnly)
        return ClipboardTransport::Osc52;
    if (_probed)
        return *_probed;

    auto supported = false;
    auto const isProbeEnd = [&supported](InputEvent const& event) {
        if (auto const* report = std::get_if<DecModeReport>(&event); report && report->mode == Osc5522Mode)
            supported = knowsMode(decModeStatusFromReply(report->status));
        return std::holds_alternative<DeviceAttributesReport>(event);
    };

    return channel.write(Osc5522Probe)
        .and_then([&] { return awaitEvent(channel, _timeouts.probe, isProbeEnd); })
        .transform([&](bool) {
            _probed = supported ? ClipboardTransport::Osc5522 : ClipboardTransport::Osc52;
            return *_probed;
        });
}

auto ClipboardWriter::send(TerminalChannel& channel,
                           ClipboardTransport transport,
                           std::string_view data,
                           std::string_view mime,
                           ClipboardTarget target) -> std::expected<ClipboardTransport, ClipboardWriteError>
{
    if (transport == ClipboardTransport::Osc52)
    {
        if (!isPlainTextMimeType(mime))
            return std::unexpected { ClipboardWriteError::UnsupportedMimeType };
        return channel.write(encodeOsc52(data, target)).transform([] { return ClipboardTransport::Osc52; });
    }

    auto status = std::optional<std::expected<void, ClipboardWriteError>> {};
    auto const isStatus = [&status](InputEvent const& event) {
        if (auto const* osc = std::get_if<OscResponse>(&event))
            status = parseOsc5522WriteStatus(osc->payload);
        return status.has_value();
    };

    return channel.write(encodeOsc5522Write(data, mime, target))
        .and_then([&] { return awaitEvent(channel, _timeouts.status, isStatus); })
        .and_then([&](bool answered) -> std::expected<ClipboardTransport, ClipboardWriteError> {
            if (!answered)
                return std::unexpected { ClipboardWriteError::NoConfirmation };
            return status->transform([] { return ClipboardTransport::Osc5522; });
        });
}

auto ClipboardWriter::awaitEvent(TerminalChannel& channel,
                                 std::chrono::milliseconds timeout,
                                 std::function<bool(InputEvent const&)> const& isWanted)
    -> std::expected<bool, ClipboardWriteError>
{
    auto const deadline = _clock.now() + timeout;
    // The deadline alone ends the wait: an empty poll may be a signal or half of a reply.
    auto now = _clock.now();
    while (now < deadline)
    {
        // Rounded UP: a sub-millisecond remainder truncated to 0 makes the last wait a spin.
        auto const remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
        auto events = channel.poll(static_cast<int>(remaining));
        if (!events)
            return std::unexpected { events.error() };
        if (std::ranges::any_of(*events, isWanted))
            return true;
        now = _clock.now();
    }
    return false;
}

} // namespace core::tui
