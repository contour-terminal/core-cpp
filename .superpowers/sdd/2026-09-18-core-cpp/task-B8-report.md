# Task B8 report: dialling and resolution, and DNS never on a loop thread

Base: `origin/master` at **`770f2dc`**, asked of the remote with `git ls-remote origin refs/heads/master`
as the dispatch said to. **It moved while I worked** — `origin/master` is `d2860dc` as I write this,
one commit ahead. That commit touches `.superpowers/sdd/` only (B6's and B12's records plus three
new global constraints) and no source, so this rebases without conflict. One of those new
constraints changed what I did: see §7.

Worktree: `D:/core-cpp-wt-b8`, detached at `770f2dc`. `D:\core-cpp`'s working tree and `master` ref
were never touched; the only file I wrote there is this report.

---

## 1. Status

Delivered. Two commits, **`770f2dc..abe699a`**, in `D:/core-cpp-wt-b8` and not pushed:

- `ce37aac` — the task (45 files, +4870/-239).
- `abe699a` — the cross-thread cancellation case, and a code comment corrected to say what §7
  could and could not demonstrate (2 files, +96/-7).
- `f228143` — one include: `ConnectFlow_test.cpp` named `core::net::EventLoop` while reaching the
  declaration through `testing/TestLoop.hpp`. B6 is removing `EventLoop.hpp` from `ISocket.hpp`'s
  include graph, and a file that names a type it does not include is the shape that breaks on
  somebody else's header change.

Every gate green on the legs this machine can run. The two I cannot run — macOS/kqueue and
FreeBSD — carry a hypothesis in `ce37aac`'s commit message for CI to refute.

---

## 2. What the dispatch still gets wrong

Run, not assumed: `git ls-tree -r --name-only 0708dd54 src/FastCache/Net/` over the whole directory,
and `git show 0708dd54:<path>` for every file named below.

**(a) The central correction is right, and it is the only source line that was.** `Net/ReactorDial.hpp`
exists, is `#if defined(__linux__) || defined(__APPLE__)`, and holds `Detail::ReadinessDialOp`,
`DialPark`, `SettleDial` and `DialReadiness<Traits>`. `Net/IocpDial.hpp` exists. `DialOptions` is
`Net/IConnector.hpp:110` **exactly** — line 110, not approximately. The four files said to have no
`_test.cpp` have none.

**(b) The traits are a QUADRUPLE, not a triple, and `ReactorDial.hpp`'s own comment says triple.**
`@tparam Traits `{ Reactor, Handler, Socket }` for the platform`` — but the body also calls
`Traits::Settle` (lines 197-199), which `EpollConnector.cpp` and `KqueueConnector.cpp` each define.
The dispatch repeats the header's wording. It cost me nothing because de-templatising removes all
four, but a lane that had tried to keep the template would have found the fourth by compile error.

**(c) `KqueueConnector` has no test at the pin.** The dispatch says to "read both wrappers", which is
right and which I did — but the reason it gives (deriving a portable dial from the Linux wrapper
alone) understates it. There is no `KqueueConnector_test.cpp`; `PlatformConnector_test.cpp` is the
only thing that ever ran the kqueue path, and it is generic. So the macOS dial upstream was covered
by one indirect suite. That is the shape R101 exists for, seen from the other side.

**(d) `Net/PlatformConnector.hpp` is a header with no `.cpp`** — the Sources list's brace expansion
`{...,PlatformConnector,...}.*` implies a pair. Cosmetic.

**(e) `BlockingConnector` is in my Sources list and cannot be built in my scope.** It needs
`BlockingSocket`, which `task-B9-brief.md` §3 rules to **B9** in the same paragraph that rules
`BlockingConnector` and `IAdmissionControl` to **B8**. `BlockingConnector.cpp` at the pin includes
`Net/BlockingSocket.hpp` and constructs a `BlockingSocket` directly. **Ruling I took: `BlockingConnector`
goes to B9 with `BlockingSocket`, and `IAdmissionControl` stays mine (it has no such dependency and
is here).** What B9 needs from me is already in: `InlineAddressResolver` is the never-suspending
resolver `syncRun` requires, and `detail::runConnectFlow` takes a null loop and a null clock.
Named again in §9.

**(f) "`IocpDial`" in the "What to build" list is not buildable in this phase, and I did not build it.**
`Net/IocpDial.hpp` issues `ConnectEx` and settles through an `IocpCompletion` dequeued by the port;
`IocpConnector.cpp` constructs an `IocpSocket`. core-cpp has no `IocpSocket` — B7a landed
`IocpBackend` and B7b owns the socket. Windows dials through the readiness path instead
(`windows/DialPrimitives.cpp`, `WSAEventSelect`), which is what `WindowsSocket` already is, and that
is **tested and green on `cl-debug` and `clangcl-release`**. §9 hands `IocpDial` to B7b/B9 explicitly.

---

## 3. What I found in the tree that the dispatch told me to check

**`selectReadinessCallback` (`src/core/net/IoBackend.hpp:236`) is exactly as described.** Read before
relying on it. A watched direction wins first — including when `Failed` came with it — then
`onError` only for a failure with no watched direction, then a fallback so a level-triggered
registration cannot report for ever. So a dial arming `Interest::Write` **is** woken by a failed
connect on every backend, and `onError` would be a second route to the same callback rather than a
necessary one. My dial therefore registers **one** callback on **one** interest, where upstream
wired three hooks to one function.

**B6's frame-free readiness park is the mechanism, and I used it rather than inventing a second.**
`ParkEntry::onReadyCallback` + `EventLoop::registerPark`/`unregisterPark`, the shape `PosixSocket::armRead`
uses. `runDueCallback` reads `onReady`/`callbackState` out of the table *before* calling, and says so,
which is what makes unregistering from inside the callback legal.

**Known-and-deliberate items I did not report as defects:** `WindowsSocket` has no `cancelRead`; the
listener side was untouched (it is mine, and §4 says what I did with it).

---

## 4. What I built

| File | What |
|---|---|
| `SocketAddress.{hpp,cpp}` | `ResolvedEndpoint`, `IAddressResolver`, `SystemAddressResolver`, `defaultAddressResolver`, `formatPeerAddress`, `detail::{isNumericHost,endpointFromSockaddr,portOfSockaddr}` |
| `IAsyncAddressResolver.hpp` | the seam, `ResolveResult`, `resolveFailure`, `InlineAddressResolver` |
| `ThreadedAddressResolver.{hpp,cpp}` | the shipped resolver, `defaultAsyncResolver()` |
| `KeepAlive.hpp` | `KeepAlive`, `KeepAliveSettings` |
| `IConnector.hpp` | `DialOptions`, `IConnector`, `makeConnector` |
| `ConnectFlow.{hpp,cpp}` | `detail::DialStep`, `detail::runConnectFlow` |
| `ReadinessDial.{hpp,cpp}` | `detail::dialReadiness` — **the de-templatised `ReactorDial.hpp`** |
| `detail/DialPrimitives.hpp` + `posix/`, `windows/` | the four OS calls a dial needs, one implementation per platform chosen by the source list |
| `Connector.cpp` | `ReadinessConnector`, the one connector |
| `SocketDeadline.hpp` | `SocketDeadlineTarget`, `armSocketDeadline` |
| `IAdmissionControl.hpp` | `IAdmissionControl`, `CountingAdmissionControl` |
| `Sockets.{hpp,cpp}` | `ListenOptions`, `listen(loop, ListenOptions)`, `adoptListener`, `connect` re-implemented over `makeConnector`, plus the injectable overload |
| `IListener.hpp` and the three listeners | `localPort` → `boundPort`; `AcceptResult` is an alias of `SocketResult`; `PosixListener::adopt`, `WindowsListener::adopt` |

**Decisions that are mine rather than upstream's**, so a reviewer can disagree with them by name:

1. **One connector, not three.** `EpollConnector` and `KqueueConnector` are byte-identical upstream
   but for two type names. core-cpp has one `EventLoop` over an `IoBackend`, so `PlatformConnector`'s
   `#if` ladder selects from a set of one. `removed` rows in `renames.json` pin all three gone.
2. **The clock stays a separate parameter of `runConnectFlow`** even though `EventLoop::clock()`
   exists. That is what lets `ConnectFlow_test` assert the total budget and the per-candidate share
   as arithmetic on a `ManualClock`, with no loop turning and no sleep anywhere.
3. **`connect(loop, host, port)` keeps its signature** and reaches a *named* process resolver,
   `defaultAsyncResolver()`, rather than a private static. A caller with its own resolver uses the
   five-argument overload. That matches `defaultAddressResolver()` and `platform::defaultSteadyClock()`.
4. **The positional `listen(loop, host, port, backlog)` stays**, forwarding to the `ListenOptions`
   form. The rename map asks for the named form; removing the positional one would have rewritten
   five test files three other lanes hold open, for no behaviour.
5. **`ReusePort` / `BindAndListen` / `BoundPortOf` / `PeerAddressOf` are NOT imported.** core-cpp's
   listeners own their bind sequence; a second one is a second answer to who owns an address.
   `removed` rows pin each.
6. **`adoptListener` got two test files rather than none or one.** See §6.

---

## 5. RED, verbatim, and the prediction I committed to first

The prediction is in this session's scratchpad and was written **before** the first build:
five translation units fail to compile, zero Catch cases run, and the refused-connect guard is
expected **green on arrival** rather than red.

### RED — `cmake --build --preset clang-debug --target core-cpp-net-test`, exit 1

```
/mnt/d/core-cpp-wt-b8/src/core/net/SocketAddress_test.cpp:2:10: fatal error: 'core/net/SocketAddress.hpp' file not found
/mnt/d/core-cpp-wt-b8/src/core/net/ConnectFlow_test.cpp:3:10: fatal error: 'core/net/ConnectFlow.hpp' file not found
/mnt/d/core-cpp-wt-b8/src/core/net/Connector_test.cpp:4:10: fatal error: 'core/net/IAsyncAddressResolver.hpp' file not found
/mnt/d/core-cpp-wt-b8/src/core/net/ReadinessDial_test.cpp:8:10: fatal error: 'core/net/KeepAlive.hpp' file not found
/mnt/d/core-cpp-wt-b8/src/core/net/ThreadedAddressResolver_test.cpp:4:10: fatal error: 'core/net/SocketAddress.hpp' file not found
```

Five, as predicted, and no more.

**And I will not dress this up: a compile-RED is weaker evidence than a runtime one.** The
thread-identity case could not be *expressed* against contour's `connect()` — `getaddrinfo` was
inline at `posix/SocketsPosix.cpp:69` and `windows/SocketsWin32.cpp:70` with no seam to inject
through — so "watch it fail" can only mean "watch the compiler refuse it". What the RED proves is
that the symbol did not exist. What it does **not** prove is that the old code resolved on the loop
thread; that is read out of the two source lines above, which is inspection rather than measurement.

**The prediction that could have come out wrong did not.** The refused-connect guard over
`BackendMatrix` passed on first run on poll and epoll, because contour's `connect()` already read
`getsockopt(SO_ERROR)` (`SocketsPosix.cpp:107-118`). I said so before running it. It is a guard added
green, not a RED, and reporting it as a RED would have been false.

### The thread-identity case, verbatim

`src/core/net/Connector_test.cpp`:

```cpp
TEST_CASE("connect() never resolves a name on the loop's thread", "[net]")
{
    // **The case this task exists for.** An injected resolver records the thread that called it,
    // and the dial asserts that thread is not the loop's. Written against contour's inline
    // `getaddrinfo` it could not even be expressed: there was no seam to inject through.
    auto source = core::net::makeBackend(core::net::preferredBackendKind());
    REQUIRE(source != nullptr);
    auto loop = EventLoop { *source };

    auto bound = core::net::listen(loop, core::net::ListenOptions { .host = "127.0.0.1" });
    REQUIRE(bound.has_value());
    auto listener = std::move(*bound);

    // A NAME, so the fast path for literals does not apply and the lookup is genuinely offloaded.
    // The inner answers from the listener's port, so no name server is involved anywhere.
    auto inner = RecordingResolver { listener->boundPort() };
    auto resolver = core::net::ThreadedAddressResolver { inner };
    auto connector = core::net::makeConnector(loop, resolver);

    auto served = false;
    auto wrote = false;
    auto loopThread = std::thread::id {};
    loop.blockOn(core::net::testing::allOf(
        acceptAndRead(listener.get(), "hello", &served),
        dialAndWrite(connector.get(), "peer.test", 80, "hello", &wrote, &loopThread)));

    CHECK(wrote);
    CHECK(served);

    auto const callers = resolver.offloaded();
    INFO("the resolver offloaded " << callers << " lookup(s)");
    CHECK(callers == 1);

    REQUIRE(inner.callers().size() == 1);
    CHECK(loopThread == std::this_thread::get_id()); // the flow really did run on the loop
    CHECK(inner.callers().front() != loopThread);    // and the lookup really did not
}
```

Two assertions rather than one is deliberate: `CHECK(loopThread == std::this_thread::get_id())`
is what stops the case passing because the *flow* moved off the loop instead of the *lookup*.
No name server is reached — the recording inner answers from the listener's own port.

### GREEN, per test file

| File | Cases | Result |
|---|---|---|
| `SocketAddress_test.cpp` | 5 | pass |
| `ThreadedAddressResolver_test.cpp` | 5 | pass |
| `ConnectFlow_test.cpp` | 8 | pass |
| `ReadinessDial_test.cpp` | 5 | pass |
| `Connector_test.cpp` | 6 | pass |
| `posix/AdoptListener_test.cpp` | 2 | pass (POSIX legs) |
| `windows/AdoptListener_test.cpp` | 2 | pass (Windows legs) |

The `net` binary: **231 cases / 1614 assertions** on `clang-debug` (from 228/1587 at the start of
this task), **218 cases on `cl-debug`** (from 215).

---

## 6. Two arrangement problems I had to solve, and how they could still bite

**(a) The saturation helper was itself a hang, and it hung the whole binary.** To leave a dial
outstanding I fill a `backlog = 1` listener's queue. My first version dialled with
`SteadyTimePoint::max()` — so the *first* fill dial that did not complete never returned.
`core-cpp.net` went from 1.5s to a ctest Timeout, and the per-case output showed
`SIGTERM - Termination request signal` after 9 assertions. Every fill dial is now bounded at 300ms,
and the helper records `saturated` only when a fill dial came back **`Timeout`** — a refusal or an
unreachable route leaves it false and the two cases `SKIP` with a sentence saying what could not be
arranged. `SKIP`, never `SUCCEED`.

On this machine neither case skipped: Linux and Windows both leave the dial outstanding. **If
`a dial that cannot complete is ended by its deadline…` or `the flow's stop token cancels a dial in
flight` SKIPs on a CI runner, that is the arrangement failing and not the dial** — the message says so.

**(b) A comma in a `TEST_CASE` name is a filter separator.** `"…by its deadline, and its descriptor
goes with it"` matched nothing when run alone, because Catch2 splits the filter on commas. Renamed to
`…ended by its deadline and takes its descriptor with it`. It cost me one confused minute and would
cost the next person the same.

**The descriptor-leak check is a real check on POSIX and abstains on Windows.** POSIX hands out the
lowest free descriptor, so a dial that abandoned one makes the next number **grow**; the case takes
that number before and after. Windows does not allocate handles lowest-first, so `nextDescriptor()`
returns `nullopt` there and the case falls back to `parkedWaiterCount() == 0`, which is portable and
which catches the leak that actually matters (a park nothing will retire).

---

## 7. A write I removed, an attempt to prove it mattered, and what the attempt actually showed

`settleDial` originally did `std::exchange(op.park, ParkId {})`. That is a **write** to `op.park`
after the stop callback has been registered, and the stop callback **reads** `op.park` and may run
on any thread. `ResultAwaitable` is safe reading its park id unatomically for a stated reason: the
id is written **once**, on the loop's thread, strictly before the callback can be registered. My
clear was a second write. It is a data race by the memory model.

`settleDial` now unregisters without clearing, and the id is written once. It costs nothing: the
loop's park ids are never reused, so a `requestCancel` naming a retired park resolves to nothing.

**Then I tried to prove the pre-fix code was actually racy, and failed.** The lead's newest global
constraint — *an unreachability claim is checked by building the case, not by auditing the
argument* — cuts the other way here too: I had written "TSan did not catch it, and could not have",
which is a derivation. So I built the case instead.

1. **New case: `a stop from ANOTHER thread cancels a dial in flight`.** Nothing in this suite, or
   anywhere else in `net`, previously stopped a flow from a thread that is not the loop's — so the
   cross-thread cancel path, which is the entire reason `cancelThrough` and `requestCancel` exist,
   had **zero** coverage. It is green, and it is worth keeping on that ground alone.
2. **It does not provoke the race, and I now know why.** An ordinary cross-thread stop is ordered:
   the callback's read of `op.park` happens-before the settle it *causes*, because `requestCancel`
   goes through the loop's mutex-guarded inbound queue. The two accesses can only overlap when the
   dial settles for its OWN reason — a deadline, an arriving readiness — inside the few
   instructions a concurrent stop spends in the callback.
3. **So I built that too, as a throwaway**: the shipped case with the dial's deadline set to fire
   at the same instant the stopping thread fires, with the fix reverted, **30 runs under
   ThreadSanitizer**. Zero reports. (A first attempt at this patched the wrong one of two identical
   lines and measured nothing; the run that counts is the second.)

**What I am therefore claiming, and not claiming.** The write was unsynchronised and removing it is
correct. I could **not** demonstrate that it ever produced a race, and I am not reporting a closed
defect — this is hardening against something real on paper and not provoked in ~1µs-wide windows
over 30 attempts. A failed attempt to construct is weaker than a successful one and still stronger
than the argument I had written first. The code comment says exactly this, numbers included, so the
next reader does not re-derive it.

---

## 8. Gates

Every number below is from a run after the last source change (`abe699a`). Nothing in this table
was carried over from an earlier round.

| Gate | Result |
|---|---|
| `clang-format.py --check` (39 changed C++ files) | formatted, clang-format 22.1.8 |
| `python-style.py --all --check` | 17 files formatted and lint clean, ruff 0.16.8 |
| `clang-tidy` preset | **0 errors** (4 rounds; see below) |
| `mkdocs build --strict` | built in 0.47s |
| `ctest -L hygiene` | inside the full runs below; 18/18 |

| Leg | Tests | Failed | Skipped | Note |
|---|---|---|---|---|
| WSL `clang-debug` | 37 | 0 | **0** | net binary 231 cases / 1614 assertions, 0 Catch skips |
| WSL `gcc-release` | 37 | 0 | **6** | the six canaries; assertions compiled out in Release |
| WSL `clang-asan-ubsan` | 37 | 0 | **0** | |
| WSL `clang-tsan` | 37 | 0 | **0** | a resolver thread is a thread, and so is §7's stopping thread |
| Windows `cl-debug` | 38 | 0 | **0** | ninja **8/8** incremental; net binary 218 cases / 2364 assertions, 5 Catch skips, all pre-existing `BackendParity_test` POSIX-only cases |
| Windows `clangcl-release` | 38 | 0 | **6** | ninja **8/8** incremental; the full build earlier in this task was **558/558** |

Skip counts are counted as `^ *N/M Test +#K: .*\*+Skipped`, not `grep -c Skipped`, which
double-counts (ctest names a skipped test inline and again in the trailing list) — that grep says 12
for `gcc-release` where the answer is 6.

**On the `clangcl-release` step counts.** The full build in this task was **558/558**, and every
build after it changed only `.cpp` files — `ReadinessDial.cpp`, then the two `AdoptListener_test.cpp`,
then `ReadinessDial{,_test}.cpp` again — and never a header. So the incremental counts (7/7, 2/2,
8/8) are the real work rather than a stale-cache green; the hazard is a clang-cl tree rebuilt
incrementally after a HEADER edit, which did not happen here. The launcher is `fastcache-cc`, read
out of `build.ninja` rather than `CMakeCache.txt`.

### clang-tidy, four rounds

Three of the four findings were real:

1. `Sockets.cpp:34` — *coroutine parameters should not be references*. **Real, and the exact hazard
   the file already documents for `EventLoop*`.** The injectable `connect` overload took
   `IAsyncAddressResolver&`; it now takes a pointer.
2. `posix/DialPrimitives.cpp:80` — `#if defined(__APPLE__)` → `#ifdef`.
3. Three `find(...) != npos` in tests → `contains`; one unused `using`.

---

## 9. What I leave for B9 and B10, named

**For B9 (UDP and blocking transports):**

1. **`BlockingConnector.{hpp,cpp}` is yours, with `BlockingSocket`.** B9's brief rules it to B8; it
   cannot be built without `BlockingSocket`, which the same paragraph rules to B9. What you need from
   me is already here: `core::net::InlineAddressResolver` never suspends (so `syncRun` over it is
   sound), and `detail::runConnectFlow` accepts a **null loop** and a **null clock**. Do not import a
   second resolver.
2. **`IAdmissionControl` is landed** (`<core/net/IAdmissionControl.hpp>`, camelBack). B9's brief lists
   it in your import set; consume it, do not import it. **Nothing in core-cpp calls it yet** — it is
   an interface with a default implementation and no accept loop consulting it. Whoever wires it into
   an accept loop owns the case that a denied accept *closes* the socket rather than queueing it.
3. **`ReusePort` is deliberately absent.** `ListenOptions` is where it goes if a consumer needs one
   port bound from several loops. `removed` row in `renames.json`.
4. **`connectUnix` was left alone.** An AF_UNIX dial resolves no name, so it still parks on
   `loop->waitWritable(fd)` directly rather than going through `dialReadiness`. It is the last
   `connect`-shaped body outside the dial, and folding it in is a reasonable B9 tidy-up. Not a defect.

**For B7b (the IOCP socket), and B9 if B7b slips:**

5. **`IocpDial` is NOT delivered, and `Net/IocpDial.hpp` is not imported.** Windows dials through
   `windows/DialPrimitives.cpp` — a `WSAEventSelect` readiness dial, matching `WindowsSocket`. When
   an `IocpSocket` exists, the `ConnectEx` form becomes worth having, and the seam for it is
   `detail::DialStep`: a second `dialStep` in `Connector.cpp` selected where the loop's backend is
   `BackendKind::Iocp`. **The guarantee that must survive that change is R101**: the completion is
   the single writer of the outcome there, so the deadline must *cancel the operation* and let the
   completion report, rather than settling the op itself — which is the opposite of what the
   readiness dial does, and the reason upstream's two dials are separate files.
6. **`adoptDialled` on Windows closes the dial's own `WSAEVENT` before constructing the socket**,
   because `WSAEventSelect` allows exactly one event object per socket and `WindowsSocket` makes its
   own. An IOCP socket has no such constraint; do not carry the close over without re-deciding it.

**For anyone writing a cross-thread cancellation case:**

7. **`a stop from ANOTHER thread cancels a dial in flight` (`ReadinessDial_test.cpp`) is the
    suite's only case that stops a flow from a thread that is not the loop's.** Before it, the
    whole `cancelThrough` / `requestCancel` route — which exists precisely for a watchdog, a signal
    handler or a peer's thread — was exercised nowhere in `net`. B10's `withTimeout` and the TLS
    work both sit on that route. Copy the shape rather than re-deriving it, and note what §7 says
    about what it does and does not prove.

**For B10 (helpers on the awaitable ISocket):**

8. **`armSocketDeadline` is landed and has no caller.** It is the one place that decides
   *a non-positive ceiling arms nothing*, and `SocketDeadlineTarget::expired` is what lets a caller
   tell *ran out of budget* from *the peer went away* — two opposite diagnoses that both arrive as a
   broken socket. `withTimeout` and `HttpServer` are where it should be consulted. **It has no test**,
   because nothing exercises it; a B10 case that arms one and asserts `expired` is the coverage it
   is owed.
9. **`AcceptResult` is now an alias of `SocketResult`.** A helper that consumes one consumes the
   other; if you write an accept-side helper, take `SocketResult`.

**For whoever owns the process lifetime:**

10. **`defaultAsyncResolver()` is a function-local static and joins at exit.** Its header says *stop it
   before the loops it hands results back to*, and nothing enforces that. A dial in flight at process
   exit, whose loop is already destroyed, would have its hand-back submitted to a destroyed loop. No
   consumer does this today (a loop outlives the dials on it), and I did not add a guard, because the
   guard would have to know a loop is gone and there is no way to ask.

---

## 10. Which cases could not run here, and what covers them

I cannot run macOS or FreeBSD. Three properties are asserted only by CI:

1. **The R101 refusal on kqueue.** `a refused connect completes with the refusal on every backend`
   runs over `BackendMatrix`, which includes `Kqueue`; on this machine `makeBackend(Kqueue)` answers
   null and the section is skipped silently by the `if (!source) continue;` the matrix's own comment
   prescribes. **The hypothesis is in the commit message**: on macOS a failed connect arrives as a
   *writable* event with `EV_EOF`, and the dial reads `SO_ERROR` rather than the callback, so the
   case should pass. If it fails, the dial is wrong and the case is the one that says so.
2. **`appleclang-debug` has never run in this project's history** (B7c's report, §5.4). This is the
   first task whose central file is exercised there. A red on that leg is a finding about kqueue, not
   a regression from this commit.
3. **The backlog-saturation arrangement on macOS/FreeBSD.** If the two outstanding-dial cases `SKIP`
   there, the stack completes a dial its listener never accepts; the message says exactly that.

CI run: not yet dispatched at the time of writing — this worktree is not pushed. The lead merges.

---

## 11. Consumer impact

- **contour** — `connect(loop, host, port)` compiles and behaves the same, and no longer resolves on
  the loop thread. `IListener::localPort()` → `boundPort()`: mechanical, `renames.json` row +
  `tools/migrate/rewrite.py`. Anything that assumed resolution happened inline must be re-read.
- **endo, tuidu** — `localPort` → `boundPort` only; neither dials.
- **fastcached** — the whole `Net/{IConnector,ConnectFlow,SocketAddress,IAsyncAddressResolver,
  ThreadedAddressResolver,KeepAlive,SocketDeadline,IAdmissionControl}` surface has rows;
  `EpollConnector`/`KqueueConnector`/`IocpConnector`/`PlatformConnector` become
  `makeConnector(loop, resolver)`, with `removed` rows pinning the three classes gone.
- **Lightweight `dbtool`, morph** — neither dials nor listens. No change.

---

## 12. One change outside my files, and why it was not optional

`tools/migrate/rewrite_test.py::test_a_pending_row_is_applied_and_said_out_loud` borrowed whichever
row happened to be `"status": "pending"` in the live `renames.json` — which was
`localPort` → `boundPort`, task B6's, the one B6 handed me to deliver. Delivering it left the table
with **zero** pending rows and the case asserting a warning that no longer had a row to warn about.

I did not report-and-leave it, because it was red and it was my change that made it red. The case now
builds its own one-row table and passes it with `--table`, so the behaviour under test is the tool's
rather than the live table's, and the next task to deliver the last pending row will not break it
again. The comment says that in the file.

---

## 13. Concerns

1. **The RED for the central case is a compile failure, not a runtime one** (§5). Stated rather than
   dressed up. If a reviewer wants a runtime RED, the only honest way to get one is to keep the old
   inline `getaddrinfo` body under a test-only name and assert a loop stall against it — which I
   judged more invasive than the evidence was worth, and which I did not do.
2. **§7 is hardening, not a closed defect, and the report says so rather than taking the credit.**
   The cross-thread cancel case now exists and is green; the race it was meant to catch was not
   provoked in 30 TSan runs of a deliberately timed variant. If a reviewer wants it settled either
   way, the remaining move is a stress harness rather than a case — which is a different kind of
   test from everything in this suite, and I did not add one.
3. **`armSocketDeadline` and `IAdmissionControl` ship with no caller and no test.** Both are in the
   plan's B8 list and both are correct as far as inspection goes, which is not far. Named in §9.
4. **`posix/DialPrimitives.cpp` carries one `#ifdef __APPLE__`**, for the socket-option *name*
   (`TCP_KEEPALIVE` vs `TCP_KEEPIDLE`). It is a constant selection rather than logic, and
   `posix/FdUtils.hpp` has the same shape for `SOCK_NONBLOCK`. If a reviewer reads
   `.agent/rules/platform.md` more strictly than I did, the fix is a `bsd/` split for one `constexpr int`.
5. **The Windows dial takes the socket's one `WSAEventSelect` slot while it is outstanding.** B7a's
   report §6 records the same constraint for its write bridge. Two components now depend on a rule
   that nothing checks.
6. **`ThreadedAddressResolver` starts its pool lazily and joins in its destructor.** A lookup already
   inside `getaddrinfo` cannot be interrupted, so `stop()` waits for it — a process shutting down
   while a DNS server is black-holing waits for the platform's resolver timeout. That is upstream's
   behaviour and there is no portable alternative, but it is a shutdown that can take seconds and
   nothing in the tree says so outside the header.

---

## 14. An infrastructure finding that is not about this task

The lead reported that the worktree registrations under `D:/core-cpp/.git/worktrees/` had been
removed, quoting `fatal: not a git repository: D:/core-cpp/.git/worktrees/core-cpp-wt-b8`, and
recreated `D:/core-cpp-wt-b8` at `770f2dc` with the old directory moved to `.orphan`.

**None of that had happened to this worktree.** Verified rather than trusted:
`rev-parse HEAD` gave my own commit, `git worktree list` listed it, the admin directory and its
`HEAD` were present, and `D:/core-cpp-wt-b8.orphan` did not exist.

**The error is real and benign, and I had hit it myself.** A Windows-created worktree's `.git` file
holds `gitdir: D:/core-cpp/.git/worktrees/core-cpp-wt-b8` — a Windows absolute path — and git
running *inside WSL* cannot resolve `D:/...`, so it prepends the cwd and reports the admin
directory missing:

```
$ wsl -e bash -lc 'cd /mnt/d/core-cpp-wt-b8 && git status'
fatal: not a git repository: /mnt/d/core-cpp-wt-b8/D:/core-cpp/.git/worktrees/core-cpp-wt-b8
$ cd /d/core-cpp-wt-b8 && git status      # Windows git, same directory, same moment
(clean)
```

I met it early, when `scripts/clang-format.py` needed a file list from `git status` under WSL, and
worked around it by producing the list with Windows git. It is why every git command in this task
ran on the Windows side and every build on the WSL side.

**Why it is worth recording here.** Five other worktrees have the same Windows `gitdir` line, so any
lane that runs git under WSL produces the identical "fatal". Read as a lost registration and
answered by moving the directory aside, it would destroy a lane's **uncommitted** work — and unlike
a lane that has committed, there would be nothing in the object store to recover from.

## Fix round 1 — 40aa8aa..8dc3361 (appended by the lead; the lane's own write was refused by the harness)

All four ruled signature items, M2, M4, M5 and L1-L6 addressed. Every gate at build exit 0: clang-tidy
564/564 with 0 diagnostics; clang-debug, gcc-release (7 Release-canary skips), clang-asan-ubsan and
clang-tsan at 38/38, with TSan clean across 30 extra runs of the six threaded cases; cl-debug and
clangcl-release --clean-first at 562/562 ninja steps and 38/38; hygiene 18/18; mkdocs --strict.

**H1, resolution inside the budget and the stop.** SlotPark registers its stop callback before
publishing the waiter; whichever of the worker or the stop takes the waiter first decides, and neither
resumes inline. runConnectFlow races the lookup against the deadline with whenAny. With a resolver that
never returns, the dial is not done at 999 ms and ends Timeout at 1000 ms; a stop from the loop thread
and from another thread both end a parked lookup.

**Each fix proved by reverting it** (defect put back, case named for it goes RED): no clock in the
connector; the free connect() on an inline resolver (offloaded 0 == 1); settle resuming inline on the
worker (gcc-release: resumption thread not the loop's); closing before expired is set; a zero ceiling
arming a timer; check-then-act admission (327 of 20000 rounds admitted both); a resolver ignoring stop.

**M4.2 lesson.** Recording the thread after connect() still passed with an inline resume on the worker,
because the dial parks on the loop again and the next resumption returns it to the loop thread. The case
now records the thread at the lookup's own resumption, through a decorator, and goes RED in Release.

**M2.** connect() throws on its own token; connectUnix returns Cancelled; both declarations say so.
CHANGELOG Breaking lists the std::string host, the throw, AddressError, and the admission change.
**L3.** Hand-off to B7b in task-B7b-handoff-from-B8.md, and the same two rules as a comment at the
dialStep seam in Connector.cpp.
