// SPDX-License-Identifier: Apache-2.0
#include <core/net/windows/WfmoBackend.hpp>

#include <core/net/Diagnostics.hpp>
#include <core/net/detail/WaitChunking.hpp>
#include <core/net/detail/WaitTimeout.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ranges>

#include <windows.h>

namespace core::net
{

namespace
{
    /// The largest handle set a single WaitForMultipleObjects call accepts.
    constexpr std::size_t MaxWaitObjects = MAXIMUM_WAIT_OBJECTS;

    /// How long a fruitless chunked sweep blocks before re-sweeping. Bounds the
    /// worst-case readiness latency (and CPU) of the >MaxWaitObjects slow path.
    constexpr DWORD SweepSliceMs = 15;

    /// The classification of a WaitForMultipleObjects return value.
    enum class WaitVerdict : std::uint8_t
    {
        Signalled, ///< A handle in the waited range resolved the wait.
        None,      ///< The wait timed out; nothing became ready.
        Failed,    ///< The wait itself failed (WAIT_FAILED): a caller bug or a bad handle.
    };

    /// Classifies a @c WaitForMultipleObjects result over @p count handles. An
    /// abandoned mutex (the @c WAIT_ABANDONED_0 range) counts as readiness: the owning
    /// thread died, so the parked reader is still resumed to observe the handle rather
    /// than have the wait silently drop it.
    /// @param waitResult The value @c WaitForMultipleObjects returned.
    /// @param count The number of handles the wait covered (1..MaxWaitObjects).
    /// @return The classification.
    [[nodiscard]] WaitVerdict classifyWait(DWORD waitResult, DWORD count) noexcept
    {
        if (waitResult == WAIT_TIMEOUT)
            return WaitVerdict::None;
        if (waitResult == WAIT_FAILED)
            return WaitVerdict::Failed;
        // WAIT_OBJECT_0 is 0, so the signalled-object range is [0, count); the lower
        // bound is implicit (waitResult is unsigned) and omitted to dodge a
        // tautological comparison. TIMEOUT/FAILED are already handled above, and the
        // abandoned range starts at WAIT_ABANDONED_0 (0x80), never overlapping
        // [0, count) since count never exceeds MaxWaitObjects (64).
        if (waitResult < WAIT_OBJECT_0 + count)
            return WaitVerdict::Signalled;
        if (waitResult >= WAIT_ABANDONED_0 && waitResult < WAIT_ABANDONED_0 + count)
            return WaitVerdict::Signalled;
        return WaitVerdict::None;
    }

    /// What one registered handle's current state says about it.
    ///
    /// A handle that has become INVALID (`WaitForSingleObject` → `WAIT_FAILED`) is a
    /// failure and is routed as one: it means the socket was closed while a flow was
    /// parked on its event, so the backend still holds the now-dead handle. Reporting
    /// it lets that flow observe the closed socket and unwind — the Windows analogue
    /// of poll(2) answering POLLNVAL for a closed descriptor. Left unrouted,
    /// `WaitForMultipleObjects` fails on the dead handle every round and the parked
    /// flow hangs forever.
    /// @param handle The registered wait handle.
    /// @param interest What the registration is watched for.
    /// @return What to report, or @c Readiness::None if the handle is not signalled.
    [[nodiscard]] Readiness probeHandle(HANDLE handle, Interest interest) noexcept
    {
        if (handle == nullptr || handle == platform::InvalidHandle)
            return Readiness::None;
        auto const status = WaitForSingleObject(handle, 0);
        if (status == WAIT_FAILED)
            return Readiness::Failed;
        if (status != WAIT_OBJECT_0)
            return Readiness::None;
        // A waitable network event says "something happened on this socket", not which
        // direction, so it is reported to the direction(s) the registration asked for.
        auto observed = Readiness::None;
        if (hasInterest(interest, Interest::Read))
            observed = observed | Readiness::Readable;
        if (hasInterest(interest, Interest::Write))
            observed = observed | Readiness::Writable;
        return observed;
    }
} // namespace

WfmoBackend::WfmoBackend()
{
    // The wakeup channel is registration 0 and is never removed, so the wait set is
    // never empty and `wake()` can always break a wait. It goes through the ordinary
    // attach/setInterest path rather than a private one, so the path shutdown depends
    // on is the path every case exercises.
    _registrations.push_back(Registration { .handler = &_wakeup.handler(), .interest = Interest::Read });
}

WfmoBackend::Registration* WfmoBackend::find(ReadinessHandler const& handler) noexcept
{
    auto const found = std::ranges::find(_registrations, &handler, &Registration::handler);
    return found == _registrations.end() ? nullptr : &*found;
}

std::expected<void, NetError> WfmoBackend::attach(ReadinessHandler& handler)
{
    if (handler.handle == platform::InvalidHandle || handler.handle == nullptr)
        return std::unexpected { makeNetError(NetErrorCode::BadHandle, 0, "WfmoBackend::attach") };
    if (find(handler) != nullptr)
        return std::unexpected { makeNetError(
            NetErrorCode::BadHandle, 0, "WfmoBackend::attach: handler is already attached") };
    // Refused rather than waited on: the handle of a completion registration is the address of
    // an overlapped operation, and `WaitForMultipleObjects` would read it as a kernel object.
    // Nothing reaches here in practice -- this backend lends no completion port, so nothing
    // issues an operation to name -- which is exactly why the refusal must be loud if it ever does.
    if (handler.kind == HandleKind::Completion)
        return std::unexpected { makeNetError(NetErrorCode::Unsupported,
                                              0,
                                              "WfmoBackend::attach: HandleKind::Completion needs a "
                                              "completion port, and this backend has none") };

    _registrations.push_back(Registration { .handler = &handler, .interest = Interest::None });
    return {};
}

std::expected<void, NetError> WfmoBackend::setInterest(ReadinessHandler& handler, Interest interest)
{
    auto* const registration = find(handler);
    if (registration == nullptr)
        return std::unexpected { makeNetError(
            NetErrorCode::BadHandle, 0, "WfmoBackend::setInterest: handler is not attached") };
    registration->interest = interest;
    return {};
}

void WfmoBackend::detach(ReadinessHandler& handler) noexcept
{
    std::erase_if(_registrations,
                  [&handler](Registration const& entry) { return entry.handler == &handler; });
    // ... and out of the batch a wait in flight is walking, which erasing above does
    // nothing about: the batch holds its own pointers, taken before any callback ran.
    _batch.withdraw(handler);
}

void WfmoBackend::collectSignalled()
{
    for (auto const& entry: _registrations)
    {
        if (entry.interest == Interest::None)
            continue; // muted: as silent as a detached registration, on every backend
        if (auto const observed = probeHandle(entry.handler->handle, entry.interest);
            observed != Readiness::None)
            _batch.add(*entry.handler, observed);
    }
}

WaitResult WfmoBackend::wait(std::optional<platform::SteadyDuration> timeout)
{
    static thread_local auto handles = std::vector<HANDLE> {};
    handles.clear();
    for (auto const& entry: _registrations)
        if (entry.interest != Interest::None && entry.handler->handle != nullptr
            && entry.handler->handle != platform::InvalidHandle)
            handles.push_back(entry.handler->handle);

    auto const timeoutMs = detail::toTimeoutMillis(timeout);
    auto const waitFor = timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs);

    // The wakeup channel is always registered and always armed, so the set is never
    // empty; this is the belt to that braces, and it keeps an indefinite wait from
    // blocking forever on nothing if a future edit ever makes it possible.
    if (handles.empty())
    {
        if (timeoutMs > 0)
            Sleep(waitFor);
        return WaitResult {};
    }

    // Fast path: the whole set fits one blocking wait.
    if (handles.size() <= MaxWaitObjects)
    {
        auto const count = static_cast<DWORD>(handles.size());
        auto const verdict =
            classifyWait(WaitForMultipleObjects(count, handles.data(), FALSE, waitFor), count);
        if (verdict == WaitVerdict::None)
            return WaitResult {};
        // Signalled OR Failed: rescan per handle. collectSignalled routes the ready
        // handles AND any now-invalid one (a socket closed under a parked flow), so a
        // WAIT_FAILED resumes that flow instead of the wait failing on the dead handle
        // every round.
        collectSignalled();
        if (_batch.size() != 0)
            return WaitResult { .dispatched = _batch.dispatch() };
        // A genuine failure with no handle to pin it on (rare). Never let it return
        // instantly as a benign timeout: with an indefinite wait that hot-spins the
        // loop. Yield a bounded slice (capped by the caller's timeout, skipped for a
        // pure poll).
        if (verdict == WaitVerdict::Failed)
        {
            reportDiagnostic(std::format("WaitForMultipleObjects failed: {}", GetLastError()));
            if (timeoutMs != 0)
                Sleep(waitFor < SweepSliceMs ? waitFor : SweepSliceMs);
        }
        return WaitResult {};
    }

    // Slow path: more handles than one wait accepts. Sweep the set in chunks with a
    // 0-timeout wait each, stopping as soon as a chunk reports readiness; between
    // fruitless sweeps block a bounded slice so the overall wait still honours its
    // timeout without hot-spinning, and rotate the first-swept chunk every call so
    // high-index handles are never starved.
    auto const total = handles.size();
    auto const chunkCount = waitChunkCount(total, MaxWaitObjects);
    auto const infinite = timeoutMs < 0;
    auto const budgetMs = infinite ? 0ULL : static_cast<ULONGLONG>(timeoutMs);
    auto const startTick = GetTickCount64();
    auto failureLogged = false;

    while (true)
    {
        auto const start = _waitRotation % chunkCount;
        auto anyReady = false;
        for (auto const step: std::views::iota(std::size_t { 0 }, chunkCount))
        {
            auto const chunk = waitChunkAt(total, MaxWaitObjects, (start + step) % chunkCount);
            auto const chunkSize = static_cast<DWORD>(chunk.count);
            auto const verdict = classifyWait(
                WaitForMultipleObjects(chunkSize, handles.data() + chunk.offset, FALSE, 0), chunkSize);
            if (verdict == WaitVerdict::Failed && !failureLogged)
            {
                reportDiagnostic(std::format(
                    "WaitForMultipleObjects (chunk at {}) failed: {}", chunk.offset, GetLastError()));
                failureLogged = true; // once per wait(), not once per sweep, to bound the spam
            }
            // Signalled OR Failed both hand off to collectSignalled below: a Failed
            // chunk carries a now-invalid handle (a socket closed under a parked flow),
            // which collectSignalled routes so that flow resumes rather than hanging.
            if (verdict == WaitVerdict::Signalled || verdict == WaitVerdict::Failed)
            {
                anyReady = true;
                break;
            }
        }
        _waitRotation = nextWaitRotation(chunkCount, start);

        if (anyReady)
        {
            collectSignalled();
            return WaitResult { .dispatched = _batch.dispatch() };
        }

        // Nothing ready this sweep. Honour the deadline, then block a bounded slice
        // and sweep again (an indefinite wait sleeps the full slice each round).
        if (infinite)
            Sleep(SweepSliceMs);
        else
        {
            auto const elapsed = GetTickCount64() - startTick;
            if (elapsed >= budgetMs)
                return WaitResult {};
            Sleep(static_cast<DWORD>(std::min<ULONGLONG>(budgetMs - elapsed, SweepSliceMs)));
        }
    }
}

} // namespace core::net
