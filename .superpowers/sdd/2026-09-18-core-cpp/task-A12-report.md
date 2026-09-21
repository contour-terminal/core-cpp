# Task A12 report: Phase A gate, pass 4 (`core::net`)

All 15 findings are fixed and pushed to `master` (`e49dc81..36e6bf3`, 13 commits).

## RED/GREEN per finding

**Six findings have a behavioural RED** — a case that failed before the fix and passes after: 1, 6,
7, 8, 9 and 10. It was captured in one run: with every production fix reverted and every new test
kept, `core-cpp-net-test` on `clang-debug` reported **7 failing cases / 10 failing assertions**
across those six; with the fixes restored it reports **130 cases, 684 assertions, all passing**
(plus `net_tls`: 6 cases, 40 assertions). The revert-and-run is the "show the red by swapping files"
technique `.agent/rules/` sanctions for behaviour.

The other nine are **not** regression tests, and the table says which is which:

- **Finding 2** is reproduced, but by its *control arm* rather than by the production path: the race
  cannot be opened from outside `accept()`, so the RED is the old primitive losing the wake-up in
  the state the race ends in, plus a build failure before `consumeNetworkEvents` existed.
- **Finding 11** and the portable arms of findings 2 and 9 are **characterisation** tests: green
  before and after, guarding the outcome rather than reproducing the defect.
- **Findings 3, 4, 5 and 12** have no test at all (see "Left deliberately").
- **Findings 13, 14 and 15** are test-discipline changes, whose "RED" was a hang, a cascade and
  undefined behaviour respectively — none of them a failing assertion.

| # | Finding | Test | RED → GREEN |
|---|---|---|---|
| 1 | HTTP head: bare-LF blank line | `HttpServer_test.cpp` "readRequest refuses a head whose blank line is a bare LF" | RED `REQUIRE_FALSE(seen.has_value())` → one request parsed with the smuggled headers. GREEN |
| 2 | Windows listener stops accepting | `windows/NetworkEvents_test.cpp`, 2 cases | RED = does not build (`consumeNetworkEvents` did not exist). See "Platform-specific" below. GREEN on `clangcl-debug`, `cl-debug`, CI `windows (*)` |
| 3 | Unchecked `BIO_new` | — | No RED possible (see "Left deliberately"). Fixed by inspection |
| 4 | `flushOut()` reports success after dropping ciphertext | — | No RED possible (same). Fixed by inspection |
| 5 | Zero-length write reads a stale `errno` | — | No RED possible (same). Fixed by inspection |
| 6 | `WriteQueue::close()` on a null socket | `WriteQueue_test.cpp` "A WriteQueue refuses a null socket at construction" | RED "no exception was thrown where one was expected". GREEN |
| 7 | Stale scan offset skips buffered bytes | `AsyncBufferedReader_test.cpp` "readUntil rescans the buffer when the delimiter changes" | RED `readUntil("X")` reported `Eof` for an `X` at index 3 it was holding. GREEN, payload `"abc"`, 3 bytes still buffered |
| 8 | `isClosed()` never latches EOF | `Socket_test.cpp` "a socket reports closed once a read observed the peer's EOF" (per backend) + "a split socket is closed once its read half observed EOF" | RED `CHECK(pair->first->isClosed())` false on poll and epoll, and the `SplitSocket` case false. GREEN |
| 9 | Poll backends disagree about `FdInterest::None` | `EventSourceParity_test.cpp` "a muted registration is reported by no event source" + "a muted registration stays silent when its peer hangs up" | RED `CHECK(silent.readyRead.empty())` false on poll and epoll. GREEN on poll, epoll (Linux), kqueue (FreeBSD + macOS CI), Windows |
| 10 | `ScriptedEventSource::detach` not idempotent | `EventLoop_test.cpp` "The scripted source detaches idempotently, like every real backend" | RED `attachedCount() == 1` gave 0 after a repeated detach. GREEN |
| 11 | Listener CLOEXEC window | `posix/UnixSocket_test.cpp` "makeStreamSocket hands back a non-blocking, close-on-exec descriptor" | Characterization, not RED (see "Platform-specific") |
| 12 | Missing `<stdexcept>` | the build | Added; no case can observe a transitive include |
| 13 | `REQUIRE` inside a `whenAll` arm | swept, `Socket_test.cpp` | The old shape's RED *was* the hang. Now the arms record and release their sibling, and the case asserts after `whenAll` returns |
| 14 | `RLIMIT_NOFILE` restored by a plain statement | `EventSourceParity_test.cpp` | Now a `detail::ScopeGuard` over an inner scope; the recovery assertion after it still runs unsqueezed |
| 15 | Unchecked `makeSocketPair()` deref | `Tls_test.cpp` ×4 | `REQUIRE(made.has_value())` before every deref |

## The two decisions

**Finding 8 — latch, do not narrow.** `ISocket::isClosed()` keeps its documented meaning ("`close()`
was called **or** a read observed EOF") and `PosixSocket`, `WindowsSocket` and `TlsSocket` now latch
it. Reasoning: "did I close this myself" is something the caller already knows; "is this connection
still worth holding" is the question it comes to `isClosed()` to ask, and `SplitSocket::isClosed()`
is built on that reading. Narrowing the contract would have left every consumer to re-derive the
answer from a read returning 0, which they cannot do without owning the read.

The latch is a **separate flag**, not `_closed`: `_closed` gates `read()`/`write()`, and a peer that
shut only its write side leaves this end able to keep writing (tmux's half-close). So `write()` keeps
working and `read()` keeps returning 0 after `isClosed()` turns true — the behaviour change is
confined to the one accessor. `TlsSocket` latches its own EOF rather than only forwarding: a
`close_notify` ends the TLS session whether or not the TCP connection underneath is open, and
forwarding alone answered "open" for a session the peer had already ended.

**Finding 9 — mute, on every backend.** `FdInterest::None` now reports nothing anywhere. Reasoning:
`None` is documented as "mute the fd without detaching it", Windows and kqueue already behaved that
way (2 of 4), and a caller that wants to be woken when a descriptor dies asks for `Read`, which
reports HUP/ERR as read-readiness by design — if `None` did that too, the two values would differ
only in name. The epoll side settles it: `EPOLLHUP`/`EPOLLERR` are level-triggered and cannot be
suppressed for a registered descriptor, so "wake on error while muted" meant a ready report on
*every* wait, i.e. a spinning pump.

Implementation per backend: poll(2) submits a muted entry with a **negative descriptor** (the kernel
ignores it and reports 0 revents) — the entry is kept, not skipped, because the routing pairs
`fds[i]` with `registrations[i]`; epoll never adds a muted registration to its set; kqueue skips the
two `EV_DELETE`s it used to issue for filters nobody armed. A muted registration still counts as
attached and is still found by `detach()` on all four.

Both are recorded under **Breaking** in `CHANGELOG.md` with migrations, as is finding 6.

## The three platform-specific findings

**Finding 2 (Windows listener).** The race is a few instructions wide and lives *between* two Winsock
calls inside `accept()`, so it cannot be opened from outside. `windows/NetworkEvents_test.cpp`
instead drives a raw listening socket into the state the race **ends** in — `::accept()` returns
`WSAEWOULDBLOCK`, a client connects, Winsock records `FD_ACCEPT` and signals the event — and pins
both arms:

- *consume*: `consumeNetworkEvents()` reports `FD_ACCEPT` (8), and `::accept()` then succeeds — the
  retry the fix performs.
- *control*: `WSAResetEvent()` in that same state leaves the event **dark for 250 ms**
  (`WAIT_TIMEOUT`) although the connection is there and is accepted immediately afterwards. That is
  the defect, reproduced: a park on that event never wakes.

RED was a build failure (`consumeNetworkEvents` did not exist). Verified locally on
`clangcl-debug` and `cl-debug`, and in CI on `windows (cl-debug | cl-release | cl-release-tls |
clangcl-release)`. `Socket_test.cpp` also gains a portable "a listener keeps accepting across
sequential connections" case, which is the end-to-end guard (green before and after — the race is
not deterministic enough to fail it, which is why the primitive has its own cases).

**Finding 9 (poll-backend divergence).** poll and epoll verified on WSL Linux (`clang-debug`,
`gcc-release`, `clang-asan-ubsan`, `clang-tsan`) — both were RED before the fix. kqueue verified via
CI: FreeBSD (`portability` run 35534416344, `core-cpp.net` passed in 0.87 s) and macOS
(`macos (appleclang)`, `macos (llvm-22)` — `core-cpp.net` passed; only `core-cpp.platform` failed
there, see below). Windows verified locally and in CI; it was already muting, and the portable arm
of the parity case pins that it still does.

**Finding 11 (listener CLOEXEC window).** Not observable from a test: after `bind()` returns, the
descriptor is close-on-exec either way, and the difference is only visible to a `fork`+`exec` racing
inside a sub-microsecond window — a test for that would be a fork/exec stress loop with no
deterministic outcome. Verified by inspection (both `bind()`s now use `makeStreamSocket()`, the
helper `connect()`/`connectUnix()` already use, which passes `SOCK_NONBLOCK|SOCK_CLOEXEC` to
`socket(2)` where the platform has them), plus a new unit case pinning the outcome the listeners
depend on — `makeStreamSocket()` really returns a non-blocking, close-on-exec descriptor — which
`FdUtils.hpp` had no direct coverage of at all. `makeNonBlockingCloexec()` is kept after `listen()`
because `makeStreamSocket()` ignores the fcntl failure on platforms without the atomic flags, and a
listener must not be handed back blocking. Exercised on Linux, FreeBSD and macOS in CI.

## Left deliberately

- **Findings 3, 4 and 5 have no RED test.** All three are defensive branches that no real transport
  can reach: `BIO_new` fails only on a failed allocation, and OpenSSL's allocator cannot be replaced
  after the library has initialised; `BIO_read` of a memory BIO that `BIO_ctrl_pending` says holds
  bytes cannot fail; and POSIX gives `write(2)`/`send(2)` no way to return 0 for a non-empty buffer
  on a stream socket, pipe or PTY. All three are fixed, and each commit message says which branch it
  could not drive and why.

  **"Unreachable" is not the whole answer, and a seam is the rest of it** — the platform task made a
  two-hop recase testable in this same window by injecting `NativeFileSystem`'s rename primitive, so
  the question is what the equivalent costs here, not whether it is possible. For finding 5 it is an
  injected syscall layer over `read`/`recv`/`write`/`send`/`recvmsg`, and it has to carry `errno`
  too, since `errno` is what the defect is about. Priced honestly: an indirect call on the path
  every byte of every connection takes, or a template that splits `PosixSocket` in two — against a
  branch that cannot fire. And `PosixSocket` is not the class that should grow it: **Task B6 merges
  it with fastcached's socket behind `IoBackend`**, which is exactly where the syscall boundary
  becomes an injected interface for the whole module rather than for one class about to be replaced.
  The same argument applies to findings 3 and 4 at **B11**'s TLS merge, where an injectable BIO pair
  makes both branches reachable. **Marked as Phase B items** (B6 for finding 5, B11 for 3 and 4)
  rather than paid for twice.
- **Finding 12** likewise: a missing include that compiles through a transitive one cannot be caught
  by a case, only by a build or an include-what-you-use pass.
- **The sequential-accept guard is green before and after.** It is a regression guard, not the RED
  for finding 2; the RED for that is the control arm in `NetworkEvents_test.cpp`.
- **`cl-release-tls` was not run locally**: OpenSSL is not installed on this Windows machine
  (`find_package(OpenSSL)` fails at configure). CI's `windows (cl-release-tls)` job covers the TLS
  changes on Windows and is green.
- **Intermediate commits were not individually built.** Several files span findings, so their hunks
  were split across commits with `git apply --cached`; every split was checked by reading (no commit
  references a symbol a later commit introduces), but only the final tree was compiled.

## Not mine — reported, not fixed

`core-cpp.platform` fails on macOS and FreeBSD, in `src/core/platform/FileSystem_test.cpp:878-891`
("rename reports why the recase failed, not why the first attempt did"). **This predates my push**:
it fails identically on `cfff6ee` (Build run 35530230409, Portability run 35530331089), which is
before any commit of mine. It is the platform agent's area (`a518402`, "platform: NativeFileSystem
takes its rename primitive"). Every other job of my run is green, `core-cpp.net` and `core-cpp.net_tls`
included, on every platform.

## Verification

Local, all green:

- `python scripts/clang-format.py --check` (pinned 22.1.8) — 369 files formatted.
- WSL: `clang-debug` (20/20 ctest), `gcc-release` (20/20), `clang-asan-ubsan` (20/20),
  `clang-tsan` (20/20), `clang-tidy` preset (no warnings).
- Windows (VS dev shell): `cl-debug` (22/22), `clangcl-debug` (net suites), `clangcl-release`
  `--clean-first` (22/22).
- `ctest -L hygiene` green in every tree above (it caught a C-style `for` and three missing
  provenance rows; both fixed).
- `python -m mkdocs build --strict`.

## CI runs (head `36e6bf3`)

| Workflow | Run id | Result |
|---|---|---|
| Build | `35533959758` | 22 of 24 jobs green; `macos (appleclang)` and `macos (llvm-22)` fail on the pre-existing `core-cpp.platform` case above, and `ci-ok` with them |
| Docs | `35533959697` | success |
| Portability | `35534416344` | `FreeBSD (system clang)` fails on the same pre-existing `core-cpp.platform` case; `core-cpp.net` and `core-cpp.net_tls` passed |

Green on my run and relevant to this task: `linux (clang-22 | clang-22-arm64 | clang-22-cxx26 |
clang-22-tracy | gcc-14 | gcc-15)`, `sanitizers (clang-asan-ubsan | clang-tsan)`,
`windows (cl-debug | cl-release | cl-release-tls | clangcl-release)`,
`emscripten (3.1.56 | latest)`, `clang-tidy`, `coverage`, `style`, `compile-cache`,
`consumer-smoke (cpm | vendored | wasm)`.

## Commits

```
e49dc81 fix(net): the HTTP head ends at the first blank line, whichever terminator made it
b75e42c fix(net): readUntil rescans the buffer when the delimiter changes
eafc6c1 fix(net)!: a WriteQueue refuses a null socket at construction
1edd482 fix(net): the TLS layer checks its BIO allocations and reports a failed flush
7ea6647 fix(net): a zero-length socket write is handled, not read as a stale errno
6adec1e fix(net): the Windows listener consumes its accept indication, never resets it
e867456 fix(net)!: a socket reports closed once a read observed the peer's EOF
aee52d6 fix(net)!: FdInterest::None mutes the descriptor on every backend
7615f60 fix(net): ScriptedEventSource::detach is idempotent, as EventSource documents
eea14a1 fix(net): the POSIX listeners create their socket close-on-exec atomically
ee06511 chore(net): EventLoop.cpp includes <stdexcept> for the throw it makes
417cb18 test(net): an assertion in a whenAll arm fails the case instead of hanging it
36e6bf3 docs: the changelog and the provenance table record the net gate's fourth pass
```

## Consumer impact

Three breaks, each with a migration in `CHANGELOG.md`:

- `ISocket::isClosed()` now answers true after a read observes the peer's EOF. A consumer that read
  it as "did I close this myself" must ask its own bookkeeping; one that polled it to drop dead
  connections gets the answer it wanted. `read()`/`write()` are unchanged, so the half-close path is
  unaffected. Grepped: no consumer in this checkout calls it outside `core::net` itself.
- `FdInterest::None` mutes on POSIX as it already did on Windows. A caller that attached with `None`
  and relied on a HUP wake-up asks for `Read`. No consumer attaches with `None`.
- `WriteQueue`'s constructor throws `std::invalid_argument` on a null socket. A caller must check
  `ITlsContext::wrap()`'s documented null before constructing.

---

# Fix round 1 (Ruling R60)

Three Important items and two report corrections. The corrections are folded into the sections
above (the RED count now reads six behavioural, with the other nine classified; finding 5's
"unreachable" now prices the seam and names B6/B11 as where it belongs).

## Item 3 — whitespace before a field-name colon (RED/GREEN)

`HttpServer.cpp` trimmed it, which RFC 9112 §5.1 makes a MUST to reject. Same class as finding 1:
a front-end that trims `Host :` back to `Host` and a server that rejects it do not agree on what the
message says, and the lenient half is the one that lets a header through under a name the other end
never saw. Exploitability here is low — one request per connection, always closed — but the parser
already refuses `Transfer-Encoding` and a conflicting `Content-Length` on exactly this reasoning.

A field name is a token, so whitespace anywhere in it is refused, and an empty name with it. The
leading-whitespace spelling was already refused one branch earlier, as an obs-fold continuation.

**RED** (HttpServer.cpp reverted, tests kept): `readRequest rejects whitespace between a field name
and its colon` fails on all four spellings —

```
field line=Host : example        REQUIRE_FALSE( seen.has_value() )  !true
field line=Host\t: example       REQUIRE_FALSE( seen.has_value() )  !true
field line=Host \t : example     REQUIRE_FALSE( seen.has_value() )  !true
field line=: headerless          REQUIRE_FALSE( seen.has_value() )  !true
test cases: 24 | 23 passed | 1 failed;  assertions: 104 | 100 passed | 4 failed
```

**GREEN**: all four refused with `NetErrorCode::Other`. The companion case,
`readRequest keeps accepting whitespace AFTER the colon`, passes before *and* after — it is the
guard that the rejection did not cost the valid spelling (`Host:  \texample \t` → `example`, and an
empty value stays a value).

## Item 1 — the finding-2 guard is bounded (RED/GREEN)

The guard now runs under a 10-second budget (`anyOf(work, budget)`), and reports through an `INFO`
that names what it waited for and how far it got.

The sentinel is the **timer's** flag, not a "the work finished" one. My first attempt used the
latter and it passed while the defect was present: `whenAny` cancels the loser, and a cancelled
`accept()` *resolves with an error* rather than throwing, so the accept arm returns cleanly on the
way out and set the flag on the failing path too. The timer's flag is reached only when the budget
really did expire first, which is the condition being reported.

**RED** (a simulated silent listener: `acceptSequentially` awaits a second `accept()` after its
first connection, so it stops accepting):

```
a listener keeps accepting across sequential connections / backend=poll   (and epoll)
Socket_test.cpp:291: FAILED:  REQUIRE_FALSE( timedOut )  with expansion: !true
with message: waited 10000ms for 3 sequential accepts on this listener;
              it accepted 2 and the client connected 3
real 0m20.1s   (two backends x the 10s budget)
```

That is the whole point of the item: 20 seconds and a sentence naming the defect, where before it
was 1500 seconds and the word "Timeout".

**GREEN**: the budget never fires — the whole `core-cpp-net-test` binary runs in **0.9 s**, so the
budget loses to the work by four orders of magnitude and can only expire on the defect.

**Backstop**: `core_cpp_add_test` takes an optional `TIMEOUT <seconds>`, and both net binaries set
`TIMEOUT 120` (they run in ~1.5 s locally and under 10 s on the slowest CI runner).

> **Superseded by `a13070c` (Ruling R64).** This paragraph originally said the bound was
> "deliberately not a project-wide default". It is one now: every test `core_cpp_add_test()`
> registers is bounded at 300 s unless a `TIMEOUT` says otherwise. Opt-in left exactly the binaries
> nobody thought about unbounded, which are the ones that need it. See the R64 addendum below.

## Item 2 — the second shape of the whenAll sweep

**The shape swept for, so the next person can repeat it**: an arm of a `whenAll` (or of an
`allOf`/`anyOf`, or of a flow joined across threads) that **gives up early — `co_return` on a failed
`connect`, `write` or `read` — while a sibling is parked on something only that arm could have
produced.** The first sweep looked for assertions inside an arm; this one looks for *exits*. The
question to ask at every early `co_return` in a test coroutine: *if I return here, what wakes my
sibling?* Where the answer is "nothing", the arm must stop the sibling — close the listener it is
parked in `accept()` on — before returning. An `accept()` is the sibling that matters, because
nothing but a connection or a close ever resolves it.

Fixed at the five sites named by the review:

| Site | Arm | Sibling it stranded |
|---|---|---|
| `Socket_test.cpp:91` | `echoClient`, failed `connect` | `echoServer` in `accept()` |
| `posix/UnixSocket_test.cpp:103` | `connectAndProbe`, failed `connectUnix` | `echoOnce` in `accept()` |
| `EventSourceParity_test.cpp:104` | `connectAndSend`, failed `connect` | `acceptAndEcho` in `accept()` |
| `Tls_test.cpp:258` | the two-reactor client, failed `connect` | the server **thread**, in `accept()` on its own loop — the hang landed on `std::thread::join` |
| `Tls_test.cpp:333` | the cancelled-handshake client, failed `connect` | same |

The two cross-thread ones cannot close the listener directly: it belongs to the server's loop and
must be closed on that loop's thread (`.agent/rules/async-and-net.md` — a loop-owned object dies on
the loop's thread). They go through a `releaseServer()` helper that **posts** the close, which is
also what breaks the wait the server is blocked in. Threading the listener through in place of the
bare port removed the now-unused `port` locals in both cases.

Each of the three single-loop arms takes the listener where it used to take a port, which is
strictly less to pass and makes the stop available at the point of the give-up.

No other site survives the sweep: every remaining early `co_return` in this module's tests is on a
path where the sibling has already accepted, and the socket closing under it is what finishes it —
noted in a comment at each so the next sweep does not have to re-derive it.

## Verification (fix round 1)

- `python scripts/clang-format.py --check` — 369 files, clean.
- WSL `clang-debug`: `core-cpp-net-test` 132 cases / 707 assertions, `core-cpp-net_tls-test` 6 / 40,
  full `ctest` 20/20; `gcc-release` 20/20; `clang-asan-ubsan` 20/20; `clang-tsan` 20/20;
  `clang-tidy` preset clean.
- Windows: `cl-debug` 22/22, `clangcl-release` 22/22.
- `ctest -L hygiene` green, `python -m mkdocs build --strict` green.

## CI (fix round 1, head `50c3c2d`)

| Workflow | Run id | Result |
|---|---|---|
| Build | `35537221014` | **all 24 jobs green**, `ci-ok` included — macOS is green again now that the platform lane fixed `FileSystem_test.cpp` |
| Docs | `35537221002` | success |
| Portability | `35537728427` (and the push-triggered `35537251076`) | **FreeBSD green** |

The pre-existing `core-cpp.platform` failure reported in the first round is gone: it was the
platform lane's, and they fixed it in this window.

## Shared-file note

My fix-round `CHANGELOG.md` hunk reached master inside another agent's commit
(`78824d1 docs: the changelog says what breaks in one place`), which staged the file whole while my
edit was in the working tree. Nothing of mine was lost or altered — all three Breaking entries are
still under **Breaking** after their restructure, and all eleven Fixed entries are present — so
there is nothing to undo, and their commits are not mine to touch. Recording it because it is the
shared-file hazard the constraints warn about, biting in the direction the warning does not
describe: the discipline protects the file from *me*, not me from another agent staging it whole.

Separately, the cli lane added a ctest `TIMEOUT` for the same reason an hour before I did, with a
raw `set_tests_properties` guarded by `if(TEST ...)`. The `TIMEOUT` keyword on `core_cpp_add_test`
that this round adds makes that guard unnecessary; I left their file alone.

---

# Fix round 1, addendum: the default test timeout (Ruling R64)

Asked for as part of the `TIMEOUT` change, which was already pushed as `f5ccf0e`, so this went in
as a **follow-up commit (`a13070c`) rather than an amend** -- `f5ccf0e` is published history and not
mine to rewrite.

**What it does.** Every test `core_cpp_add_test()` registers is bounded at **300 seconds**; an
explicit `TIMEOUT` still wins. The keyword alone was opt-in, which left exactly the binaries nobody
thought about unbounded -- and those are the ones that need it, because a case here can hang rather
than fail (a lost readiness wake-up parks a flow with nothing to resume it; a wrapping loop stops
making progress -- both have happened in this tree in the last two days).

**Verified, not assumed** -- the generated `CTestTestfile.cmake` on both platforms:

| Test | clang-debug | cl-debug | |
|---|---|---|---|
| `core-cpp.cli` | 60 | 60 | its own tighter bound, set after the call, survives |
| `core-cpp.net`, `core-cpp.net_tls` | 120 | 120 | explicit, survives |
| everything else | 300 | 300 | the default |
| `tests/` checks (`vendor-selftest` at 87 s, …) | none | none | registered directly, out of this function's reach |

`core-cpp.tui` is the binary the number is sized against: 9.4 s on clang-debug, **11.07 s measured
under asan/ubsan in this round**, 9.81 s on cl-debug -- ~27x of headroom.

**Green:** WSL `clang-debug` 20/20 and `clang-asan-ubsan` 20/20 with the default in place. On
Windows, the 10 binaries untouched by other lanes' in-flight work pass 10/10 and the timeouts
generate correctly.

**Two failures on Windows are other lanes' uncommitted work in this shared checkout, not mine** --
my change is CMake-only and Linux was 20/20 green with it:

- `core-cpp.tui` fails at `src/core/tui/GenericSyntaxHighlighter_test.cpp:955` (two assertions);
  that file is modified-uncommitted in the tree.
- `core-cpp.cmake-hygiene` fails for two untracked headers with no provenance row,
  `src/core/async/DetachedTask.hpp` and `src/core/async/ParkedWork.hpp`.
- `ParkedWork.hpp` is also the one file failing `clang-format --check`. I did not format it:
  rewriting another lane's work-in-progress under them is the same mistake as staging their hunks.
- A cl-debug build failure I hit first was transient -- their edits mid-save -- and cleared on the
  next run.

---

# Fix round 2 (Ruling R70)

One Important and four Minor. The re-review cleared items 1, 2 and 4 of round 1, including reasoning
my sentinel's cancellation path for itself.

## Item 1 — the sixth give-up site (RED/GREEN)

`posix/UnixSocket_test.cpp` — `connectAndProbe` returned on a failed write with the comment *"the
server already accepted; its arm sees this socket close and finishes"*. It does see the close. That
is not the same as finishing.

`echoOnce` is a **draining loop**: `while (!*served)`, and a read of 0 takes its `continue`, not a
`co_return`. So the close the comment relied on sends it straight back into `accept()` with
`*served` still false, and it parks there for ever.

**RED** (the branch forced by closing the socket before the write, with the pre-fix behaviour):

```
$ time timeout 45 ./core-cpp-net-test "listenUnix + connectUnix echo over a socket file"
FAILED: {Unknown expression after the reported line}
due to a fatal error condition:  SIGTERM - Termination request signal
real  0m45.056s
```

It did not fail. It ran until I killed it — 45 s here, 1500 s under ctest's default.

**GREEN** (same forced branch, with the `listener->close()`):

```
UnixSocket_test.cpp:168: FAILED:  REQUIRE( served )  with expansion: false
real  0m0.107s
```

0.107 s and a named assertion, instead of a hang.

**I also corrected the claim rather than quietly fixing the site.** `50c3c2d`'s message said "every
early return that remains is on a path where the sibling has already accepted and the socket closing
under it is what finishes it". That was false at this one site, and *saying it is what made the site
look swept* — the comment I added there was the thing that stopped the next reader from checking.
The new commit says so.

## The lesson: a sibling's shape decides whether an early return is safe

The sweep's question from round 1 — *if this arm returns here, what wakes its sibling?* — was right.
My answer at this site was wrong, because I checked whether the peer would **observe** my close
rather than whether observing it would **end** the peer's flow. Those differ exactly when the
sibling loops:

| Sibling shape | On its peer's EOF | Is an early return safe? |
|---|---|---|
| `co_return`s on EOF (`echoServer`) | finishes | **Yes** — returning destroys the socket, and the EOF ends it |
| `continue`s on EOF (`echoOnce`) | loops back into `accept()` | **No** — only a stop reaches it |

Both shapes are in this module, 300 lines apart, and I had already written the correct pairing for
the looping one (`unixProbe` in `Socket_test.cpp` closes the listener on exactly this branch). The
question to carry forward is not "does my peer observe my close" but **"does observing my close end
my peer's flow"**.

## Item 2 — `unixEcho`'s discarded write (the same rule, opposite answer)

Fixed rather than argued away. It discarded its write result and walked into `read()`, parking there
while its sibling parked in its own `read()` waiting for bytes never sent — two arms, no stop, the
same hang by another route. Reachability is thin (a 9-byte write to a fresh AF_UNIX socket), but
"thin" is not "examined".

Here a plain `co_return` **is** the fix, no listener close: returning destroys the socket, and the
sibling is `echoServer`, which `co_return`s on the EOF. Same question as item 1, opposite answer,
because the sibling has the opposite shape — which is why the rule above is worth stating as a rule.

## Items 3-5 — the corrections

- **`cmake/CoreCppTargets.cmake`**: the comment claimed `tests/` was the only registration the
  300-second default does not reach. `core-cpp.async-link-smoke` is a bare `add_test()` too.
  **Named, not fixed**: `src/core/async/` is another lane's and mid-task, and the controller is
  routing the bound to them.

  Corrected once more in `f90aacb`, on the controller's note that they are bounding it: my first
  wording said it "is unbounded today", which is a fact with an expiry date — once that lane lands,
  the sentence is wrong in the other direction. The comment states the RULE instead, which does not
  expire: the default reaches only what `core_cpp_add_test()` registers, a bare `add_test()`
  elsewhere keeps ctest's 1500-second default unless it sets its own `TIMEOUT`, and exactly two
  places register that way (the checks in `tests/`, and `core-cpp.async-link-smoke`). Checked by
  grepping every `add_test(` in the tree rather than trusting the two I already knew about — those
  are the only two. Naming them tells the next person where to look; "means deciding its bound with
  it" tells them what to do when they add a third.
- **`Socket_test.cpp`**: "three orders of magnitude" was wrong; the measured figure is 0.147 s for
  both sections against the 10 s budget, about **70x**. The comment says the measured number now.
- **This report**: the "deliberately not a project-wide default" paragraph now carries a
  superseded-by note pointing at `a13070c`, instead of contradicting it.

## Verification (fix round 2)

- `clang-format --check` on both files I touched: clean.
- WSL `clang-debug`: 21/22 (the one failure is not mine, below). `gcc-release`: my module and every
  other complete binary 11/11. `clang-asan-ubsan`: net 3/3.
- Windows `cl-debug`: 9/9 over the affected binaries. `clangcl-release`: net 2/2.
- **Build `35538763899` on `a13070c` completed green — all 24 jobs** — which is the run that covers
  the default timeout, as the round asked. CI for this round (`98937f7`) is being watched.

## Not mine, again

Left alone, per the round's instruction and because they are other lanes' live work:

- `core-cpp.cmake-hygiene` fails on **10** provenance rows, every one under `src/core/async/`
  (`AsyncQueue`, `DetachedTask`, `IExecutor`, `ParkedWork`, `ResumeOn`, `SyncRun`,
  `ThreadPoolExecutor` and their tests).
- `gcc-release` does not build `src/core/async/IExecutor.hpp:56` —
  `error: 'virtual void IExecutor::submit(ParkedWork)' was hidden [-Werror=overloaded-virtual=]`.
  Worth passing to that lane: it is a GCC-only `-Werror` break, so a clang-only local loop misses it.
- Seven `src/core/async/*` files fail `clang-format --check`. I did not format them.
- `src/core/net/NetError.{hpp,_test.cpp}` untouched, per the round's instruction.

---

# Ruling R72: the `tests/` checks carry their own bounds

`9d0bbc3`. These were the last unbounded registrations in the tree.

**First, a correction to what I told the controller.** I said "the `tests/` checks are unbounded
today". Two of the nine already were bounded — `migrate-renames` and `migrate-codemods` at 300,
added by the tools/migrate lane after the grep I based that on. Seven were unbounded, not nine. The
grep was right when it ran and stale by the time I quoted it; I should have re-run it before making
the claim rather than citing a measurement taken an hour earlier in a tree three lanes are writing
to.

## The numbers, measured rather than chosen

Worst of two runs on each of `clang-debug` (WSL) and `cl-debug`:

| check | linux | windows | bound | headroom |
|---|---|---|---|---|
| `exit-codes` | 0.09 | 0.09 | 60 | 600x |
| `platform-sources` | 0.35 | 0.42 | 60 | 140x |
| `check-release-selftest` | 0.47 | 1.16 | 60 | 50x |
| `layering` | 3.32 | 1.36 | 120 | 36x |
| `cmake-hygiene` | 7.57 | 13.79 | 180 | 13x |
| `cmake-hygiene-selftest` | 13.71 | 1.04 | 180 | 13x |
| `vendor-selftest` | 92.80 | 30.73 | 600 | 6x |

One number per check, not one for the file: three orders of magnitude separate the ends of that
column, and a single bound would be either useless at the top or dangerous at the bottom. The three
cheapest share a **floor** of 60 rather than a multiple of their own runtime — at 0.1 s the cost is
process startup on a loaded runner, not the work, so a multiple would be measuring the wrong thing.
`layering` gets more than its 3.3 s asks for because it configures real CMake projects, the part of
this set most exposed to a cold runner. `vendor-selftest` gets the most in absolute terms and the
least in multiples: it builds a git repository per case and hashes a tree, and it was the only check
that moved appreciably between runs (84 s and 93 s).

**On the "already unbounded by design" question**: none of them qualifies. Every one is
compute-bound — it reads the tree, spawns `cmake`, or shells out to a **local** git. No network, no
peer, no user input. So none is legitimately slow for a reason a bound would cut short, and every
number above can only fire on a defect. Said in the comment too, because it is the premise the
numbers rest on.

**Verified** on both platforms from the generated `CTestTestfile.cmake`, the same way as the 300:
every registration bounded, none unbounded, `migrate-*` keeping their 300 and the Windows canaries
their 60. Then run: every check completes well inside its bound on both (`vendor-selftest` 82 s
against 600, `cmake-hygiene-selftest` 12.8 s against 180, `layering` 3.2 s against 120), and no
check reported `Timeout`.

## The cross-reference in `CoreCppTargets.cmake`

The controller asked for its last correction only *if true at landing*. Half of it is: `tests/` sets
its own bounds now, so "they want a looser bound than this one" is no longer what is true of them,
and it now reads "which set theirs per check because their runtimes differ by three orders of
magnitude (0.09s to 93s)". The other half is **not** yet true — `core-cpp.async-link-smoke` is still
unbounded (checked in the generated file, not the source) — so the sentence deliberately claims
nothing about its state, and the rule it states holds either way.

## No CHANGELOG entry, deliberately

`tests/` checks are not public API, not a dependency, and nothing a consumer can observe;
`.agent/rules/library-hygiene.md` ties entries to the module's `FILE_SET HEADERS` and to
dependencies. The entry already landed for the 300-second default remains true and unqualified.
`CHANGELOG.md` is also mid-rewrite by the async lane right now (56 insertions of Phase B surface),
so adding a hunk would invite the same sweep that took my last one. Flagged to the controller to
overrule if they disagree.

## Not mine (the list has changed again)

- `core-cpp.cmake-hygiene` now fails on **`src/core/net/detail/WaitTimeout.hpp`** and its siblings.
  A Phase B lane has started inside **`src/core/net/`** — `IoBackend.hpp`, `PollBackend.{hpp,cpp}`,
  `EpollBackend.hpp`, `detail/ReadyBatch.hpp`, `detail/WaitTimeout.hpp`, `detail/WakeupChannel.hpp`,
  all untracked. My task brief said `src/core/net/` was mine; it is shared now. Worth the
  controller knowing, since it changes who may sweep what in this directory.
- `core-cpp.migrate-renames` and `core-cpp.migrate-codemods` fail — the tools/migrate lane's.
- The async lane's provenance rows are fixed; their files no longer appear in the violations.

---

# CI coverage, stated the way R73 requires

`master` is linear, so a green Build at head H proves everything in H's history builds and passes
together. What follows is therefore per-commit **containment**, not per-head runs — and the runs on
my own heads are mostly worthless, for a reason worth writing down.

**The supersession mechanism.** `build.yml` sets `cancel-in-progress: false`, which protects a run
that has already *started*. GitHub supersedes a **pending** run in a concurrency group regardless of
that setting. With four lanes pushing against a ~20-minute, 24-job Build, every head that queues
behind a running build is cancelled **before executing a single job**. Such a run reports
`conclusion: cancelled` and `jobs: []` — it looks like a real result in a run list, and is not one.
`gh run view <id> --json jobs --jq '.jobs | length'` is what tells them apart. Three of my heads died
that way (`f90aacb`, and the runs on `aa49c53`/`73d4569` that would have covered it).

| My commit | What it is | Covered by a completed green run? |
|---|---|---|
| `edb1340`, `f5ccf0e`, `50c3c2d` | round 1 | **Yes** — Build `35537221014`, green, 24/24 |
| `a13070c` | the 300s default | **Yes** — Build `35538763899`, green, 24/24 |
| `dda4050`, `98937f7` | round 2 | **Yes** — Build `35540278214`, green on `98937f7` |
| `f90aacb` | a comment reword | **No run of its own**; its content is text, and it is contained in `9d0bbc3` below |
| `9d0bbc3` | R72, the `tests/` bounds | **Not yet.** Its own run (`35541190425`) was pending at the time of writing and the two runs before it were cancelled unexecuted. Watching for the first *completed green* run that contains it. |

So: **R72 is landed and verified locally on both platforms, and is not yet covered by a green CI
run.** Stating that plainly rather than citing `35541190425`, which had executed no jobs — the
failure mode R73 exists to prevent, and one I was a step away from committing.

A note on what a green run buys here beyond my own commits: `35540278214` covered three other
lanes' work that had queued behind it — the merged error vocabulary, the rename table, and the tui
registration seam — so one run verified four lanes together. That is the property worth having, and
it is why "green on a descendant" is a stronger claim than "green on my own head", not a weaker one.
