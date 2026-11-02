// SPDX-License-Identifier: Apache-2.0
#include <core/platform/CpuCounters.hpp>
#include <core/platform/detail/CpuCounterProbe.hpp>
#include <core/platform/detail/UnavailableCpuCounterSource.hpp>
#include <core/platform/testing/FakeCpuCounterSource.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using core::platform::AllCpuCounters;
using core::platform::CpuCounter;
using core::platform::CpuCounterSample;
using core::platform::measure;
using core::platform::toString;
using core::platform::detail::CounterProbe;
using core::platform::detail::probeCounters;
using core::platform::detail::UnavailableCpuCounterSource;
using core::platform::testing::FakeCpuCounterSource;

namespace
{

CpuCounterSample sampleOf(std::uint64_t instructions, std::uint64_t cycles)
{
    auto sample = CpuCounterSample {};
    sample.set(CpuCounter::Instructions, instructions);
    sample.set(CpuCounter::Cycles, cycles);
    return sample;
}

} // namespace

TEST_CASE("CpuCounterSample delta keeps measures present in both", "[platform][cpucounters]")
{
    auto const delta = sampleOf(350, 80) - sampleOf(100, 50);
    CHECK(delta.get(CpuCounter::Instructions) == 250);
    CHECK(delta.get(CpuCounter::Cycles) == 30);
    CHECK_FALSE(delta.get(CpuCounter::ReferenceCycles).has_value());
}

TEST_CASE("CpuCounterSample delta drops a measure missing at either end", "[platform][cpucounters]")
{
    auto start = CpuCounterSample {};
    start.set(CpuCounter::Instructions, 100);
    auto end = CpuCounterSample {};
    end.set(CpuCounter::Cycles, 5);

    auto const delta = end - start;
    for (auto const counter: AllCpuCounters)
        CHECK_FALSE(delta.get(counter).has_value());
}

TEST_CASE("CpuCounterSample delta drops a measure that went backwards", "[platform][cpucounters]")
{
    auto start = CpuCounterSample {};
    start.set(CpuCounter::Instructions, 500);
    auto end = CpuCounterSample {};
    end.set(CpuCounter::Instructions, 400);

    CHECK_FALSE((end - start).get(CpuCounter::Instructions).has_value());
}

TEST_CASE("measure() returns the delta around the work and runs it once", "[platform][cpucounters]")
{
    auto source = FakeCpuCounterSource { { sampleOf(10, 0), sampleOf(70, 0) }, { CpuCounter::Instructions } };
    auto runs = 0;

    auto const delta = measure(source, [&] { ++runs; });

    CHECK(runs == 1);
    CHECK(delta.get(CpuCounter::Instructions) == 60);
}

TEST_CASE("an unavailable source supports nothing and reports its reason", "[platform][cpucounters]")
{
    auto source = UnavailableCpuCounterSource { "no counters here" };
    CHECK(source.read() == CpuCounterSample {});
    CHECK(source.note().empty());
    for (auto const counter: AllCpuCounters)
    {
        CHECK_FALSE(source.supports(counter));
        CHECK(source.unavailableReason(counter) == "no counters here");
    }
}

TEST_CASE("the fake source repeats its last sample and reports unsupported measures",
          "[platform][cpucounters]")
{
    auto source = FakeCpuCounterSource { { sampleOf(1, 2), sampleOf(3, 4) }, { CpuCounter::Cycles } };
    CHECK(source.read() == sampleOf(1, 2));
    CHECK(source.read() == sampleOf(3, 4));
    CHECK(source.read() == sampleOf(3, 4));
    CHECK(source.supports(CpuCounter::Cycles));
    CHECK_FALSE(source.supports(CpuCounter::Instructions));
    CHECK(source.unavailableReason(CpuCounter::Instructions) == "fake: unsupported");
}

TEST_CASE("toString() names every measure", "[platform][cpucounters]")
{
    CHECK(toString(CpuCounter::Instructions) == "instructions");
    CHECK(toString(CpuCounter::Cycles) == "cycles");
    CHECK(toString(CpuCounter::ReferenceCycles) == "reference cycles");
}

TEST_CASE("the default source is never null", "[platform][cpucounters]")
{
    auto const source = core::platform::makeDefaultCpuCounterSource();
    CHECK(source != nullptr);
}

TEST_CASE("the probe calls a zero instruction delta unpopulated", "[cpucounters]")
{
    CHECK(probeCounters(0) == CounterProbe::NotPopulated);
    CHECK(probeCounters(12'000'000) == CounterProbe::Populated);
}
