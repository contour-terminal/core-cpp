// SPDX-License-Identifier: Apache-2.0
#include <core/net/PollEventSource.hpp>

#include <core/net/Diagnostics.hpp>
#include <core/net/detail/WaitChunking.hpp>

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
        Failed,    ///< The wait itself failed (WAIT_FAILED): a caller bug or bad handle.
    };

    /// Classifies a @c WaitForMultipleObjects result over @p count handles. An
    /// abandoned mutex (the @c WAIT_ABANDONED_0 range) counts as readiness: the
    /// owning thread died, so the parked reader is still resumed to observe the
    /// handle rather than have the wait silently drop it.
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

    /// Appends the ready tokens of every currently-signalled registration to
    /// @p outcome. A full rescan (rather than trusting one wait's returned index)
    /// keeps the fast and chunked paths reporting identically and picks up every
    /// handle ready this round, not merely the first the OS named.
    ///
    /// A handle that has become INVALID (WaitForSingleObject → WAIT_FAILED) is routed
    /// as read-ready too: it means the socket was closed while a reader was parked on
    /// its event, so the reactor still holds the now-dead handle. Resuming that reader
    /// lets it observe the closed socket and unwind — the Windows analogue of POSIX
    /// poll(2) reporting POLLNVAL for a closed fd (see routePollRevents). If it is not
    /// routed, WaitForMultipleObjects fails on the dead handle every round and the
    /// parked reader hangs forever.
    /// @param registrations The full registration list.
    /// @param outcome The outcome to append ready tokens to.
    void collectSignalled(std::vector<FdRegistration> const& registrations, WaitOutcome& outcome)
    {
        auto const ready = [](HANDLE handle) {
            if (handle == nullptr)
                return false;
            auto const status = WaitForSingleObject(handle, 0);
            return status == WAIT_OBJECT_0 || status == WAIT_FAILED;
        };
        for (auto const& reg: registrations)
        {
            if (!ready(reg.fd))
                continue;
            if (hasInterest(reg.interest, FdInterest::Read))
                outcome.readyRead.push_back(reg.token);
            if (hasInterest(reg.interest, FdInterest::Write))
                outcome.readyWrite.push_back(reg.token);
        }
    }
} // namespace

WaitOutcome PollEventSource::wait(int timeoutMs)
{
    auto const& registrations = _registry.registrations();
    static thread_local std::vector<HANDLE> handles;
    handles.clear();
    for (auto const& reg: registrations)
        if (reg.fd != nullptr && reg.fd != platform::InvalidHandle && reg.interest != FdInterest::None)
            handles.push_back(reg.fd);

    auto outcome = WaitOutcome {};

    if (handles.empty())
    {
        if (timeoutMs > 0)
            Sleep(static_cast<DWORD>(timeoutMs));
        return outcome;
    }

    auto const timeout = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);

    // Fast path: the whole set fits one blocking wait — behaviour unchanged from
    // before, except a genuine failure is surfaced instead of masqueraded as a
    // timeout.
    if (handles.size() <= MaxWaitObjects)
    {
        auto const count = static_cast<DWORD>(handles.size());
        auto const verdict =
            classifyWait(WaitForMultipleObjects(count, handles.data(), FALSE, timeout), count);
        if (verdict == WaitVerdict::None)
            return outcome;
        // Signalled OR Failed: rescan per handle. collectSignalled routes the ready
        // handles AND any now-invalid one (a socket closed under a parked reader), so a
        // WAIT_FAILED resumes that reader instead of the wait failing on the dead handle
        // every round.
        collectSignalled(registrations, outcome);
        if (!outcome.readyRead.empty() || !outcome.readyWrite.empty())
            return outcome;
        // A genuine failure with no handle we can pin (rare). Never let it return
        // instantly as a benign timeout: with an indefinite wait that hot-spins the
        // loop. Yield a bounded slice (capped by the caller's timeout, skipped for a
        // pure poll).
        if (verdict == WaitVerdict::Failed)
        {
            reportDiagnostic(std::format("WaitForMultipleObjects failed: {}", GetLastError()));
            if (timeoutMs != 0)
                Sleep(timeout < SweepSliceMs ? timeout : SweepSliceMs);
        }
        return outcome;
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
                failureLogged = true; // log once per wait(), not once per sweep, to bound spam.
            }
            // Signalled OR Failed both hand off to collectSignalled below: a Failed chunk
            // carries a now-invalid handle (a socket closed under a parked reader), which
            // collectSignalled routes so that reader resumes rather than hanging.
            if (verdict == WaitVerdict::Signalled || verdict == WaitVerdict::Failed)
            {
                anyReady = true;
                break;
            }
        }
        _waitRotation = nextWaitRotation(chunkCount, start);

        if (anyReady)
        {
            collectSignalled(registrations, outcome);
            return outcome;
        }

        // Nothing ready this sweep. Honour the deadline, then block a bounded slice
        // and sweep again (an indefinite wait sleeps the full slice each round).
        if (infinite)
            Sleep(SweepSliceMs);
        else
        {
            auto const elapsed = GetTickCount64() - startTick;
            if (elapsed >= budgetMs)
                return outcome;
            Sleep(static_cast<DWORD>(std::min<ULONGLONG>(budgetMs - elapsed, SweepSliceMs)));
        }
    }
}

} // namespace core::net
