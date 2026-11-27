# http

HTTP/1.1 (RFC 9110, RFC 9112) as coroutines: the message vocabulary, the framing rules, a
connection with streamed bodies and trailers, a client that keeps connections alive, and a
keep-alive server. Namespace `core::http`, directory `src/core/http/`, target `core::http`. It links
[base](base.md) and [net](net.md) (and [async](async.md) through it). Built with
`CORE_CPP_WITH_HTTP` (ON), natively only.

**HTTP/1.1 only.** No HTTP/2 or 3, no client pipelining, no upgrade (a `101` is a `ProtocolError`
on the client), and the server answers pipelined requests one at a time. `core::net`'s older
`serve()` in `<core/net/HttpServer.hpp>` is a separate, minimal server and is unchanged; the two
share only `net::acceptLoop()`.

| Header | What it has |
|---|---|
| `<core/http/Message.hpp>` | `HttpErrorCode` (one code per case of `wasi:http`'s `error-code`, in its order) and its `toString()`, `HttpError` (the code, the payload its case carries, a detail and a `RefusalReason`), `HttpResult<T>`; `HttpVersion`; `Field`, `Fields`; the validating `Method::parse()`, `RequestTarget::parse()` (`RequestTargetForm`) and `ReasonPhrase::parse()`; `RequestHead`, `ResponseHead` |
| `<core/http/Framing.hpp>` | `HttpLimits`; `MessageRole`, `BodyFraming`, `Persistence`, `Framing`; `requestFraming()`, `responseFraming()`, `isFramingField()`; `HeadParser` (`Progress`, `FeedResult`), `ChunkedDecoder` (`ChunkStep`), `chunkHeader()`, `lastChunk()` |
| `<core/http/Body.hpp>` | `IBodyReader`, `IBodySource`, `BufferBodySource`, `BodyChunk`, `BodyEnd`, `TrailerIntent`, `framingFor()` |
| `<core/http/Connection.hpp>` | `Connection`, `WireTimeouts`, `Reuse` |
| `<core/http/HttpClient.hpp>` | `HttpClient`, `HttpClientOptions`, `HttpTimeouts`, `Origin`, `Scheme`, `defaultPort()`, `OutgoingRequest`, `IncomingResponse` |
| `<core/http/HttpServer.hpp>` | `serveHttp()`, `serveHttpConnection()`, `IHttpHandler`, `IncomingRequest`, `OutgoingResponse`, `HttpServerOptions`, `defaultServerLimits()`, `ErrorStatusRow`, `errorStatusTable()`, `errorStatusFor()`, `statusPhrase()` |
| `<core/http/testing/ScriptedConnector.hpp>` | `testing::ScriptedConnector`, an `net::IConnector` whose connections are in-memory socket pairs, and `testing::ScriptedDial` |

## The pure layer

`Message.hpp` and `Framing.hpp` hold no socket; every rule is tested from a table, and the request
smuggling corpus (`SmugglingCorpus.hpp`) is run by both `Framing_test.cpp` and `HttpServer_test.cpp`.

- **No value that could split a message reaches the wire.** A field name must be a token; a value
  holds no CR, LF, NUL or other control character but HTAB, and no leading or trailing whitespace.
  `Method`, `RequestTarget` and `ReasonPhrase` exist only through their validating `parse()`.
- **The connection writes the framing.** `Transfer-Encoding`, `Content-Length`, `Connection`,
  `Keep-Alive`, `TE` and `Upgrade` (`isFramingField()`), and the client's `Host`, are refused from a
  caller.
- **`requestFraming()` refuses what a smuggler sends**: `Transfer-Encoding` with `Content-Length`,
  `Transfer-Encoding` in HTTP/1.0, `chunked` twice or not last, a malformed coding list, a
  `Content-Length` that is not one decimal value; and a request coding other than exactly
  `chunked` (a server answers `501`). Every refusal is fatal to the connection.
- **`HeadParser` holds one line at a time** and refuses past `HttpLimits`: a bare LF or CR, space
  before a colon, an obs-fold in a request, two `Host` fields, an invalid one, or none in HTTP/1.1.
  `ChunkedDecoder` is a transition table; a chunk size has at most 16 hex digits.
- **`HttpLimits` bounds everything a peer sends**: the head (64 KiB), a field (16 KiB), the number
  of fields (100), the trailers (16 KiB), a chunk line (1 KiB), a read (64 KiB, also the
  connection's read buffer) and, optionally, the body. Nothing a peer sends makes the host hold more.

## Connection

`Connection` is one HTTP/1.1 connection over an `net::ISocket`, either end; its `MessageRole` is
what it reads. Bodies stream in both directions, a chunk at a time, with trailers.

- **The read side runs head, reader, head.** After a head the caller frames it and asks
  `bodyReader()` for its body before the next head read.
- **The request role is serial.** Request N+1's head is read only once response N is written in
  full; asked earlier, the read answers `ProtocolError` as a value, consuming nothing.
- **Failures are kept per direction.** A read failure leaves the write side free to answer (400,
  408); a refusal before the first wire byte leaves the connection as it was; a failure after it
  breaks that direction.
- **`WireTimeouts`**: `firstByte`, `betweenBytes`, `write`, `head` (from a head's first byte to its
  last, a budget of its own) and `body`; **zero means no deadline**. A read deadline retires the
  read through `cancelRead()` and keeps the socket open, so a socket decorator that does not forward
  `cancelRead` disables the read deadlines.

## HttpClient

`HttpClient(loop, connector, tls, options)` sends one request at a time on each connection and
keeps connections alive between requests.

- **Every step has its own codes.** Checked before the dial (`RequestUriInvalid`,
  `ProtocolError`, `ConfigurationError` for `https` without a TLS context); the dial through the
  injected `net::IConnector` (`DnsError`, `ConnectionRefused`, `DestinationUnavailable`,
  `ConnectionTimeout`); TLS; the exchange.
- **TLS fails closed.** For `Scheme::Https` the client calls `net::ITlsContext::wrapClient()` with
  the origin's host, then runs the handshake before any request byte. A context that cannot bind a
  connection to a name, every context from the pinned-name `makeTlsClientContext(caPem,
  expectedHostName, options)` included, answers `ConfigurationError`; use
  `net::makeTlsClientContext(TrustSource, TlsClientOptions)` or `net::makeSystemTlsClientContext()`.
  A refused handshake is `TlsCertificateError`, `TlsProtocolError` or `TlsAlertReceived`, with the
  `net::TlsFailure` attached.
- **The pool.** Idle connections are kept per origin (scheme, host case-insensitively, effective
  port): at most `maxIdlePerOrigin` (4) each, for `idleTimeout` (30 s); at most
  `maxConnectionsPerOrigin` (16) open to one origin and `maxConnectionsTotal` (256) to all, idle
  ones included. Past a cap, an idle connection of any origin is closed to make room, or the request
  is `ConnectionLimitReached` at once, without a dial and without waiting.
- **Interim responses are bounded.** Up to 32 interim (1xx) heads are skipped before the final one;
  a 33rd is `ProtocolError`, so a server cannot keep a request reading heads for ever.
- **A connection holding unread bytes is never pooled.** Bytes a server sent past the end of its
  response (after a HEAD's `Content-Length` answer, say) answer no request this client sent, so the
  connection is closed rather than handed to the next request.
- **A stale pooled connection is retried once**, on a fresh dial, and only where that cannot repeat
  an effect: an idempotent method with no body.
- **`send()` completes only once the whole request body is written.** An early final response (a
  413 to an upload) is not returned while the server stops reading, and a body source that waits on
  the response body deadlocks. `HttpTimeouts::firstByte` runs from the start of the exchange and the
  upload counts toward it, as `wasi:http`'s first-byte timeout does in wasmtime.
- `HttpTimeouts::connect` bounds the dial and then the handshake, each on its own; **zero means no
  deadline**, here and in every other timeout. Each of `connect`, `firstByte` and `betweenBytes`
  defaults to **600 s**, wasmtime's `wasi:http` default, so a client built with no options still
  bounds every wait.

## serveHttp

`serveHttp(loop, listener, handler, options)` serves every connection the listener accepts in a
flow of its own, each through `serveHttpConnection()`. The handler is one request in and one
response out (`IHttpHandler::handle()`), its body streamed both ways.

- **Every wait on a client is bounded by default** (`HttpServerOptions`): the wait for a request's
  first byte (`idleTimeout`, 5 s, past which the connection closes unanswered), the head from its
  first byte (`headTimeout`, 10 s, then `408`), between body reads (`bodyBetweenBytes`, 10 s), a body
  as a whole (`bodyTotal`, 60 s) and each write (`writeTimeout`, 30 s). A body is bounded to 64 MiB
  (`defaultServerLimits()`), at most 512 connections are served at once (`admission`), and 1000
  requests per connection.
- **Pipelined requests are served in order, one at a time.** Request N+1 is read once response N is
  written in full; the handler never sees two requests of one connection at once.
- **Refusals are one table.** `errorStatusTable()` maps a refused request to `400`, `408`, `411`,
  `413`, `414`, `431`, `501`, `505` or `500`; a code with no row (the client went away, a write
  failed) and a timeout before any byte of a request are not answered: the connection closes.
- **The connection closes**, with `Connection: close` where the response can still say so, after a
  request that asked for it or HTTP/1.0 without keep-alive, the last request of
  `maxRequestsPerConnection`, a body over `maxDrainBytes` left unread, `Expect: 100-continue`
  answered without the handler reading, a body reader still alive when the response is written, and
  any failure. Every close lingers (`net::closeLingering()`).
- **Connections outlive the accept loop.** Closing the listener ends `serveHttp()`, not the
  connections it started; they end with their client, their deadlines or the loop.

## Limits

- HTTP/1.1 only, no client pipelining, no upgrade, no proxy support; the server is serial per
  connection.
- `HttpClient::send()` returns the response head only after the request body is written (above).
- The client does not wait for a connection to come free: past a cap it closes an idle one or
  refuses.
- Trust is loaded as a `Task` (`net::ITrustStore::load()`, `net::makeSystemTlsClientContext()`), and
  on macOS and Windows a platform verifier, not OpenSSL's store, decides (see [net](net.md)). `http`
  itself links no OpenSSL: it reaches TLS only through `net::ITlsContext*` and
  `net::ISocket::tlsSession()`.

## Tests

| Test | Binary | Labels | What |
|---|---|---|---|
| `core-cpp.http` | `core-cpp-http-test` | `core-cpp`, `http` | the pure layer and the smuggling corpus, `Connection`, `HttpClient` and the server, over `net::testing`'s in-memory pairs on a test loop; TLS through a scripted context, so the binary links no `net_tls` |
| `core-cpp.http_loopback` | `core-cpp-http_loopback-test` | `core-cpp`, `http`, `loopback` | `Connection` and the server over real sockets on every backend this platform builds, and one `HttpClient` round trip against `serveHttp()`, each case under a watchdog |
