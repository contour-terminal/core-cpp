// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `IoBackend` — the one blocking primitive an @c EventLoop drives, and the
/// readiness dispatch it performs on the way back out.
///
/// **Backends dispatch, the loop resumes.** A backend does not report *which*
/// registrations became ready and leave the caller to look them up; it invokes the
/// callbacks on the @c ReadinessHandler the caller registered, and those callbacks
/// only ever ENQUEUE. A coroutine is resumed by the loop, on the loop's thread, in
/// the turn step that drains the ready queue — never from inside a backend's walk
/// over its own ready list. The whole of that walk is a batch the kernel already
/// wrote: resuming from within it lets a resumed frame free the object whose entry
/// the walk has not reached yet, which is a use-after-free with no diagnostic
/// (`.agent/rules/async-and-net.md`,
/// [fastcached#475](https://github.com/LASTRADA-Software/fastcached/issues/475)).
///
/// This is the single dependency-injection seam between the loop and the OS. Every
/// backend behaves identically; they differ only in what a wait costs and in which
/// platform provides them. `BackendParity_test` holds them to that, and
/// @c testing::ScriptedBackend lets a loop be driven with no kernel at all.

#include <core/net/NetError.hpp>
#include <core/platform/Clock.hpp>
#include <core/platform/Types.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string_view>

namespace core::net
{

/// What a registration is watched for. A bit set, so one registration can watch
/// readability and writability together.
enum class Interest : std::uint8_t
{
    None = 0,            ///< Watch nothing: mute the handle without detaching it.
    Read = 0b0000'0001,  ///< Watch for readability (data available, EOF, or hangup).
    Write = 0b0000'0010, ///< Watch for writability (space in the send buffer).
};

/// @param a The first mask.
/// @param b The second mask.
/// @return The union of two interest masks.
[[nodiscard]] constexpr Interest operator|(Interest a, Interest b) noexcept
{
    return static_cast<Interest>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

/// @param set The mask to test.
/// @param bit The single interest bit to look for.
/// @return True if @p bit is present in @p set.
[[nodiscard]] constexpr bool hasInterest(Interest set, Interest bit) noexcept
{
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(bit)) != 0;
}

/// What a native handle IS, which decides how a backend may wait on it.
///
/// One platform's handles are not interchangeable: a Windows waitable object is
/// waited on with `WaitForMultipleObjects` and a SOCKET is not, and an IOCP backend
/// associates a socket with its port and a console handle with a threadpool wait.
/// Stating the kind at registration is how a backend refuses what it cannot serve
/// instead of waiting on it and reporting nothing.
enum class HandleKind : std::uint8_t
{
    Fd,      ///< A POSIX file descriptor: what poll(2), epoll and kqueue take.
    Socket,  ///< A Winsock SOCKET.
    Waitable ///< A Windows object with a signalled state (an event, console input, a WSAEVENT).
};

/// What a handle is on this platform when the caller does not say.
#ifdef _WIN32
constexpr HandleKind DefaultHandleKind = HandleKind::Waitable;
#else
constexpr HandleKind DefaultHandleKind = HandleKind::Fd;
#endif

/// What a backend observed for one registration in a single wait, in the vocabulary
/// every kernel's answer is translated into.
///
/// Portable on purpose: the routing decision it feeds (@c selectReadinessCallback)
/// is where a real defect lived, and a unit test reaches it here without a socket, a
/// backend, or a way to provoke a kernel error.
enum class Readiness : std::uint8_t
{
    None = 0,               ///< Nothing was reported.
    Readable = 0b0000'0001, ///< Data, EOF, or an accepted connection is waiting.
    Writable = 0b0000'0010, ///< The send buffer has room.
    /// The kernel reported a failure condition: `EPOLLERR`/`EPOLLHUP`,
    /// `POLLERR`/`POLLHUP`/`POLLNVAL`, or a Windows wait that failed on the handle.
    /// It arrives whether or not it was asked for, and can arrive with neither
    /// direction set.
    ///
    /// @warning **Best-effort, and NOT portable. Nothing above a backend may depend on
    /// it.** It is a hint, never a fact. The backends genuinely disagree and are each
    /// right to: poll and epoll set it for a peer hangup, while kqueue reports the same
    /// hangup as ordinary readability — `EV_EOF` on a read filter means the peer called
    /// `shutdown(WR)`, which is an EOF and not an error — and Wfmo sets it only when a
    /// wait fails on the handle itself. Unifying them would mean either calling a
    /// normal close a failure on macOS or suppressing a real error elsewhere.
    ///
    /// What IS portable, and what every handler actually needs, is that **a peer hangup
    /// wakes the direction the handler watches, on every backend**. The caller is then
    /// woken, calls `read()` or `write()`, and learns what happened from that — the
    /// idiom on every platform. A handler that branches on @c Failed for correctness
    /// is a handler that behaves differently on macOS.
    Failed = 0b0000'0100,
};

/// @param a The first mask.
/// @param b The second mask.
/// @return The union of two readiness masks.
[[nodiscard]] constexpr Readiness operator|(Readiness a, Readiness b) noexcept
{
    return static_cast<Readiness>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

/// @param set The mask to test.
/// @param bit The single readiness bit to look for.
/// @return True if @p bit is present in @p set.
[[nodiscard]] constexpr bool hasReadiness(Readiness set, Readiness bit) noexcept
{
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(bit)) != 0;
}

struct ReadinessHandler;

/// What a backend invokes when a registration becomes ready. It may ENQUEUE and
/// nothing else: resuming a coroutine from here is the use-after-free
/// @c IoBackend's file comment describes.
using ReadinessCallback = void (*)(ReadinessHandler&) noexcept;

/// One registration: the handle to watch, who owns it, and what to call when it is
/// ready.
///
/// Function pointers rather than `std::function` because a handler is embedded in a
/// socket or a loop's park by the thousand and is registered on a hot path; the
/// `owner` back-pointer is how a callback recovers its enclosing object without
/// `offsetof`, which is undefined behaviour on the non-standard-layout types that
/// actually hold one.
///
/// The handler's ADDRESS is its registration's identity, so it must not move between
/// @c IoBackend::attach and @c IoBackend::detach.
struct ReadinessHandler
{
    platform::NativeHandle handle = platform::InvalidHandle; ///< The watched handle (not owned).
    HandleKind kind = DefaultHandleKind;                     ///< What @c handle is.
    void* owner = nullptr;                                   ///< The enclosing object, for the callbacks.

    ReadinessCallback onReadable = nullptr; ///< Called when @c handle is readable.
    ReadinessCallback onWritable = nullptr; ///< Called when @c handle is writable.

    /// Called for a failure that reaches NEITHER watched direction — a last resort,
    /// never a pre-emption.
    ///
    /// Optional, and it exists because an error has to have somewhere to go. A failure
    /// arrives whether or not it was asked for and can arrive with NEITHER direction
    /// set — a failed outbound connect is exactly that — and an event matching no
    /// branch is not harmlessly ignored: the registration is level-triggered, so it is
    /// reported again on the very next wait and the loop spins at 100% CPU while never
    /// telling anyone.
    ///
    /// It does **not** displace a direction the handler watches. A hangup that arrives
    /// as `POLLIN|POLLHUP` — a socket with unread bytes still in it — goes to
    /// @c onReadable, because the caller has a `read()` to learn the failure through
    /// and bytes to collect on the way. Setting this field therefore never costs a
    /// wakeup; before Ruling R101 it did, and the bytes were lost with it. A handler
    /// that leaves it null has such a failure delivered to whichever direction it does
    /// watch, which is what a parked read and a parked accept both want anyway.
    ReadinessCallback onError = nullptr;
};

/// Which of @p handler's callbacks services @p observed, or nullptr when none does.
///
/// Pure, and separate from every backend, because the two rules it encodes were each
/// a defect and a unit test can reach them here without a kernel:
///
/// - **A watched direction wins; a failure is routed, never dropped.** A failure that
///   arrives alongside a direction this handler watches goes to THAT direction, and
///   @c ReadinessHandler::onError takes it only when no watched direction accompanies
///   it. The older order — failure first — was a defect with teeth, because this
///   function returns exactly one callback: a peer hangup on a socket with unread
///   bytes arrives as `POLLIN|POLLHUP` on poll and epoll, so the moment a handler set
///   `onError` the reader stopped being woken and those bytes were never read. The
///   reason this order is *correct* and not merely safer is that no platform lets a
///   caller learn what went wrong from the readiness bits: it is woken, it calls
///   `read()` or `write()`, and that reports the error. A reader needs the wakeup so
///   it can read 0; a dial needs it and then checks `SO_ERROR`. Neither asks which
///   callback fired.
/// - **At most one callback per registration per wait.** A callback resumes nothing,
///   but it does enqueue, and the loop that later drains that queue may run a frame
///   that frees the object this handler is embedded in. Dereferencing @p handler a
///   second time in the same walk is then a use-after-free. Servicing one condition
///   costs nothing: level-triggering reports the rest on the next wait
///   (`.agent/rules/async-and-net.md`).
///
/// @param handler The handler the readiness was reported for.
/// @param observed What the backend saw, translated out of the kernel's own bits.
/// @return The callback to invoke, or nullptr when the handler watches nothing this
///         readiness speaks to.
[[nodiscard]] constexpr ReadinessCallback selectReadinessCallback(ReadinessHandler const& handler,
                                                                  Readiness observed) noexcept
{
    // A watched direction first, INCLUDING when a failure came with it: the caller has
    // a read or a write to have the error reported through, and taking the wakeup away
    // from it loses whatever the kernel had already buffered.
    if (hasReadiness(observed, Readiness::Readable) && handler.onReadable != nullptr)
        return handler.onReadable;
    if (hasReadiness(observed, Readiness::Writable) && handler.onWritable != nullptr)
        return handler.onWritable;
    auto const failed = hasReadiness(observed, Readiness::Failed);
    // Only now: a failure with no watched direction to carry it. The failed outbound
    // connect that arrives as POLLERR alone is exactly this, and it is what onError
    // exists for.
    if (failed && handler.onError != nullptr)
        return handler.onError;
    // And a failure with neither a direction nor an onError still has to wake someone,
    // or a level-triggered registration reports it again on every wait for ever.
    if (failed)
        return handler.onReadable != nullptr ? handler.onReadable : handler.onWritable;
    return nullptr;
}

/// Which multiplexing backend an @c IoBackend is.
///
/// Named rather than probed: a test says which backend it wants, and
/// @c makeBackend answers null for one this platform does not build. The default
/// per platform is @c preferredBackendKind().
enum class BackendKind : std::uint8_t
{
    Poll = 0,   ///< poll(2), POSIX. Portable; a wait is O(registered).
    Epoll,      ///< epoll(7), Linux only. A wait is O(ready).
    Kqueue,     ///< kqueue(2), macOS and the BSDs. A wait is O(ready).
    Iocp,       ///< I/O completion ports, Windows. Arrives in Task B7; @c makeBackend answers null.
    Wfmo,       ///< WSAEventSelect + WaitForMultipleObjects, Windows. IOCP's fallback for one release.
    HostDriven, ///< No wait of its own: a host (a browser's event loop, a Qt one) pumps the loop.
    Scripted,   ///< The test double whose readiness a case writes out in advance.
    Null,       ///< Reports nothing, ever. What a loop with no I/O at all is driven by.

    Last, ///< Not a kind: the number of kinds above it, so a table or a test can cover every one
          ///< without restating the list. Never constructed, never returned, never compared against.
};

/// @param kind The backend kind to name.
/// @return Its lowercase name, or `"unknown"` for a value that is not one of the kinds
///         (including `Last`).
///
/// No `default` in the switch, deliberately: a kind added without a row here then fails to
/// compile rather than rendering as `"unknown"` in every label that carries it.
[[nodiscard]] constexpr std::string_view toString(BackendKind kind) noexcept
{
    switch (kind)
    {
        case BackendKind::Poll: return "poll";
        case BackendKind::Epoll: return "epoll";
        case BackendKind::Kqueue: return "kqueue";
        case BackendKind::Iocp: return "iocp";
        case BackendKind::Wfmo: return "wfmo";
        case BackendKind::HostDriven: return "host-driven";
        case BackendKind::Scripted: return "scripted";
        case BackendKind::Null: return "null";
        case BackendKind::Last: break;
    }
    return "unknown";
}

/// What one @c IoBackend::wait observed and dispatched.
///
/// A count rather than a list: the ready registrations have already been serviced by
/// the time this returns — that is what "backends dispatch" means — so there is
/// nothing left for the caller to route. The loop uses it to tell a wait that did
/// something from one that only timed out; a test uses it to pin how many callbacks
/// a single wait is allowed to run.
struct WaitResult
{
    /// How many handler callbacks this wait invoked. At most one per registration:
    /// a callback may free the object its handler is embedded in, so a second
    /// callback on the same handler would dereference freed memory. Level-triggered
    /// interest re-reports whatever was left over on the next wait.
    std::size_t dispatched = 0;

    /// @return True if two results report the same dispatch count.
    [[nodiscard]] friend constexpr bool operator==(WaitResult, WaitResult) noexcept = default;
};

/// The wait an @c EventLoop drives, and the readiness dispatcher behind it.
///
/// Lifetime: every registered @c ReadinessHandler must be detached before it is
/// destroyed, and the backend must outlive the loop that drives it.
///
/// Threading: every member but @c wake() must be called on the thread that calls
/// @c wait(). @c wake() is the one thread-safe member, and it is how another thread
/// breaks an in-flight wait.
class IoBackend
{
  public:
    IoBackend() = default;
    virtual ~IoBackend() = default;

    IoBackend(IoBackend const&) = delete;
    IoBackend& operator=(IoBackend const&) = delete;
    IoBackend(IoBackend&&) = delete;
    IoBackend& operator=(IoBackend&&) = delete;

    /// @return Which backend this is. For a label in a test's section name, and for the
    ///         one or two places a caller genuinely has to know (`makeBackend` round-trips
    ///         through it).
    [[nodiscard]] virtual BackendKind kind() const noexcept = 0;

    /// Registers @p handler with this backend, watching nothing yet.
    ///
    /// Interest arrives separately, through @c setInterest, and that split is the
    /// kernels' rather than a preference: epoll has an "add with no interest"
    /// operation and kqueue does not — a kqueue filter IS the registration — so an
    /// `attach` that claimed to have registered the descriptor would be telling the
    /// truth on one platform and not the other. What this answers is that the handler
    /// and this backend are usable together; whether the KERNEL knows about the
    /// descriptor is what @c setInterest answers, and only that.
    /// ([fastcached#1057](https://github.com/LASTRADA-Software/fastcached/issues/1057).)
    ///
    /// The handler's address is the registration's identity and must be stable until
    /// @c detach. The same native handle may be attached through two handlers — a
    /// reader and a writer park on one socket — and each is its own registration.
    /// @param handler The handler to register (not owned; the caller keeps it alive
    ///        and detaches it before destroying it).
    /// @return Nothing on success, or why the registration was refused:
    ///         @c NetErrorCode::BadHandle for an invalid handle,
    ///         @c NetErrorCode::Unsupported on a backend with no readiness at all
    ///         (@c HostDrivenBackend), @c NetErrorCode::SystemError when the backend's
    ///         own kernel object could not be created.
    [[nodiscard]] virtual std::expected<void, NetError> attach(ReadinessHandler& handler) = 0;

    /// Sets what @p handler is watched for, arming or dropping the kernel registration.
    ///
    /// @c Interest::None mutes the registration without detaching it, and mute means
    /// SILENT on every backend: a muted registration reports nothing, not even the
    /// hangup and error conditions a kernel volunteers whatever was asked for. epoll
    /// and poll(2) both report `EPOLLHUP`/`POLLHUP` for a registered descriptor
    /// regardless of interest, so a muted one left in the set woke the flow the caller
    /// had asked to be silent — on epoll, on every single wait, spinning the pump while
    /// it did. It is therefore removed from the wait set outright, and @c detach still
    /// finds it.
    ///
    /// **The kernel's refusal is reported, never swallowed.** A registration the caller
    /// believes succeeded and the kernel never accepted parks a flow with nothing left
    /// to resume it — the hang has no message and no stack. kqueue refuses a filter on a
    /// descriptor it cannot arm, and epoll and kqueue both need a private descriptor for
    /// a second registration of one handle, which descriptor exhaustion denies.
    /// ([fastcached#1054](https://github.com/LASTRADA-Software/fastcached/issues/1054),
    /// [fastcached#1057](https://github.com/LASTRADA-Software/fastcached/issues/1057).)
    /// @param handler A handler previously attached to this backend.
    /// @param interest What to watch for; @c Interest::None mutes it.
    /// @return Nothing on success, or why the kernel refused.
    [[nodiscard]] virtual std::expected<void, NetError> setInterest(ReadinessHandler& handler,
                                                                    Interest interest) = 0;

    /// Removes @p handler's registration, and withdraws it from the ready batch a
    /// @c wait in flight is walking.
    ///
    /// The second half is the load-bearing one. Dropping the kernel registration stops
    /// FUTURE reports; it does nothing about entries the kernel has already written
    /// into the batch this wait dequeued. A callback that detaches another handler and
    /// then frees its owner would otherwise leave a dangling entry the same dispatch
    /// walks into. Withdrawing here rather than validating at dispatch is deliberate:
    /// the handler is still ALIVE at this moment, so the comparison is against a live
    /// pointer and needs no generation counter — a scheme that validated a dequeued
    /// pointer after the fact would have to survive address reuse, which a bare pointer
    /// cannot. This is why @c detach must be called BEFORE the handler's owner is freed.
    /// ([fastcached#475](https://github.com/LASTRADA-Software/fastcached/issues/475).)
    ///
    /// Idempotent: detaching an unattached or already-detached handler is a no-op.
    /// @param handler The handler to remove. Must still be alive.
    virtual void detach(ReadinessHandler& handler) noexcept = 0;

    /// Waits for readiness and dispatches it: the only blocking call in the layer, and
    /// the only place a handler callback runs.
    ///
    /// It NEVER resumes a coroutine. The callbacks it invokes enqueue, and the loop
    /// resumes what they enqueued after this returns.
    /// @param timeout How long to block, or nullopt to block until something happens.
    ///        A zero duration polls.
    /// @return How many callbacks ran.
    [[nodiscard]] virtual WaitResult wait(std::optional<platform::SteadyDuration> timeout) = 0;

    /// Breaks an in-flight @c wait, from any thread. The ONE member that is safe to
    /// call off the wait's thread; a wake with no wait in flight makes the next wait
    /// return at once rather than being lost.
    virtual void wake() noexcept = 0;

    /// @return True if this backend has no wait of its own and is pumped by a host
    ///         (a browser's event loop, a Qt one). A host-driven loop must not be
    ///         `run()` or blocked on; it advances only when the host pumps it.
    [[nodiscard]] virtual bool isHostDriven() const noexcept { return false; }

    /// Asks the host to pump the loop at @p deadline. Meaningful only on a host-driven
    /// backend, where the loop has no wait to put a timeout on; every other backend
    /// takes its timeout through @c wait and ignores this.
    /// @param deadline When the next pump is due, or nullopt if nothing is scheduled.
    virtual void armWakeAt(std::optional<platform::SteadyTimePoint> /*deadline*/) noexcept {}
};

/// @return The kind @c makeDefaultBackend() prefers on this platform, whether or not
///         constructing it would succeed.
[[nodiscard]] BackendKind preferredBackendKind() noexcept;

/// Creates the best backend available on this platform.
///
/// Prefers the scalable native one and falls back when it is unavailable — the
/// platform has none, or the kernel refused to create its object under descriptor
/// exhaustion. The fallback is silent by design: every backend is behaviourally
/// equivalent, so a caller has nothing to decide.
/// @return A backend, never null.
/// @throws std::runtime_error if no backend could create its wakeup channel, which
///         is descriptor or handle exhaustion and not a condition a caller recovers
///         from — a loop that cannot be woken across threads deadlocks on shutdown.
[[nodiscard]] std::unique_ptr<IoBackend> makeDefaultBackend();

/// Creates a backend of a specific kind — for a test that must exercise one in
/// particular rather than whatever this platform prefers.
/// @param kind The backend to build.
/// @return The backend, or nullptr if @p kind is not built on this platform or its
///         kernel object could not be created.
/// @throws std::runtime_error as @c makeDefaultBackend does.
[[nodiscard]] std::unique_ptr<IoBackend> makeBackend(BackendKind kind);

} // namespace core::net
