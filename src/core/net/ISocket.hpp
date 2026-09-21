// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `ISocket` — a connected, bidirectional byte transport whose operations are frame-free,
/// stop-aware awaitables.
///
/// Implementations: @c PosixSocket / @c WindowsSocket (production, reactor-driven non-blocking
/// I/O), @c TlsSocket (a decorator) and @c InMemoryTransport (deterministic, in-process, for
/// tests). The production ones are driven by an @c EventLoop: a slow read or write parks the
/// operation on the loop's park table rather than blocking a thread, and allocates no coroutine
/// frame to do it — see `<core/net/IoAwaitable.hpp>` for why that shape, and what it costs a
/// caller that needs to STORE an operation (`core::async::asTask`).
///
/// Merged from contour's `net::ISocket` and fastcached's `FastCache::ISocket`
/// (`FastCache/Net/ISocket.hpp` at `0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`).

#include <core/net/IoAwaitable.hpp>
#include <core/net/IoResult.hpp>
#include <core/net/SocketContract.hpp>

#include <chrono>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>

namespace core::net
{

/// One read's payload plus an optionally received file descriptor (SCM_RIGHTS over AF_UNIX).
/// @c fd is -1 when none arrived; ownership of a received fd transfers to the caller.
struct ReadWithFd
{
    std::size_t bytesRead = 0;
    int fd = -1;
};

/// A connected, streamed, bidirectional byte transport.
///
/// **Every buffer passed here must stay valid, and at the same address, until the operation
/// resumes.** The operation may park and resume on a later turn of the loop, and a completion-based
/// backend writes into the buffer while it is parked — so it is a requirement rather than a
/// convention, and it is IOCP that makes it one.
class ISocket
{
  public:
    ISocket() = default;
    virtual ~ISocket() = default;

    ISocket(ISocket const&) = delete;
    ISocket& operator=(ISocket const&) = delete;
    ISocket(ISocket&&) = delete;
    ISocket& operator=(ISocket&&) = delete;

    /// Reads up to @p buffer.size() bytes into @p buffer.
    ///
    /// **The buffer must be non-empty, and that is a precondition rather than a preference.** `0`
    /// here means *the peer has finished sending*, so a zero-length read has no truthful answer to
    /// give: every transport's receive primitive returns `0` for it, and the caller is handed a
    /// graceful close that never happened. Enforced by @c contract::requireReadBuffer in Debug
    /// builds ([fastcached#838](https://github.com/LASTRADA-Software/fastcached/issues/838)).
    ///
    /// **A socket has ONE read operation, and this shares it with @c readWithFd and
    /// @c waitReadable.** Arming any of them while another is parked drops the parked awaitable:
    /// that coroutine is never resumed and never freed, with no assertion, no error and no log.
    /// Enforced by @c contract::claimReadSlot in Debug builds
    /// ([fastcached#663](https://github.com/LASTRADA-Software/fastcached/issues/663)). What a
    /// caller must therefore do: hold at most one outstanding read per socket, and resolve or
    /// abandon a parked @c waitReadable through @c cancelRead before issuing a `read`.
    ///
    /// @param buffer Destination span; must be non-empty and outlive the operation.
    /// @return The byte count read, `0` on a clean EOF, or a @c NetError. Throws
    ///         @c async::OperationCancelled if the awaiting flow's own token is stopped; a cancel
    ///         from the SOCKET (@c close, @c cancelRead) is a @c NetErrorCode::Cancelled value.
    [[nodiscard]] virtual IoAwaitable read(std::span<std::byte> buffer) = 0;

    /// Reads like @c read but also accepts ONE SCM_RIGHTS file descriptor where the transport
    /// supports fd passing (AF_UNIX on POSIX).
    ///
    /// The default reads through @c read and reports `fd = -1`, which is the documented behaviour
    /// everywhere fd passing is not a thing, Windows included. It costs a coroutine frame, because
    /// a base class has nowhere frame-free to keep the inner operation — a transport that can pass
    /// descriptors implements this natively over its own read slot instead, and every one here
    /// does. It shares the read-op slot with @c read and @c waitReadable.
    /// @param buffer Destination span; must be non-empty and outlive the operation.
    /// @return Bytes read (`0` = clean EOF) plus the received fd or -1.
    [[nodiscard]] virtual ResultAwaitable<ReadWithFd> readWithFd(std::span<std::byte> buffer);

    /// Writes ALL of @p buffer's bytes, looping over partial writes and backpressure.
    ///
    /// **It resolves only once every byte is gone**, which is why it is multi-step and why the
    /// retry runs at the socket rather than in the awaiting coroutine: a buffer larger than the
    /// send window takes as many writable edges as it takes.
    ///
    /// A socket has ONE write operation; arming a second over a parked one drops it, exactly as on
    /// the read side. Enforced by @c contract::claimWriteSlot in Debug builds
    /// ([fastcached#893](https://github.com/LASTRADA-Software/fastcached/issues/893)).
    /// @param buffer Source span; must outlive the operation.
    /// @return The byte count written (== `buffer.size()` on success), or a @c NetError.
    [[nodiscard]] virtual IoAwaitable write(std::span<std::byte const> buffer) = 0;

    /// Gather-write: sends every segment in order as one logical write, using a single scattered
    /// syscall (`sendmsg`/`WSASend`) where the platform allows.
    ///
    /// Avoids copying a large payload into one contiguous buffer — the canonical use is a reply
    /// assembled as `[header][body][trailer]` where `body` points straight into a
    /// reference-counted payload. Resolves only once ALL bytes are sent.
    ///
    /// **Every segment's bytes AND the @p segments span itself must stay valid and at a stable
    /// address until the operation resumes.** To anchor a reference-counted payload for exactly
    /// that long, pass it as @p keepAlive: the implementation stores the handle alongside the
    /// in-flight operation, so the bytes outlive the suspension even if the caller's own owner goes
    /// out of scope. It is type-erased so any owner shape works.
    /// The default writes each segment in turn through @c write, which keeps the part of the
    /// contract that matters — every byte, in order, before it resolves — and gives up only the
    /// syscall count, which a transport with no scattered send was never going to have.
    /// @param segments Ordered, non-owning views to gather, in send order.
    /// @param keepAlive Optional owner pinning the segments' backing storage.
    /// @return The total byte count written, or a @c NetError.
    [[nodiscard]] virtual IoAwaitable writeVectored(std::span<std::span<std::byte const> const> segments,
                                                    std::shared_ptr<void const> keepAlive = {});

    /// Performs any transport-level handshake required before application I/O.
    ///
    /// Plaintext sockets need none, so the default resolves immediately; a TLS decorator overrides
    /// it to drive the handshake. An accept loop awaits this once before protocol autodetection, so
    /// it stays transport-agnostic and a slow handshake runs on the per-connection flow rather than
    /// blocking the accept loop.
    /// @return Nothing on success, or a @c NetError.
    [[nodiscard]] virtual ResultAwaitable<void> handshakeIfNeeded();

    /// Suspends until the socket is readable — data or EOF pending — WITHOUT consuming any bytes.
    ///
    /// **The count says WHICH kind of readable, and it is not advisory**
    /// ([fastcached#677](https://github.com/LASTRADA-Software/fastcached/issues/677)):
    ///
    ///   - `0`  the peer has closed its write side. A @c read here returns EOF.
    ///   - `>0` bytes are pending. A @c read here returns some of them.
    ///
    /// It was once documented as advisory, and it was: the reactor sockets answered `1` whatever
    /// their `recv(MSG_PEEK)` had just measured, and a completion-based one completed with the byte
    /// count of a ZERO-byte receive, which is `0` however much data is waiting — so the same call
    /// reported opposite numbers on Windows and Linux for the same event. The kernel draws the
    /// distinction and the probe already computes it; only the reporting threw it away.
    ///
    /// **The default answers `1`, and that is the fail-safe direction.** A transport that cannot
    /// tell must not claim EOF: a false `>0` costs one `read` that discovers the truth, while a
    /// false `0` tells a caller its peer is gone.
    ///
    /// **"Consumes nothing" is about bytes the CALLER could have read**, not about the transport's
    /// own buffering. A decorator may have to consume and decode raw bytes to answer at all — a TLS
    /// peer's close is a record, and a raw peek reads it as pending data — and what the contract
    /// forbids is losing a byte the next `read` would have returned.
    ///
    /// This shares the socket's single read-op slot with @c read and @c readWithFd; see @c read.
    /// @return `0` for EOF, `>0` for pending data, or a @c NetError.
    [[nodiscard]] virtual IoAwaitable waitReadable();

    /// Retires a read-side operation THIS caller armed, freeing the coroutine parked on it, without
    /// closing the socket.
    ///
    /// **Until this existed, a parked read could only be retrieved by @c close, which is what made
    /// the caller-side rule on @c read unfollowable**: a caller holding a parked @c waitReadable
    /// must resolve or abandon it before it reads, and *abandon* had no spelling short of tearing
    /// the connection down
    /// ([fastcached#710](https://github.com/LASTRADA-Software/fastcached/issues/710)).
    ///
    /// **It is the CALLER's call, and that is why it is a verb here** rather than something `read`
    /// does for you: at the arm site a socket cannot tell a stale parked wait from a live one, and
    /// cancelling a live one is a false disconnect that drops a healthy client. The caller can
    /// tell, because it knows when its own iteration ended.
    ///
    /// **The parked awaitable is COMPLETED with @c NetErrorCode::Cancelled, not dropped**, or this
    /// would be the leak it exists to remove — and as a VALUE rather than an exception, because a
    /// cancel from the resource is not a cancel of the flow.
    ///
    /// **It retires whatever is parked NOW, which is not the same as being idempotent**
    /// ([fastcached#1233](https://github.com/LASTRADA-Software/fastcached/issues/1233)). A
    /// retirement that completes its victim INLINE resumes that coroutine before this call returns,
    /// so a coroutine that arms its next read there leaves a NEW read in the slot and a second call
    /// retires THAT one. A call with the slot EMPTY is a no-op.
    ///
    /// **The default does nothing, and that is for FAKES** — a scripted double whose reads resolve
    /// inline has no frame to free. **It is NOT a safe default for a transport whose reads park**,
    /// and every transport this library hands out that can park a read overrides it. A new one that
    /// can must too, and nothing but this sentence enforces that.
    ///
    /// Not a @c close: the socket stays open and a later @c read works.
    virtual void cancelRead() noexcept {}

    /// Closes this socket's WRITE half, leaving the read half open, so the peer observes EOF while
    /// this side can still receive.
    ///
    /// **What EOF means on this wire, decided once**
    /// ([fastcached#671](https://github.com/LASTRADA-Software/fastcached/issues/671)): *the peer
    /// has finished sending*. A server answers everything already determined and abandons anything
    /// still pending. So a half-close is a statement about INPUT, not a departure.
    ///
    /// **The default does nothing, and that is for FAKES.** A no-op costs a peer the early EOF — it
    /// learns at the eventual @c close instead — which delays a notification rather than falsifying
    /// one.
    ///
    /// Idempotent, and not a @c close: reads keep working and @c isClosed stays false. A caller
    /// that wants both calls both.
    virtual void shutdownWrite() noexcept {}

    /// Re-arms how long a single read may wait before it reports a deadline expiry.
    ///
    /// Exists so a caller can hold **two** different bounds over one connection: *how long may this
    /// peer stay silent before it asks anything* and *how long may one read take once it has
    /// started* are different questions, and a surface that answers them with one number gets one
    /// of them wrong
    /// ([fastcached#828](https://github.com/LASTRADA-Software/fastcached/issues/828)).
    ///
    /// **The default does nothing, and unlike @c cancelRead's that is safe.** A transport that
    /// cannot re-arm keeps whatever bound it already had, so the behaviour degrades to exactly what
    /// it was before this existed — a weaker bound, never a wrong one.
    ///
    /// A reactor socket implements it over its loop's timers, so it costs no wait of its own: there
    /// is ONE deadline mechanism in this library and a receive deadline is a consumer of it.
    /// @param deadline How long a read may wait. Non-positive leaves the current setting alone,
    ///        matching `SO_RCVTIMEO`'s own reading of zero.
    virtual void setReceiveDeadline(std::chrono::milliseconds deadline) noexcept;

    /// @return The remote peer's printable address ("127.0.0.1", "::1"), or "" if unknown (an
    ///         in-memory transport).
    [[nodiscard]] virtual std::string peerAddress() const { return {}; }

    /// Closes the socket. Idempotent; a subsequent read or write resolves with
    /// @c NetErrorCode::BadHandle.
    ///
    /// **A parked operation is retrieved here, and — apart from @c cancelRead — this is the only
    /// thing that can retrieve one.** An implementation that leaves a parked read or write alone
    /// leaves a coroutine frame nobody can free. Every transport this library hands out completes
    /// such an operation with @c NetErrorCode::Cancelled, as a VALUE: the flow is alive and asked
    /// about a socket that has gone away.
    ///
    /// **The order is load-bearing: detach the operation FIRST, complete it LAST, and touch no
    /// member afterwards.** Completing resumes the awaiting coroutine, and a coroutine that OWNS
    /// the socket runs to its end and destroys it before the completion returns — a
    /// heap-use-after-free this lineage has already had.
    virtual void close() noexcept = 0;

    /// @return True once @c close has been called, or a read observed the peer's EOF.
    ///
    /// The second half is what a consumer polls this for: a connection whose peer hung up is no
    /// longer worth holding, and a socket that reported it only for its OWN close left such a
    /// consumer polling a dead connection for ever.
    ///
    /// The EOF latch is answered here and nowhere else: a peer that shut only its write side leaves
    /// this end able to keep writing, so @c write keeps working — and @c read keeps returning 0 —
    /// after this turns true.
    [[nodiscard]] virtual bool isClosed() const noexcept = 0;
};

/// A connected socket, or why one could not be produced.
///
/// One name because accept and connect answer the same question and their results are the same
/// type, so a helper that consumes one consumes the other.
using SocketResult = std::expected<std::unique_ptr<ISocket>, NetError>;

} // namespace core::net
