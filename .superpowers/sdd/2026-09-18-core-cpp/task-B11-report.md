# Task B11: one TLS layer (`core::net_tls`)

> **Current SHAs, after review fix round 1 on `e43dddc`:** B10 = `b2a9bbf`, B11 = `126ea36`, fixes = `ae6aea6` and `4bb1feb`. Sections 1 to 5 name the pre-rebase SHAs; section 6 is the fix round.

**Status: DONE_WITH_CONCERNS.** One commit, **`02eb8d0`**, on top of B10's `974d8ef`. It is not
pushed. Worktree: `D:/core-cpp-wt-b11`.

## 1. What landed

- **`TlsSocket` now overrides the three verbs it inherited.**
  - `handshakeIfNeeded()` drives the gated handshake to completion.
  - `waitReadable()` uses the `SSL_pending` fast path, then `SSL_peek`. It answers `0` for a
    `close_notify` or a raw EOF, answers `>0` for decoded data, and parks until the peer acts.
  - **`shutdownWrite()` writes and flushes `close_notify`, and only then half-closes the inner
    socket.** Without a handshake there is nothing to close cryptographically, so only the
    transport is half-closed.
- **From fastcached's record pump:** `ERR_clear_error()` before every classified `SSL_*` call,
  the `SSL_peek` probe, `requireReadBuffer` on `read`, and the P-256 generator with
  subjectAltNames and a fingerprint.
- **Kept from contour:** the `ITlsContext` seam, client verification (a pinned CA plus a
  host/IP check, or trust on first use), the concurrent read/write handshake gate (now a
  `SerialGate`, used twice), and the PEM material functions.
- **New API**, per the plan:
  - `SelfSignedOptions{commonName, subjectNames, validity}`. The consumer default
    `"contour-daemon"` is gone; the new default is `"localhost"`.
  - `makeTlsServerContextFromFiles(path, path)`.
  - `certificateFingerprint(pem)` and `ITlsContext::certificateFingerprint()`.
  - `<core/net/ITlsContext.hpp>` in **`core::net`** with `wrapTls(socket, context)`. A null context
    returns the socket unchanged, so an accept path compiles without TLS. This is fastcached's
    `WrapTls` without its `#if FC_TLS_ENABLED`.
- **`testing::StrictTlsPeer`**, a public double in `core::net_tls` in the pattern of the
  `testing/` directory. It is OpenSSL driven by hand, and it reads the way OpenSSL 3 reads from a
  socket: a FIN before `close_notify` is a **truncation**.
- **`core-cpp.openssl-seam` and its self-test**, both `tree-level`, both with `style` steps.
  `check-tree-level-coverage.py` now counts 17, all covered.
- **Bookkeeping:**
  - `renames.json`: +10 fastcached rows (3 include, 2 symbol, 1 member, 4 removed).
    `check-renames` reports 598 rows and 0 failures.
  - Provenance: 5 rows added and 3 amended.
  - CHANGELOG: Added ×2, Fixed ×3, Breaking ×1 with migrations.
  - Rulebook: a new "TLS" section in `async-and-net.md` with four rules.
  - `net.md`: updated.
  - The `ISocket.hpp` notes saying "TlsSocket does not override it yet" are corrected.

### Two defects found on the way, beyond the brief

1. **A read beside a parked write put a second write into the inner socket.** A 20000-byte write
   is two records, more than one 16 KiB flush chunk. Its first chunk parks, and the rest stays in
   the outgoing BIO. A read that reaches `WANT_READ` then flushed that rest itself, which is a
   second operation in the inner socket's single write slot, with the ciphertext out of order.
   The fix is one flush at a time. A write waits for a flush in progress. A read **skips** it,
   because that flush drains the BIO to empty, and a read parked behind a write could not be
   retired by `cancelRead`.
2. **`makeTlsServerContext` served only the first certificate** of a PEM its documentation calls
   a chain. Intermediates are now added with `SSL_CTX_add1_chain_cert`.

## 2. RED, then GREEN

**RED A, compile, against the base `Tls.{hpp,cpp}`:** `TlsContext_test.cpp` fails with
`error: no member named 'SelfSignedOptions' in namespace 'core::net'`, then errors on
`certificateFingerprint` and more. The full log is in the scratchpad (`b11-redA.log`).

**RED B, behaviour.** This is `TlsSocket_test.cpp` built against the base implementation. It
compiles, because it uses only API the base has. **Predicted: all 6 cases red. Actual: 6 of 6
cases red, 13 failed assertions.** Verbatim:

```
shutdownWrite sends close_notify before the transport's FIN
  TlsSocket_test.cpp:198: FAILED:  REQUIRE( finished )  with expansion:  false
handshakeIfNeeded completes the handshake before any application byte
  TlsSocket_test.cpp:221: FAILED:  REQUIRE( finished )
handshakeIfNeeded over non-TLS input fails instead of hanging
  TlsSocket_test.cpp:243: FAILED:  CHECK_FALSE( *ok )
waitReadable parks until the peer acts, and tells data from close_notify
  :256 CHECK_FALSE( outcome.resolved )   :275 CHECK_FALSE( outcome.resolved )
  :281 CHECK( **outcome.result == 0 )    :298 CHECK( **outcome.result == 0 )
  :312 CHECK( **outcome.result == 0 )    :320 REQUIRE_FALSE( outcome.resolved )
A stale OpenSSL error on the loop's thread does not fail a healthy read
  TlsSocket_test.cpp:355: FAILED:  REQUIRE( read->has_value() )
A read beside a parked write never puts a second write into the inner socket
  TlsSocket_test.cpp:461: FAILED:  CHECK( gated->maxInFlight() == 1 )  with expansion:  2 == 1
  TlsSocket_test.cpp:468: FAILED:  REQUIRE( received->has_value() )
test cases:  6 |  0 passed |  6 failed
assertions: 95 | 82 passed | 13 failed
```

That run also ended in `terminate ... OperationCancelled` from a detached helper during
teardown. The detached helpers now catch their own cancellation, so one failing case cannot take
the binary down.

**GREEN:** `core-cpp-net_tls-test` reports **224 assertions in 21 test cases**, stable across 3
consecutive runs.

**Three mutations, each applied, run and reverted, the tree confirmed byte-identical after:**

| # | Arm removed | Predicted | Actual |
|---|---|---|---|
| M1 | `shutdownWrite` goes straight to the inner socket, skipping `close_notify` (the "obvious one-line fix") | the strict peer sees `Truncated` | 1 case red, but at `REQUIRE(finished)`: after a truncation the peer's session is dead and cannot answer, so the exchange ran into its bound. **I moved the `end == CloseNotify` CHECK above the bound**, so a regression now reports its reason and not only its hang |
| M2 | `ERR_clear_error()` before `SSL_read` | the stale-error case | 1 case: `REQUIRE( read->has_value() )` |
| M3 | the flush gate (`while (_flushing.busy())` → `while (false)`) | the concurrency case | 1 case: `maxInFlight 2 == 1`, then the peer's record layer fails (`received->has_value()`) |

The seam gate was mutated too: the include pattern pointed at a nonexistent directory, and the
type alternation replaced. **9 of its 12 self-test cases failed**, so the self-test has teeth.

## 3. Decisions the brief asked for

- **What the brief's own grep still gets wrong.** I ran `git ls-tree -r --name-only 0708dd54 |
  grep -iE "tls|cert|ssl|pem|crypto"` and then grepped file **contents** for OpenSSL, `TlsSocket`,
  `ITlsContext` and `close_notify`. Three problems:
  1. **The named gate guards the wrong library.** `scripts/check-crypto-seam.cmake` is fastcached's
     **Monocypher** seam (Ed25519 and X25519), not OpenSSL's. Ported verbatim, it would enforce
     nothing about "no OpenSSL type in any header". Worse, fastcached's own TLS headers
     forward-declare `struct ssl_st` and `struct ssl_ctx_st`, which is precisely an OpenSSL type
     in a header. I wrote `tests/cmake/check-openssl-seam.cmake` in that gate's shape (permitted
     units with reasons, stale-row positive controls, a refusal to call an empty walk clean) for
     OpenSSL. It also refuses forward-declared struct tags in headers.
  2. **Relevant files a name grep cannot see:**
     - `vcpkg.json` (the OpenSSL dependency);
     - `scripts/check-cancel-read-declared.cmake` (a gate that every parking transport, TlsSocket
       named, declares `cancelRead`). core-cpp's `ISocket::cancelRead` doc says "nothing but this
       sentence enforces that", so this is **a B13 candidate**;
     - `scripts/check-write-slot-guard.sh` (it names TlsSocket's slot ownership);
     - `Protocol/SealedFrameSocket` (another socket decorator);
     - `Core/SecureBytes.cpp` and `Core/Sha256.hpp` (crypto, but deliberately NOT OpenSSL).
  3. It **does** catch `TlsWrap.hpp`, which the brief's list omits; its logic is ported as
     `wrapTls`.
- **`CryptoError`: neither widened nor imported.** It is the refusal type of fastcached's
  Monocypher primitives (key lengths, a low-order X25519 point, HKDF bounds), none of which exist
  in core-cpp. TLS record-pump failures stay `NetError` values: `SystemError` with the OpenSSL
  reason, or `Eof`. The vocabulary is not widened, because a caller of `ISocket` should not have
  to handle a failure only one transport produces. Context and material functions return a
  string reason, as contour's did, because what fails there is configuration. `Tls.hpp`'s file
  comment records this.
- **`RosterCertificate`** is out of scope and was not touched.
- **Committed certificates:** none imported. Every certificate is generated at run time;
  `generateSelfSignedCertificate` is the one shared fixture.
- **`tls-smoke`: not ported.** Upstream's script starts the fastcached daemon and dials it with
  `openssl s_client`, which is consumer-specific. The equivalent here is the `net_tls` binary
  itself: `StrictTlsPeer` is an independent OpenSSL endpoint, and `cl-release-tls` runs that
  binary.
- **B8's shape changes:** none of the changed surfaces are touched. The tests call
  `connect(loop, "127.0.0.1", port)`, which compiles with both the `string_view` and the
  `std::string` host.

## 4. Gates

**The commit was amended twice after the first matrix, and each time a gate caught something:**

1. **`cl-release-tls`**, run locally against a vcpkg OpenSSL at CI's pinned vcpkg commit, stopped
   the first version: `StrictTlsPeer.cpp(54): warning C4267 size_t -> int`, fatal under `/WX`.
2. **`gcc-release`** stopped it too: GCC 14's `-Wnull-dereference` fires inside libstdc++'s
   `streambuf` when `readFile` built a string from `istreambuf_iterator`. `readFile` now sizes the
   file and reads it in one call, and it formats the path from `u8string()`, so the error path can
   no longer throw on Windows.
3. **`clang-tidy`** (tree deleted first) found `bugprone-unused-return-value` on
   `intermediate.release()` after `SSL_CTX_add0_chain_cert`. The loop now uses `add1`, so the
   context takes its own reference and no ownership changes hands.

Every number below is for **`02eb8d0`**. The two amends after the first full run touched only
`Tls.cpp` and `StrictTlsPeer.cpp`, both compiled only with `CORE_CPP_WITH_TLS`, so the WSL legs
were rebuilt incrementally (their full step counts from the first run on `454e989` are given
beside them). The Windows non-TLS legs do not compile either file, so their `454e989` runs stand.

| Gate | Build exit | Result |
|---|---|---|
| `clang-format --all --check` (22.1.8) | – | 467 files clean |
| `clang-tidy` (tree deleted, then `-k 0`) | 0 | 1 finding, fixed above; after the fix 0 findings, **40/40**, 0 skipped |
| WSL `clang-debug` | 0 | **40/40**, 0 skipped; `core-cpp-net_tls-test` **224 assertions / 21 cases** |
| WSL `gcc-release` | 0 (568 steps on the first run, failing on the warning above) | **40/40**, 7 skipped (`NDEBUG` canaries) |
| WSL `clang-asan-ubsan` | 0 | **40/40**, 0 skipped |
| WSL `clang-tsan` | 0 (568 steps full on `454e989`) | **40/40**, 0 skipped |
| Windows `cl-debug --clean-first` (`454e989`) | 0, **562 steps** | **40/40**, 0 skipped |
| Windows `clangcl-release --clean-first` (`454e989`) | 0, **562 steps** | **40/40**, 6 skipped |
| Windows **`cl-release-tls`** (local vcpkg OpenSSL, `--clean-first` then `-k 0`) | **2**, 570 of 571 steps | **40/41**: `core-cpp.net_tls` passes (`224 assertions in 21 test cases`, run directly as well); `core-cpp.net` is **Not Run**, see concern 4 |
| `ctest -L hygiene` (inside every run above) | – | green, including `openssl-seam` (27s under WSL load) and `openssl-seam-selftest` (12/12) |
| `check-tree-level-coverage.py` | – | 17 of 17 covered |
| `check-renames.py` | – | 598 rows, 0 failures |
| `mkdocs build --strict` | exit 0 | |

## 5. Concerns

1. **A core-cpp TLS socket still READS leniently.** A transport EOF before `close_notify` is a
   zero-byte read, as upstream and the ISocket contract have it. So a core-cpp peer cannot detect
   a truncation that a strict peer can. What changed is that core-cpp now **sends** the alert. A
   strict-read option would be a follow-up, not something to slip in here.
2. **Breaking:** the self-signed default common name is now `"localhost"`, and keys are P-256
   instead of RSA-2048. contour's two callers need `{ .commonName = ... }`; the migration is in the
   CHANGELOG. fastcached's default validity was 1 year and is now 10; the migration note says to
   pass `.validity`.
3. **`writePlain`'s `WANT_READ` arm is unreachable after the handshake** because every context
   sets `SSL_OP_NO_RENEGOTIATION`. Were it reached beside a parked read, it would double-arm the
   inner read slot. This is documented at the arm, not asserted.
4. **Not mine, and found only because I ran `cl-release-tls` locally:** with the local MSVC
   14.51, `/O2` fails `ReadinessDial_test.cpp` (B8's file) with `error C4737: Unable to perform
   required tail call`, five sites, under `/WX`. It reproduces on B10's `974d8ef` under
   `cl-release`, so it predates this commit, and it comes from symmetric transfer in coroutine
   code MSVC's optimiser cannot tail-call. CI's `cl-release` and `cl-release-tls` legs will show
   whether the runner's MSVC does the same. If it does, both legs are red at base.
5. The seam scan takes **65s under WSL**, because it reads roughly 560 files from `/mnt/d`. Its
   test `TIMEOUT` is 300, and natively it takes seconds.
6. `testing::StrictTlsPeer` makes `core::net_tls` ship a test double. It follows the `testing/`
   convention, but it is new public API.

## 6. Review fix round 1

**Commits:**
- **`ae6aea6`**: the fixes and their cases.
- **`4bb1feb`**: moves the S3a case into a POSIX-only file, fixes a clang-tidy finding in the
  bounded `serve` test, and removes one CHANGELOG blank line.

Both sit on B10 = `b2a9bbf` and B11 = `126ea36`, rebased onto **`e43dddc`**. Nothing is amended
and nothing is pushed.

### RED, per finding, against the pre-fix `Tls.cpp`

**Predicted:**
- B1 and S1 crash.
- S2 hangs, because the stopped reader is still parked when the case expects it gone.
- S3a fails the peer exchange after the cancel.
- S3b sees the write settle.

**Actual (`redcases.sh`, clang-debug, one case per process):**

```
Destroying a TLS socket mid-handshake resolves every operation parked on it          exit=134
  double free or corruption (!prev)
  TlsLifetime_test.cpp:232: FAILED: {Unknown expression after the reported line}  SIGABRT
A flow released from the handshake gate may destroy the socket before the others run exit=139
  TlsLifetime_test.cpp:252: FAILED: {Unknown expression after the reported line}  SIGSEGV
A read that loses a race while parked on the handshake leaves nothing behind         exit=124
  TlsLifetime_test.cpp:284: FAILED: {Unknown expression after the reported line}  SIGTERM
cancelRead during a handshake the read drives leaves the socket usable               exit=1
  TlsLifetime_test.cpp:336: FAILED:
    REQUIRE( c.run(peerHandshakesAndSays(c.peer.get(), c.wire.get(), "after the cancel")) == std::optional<bool> { true } )
A reader's cancelRead does not reach into a write that drives the handshake          exit=1
  TlsLifetime_test.cpp:361: FAILED:  CHECK_FALSE( write.settled )  with expansion: !true
  TlsLifetime_test.cpp:363: FAILED:  REQUIRE( c.run(peerHandshakesAndSays(..., "late")) == ... )
```

- **B1:** `~TlsSocket` freed the gates before the inner socket. The inner socket's destructor
  unwinds a driver parked in the handshake or in a flush, and that unwind leaves a gate that no
  longer exists.
- **S1:** a released waiter dropped the socket, and the loop over the remaining waiters then
  resumed the next one onto freed memory.
- **S2:** the stopped reader stayed parked. It would have come back when the handshake completed.
- **S3a:** the cancel's `Cancelled` was stored as the sticky handshake error.
- **S3b:** `cancelRead` retired the inner read of the WRITE that was driving the handshake.

**GREEN:** the five cases pass (9, 11, 15, 12 and 15 assertions). The binary passes 316
assertions in 27 cases, before the S4 and nit cases were added.

### Fixes

**`SerialGate` is shared state behind a `shared_ptr`** held by the socket, every awaiter and every
holder's scope guard.
- **Destruction order:** `~TlsSocket` abandons both gates, then drops the inner socket, then calls
  `SSL_free`. Every waiter unwinds with `OperationCancelled`, now or on arrival.
- **Waiters are never resumed inline.** `leave`, `retireReaders` and `abandon` submit each waiter
  to the loop, which is why `ITlsContext::wrap` and `wrapTls` now take an `async::IExecutor&` (a
  Breaking entry).
- **A waiter is stop-aware.** Its `StopCallback` is registered before the park is published, as in
  `AsyncQueue`. A stopped waiter is taken off the gate and submitted, and `await_resume` throws if
  its token was stopped, whatever released it.
- **`cancelRead` retires the READ direction only.** `Direction {Read, Write, Any}` tags both the
  gate waiters and the owner of the inner read, and a cancel's `Cancelled` is never stored as the
  handshake's error.

### S4

**Case:** "an IP-literal host is verified against the certificate's IP entries". It checks a
match, a mismatch against `127.0.0.2`, and a certificate with a CN only.

**Mutation:** the `set1_ip_asc` arm is disabled, so an IP literal goes to `set1_host`. Predicted:
the match fails. Actual: `Tls_test.cpp:241: FAILED: CHECK( verifiesAgainst(*byAddress,
"127.0.0.1") )`, 15 of 16 assertions pass. The mutation is reverted, and a grep confirms it is
gone.

### Nits taken

- The handshake's failure reason is captured before the flush that can park.
- The CA PEM is read as a bundle; there is a case that every CA in it is trusted.
- Validity is refused when not positive and is set through `X509_time_adj_ex`; there is a case
  that 100 years verifies.
- `waitReadable`'s `close_notify` sets `_peerClosed`.
- `StrictTlsPeer` tells a wire error apart from EOF.
- `AsyncBufferedReader` is neither copyable nor movable.
- `serve()` documents that it has no deadline, and its test is bounded by `withTimeout(10s)`.
- `SSL_OP_NO_RENEGOTIATION` is in the CHANGELOG.

### Found by the Windows leg

The S3a case failed on `cl-release-tls` at `TlsLifetime_test.cpp(330)`,
`REQUIRE( c.pumpUntil([&] { return read.settled; }) )`, with 29 of 30 cases passing.

`WindowsSocket` has no `cancelRead`, so the reader's inner read is never retired. That is the same
reason `CancelRead_test` is POSIX-only. S3a now lives in `posix/TlsCancelRead_test.cpp`, under
`SOURCES_POSIX` of the `net_tls` test. S3b stays portable: it asserts that the write's inner read
is **not** touched, which holds on every backend.

### Gates on the final tree

| Gate | Build exit | Result |
|---|---|---|
| WSL `clang-debug` (`--clean-first` on `ae6aea6`: 597 steps; then incremental to `4bb1feb`) | 0 | **42/42**, 0 skipped; `net_tls` **346 assertions / 30 cases** |
| WSL `clang-asan-ubsan` (`--clean-first` on `ae6aea6`, then incremental) | 0 | **42/42**, 0 skipped, 0 sanitizer reports |
| Windows `cl-debug` (`--clean-first` on `ae6aea6`: 590 steps; then incremental to `4bb1feb`, 2 steps) | 0 | **42/42**, 0 skipped |
| Windows `cl-release-tls`, `net_tls` target | 0 | **334 assertions / 29 cases** (the POSIX case is not built) |
| WSL `clang-tidy` (tree deleted first, `-k 0`) | 1, 605 of 606 steps | **1 finding**: `bugprone-use-after-move` at `HttpServer_test.cpp:742`, this round's own nit. The `std::move(handler)` sat inside `REQUIRE`'s `do`/`while`; the call is hoisted out of the macro. After the fix: build 0, **42/42**, 0 findings |
| `clang-format --all --check` (22.1.8) | – | 511 files clean |
| `check-tree-level-coverage.py` | – | 17 of 17 covered |
| `mkdocs build --strict` | 0 | |

**Two notes on the tidy leg:**
- **`/tmp` was wiped mid-run.** In the first attempt, the logs of all three WSL legs disappeared
  from `/tmp` during the run (WSL is shared with other sessions). The asan line came back truncated
  and tidy's logs were lost, so both legs were rerun, with logs in the scratchpad.
- **The UCD download fails.** unicode.org answered the tidy tree's `UCD.zip` download with an HTTP
  error. The tree was seeded with the `_ucd` directory of this worktree's own `clang-debug` tree:
  the same 17.0.0 zip, identical MD5.
