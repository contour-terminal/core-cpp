// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Test-only helpers the `core::http` cases share: raw wire text written to a socket, a body read
/// to its end, a flow run on a hand-driven loop until it has an answer, a server end a case scripts,
/// and a TLS context whose handshake and failures a case scripts (R29: no `core::net_tls`, so the
/// binaries build without `CORE_CPP_WITH_TLS`). Test-only: in no `FILE_SET`, so never installed.

#include <core/async/Task.hpp>
#include <core/http/Body.hpp>
#include <core/http/Connection.hpp>
#include <core/http/Framing.hpp>
#include <core/http/HttpClient.hpp>
#include <core/http/Message.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/ISocket.hpp>
#include <core/net/ITlsContext.hpp>
#include <core/net/ITlsSession.hpp>
#include <core/net/IoAwaitable.hpp>
#include <core/net/NetError.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core::http::test
{

/// Whether this process runs under wine rather than on Windows or a POSIX system. Defined per
/// platform (`posix/HttpTestSupport.cpp`, `windows/HttpTestSupport.cpp`); only a binary that links
/// one of them may call it. A case uses it to accept a behaviour wine has and no native kernel
/// does, and to fail where the same behaviour shows natively. A @c bool because the name asks it.
[[nodiscard]] bool runsUnderWine() noexcept;

/// @param text The bytes to view.
/// @return @p text as the byte span a socket write takes.
[[nodiscard]] inline std::span<std::byte const> bytesOf(std::string_view text) noexcept
{
    return { reinterpret_cast<std::byte const*>(text.data()), text.size() };
}

/// @param bytes Bytes a socket or a body reader handed out.
/// @return The same bytes as text.
[[nodiscard]] inline std::string textOf(std::span<std::byte const> bytes)
{
    return { reinterpret_cast<char const*>(bytes.data()), bytes.size() };
}

/// Writes @p text to @p socket in full, then closes it: a peer that sends exactly these bytes.
/// @param socket The writer; must outlive the returned task.
/// @param text The wire bytes, owned by the frame.
inline async::Task<void> writeAndClose(net::ISocket* socket, std::string text)
{
    if (!text.empty())
        static_cast<void>(co_await socket->write(bytesOf(text)));
    socket->close();
}

/// Writes @p text to @p socket in full and leaves it open: a peer that then falls silent.
/// @param socket The writer; must outlive the returned task.
/// @param text The wire bytes, owned by the frame.
inline async::Task<void> writeOnly(net::ISocket* socket, std::string text)
{
    static_cast<void>(co_await socket->write(bytesOf(text)));
}

/// Reads from @p socket until EOF or an error: what a peer received.
/// @param socket The reader; must outlive the returned task.
/// @return Every byte read, in order.
inline async::Task<std::string> readUntilClosed(net::ISocket* socket)
{
    auto text = std::string {};
    auto chunk = std::vector<std::byte>(256);
    while (true)
    {
        auto const got = co_await socket->read(std::span<std::byte> { chunk });
        if (!got.has_value() || *got == 0)
            co_return text;
        text += textOf(std::span<std::byte const> { chunk }.first(*got));
    }
}

/// Reads one bodiless request head off @p socket as raw bytes and answers it with @p reply: a server
/// end that takes a second request on a connection a @c ScriptedServer, whose @c Connection holds its
/// raw reply for unwritten, has already served.
/// @param socket The server end; must outlive the returned task.
/// @param reply The raw response bytes.
/// @return The request's start line, or empty if the peer closed or failed first.
inline async::Task<std::string> answerHeadOnly(net::ISocket* socket, std::string reply)
{
    auto text = std::string {};
    auto chunk = std::vector<std::byte>(512);
    while (!text.contains("\r\n\r\n"))
    {
        auto const got = co_await socket->read(std::span<std::byte> { chunk });
        if (!got.has_value() || *got == 0)
            co_return std::string {};
        text += textOf(std::span<std::byte const> { chunk }.first(*got));
    }
    static_cast<void>(co_await socket->write(bytesOf(reply)));
    co_return text.substr(0, text.find("\r\n"));
}

/// A body read to its end, chunk by chunk.
struct ReadBody
{
    std::string text;                    ///< Every body byte, in order.
    std::vector<std::size_t> chunkSizes; ///< The size of each chunk @c read handed out.
    std::optional<HttpError> error;      ///< What ended the read early, if anything did.
};

/// Reads @p reader until @c BodyEnd::Reached or an error, @p maxBytes at a time.
/// @param reader The body; must outlive the returned task.
/// @param maxBytes What each @c read asks for.
/// @return What was read and what, if anything, ended it.
inline async::Task<ReadBody> readAll(IBodyReader* reader, std::size_t maxBytes)
{
    auto out = ReadBody {};
    while (true)
    {
        auto chunk = co_await reader->read(maxBytes);
        if (!chunk.has_value())
        {
            out.error = std::move(chunk.error());
            co_return out;
        }
        out.chunkSizes.push_back(chunk->bytes.size());
        out.text += textOf(chunk->bytes);
        if (chunk->end == BodyEnd::Reached)
            co_return out;
    }
}

/// Runs @p task and stores its answer in @p out, for a case that drives the loop by hand and then
/// asserts the answer is there: a flow that never finishes leaves @p out empty, never a hang.
/// @tparam T The task's result.
/// @param task The flow.
/// @param out Where its answer goes; must outlive the returned task.
template <typename T>
async::Task<void> into(async::Task<T> task, std::optional<T>* out)
{
    *out = co_await std::move(task);
}

// -------------------------------------------------------------------------------------------------
// The server end of a scripted exchange.

/// What a server end read of one request: the head and the body.
struct ServedRequest
{
    std::optional<RequestHead> head; ///< The head, if it was read.
    ReadBody body;                   ///< The body, as far as it was read.
    std::optional<HttpError> error;  ///< What ended the read early, if anything did.
};

/// Whether a scripted server closes its end after its reply.
enum class AfterReply : std::uint8_t
{
    StaysOpen,
    Closes,
};

/// The server end of one connection a case scripts: it reads a request through a @c Connection in
/// the request role, and answers with raw bytes, so a case can send what no well-behaved server would.
class ScriptedServer
{
  public:
    /// @param loop The loop the connection runs on.
    /// @param socket The server end, from @c testing::ScriptedConnector::acceptNext; must not be null.
    ScriptedServer(net::EventLoop& loop, std::unique_ptr<net::ISocket> socket):
        _connection { loop, std::move(socket), MessageRole::Request, HttpLimits {}, WireTimeouts {} }
    {
    }

    /// @return The connection, for its socket.
    [[nodiscard]] Connection& connection() noexcept { return _connection; }

    /// Reads one request, its head and its body to the end, then writes @p reply as it is.
    /// @param reply The raw response bytes; none writes nothing.
    /// @param after Whether to close once they are written.
    /// @return What was read.
    [[nodiscard]] async::Task<ServedRequest> answer(std::string reply, AfterReply after)
    {
        return answerOn(&_connection, std::move(reply), after);
    }

  private:
    static async::Task<ServedRequest> answerOn(Connection* connection, std::string reply, AfterReply after)
    {
        auto out = ServedRequest {};
        auto head = co_await connection->readRequestHead();
        if (!head.has_value())
        {
            out.error = std::move(head.error());
            co_return out;
        }
        auto framing = requestFraming(head->version, head->fields);
        if (!framing.has_value())
        {
            out.error = std::move(framing.error());
            co_return out;
        }
        out.head = std::move(*head);
        auto reader = connection->bodyReader(*framing);
        out.body = co_await readAll(reader.get(), 4096);
        if (!reply.empty())
            static_cast<void>(co_await connection->socket().write(bytesOf(reply)));
        if (after == AfterReply::Closes)
            connection->close();
        co_return out;
    }

    Connection _connection;
};

/// A request for @p url, `http://host[:port]/target` or `https://…`, IPv6 hosts in brackets; without
/// a port the origin names none, so the scheme's default applies.
/// @param url The URL; anything else fails the case.
/// @param method The method.
/// @return The request, without a body.
[[nodiscard]] inline OutgoingRequest get(std::string_view url, std::string_view method = "GET")
{
    auto request = OutgoingRequest {};
    auto rest = url;
    if (rest.starts_with("https://"))
    {
        request.origin.scheme = Scheme::Https;
        rest.remove_prefix(8);
    }
    else
    {
        REQUIRE(rest.starts_with("http://"));
        rest.remove_prefix(7);
    }
    auto const slash = rest.find('/');
    auto authority = rest.substr(0, slash);
    auto const target = slash == std::string_view::npos ? std::string_view { "/" } : rest.substr(slash);
    auto const portAt = authority.rfind(':');
    if (portAt != std::string_view::npos && authority.find(']', portAt) == std::string_view::npos)
    {
        auto const digits = authority.substr(portAt + 1);
        auto port = std::uint16_t { 0 };
        auto const parsed = std::from_chars(digits.data(), digits.data() + digits.size(), port);
        REQUIRE(parsed.ec == std::errc {});
        request.origin.port = port;
        authority = authority.substr(0, portAt);
    }
    request.origin.host = std::string { authority };
    auto parsedMethod = Method::parse(method);
    REQUIRE(parsedMethod.has_value());
    request.head.method = std::move(*parsedMethod);
    auto parsedTarget = RequestTarget::parse(target);
    REQUIRE(parsedTarget.has_value());
    request.head.target = std::move(*parsedTarget);
    return request;
}

/// Starts @p client sending @p request on @p loop; its answer lands in @p out.
/// @param loop The client's loop.
/// @param client The client; must outlive the flow.
/// @param request The request.
/// @param out Where the answer goes; must outlive the flow.
inline void startSend(net::EventLoop& loop,
                      HttpClient& client,
                      OutgoingRequest request,
                      std::optional<HttpResult<IncomingResponse>>* out)
{
    loop.spawn(into(client.send(std::move(request)), out));
}

/// The next chunk after @p gap on @p loop's clock, or a failure of code @p fail instead.
inline async::Task<HttpResult<BodyChunk>> chunkAfter(net::EventLoop* loop,
                                                     std::chrono::milliseconds gap,
                                                     std::string text,
                                                     BodyEnd end,
                                                     std::optional<HttpErrorCode> fail)
{
    co_await loop->delay(gap);
    if (fail.has_value())
    {
        auto error = HttpError {};
        error.code = *fail;
        co_return std::unexpected(std::move(error));
    }
    auto const bytes = bytesOf(text);
    co_return BodyChunk { .bytes = std::vector<std::byte>(bytes.begin(), bytes.end()), .end = end };
}

/// @return No trailers.
inline async::Task<HttpResult<Fields>> noTrailers()
{
    co_return Fields {};
}

/// A body of a known length whose chunks arrive @c gap apart on the loop's clock: an upload slower
/// than a response timeout. With @c failAt set, that chunk fails (@c HttpErrorCode::InternalError)
/// instead of arriving.
class SlowBodySource final: public IBodySource
{
  public:
    /// @param loop Whose clock the gaps are measured on.
    /// @param chunks The body, chunk by chunk; must not be empty.
    /// @param gap The wait before each chunk.
    /// @param failAt The index of the chunk that fails, if one does.
    SlowBodySource(net::EventLoop& loop,
                   std::vector<std::string> chunks,
                   std::chrono::milliseconds gap,
                   std::optional<std::size_t> failAt = std::nullopt):
        _loop { &loop }, _chunks { std::move(chunks) }, _gap { gap }, _failAt { failAt }
    {
    }

    [[nodiscard]] std::optional<std::uint64_t> knownLength() const noexcept override
    {
        auto total = std::uint64_t { 0 };
        for (auto const& chunk: _chunks)
            total += chunk.size();
        return total;
    }

    [[nodiscard]] TrailerIntent trailerIntent() const noexcept override { return TrailerIntent::None; }

    [[nodiscard]] async::Task<HttpResult<BodyChunk>> next() override
    {
        auto const index = _next++;
        auto const end = _next == _chunks.size() ? BodyEnd::Reached : BodyEnd::More;
        auto const fail = _failAt == index ? std::optional { HttpErrorCode::InternalError } : std::nullopt;
        return chunkAfter(_loop, _gap, _chunks.at(index), end, fail);
    }

    [[nodiscard]] async::Task<HttpResult<Fields>> trailers() override { return noTrailers(); }

  private:
    net::EventLoop* _loop;
    std::vector<std::string> _chunks;
    std::chrono::milliseconds _gap;
    std::optional<std::size_t> _failAt;
    std::size_t _next = 0;
};

// -------------------------------------------------------------------------------------------------
// TLS, scripted (R29).

/// How a @c ScriptedTlsSocket's handshake ends.
enum class HandshakeOutcome : std::uint8_t
{
    Succeeds,          ///< At once.
    RefusedByTls,      ///< With @c TlsScript::failure reported by the session, and an error.
    CutByTransport,    ///< With @c TlsScript::transportError and no TLS failure, as a reset would.
    ParksThenSucceeds, ///< After @c TlsScript::park on the loop's clock, honouring a stop.
};

/// What a @c ScriptedTlsSocket's reads do once the handshake has succeeded.
enum class ReadOutcome : std::uint8_t
{
    Forwards,     ///< They read the transport.
    RefusedByTls, ///< The first fails, the session reporting @c TlsScript::failure, as an alert would.
};

/// What a @c ScriptedTlsContext's connections do.
struct TlsScript
{
    std::optional<net::NetError> wrapError; ///< What @c wrapClient answers instead of a socket, if set.
    HandshakeOutcome handshake = HandshakeOutcome::Succeeds; ///< How the handshake ends.
    ReadOutcome reads = ReadOutcome::Forwards;               ///< What the reads after it do.
    net::TlsFailure failure;                                 ///< What a refusal reports.
    net::NetError transportError =
        net::makeNetError(net::NetErrorCode::ConnReset); ///< A cut handshake's error.
    std::chrono::milliseconds park { 0 };                ///< How long a parked handshake waits.
};

/// @return The script whose handshake ends as @p handshake, everything else as the defaults say.
[[nodiscard]] inline TlsScript scriptOf(HandshakeOutcome handshake)
{
    auto script = TlsScript {};
    script.handshake = handshake;
    return script;
}

/// What a scripted context's connections saw, kept by the context so it outlives them.
struct TlsRecord
{
    std::vector<std::string> serverNames;  ///< The @c net::TlsPeer::serverName of every wrapClient.
    std::size_t wraps = 0;                 ///< How many sockets were wrapped.
    std::size_t handshakes = 0;            ///< How many handshakes were asked for.
    std::size_t writes = 0;                ///< How many writes reached the TLS sockets.
    std::size_t writesBeforeHandshake = 0; ///< How many of them came before a handshake had succeeded.
};

/// The session of a @c ScriptedTlsSocket: it reports a failure once one has been scripted to happen.
class ScriptedTlsSession final: public net::ITlsSession
{
  public:
    ScriptedTlsSession() = default;

    [[nodiscard]] std::expected<std::array<std::byte, 32>, std::string> channelBinding() const override
    {
        return std::unexpected(std::string { "scripted" });
    }
    [[nodiscard]] std::optional<std::array<std::byte, 32>> peerCertificateDigest() const override
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<net::TlsVersion> negotiatedVersion() const override { return std::nullopt; }
    [[nodiscard]] std::optional<net::TlsFailure> failure() const override { return _failure; }

    /// Makes @c failure report @p failure from now on.
    void fail(net::TlsFailure failure) { _failure = std::move(failure); }

  private:
    std::optional<net::TlsFailure> _failure;
};

/// A "TLS" socket that encrypts nothing: it forwards to the transport it owns, and its handshake and
/// reads end as its @c TlsScript says.
class ScriptedTlsSocket final: public net::ISocket
{
  public:
    ScriptedTlsSocket(std::unique_ptr<net::ISocket> inner,
                      net::EventLoop& loop,
                      TlsScript script,
                      TlsRecord& record):
        _inner { std::move(inner) }, _loop { &loop }, _script { std::move(script) }, _record { &record }
    {
    }

    [[nodiscard]] net::IoAwaitable read(std::span<std::byte> buffer) override
    {
        if (_script.reads == ReadOutcome::RefusedByTls)
        {
            _session.fail(_script.failure);
            return net::IoAwaitable { std::unexpected(
                net::makeNetError(net::NetErrorCode::SystemError, 0, "scripted TLS record refused")) };
        }
        return _inner->read(buffer);
    }

    [[nodiscard]] net::IoAwaitable write(std::span<std::byte const> buffer) override
    {
        countWrite();
        return _inner->write(buffer);
    }

    [[nodiscard]] net::IoAwaitable writeVectored(std::span<std::span<std::byte const> const> segments,
                                                 std::shared_ptr<void const> keepAlive = {}) override
    {
        countWrite();
        return _inner->writeVectored(segments, std::move(keepAlive));
    }

    [[nodiscard]] net::ResultAwaitable<void> handshakeIfNeeded() override
    {
        ++_record->handshakes;
        switch (_script.handshake)
        {
            case HandshakeOutcome::Succeeds:
                _isEstablished = Established::Yes;
                return net::ResultAwaitable<void> { net::ResultAwaitable<void>::Result {} };
            case HandshakeOutcome::RefusedByTls:
                _session.fail(_script.failure);
                return net::ResultAwaitable<void> { net::ResultAwaitable<void>::Result { std::unexpected(
                    net::makeNetError(net::NetErrorCode::SystemError, 0, "scripted handshake refused")) } };
            case HandshakeOutcome::CutByTransport:
                return net::ResultAwaitable<void> { net::ResultAwaitable<void>::Result {
                    std::unexpected(_script.transportError) } };
            case HandshakeOutcome::ParksThenSucceeds:
                return net::ResultAwaitable<void> { parkThenSucceed(this) };
        }
        return net::ResultAwaitable<void> { net::ResultAwaitable<void>::Result {} };
    }

    void cancelRead() noexcept override { _inner->cancelRead(); }
    [[nodiscard]] net::ITlsSession const* tlsSession() const noexcept override { return &_session; }
    void close() noexcept override { _inner->close(); }
    [[nodiscard]] bool isClosed() const noexcept override { return _inner->isClosed(); }

  private:
    /// Whether a handshake has succeeded yet.
    enum class Established : std::uint8_t
    {
        No,
        Yes,
    };

    static async::Task<std::expected<void, net::NetError>> parkThenSucceed(ScriptedTlsSocket* self)
    {
        co_await self->_loop->delay(self->_script.park);
        self->_isEstablished = Established::Yes;
        co_return std::expected<void, net::NetError> {};
    }

    void countWrite() noexcept
    {
        ++_record->writes;
        if (_isEstablished == Established::No)
            ++_record->writesBeforeHandshake;
    }

    std::unique_ptr<net::ISocket> _inner;
    net::EventLoop* _loop;
    TlsScript _script;
    TlsRecord* _record;
    ScriptedTlsSession _session;
    Established _isEstablished = Established::No;
};

/// A client context that binds the name it is given, and whose connections do what @c TlsScript says.
class ScriptedTlsContext final: public net::ITlsContext
{
  public:
    /// @param loop Where a parked handshake waits.
    /// @param script What every connection does.
    ScriptedTlsContext(net::EventLoop& loop, TlsScript script): _loop { &loop }, _script { std::move(script) }
    {
    }

    [[nodiscard]] std::unique_ptr<net::ISocket> wrap(std::unique_ptr<net::ISocket> inner,
                                                     async::IExecutor& /*executor*/) override
    {
        ++_record.wraps;
        return std::make_unique<ScriptedTlsSocket>(std::move(inner), *_loop, _script, _record);
    }

    [[nodiscard]] std::expected<std::unique_ptr<net::ISocket>, net::NetError> wrapClient(
        std::unique_ptr<net::ISocket> inner, async::IExecutor& executor, net::TlsPeer const& peer) override
    {
        _record.serverNames.push_back(peer.serverName);
        if (_script.wrapError.has_value())
            return std::unexpected(*_script.wrapError);
        return wrap(std::move(inner), executor);
    }

    /// @return What its connections saw.
    [[nodiscard]] TlsRecord const& record() const noexcept { return _record; }

  private:
    net::EventLoop* _loop;
    TlsScript _script;
    TlsRecord _record;
};

/// A client context written before @c net::ITlsContext::wrapClient existed: it overrides @c wrap only,
/// so the default @c wrapClient refuses any server name (R23), as every legacy context's does.
class LegacyTlsContext final: public net::ITlsContext
{
  public:
    LegacyTlsContext() = default;

    [[nodiscard]] std::unique_ptr<net::ISocket> wrap(std::unique_ptr<net::ISocket> inner,
                                                     async::IExecutor& /*executor*/) override
    {
        ++_wraps;
        return inner;
    }

    /// @return How many sockets were wrapped.
    [[nodiscard]] std::size_t wraps() const noexcept { return _wraps; }

  private:
    std::size_t _wraps = 0;
};

} // namespace core::http::test
