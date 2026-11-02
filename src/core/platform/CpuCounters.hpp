// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>

namespace core::platform
{

/// A CPU work measure a counter source may be able to report.
enum class CpuCounter : std::uint8_t
{
    Instructions,
    Cycles,
    ReferenceCycles,
    Last, ///< Not a measure: the count of them, which sets the extent of every table indexed by CpuCounter.
};

/// The number of measures.
inline constexpr std::size_t CpuCounterCount = std::to_underlying(CpuCounter::Last);

/// Every CpuCounter, in enumerator order; the index of a measure in CpuCounterSample::values.
inline constexpr std::array<CpuCounter, CpuCounterCount> AllCpuCounters {
    CpuCounter::Instructions,
    CpuCounter::Cycles,
    CpuCounter::ReferenceCycles,
};

namespace detail
{
    /// True when AllCpuCounters lists every enumerator before Last, in order, so a counter's value is its
    /// index.
    [[nodiscard]] constexpr bool cpuCountersAreInOrder() noexcept
    {
        for (auto const index: std::views::iota(std::size_t { 0 }, AllCpuCounters.size()))
            if (std::to_underlying(AllCpuCounters[index]) != index)
                return false;
        return true;
    }
} // namespace detail

static_assert(detail::cpuCountersAreInOrder(),
              "AllCpuCounters must list the CpuCounter enumerators in order: it is the index of "
              "CpuCounterSample::values");

/// Returns the human-readable name of a measure.
/// @param counter The measure.
/// @return "instructions", "cycles" or "reference cycles".
[[nodiscard]] constexpr std::string_view toString(CpuCounter counter) noexcept
{
    switch (counter)
    {
        case CpuCounter::Instructions: return "instructions";
        case CpuCounter::Cycles: return "cycles";
        case CpuCounter::ReferenceCycles: return "reference cycles";
        case CpuCounter::Last: break;
    }
    return "unknown";
}

/// One reading of every measure; a measure the source cannot report is absent.
struct CpuCounterSample
{
    /// Returns the value of a measure, or nothing when it was not reported.
    /// @param counter The measure.
    [[nodiscard]] std::optional<std::uint64_t> get(CpuCounter counter) const noexcept
    {
        return values[std::to_underlying(counter)];
    }

    /// Records the value of a measure.
    /// @param counter The measure.
    /// @param value Its value.
    void set(CpuCounter counter, std::uint64_t value) noexcept
    {
        values[std::to_underlying(counter)] = value;
    }

    /// The measures, indexed by CpuCounter.
    std::array<std::optional<std::uint64_t>, CpuCounterCount> values {};

    auto operator<=>(CpuCounterSample const&) const = default;
};

/// The work done between two samples.
/// @param end The later sample.
/// @param start The earlier sample.
/// @return A sample whose measure is present only if present in both and end >= start.
[[nodiscard]] constexpr CpuCounterSample operator-(CpuCounterSample const& end,
                                                   CpuCounterSample const& start) noexcept
{
    auto delta = CpuCounterSample {};
    for (auto const counter: AllCpuCounters)
    {
        auto const last = end.get(counter);
        auto const first = start.get(counter);
        if (last && first && *last >= *first)
            delta.set(counter, *last - *first);
    }
    return delta;
}

/// Where CPU work counts come from: the operating system's counters, or a test double.
class ICpuCounterSource
{
  public:
    ICpuCounterSource() = default;
    ICpuCounterSource(ICpuCounterSource const&) = delete;
    ICpuCounterSource(ICpuCounterSource&&) = delete;
    ICpuCounterSource& operator=(ICpuCounterSource const&) = delete;
    ICpuCounterSource& operator=(ICpuCounterSource&&) = delete;
    virtual ~ICpuCounterSource() = default;

    /// Reads the counters now; unsupported measures are absent.
    [[nodiscard]] virtual CpuCounterSample read() = 0;

    /// Why a measure is not reported.
    /// @param counter The measure.
    /// @return Empty when the measure is supported. The view lives as long as the source.
    [[nodiscard]] virtual std::string_view unavailableReason(CpuCounter counter) const noexcept = 0;

    /// Notes about what is counted (e.g. "child processes are counted (kernel < 5.13)"); may be empty.
    [[nodiscard]] virtual std::string_view note() const noexcept = 0;

    /// Whether a measure is reported; derived from unavailableReason(), so they cannot disagree.
    /// @param counter The measure.
    [[nodiscard]] bool supports(CpuCounter counter) const noexcept
    {
        return unavailableReason(counter).empty();
    }
};

/// Creates the platform's counter source.
/// @return A source; never null, never throws. Where the platform cannot count, every measure is
///         unsupported and says why.
[[nodiscard]] std::unique_ptr<ICpuCounterSource> makeDefaultCpuCounterSource();

/// Runs work and returns the counters it spent.
/// @param source Where the counts come from.
/// @param work Called exactly once, between two reads.
/// @return The delta between the two reads.
template <std::invocable Work>
[[nodiscard]] CpuCounterSample measure(ICpuCounterSource& source, Work&& work)
{
    auto const start = source.read();
    std::forward<Work>(work)();
    auto const end = source.read();
    return end - start;
}

} // namespace core::platform
