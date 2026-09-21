// SPDX-License-Identifier: Apache-2.0

// winsock2.h MUST precede windows.h / ws2tcpip.h (which project headers pull in),
// so this block leads every Win32 net translation unit.
// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

#include <core/net/windows/WfmoBackend.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <ranges>
#include <tuple>
#include <vector>

namespace
{

/// A MANUAL-reset Win32 event, owned.
///
/// The wait set is waitable HANDLEs, not sockets — @c WfmoBackend waits on whatever
/// @c ReadinessHandler::handle names and never calls `WSAEventSelect` itself, which is
/// the socket's job. So an event is a registration in the full sense, costs a syscall
/// to make and one to signal, and lets a case build a set larger than
/// `MAXIMUM_WAIT_OBJECTS` without eighty loopback connections to pay for it.
///
/// Manual-reset is not a detail. `WaitForMultipleObjects` CONSUMES an auto-reset
/// event when it returns it, and the backend's wait is a detector followed by a rescan
/// — `collectSignalled()` probes each handle with `WaitForSingleObject(h, 0)` to find
/// out WHICH one fired. An auto-reset event is therefore eaten by the detector and
/// invisible to the rescan, so its readiness is dispatched to nobody. That is a
/// precondition of the backend, which @c WfmoBackend.hpp now states; a WSAEVENT from
/// `WSAEventSelect` is manual-reset, which is why the backend is correct for the
/// handles it actually gets. Writing this fixture with auto-reset events is how the
/// precondition was found.
class OwnedEvent
{
  public:
    OwnedEvent(): _handle { CreateEventW(nullptr, TRUE, FALSE, nullptr) } {}

    OwnedEvent(OwnedEvent const&) = delete;
    OwnedEvent& operator=(OwnedEvent const&) = delete;
    OwnedEvent(OwnedEvent&&) = delete;
    OwnedEvent& operator=(OwnedEvent&&) = delete;

    ~OwnedEvent()
    {
        if (_handle != nullptr)
            CloseHandle(_handle);
    }

    /// @return The raw handle, for registering and for asserting it was made.
    [[nodiscard]] HANDLE get() const noexcept { return _handle; }

    /// Signals the event, which is what makes its registration ready.
    void signal() const noexcept { std::ignore = SetEvent(_handle); }

  private:
    HANDLE _handle;
};

/// One registered event and the callbacks it counts.
struct EventProbe
{
    OwnedEvent event {};
    core::net::ReadinessHandler handler {};
    int readable = 0;
    int failed = 0;

    EventProbe() noexcept
    {
        handler = core::net::ReadinessHandler { .handle = event.get(),
                                                .kind = core::net::HandleKind::Waitable,
                                                .owner = this,
                                                .onReadable = &EventProbe::readableCallback,
                                                .onWritable = nullptr,
                                                .onError = &EventProbe::errorCallback };
    }

    EventProbe(EventProbe const&) = delete;
    EventProbe& operator=(EventProbe const&) = delete;
    EventProbe(EventProbe&&) = delete;
    EventProbe& operator=(EventProbe&&) = delete;
    ~EventProbe() = default;

    /// @return How many callbacks of any kind this probe has had.
    [[nodiscard]] int total() const noexcept { return readable + failed; }

    static void readableCallback(core::net::ReadinessHandler& handler) noexcept
    {
        ++static_cast<EventProbe*>(handler.owner)->readable;
    }

    static void errorCallback(core::net::ReadinessHandler& handler) noexcept
    {
        ++static_cast<EventProbe*>(handler.owner)->failed;
    }
};

} // namespace

TEST_CASE("WfmoBackend serves a handle past MAXIMUM_WAIT_OBJECTS", "[net][backend][windows]")
{
    using core::net::Interest;

    // `WaitForMultipleObjects` refuses a set larger than 64, so past that the backend
    // stops making one blocking call and starts sweeping chunks with a zero timeout,
    // rotating which chunk it starts on. detail::WaitChunk's arithmetic is tested on
    // every platform; what is NOT tested anywhere else is this backend's USE of it,
    // and the integration is the half that fails in the field. The failure it would
    // fail as: a registration in any chunk but the first is never looked at, so a flow
    // parked on it waits forever while the loop reports a clean timeout.
    //
    // Two mechanisms each guarantee a late chunk is reached, and this was measured
    // rather than assumed: sweeping every chunk per wait, and rotating the chunk a
    // sweep starts on. Removing EITHER leaves this case green, because the other still
    // gets there — a sweep of one chunk per wait reaches chunk 1 on the next call, and
    // a pinned rotation still sweeps forward through every chunk. Only removing BOTH
    // turns it red, and then exactly on the two indices that live past the boundary.
    // So what it pins is the property, not one of its two defences, which is the right
    // thing to pin and worth knowing before anyone deletes one of them as redundant.
    //
    // 80 registrations, so the set is 81 with the wakeup channel: two chunks, the
    // second genuinely short (17 of 64) rather than a convenient exact fill.
    constexpr std::size_t Count = 80;

    auto backend = core::net::WfmoBackend {};
    auto probes = std::vector<std::unique_ptr<EventProbe>> {};
    probes.reserve(Count);
    for ([[maybe_unused]] auto const i: std::views::iota(std::size_t { 0 }, Count))
    {
        auto probe = std::make_unique<EventProbe>();
        REQUIRE(probe->event.get() != nullptr);
        REQUIRE(backend.attach(probe->handler).has_value());
        REQUIRE(backend.setInterest(probe->handler, Interest::Read).has_value());
        probes.push_back(std::move(probe));
    }

    // Indices into `probes`, chosen by where they land in the wait array. The wakeup
    // channel is registration 0, so probes[i] is handles[i + 1]: probes[62] is
    // handles[63], the LAST of chunk 0, and probes[63] is handles[64], the first of
    // chunk 1. Those two are the boundary the arithmetic is most likely to be off by
    // one about. probes[79] is handles[80], the last handle of all and the one a sweep
    // that quits after chunk 0 can never reach. probes[0] is the control: if it fails
    // too, the case is broken rather than the chunking.
    auto const index =
        GENERATE(std::size_t { 0 }, std::size_t { 62 }, std::size_t { 63 }, std::size_t { 79 });
    CAPTURE(index);

    probes[index]->event.signal();

    // Bounded, and the bound is what it waits FOR: one sweep of two chunks plus a
    // dispatch, which is microseconds. 2s is four orders of magnitude of slack for the
    // slowest runner, and a regression exhausts it rather than hanging the binary.
    auto served = 0;
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds { 2 };
    while (probes[index]->total() == 0 && std::chrono::steady_clock::now() < deadline)
        served += static_cast<int>(backend.wait(std::chrono::milliseconds { 50 }).dispatched);

    CHECK(probes[index]->readable >= 1);
    CHECK(served >= 1);

    // And only that one: a chunked sweep that reported its whole chunk rather than the
    // handle that signalled would pass the check above while waking 63 flows that have
    // nothing to read.
    auto others = 0;
    for (auto const i: std::views::iota(std::size_t { 0 }, Count))
        if (i != index)
            others += probes[i]->total();
    CHECK(others == 0);

    for (auto const& probe: probes)
        backend.detach(probe->handler);
}
