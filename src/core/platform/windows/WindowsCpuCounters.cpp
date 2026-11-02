// SPDX-License-Identifier: Apache-2.0
#include <core/platform/CpuCounters.hpp>
#include <core/platform/detail/UnavailableCpuCounterSource.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <system_error>

#include <windows.h>

namespace core::platform
{

namespace
{
    /// The reason Instructions and Cycles are not reported.
    constexpr std::string_view NoRetiredCounters = "not available to unprivileged processes on Windows";

    /// Reads the cycles this process's threads have consumed, from the CPU's reference clock.
    /// @param cycles Receives the count.
    /// @return 0 on success, otherwise the GetLastError() of the failed call.
    [[nodiscard]] DWORD readProcessCycles(std::uint64_t& cycles) noexcept
    {
        auto value = ULONG64 { 0 };
        if (QueryProcessCycleTime(GetCurrentProcess(), &value) == 0)
            return GetLastError();
        cycles = value;
        return 0;
    }

    /// Counts the reference cycles of every thread of this process.
    class WindowsCpuCounterSource final: public ICpuCounterSource
    {
      public:
        [[nodiscard]] CpuCounterSample read() override
        {
            auto sample = CpuCounterSample {};
            auto cycles = std::uint64_t { 0 };
            if (readProcessCycles(cycles) == 0)
                sample.set(CpuCounter::ReferenceCycles, cycles);
            return sample;
        }

        [[nodiscard]] std::string_view unavailableReason(CpuCounter counter) const noexcept override
        {
            return counter == CpuCounter::ReferenceCycles ? std::string_view {} : NoRetiredCounters;
        }

        [[nodiscard]] std::string_view note() const noexcept override { return {}; }
    };
} // namespace

std::unique_ptr<ICpuCounterSource> makeDefaultCpuCounterSource()
{
    auto cycles = std::uint64_t { 0 };
    if (auto const error = readProcessCycles(cycles); error != 0)
        return std::make_unique<detail::UnavailableCpuCounterSource>(
            std::system_category().message(static_cast<int>(error)));
    return std::make_unique<WindowsCpuCounterSource>();
}

} // namespace core::platform
