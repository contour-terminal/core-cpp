// SPDX-License-Identifier: Apache-2.0
//
// `detail::ParkMap`, the open-addressing table the park table keys its parks in, and the park
// recycling beside it.
//
// The map replaced a `std::unordered_map` on the once-per-operation path, so it has to agree with
// one exactly -- including on the id that is never a key. `ParkId::invalid()` is zero, zero is the
// map's empty-slot marker, and `unregisterPark(ParkId::invalid())` is a documented no-op that real
// callers make. A first version matched zero against the first empty slot on the probe, counted a
// removal and shifted live entries out of their runs: a park then vanished from the table while its
// flow still waited, and a TLS case hung on a timer nobody could fire.
#include <core/net/detail/ParkTable.hpp>
#include <core/net/testing/TestLoop.hpp>
#include <core/platform/Clock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <vector>

using core::net::ParkId;
using core::net::ParkWake;
using core::net::detail::HandleWatch;
using core::net::detail::Park;
using core::net::detail::ParkMap;
using core::net::detail::ParkTable;
using core::net::testing::TestLoop;
using core::platform::ManualClock;

namespace
{
/// A handle other than `InvalidHandle`, however the platform spells one; never used as one.
template <typename Handle = core::platform::NativeHandle>
Handle someHandle()
{
    if constexpr (std::is_pointer_v<Handle>)
        return reinterpret_cast<Handle>(std::intptr_t { 7 });
    else
        return Handle { 7 };
}

/// SplitMix64: the same sequence on every run and every standard library, which is the point here.
/// A failure names a step, and the step has to be reproducible to be worth naming.
struct SplitMix64
{
    std::uint64_t state = 0;

    std::uint64_t operator()() noexcept
    {
        auto z = (state += 0x9E37'79B9'7F4A'7C15ULL);
        z = (z ^ (z >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D0'49BB'1331'11EBULL;
        return z ^ (z >> 31U);
    }
};
} // namespace

TEST_CASE("ParkMap refuses the zero id without touching what it holds", "[net][parktable]")
{
    auto map = ParkMap {};
    auto held = std::make_unique<Park>();
    auto* const raw = held.get();
    map.insert(ParkId { 1 }, std::move(held));

    CHECK(map.erase(ParkId::invalid()) == nullptr);
    CHECK(map.size() == 1);
    CHECK(map.find(ParkId { 1 }) == raw);
    CHECK(map.find(ParkId::invalid()) == nullptr);
}

TEST_CASE("ParkMap agrees with an unordered_map over a long random run", "[net][parktable]")
{
    // Few live parks and many ids, which is a loop's shape: an id is filed and taken per operation
    // while only a handful are live at once, so every removal shifts a short run and the probe
    // sequences cross the end of the array again and again.
    auto map = ParkMap {};
    auto reference = std::unordered_map<std::uint64_t, Park*> {};
    auto live = std::vector<std::uint64_t> {};
    auto rng = SplitMix64 { .state = 1729 };
    auto next = std::uint64_t { 0 };
    for ([[maybe_unused]] auto const step: std::views::iota(0, 200'000))
    {
        auto const choice = live.size() > 12 ? 1 + (rng() % 3) : rng() % 4;
        if (choice == 0 || live.empty())
        {
            auto park = std::make_unique<Park>();
            reference.emplace(++next, park.get());
            map.insert(ParkId { next }, std::move(park));
            live.push_back(next);
        }
        else if (choice == 1)
        {
            // Narrowed explicitly: `std::size_t` is 32 bits under WebAssembly.
            auto const index = static_cast<std::size_t>(rng() % live.size());
            auto const id = live[index];
            auto const taken = map.erase(ParkId { id });
            REQUIRE(taken.get() == reference.at(id));
            reference.erase(id);
            live[index] = live.back();
            live.pop_back();
        }
        else if (choice == 2)
        {
            // An id that is not held -- zero, one never filed, or one already taken -- removes
            // nothing.
            auto const absent = rng() % 3 == 0 ? std::uint64_t { 0 } : next + 1 + (rng() % 8);
            REQUIRE(map.erase(ParkId { absent }) == nullptr);
        }
        else
        {
            auto const id = rng() % (next + 4);
            auto const found = reference.find(id);
            REQUIRE(map.find(ParkId { id }) == (found == reference.end() ? nullptr : found->second));
        }
        REQUIRE(map.size() == reference.size());
    }
}

TEST_CASE("ParkTable hands a recycled park back reset, and keeps no park that still holds work",
          "[net][parktable]")
{
    // Every field but `parked` set to something other than its default: `recycle` resets a park
    // field by field, and a field it forgets reaches the next operation's park.
    auto clock = ManualClock {};
    auto loop = TestLoop { clock };
    auto table = ParkTable {};
    auto watch = HandleWatch {};
    auto park = table.acquire();
    park->handler.owner = &table;
    park->loop = &loop;
    park->handle = someHandle();
    park->waiterKey = &table;
    park->attached = true;
    park->watch = &watch;
    park->onExpired = [](void*) {
    };
    park->onReady = [](void*, ParkWake) {
    };
    park->callbackState = &table;
    park->ownedByLoop = true;
    park->deadline = clock.now() + std::chrono::seconds { 1 };
    auto const id = table.add(std::move(park));
    auto taken = table.take(id);
    REQUIRE(taken != nullptr);
    // The two the table writes itself, set after it is done with them.
    taken->sequence = 3;
    taken->handleIndexed = true;
    auto* const raw = taken.get();

    table.recycle(std::move(taken));
    auto again = table.acquire();
    CHECK(again.get() == raw); // reused rather than reallocated
    CHECK(again->id == ParkId::invalid());
    CHECK(!again->parked);
    CHECK(again->handler.owner == nullptr);
    CHECK(again->loop == nullptr);
    CHECK(again->handle == core::platform::InvalidHandle);
    CHECK(again->waiterKey == nullptr);
    CHECK(!again->attached);
    CHECK(again->watch == nullptr);
    CHECK(!again->handleIndexed);
    CHECK(again->onExpired == nullptr);
    CHECK(again->onReady == nullptr);
    CHECK(again->callbackState == nullptr);
    CHECK(!again->ownedByLoop);
    CHECK(!again->deadline.has_value());
    CHECK(again->sequence == 0);
}
