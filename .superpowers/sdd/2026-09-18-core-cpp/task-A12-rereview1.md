# Task A12 — re-review of fix round 1 (scoped)

Scope: the four items of Ruling R60's fix round, as given. Commits `edb1340`, `f5ccf0e`, `50c3c2d`,
plus the **uncommitted** working-tree change to `cmake/CoreCppTargets.cmake`. Nothing else in
`core::net` was reviewed.

Verdicts: item 1 **ADDRESSED**, item 2 **ADDRESSED**, item 3 **PARTIALLY ADDRESSED** (the five named
sites are fixed correctly; my own sweep found a sixth), item 4 **ADDRESSED** (but not committed).

---

## Item 1 — whitespace between a field name and its colon: **ADDRESSED**

`src/core/net/HttpServer.cpp:156-158`

```cpp
auto const name = line.substr(0, colon);
if (name.empty() || name.find_first_of(Whitespace) != std::string_view::npos)
    return std::nullopt;
```

What I checked, and why it convinced me rather than the report:

- **Coverage of the spellings.** `Whitespace` is `" \t\r\n\f\v"` (`HttpServer.cpp:60`), and the test
  is `find_first_of` over the *whole* name, not a prefix/suffix trim, so SP, HTAB, a mixed run
  (`"Host \t : x"`) and an interior `"Ho st: x"` are all refused. An empty name (`": headerless"`)
  is refused by the first disjunct. The four spellings are pinned at
  `HttpServer_test.cpp:380-385` and I ran them (below).
- **Whitespace *after* the colon survives.** The trim was not removed, it was moved: `trim()` still
  applies to `line.substr(colon + 1)` at `HttpServer.cpp:162`, and the companion case
  `HttpServer_test.cpp:411` pins `"Host:  \texample \t"` → `"example"` plus an empty-valued
  `X-Empty`. Both passed.
- **Bounds.** `colon != npos` is established one branch earlier (`:145-147`), so `substr(0, colon)`
  and `substr(colon + 1)` are both in range. A header line that is *only* whitespace never reaches
  this code at all: `line.empty()` is consumed at `:130` (end-of-head) and a line whose first byte is
  SP or HTAB is refused as an obs-fold continuation at `:142`. I walked the remaining spellings by
  hand: `" "` → obs-fold reject; `"\t\t"` → obs-fold reject; `"\v"` → no colon → reject; `"\f:"` →
  name `"\f"` → whitespace → reject; `"\r\r\n"` → one CR stripped, `"\r"` remains, no colon →
  reject. `line.front()` is never called on an empty view.
- **No collateral.** `iequals(name, "Content-Length")` and `iequals(name, "Transfer-Encoding")` now
  see an untrimmed name, but a name that reaches them provably contains no whitespace, so behaviour
  is identical. A field name is a `token` (RFC 9112 §5.1), so no legal header loses.

Ran (WSL, `out/build/clang-debug`):

```
./src/core/net/core-cpp-net-test '[http]'
All tests passed (112 assertions in 24 test cases)
```

CHANGELOG: `CHANGELOG.md:724-730` under **Fixed**, accurate, and it states the surviving
value-padding behaviour explicitly. (The entries are on master via `78824d1`, as the brief said.)

---

## Item 2 — the Windows-listener guard fails inside a budget: **ADDRESSED**

`src/core/net/Socket_test.cpp:241-295`

**The budget is real.** `Budget = 10000ms` (`:255`), raced as
`core::net::testing::anyOf(run(...), budget(...))` (`:284-285`). `anyOf` is `whenAny` (`testing/CoroTestSupport.hpp:37-40`),
and `budget` is `co_await lp->delay(limit); *expired = true;`. `EventLoop::delay` schedules against
`EventLoop::clock()`, i.e. the monotonic `SteadyClock` — a monotonic bound, not counted sleeps
(`.agent/rules/testing.md`, "Bound it on a monotonic clock").

**The message names what it waited for and what it observed.** `INFO` at `:287-289`, Catch2-scoped
over the `REQUIRE_FALSE` and both `CHECK`s. Observed in a `-s` run:

```
waited 10000ms for 3 sequential accepts on this listener; it accepted 3 and the client connected 3
```

**The sentinel — reasoned from the cancellation path, not the comment.** `timedOut` is written at
exactly one place: the statement *after* `co_await lp->delay(limit)` returns normally. That await
returns normally only from `DelayAwaiter::await_resume` (`src/core/net/EventLoop.hpp:379-384`):

```cpp
void await_resume()
{
    _cancelReg.reset();
    if (_token.stop_requested())
        throw async::OperationCancelled {};
}
```

So the write is reachable only if the awaiting flow's stop token is **not** requested at resume time.
Now trace the losing path: when `run` completes first, `WhenAnyRunner::FinalAwaiter::await_suspend`
latches the winner and calls `race->childStop.request_stop()` (`src/core/async/WhenAny.hpp:114-119`)
*before* anything resumes the budget arm; `request_stop()` is what fires `DelayAwaiter`'s registered
callback → `EventLoop::requeueForCancellation` → the frame resumes into `await_resume`, which sees
`stop_requested()` and throws. The `*expired = true;` statement is therefore **unreachable on the
cancelled path**. It is not a "the work finished" flag, and it does not inherit the defect of the
first attempt.

I confirmed the implementer's diagnosis of *why* the first attempt was wrong, because it is the same
mechanism that would have re-broken this one: a cancelled `accept()` does **not** throw — it swallows
`OperationCancelled` and returns `std::unexpected(Cancelled)`
(`src/core/net/posix/AcceptLoop.cpp:52-60`), so any flag at the end of the accept arm is set on the
failing path too. The timer's flag has no such swallow between the cancellation and the write.

**And the converse direction still fails.** Defect present → `run` parks → the timer fires at 10 s,
sets `timedOut`, falls straight into its final suspension and claims the win → `run`'s parked accept
is cancelled and unwinds (`acceptSequentially` `co_return`s on `!conn.has_value()`,
`Socket_test.cpp:148-149`; `connectSequentially`'s `delay` throws) → `whenAny` resumes the parent only
once **every** child has finished (`WhenAny.hpp:120-121`, `:56-59`), so reading `timedOut` after
`blockOn` is not racy → `REQUIRE_FALSE(timedOut)` fails with the message. No hang either way.

Ran: the case alone, both backends, **0.147 s wall** — i.e. the budget cannot expire on a slow
machine short of a ~68× stall.

`core_cpp_add_test`'s `TIMEOUT` backstop is item 4.

---

## Item 3 — the five give-up sites: **PARTIALLY ADDRESSED**

### The five named sites are fixed, and fixed correctly

| Site | Now | Checked |
|---|---|---|
| `Socket_test.cpp:97-104` (`echoClient`) | takes the `IListener*`, `listener->close()` before `co_return` | sibling `echoServer` (`:76`) accepts once; the close resolves its parked `accept()` with `Cancelled` |
| `posix/UnixSocket_test.cpp:108-115` (`connectAndProbe`) | same | see the sixth site below — the *connect* branch is right, the *write* branch is not |
| `EventSourceParity_test.cpp:105-112` (`connectAndSend`) | same | sibling `acceptAndEcho` (`:85`) accepts once |
| `Tls_test.cpp:266-271` (two-reactor client) | `releaseServer(remote, acceptor)` | posted, see below |
| `Tls_test.cpp:345-350` (cancelled-handshake client) | `releaseServer(remote, acceptor)` | posted, see below |

The two cross-thread ones go through `Tls_test.cpp:62-65`:

```cpp
void releaseServer(core::net::EventLoop* serverLoop, core::net::IListener* listener)
{
    serverLoop->post([listener] { listener->close(); });
}
```

This satisfies `.agent/rules/async-and-net.md:148` ("an object an event loop owns is destroyed on
that loop's thread") and it actually works, which I checked rather than assumed:

- `EventLoop::post` appends under `_postMutex` and writes a byte to the self-pipe
  (`EventLoop.cpp:61-74`); the pipe is never null — the constructor throws if it cannot be created
  (`EventLoop.cpp:20-22`) — so the cross-thread wake cannot be silently lost.
- `pumpOnce` calls `runPostedCallbacks()` at the *top of every pump* (`EventLoop.cpp:298-300`), not
  only on pipe readiness, so a `post()` that lands before the server thread's `blockOn` even starts
  is still run.
- `listener->close()` then runs on the server loop's thread: `notifyHandleClosing` supplies the
  readiness epoll/kqueue cannot (`posix/PosixListener.cpp:44-50`), the parked accept resumes, and
  `acceptOne` returns `Cancelled` because `*closed` is now true (`AcceptLoop.cpp:24-25`). The server
  flow returns, `blockOn` returns, the thread exits, `join()` completes.

Suites green: `core-cpp-net-test` 603 assertions / 109 cases, `core-cpp-net_tls-test` 40/6
(clang-debug), and under asan/ubsan `core-cpp.net` 1.01 s, `core-cpp.net_tls` 0.55 s.

### My own sweep found a sixth — see finding **N1**

`posix/UnixSocket_test.cpp:121-122`. The sweep's own commit message
(`50c3c2d`) claims *"Every early return that remains is on a path where the sibling has already
accepted and the socket closing under it is what finishes it. Each now says so"* — that claim is
false at exactly one place, and the comment added there says the false thing.

### What I swept, so the next person does not repeat it

Every `whenAll`/`allOf`/`whenAny`/`anyOf`/`std::thread` composition in
`src/core/net/**/*_test.cpp` (11 files), asking at each early exit *what wakes the sibling*:

- `AsyncBufferedReader_test.cpp`, `WriteQueue_test.cpp`, `NetError_test.cpp`,
  `posix/FdPassing_test.cpp`, `windows/NetworkEvents_test.cpp` — no concurrent arms at all.
  `WriteQueue_test`'s spawned drains are joined through `testing::waitUntil`, which is bounded
  (`maxTicks = 1000`, `CoroTestSupport.hpp:45`).
- `EventLoop_test.cpp` — no `whenAll`; the two `std::thread` cases only `post()`, which wakes the
  parked flow by construction.
- `HttpServer_test.cpp:602,645` — `whenAny(serve(...), client(...))`. The client's early
  `co_return` on a failed connect is safe *because* `whenAny` cancels `serve`. `exchange()` is fully
  sequential.
- `EventSourceParity_test.cpp` — every parked arm is paired with an unconditional
  `delay`-then-`close`/`reset` arm, so nothing depends on a peer's success.
- `Socket_test.cpp` — `unixProbe` (`:404`) closes the listener on **both** a failed connect and a
  failed write, which is the correct shape for a *draining* server loop; `duplexBulk`'s client
  (`:658`) closes on a failed connect. `writeBulk`'s `co_return` at `:618` looked like a candidate:
  it strands the peer's `readBulk`. It resolves, because the *other* direction still completes, the
  arm's `whenAll` then returns, the socket is destroyed and the peer's read sees EOF. I traced all
  four combinations of which side's write fails; none deadlocks. Worth recording so the next sweep
  does not re-derive it — the commit message's "each now says so" does not cover these two.
- `Tls_test.cpp` — the `socketpair` cases have no listener; the two thread cases are the fixed ones.
- `posix/UnixSocket_test.cpp` — the sixth site.

---

## Item 4 — `core_cpp_add_test()`'s default `TIMEOUT`: **ADDRESSED** (uncommitted)

`cmake/CoreCppTargets.cmake:326-344` (working tree; **not** in the three commits — see N4).

```cmake
set(timeout 300)
if(DEFINED arg_TIMEOUT)
    set(timeout ${arg_TIMEOUT})
endif()
set_tests_properties(core-cpp.${name} PROPERTIES TIMEOUT ${timeout})
```

- **Unconditional.** The `set_tests_properties` call is outside the `if`, so the property lands on
  every registration. The earlier committed form (`if(DEFINED arg_TIMEOUT) set_tests_properties(...)`)
  was opt-in; this supersedes it.
- **The explicit value still wins.** Verified from the *generated* files rather than the source, in
  three trees already reconfigured with this change
  (`out/build/{clang-debug,cl-debug,clang-asan-ubsan}/src/core/*/CTestTestfile.cmake`):
  `core-cpp.async`, `async-fallback`, `base`, `log`, `net_types`, `platform`, `testing`, `tui`,
  `tui_output` all carry `TIMEOUT "300"`; `core-cpp.net` and `core-cpp.net_tls` carry `"120"`;
  `core-cpp.cli` carries `"60"`. All **12** tests registered through `core_cpp_add_test` are bounded,
  none is missing the property. The cli lane's raw `set_tests_properties(... TIMEOUT 60)`
  (`src/core/cli/CMakeLists.txt:15`) runs after the function and wins — tighter, so no conflict.
- **The justification matches reality.** I measured it rather than taking the number:

  | Binary | cl-debug (measured) | clang-asan-ubsan (measured) | comment claims |
  |---|---|---|---|
  | `core-cpp.tui` | **9.78 s** | **11.10 s** | 9.7 s / 11.1 s |
  | `core-cpp.net` | 0.87 s | 1.01 s | "every other one is under a second" |
  | everything else | ≤ 0.02 s | ≤ 0.6 s | — |

  `core-cpp.tui` is indeed the slowest binary this function registers, by an order of magnitude, and
  300 s is ~27× its slowest sanitizer run. On clang-debug the `tui` label totals 9.33 s, matching the
  claimed 9.4 s. The one overstatement is "every other one is under a second": `core-cpp.net` is
  1.01 s under asan. Immaterial to the number chosen.
- **Nothing in `tests/` relies on being unbounded.** Every check there is registered with a bare
  `add_test` (`tests/CMakeLists.txt:8,17,21,29,37,...`), so the default does not reach them and
  nothing changed for them; they keep ctest's 1500 s. I measured the two slow ones on clang-debug:
  `core-cpp.vendor-selftest` 95.3 s, `core-cpp.cmake-hygiene-selftest` 54.1 s — both comfortably
  inside 1500 s, neither affected. The comment's parenthetical is one test short (see N3).

CHANGELOG: `CHANGELOG.md:732-740`, and it already describes the 300 s project-wide default, the
two 120 s net overrides and the cli 60 — i.e. the changelog matches the *working tree*, not the
commits.

---

## New findings

### N1 — Important. A sixth site: `connectAndProbe` strands a *draining* accept loop

`src/core/net/posix/UnixSocket_test.cpp:121-122`

```cpp
if (auto const wrote = co_await sock->write(bytes); !wrote.has_value())
    co_return; // the server already accepted; its arm sees this socket close and finishes
```

The comment is wrong for this sibling. `echoOnce` at `:85-101` is not a one-shot accept — it is a
**draining loop**:

```cpp
while (!*served)
{
    auto accepted = co_await listener->accept();
    if (!accepted.has_value()) co_return;
    auto conn = std::move(*accepted);
    auto const got = co_await conn->read(buffer);
    if (!got.has_value() || *got == 0)
        continue;                      // <-- back to accept(), *served still false
    ...
}
```

If the client's `write()` fails, the client arm returns, `sock` is destroyed, the server's `read`
returns 0 — and the arm takes `continue`, not `co_return`, back into `co_await listener->accept()`
with `*served` still false. The listener is open, no further client exists, `whenAll` cancels
nobody, and `blockOn` never returns. **The case hangs instead of failing** — exactly the shape this
round was sent to close, at the second early exit of one of the five functions it edited.

That this is a miss and not a judgement call is settled inside the round's own diff: the analogous
pair in `Socket_test.cpp` — `unixProbe` (`:404`) against the draining `echoOnceDraining` (`:382`) —
*does* close the listener on the failed-write branch (`:416-420`). The same author wrote the correct
shape 300 lines away.

Fix: close the listener on this branch too, as `unixProbe` does, and drop the comment.

```cpp
if (auto const wrote = co_await sock->write(bytes); !wrote.has_value())
{
    listener->close(); // echoOnce() loops back into accept(): nothing else will wake it
    co_return;
}
```

Both of this helper's call sites (`:152`, `:216`) are affected.

### N2 — Minor. `unixEcho`'s client ignores its write result, and both arms then park in `read()`

`src/core/net/Socket_test.cpp:464`

```cpp
std::ignore = co_await (*socket)->write(std::as_bytes(std::span { request }));
auto buffer = std::array<std::byte, 32> {};
auto const n = co_await (*socket)->read(buffer);
```

If that write fails, nothing is sent, the client parks in `read()` with its socket still open, and
the sibling `echoServer` (`:76`) parks in `read()` on the accepted connection. Neither socket closes,
`whenAll` cancels nobody: hang, not failure. Not the shape swept for (there is no early exit — the
result is simply discarded), and the reachability is thin (a 9-byte write on a just-connected
AF_UNIX socket), but it is the same failure mode and it sits in a function this round edited.
Suggest `if (!(co_await ...)) { acceptor->close(); co_return; }`, matching `echoClient`.

### N3 — Minor. The 300 s comment names `tests/` as the only exception; one test in `src/` is also uncovered

`cmake/CoreCppTargets.cmake` (working tree), the parenthetical *"(tests/ registers its own checks
directly — vendor-selftest takes 87s — and this default does not reach them.)"*.
`core-cpp.async-link-smoke` is registered with a bare `add_test` in
`src/core/async/CMakeLists.txt:100`, so it is unbounded too. It runs in ~2 ms, so nothing is at risk
— but the sentence reads as "everything under `src/` is covered", and it is not. One clause.
(Measured `vendor-selftest` at 95.3 s on clang-debug, not 87 s; also immaterial.)

### N4 — Minor, but the team lead should know. Item 4's fix is uncommitted

`git status` shows `M cmake/CoreCppTargets.cmake`. The three commits under review contain only the
*optional* `TIMEOUT` keyword; the unconditional 300 s default — the actual substance of item 4 — is
in the working tree only. It is real (the three reconfigured build trees prove it landed), but it is
not on `master`, it is not covered by the CI runs the report cites (head `50c3c2d`), and it shares a
file with no other lane, so there is nothing blocking the commit.

Relatedly, `task-A12-report.md:275-278` still says the `TIMEOUT` is *"deliberately not a project-wide
default: that would change every other module's tests, which is not this task's to do."* The working
tree now does exactly that. The report needs that paragraph rewritten when the change is committed.

### N5 — Minor. Two comment claims overstate their margin

- `Socket_test.cpp:252-253`: *"The budget loses the race to the work by three orders of magnitude"*.
  Measured: the case runs 0.147 s for **both** backend sections, i.e. ~60–75 ms per section against a
  10 000 ms budget — ~2 orders, not 3. (`f5ccf0e`'s message says "four orders", quoting the whole
  binary's runtime against one section's budget, which compares the wrong two numbers.) The bound is
  still amply safe; only the sentence is wrong.
- `.agent/rules/testing.md:72-75` asks a timed-out wait to say "whether that state was still moving".
  The `INFO` gives what it waited for, how long, and the end state (accepted N, connected M) — three
  of four. The counts do let a reader distinguish a wedged listener from a slow machine in practice,
  so this is a note, not a defect.

---

## Pre-existing, not this round

- `core-cpp.cmake-hygiene` fails on clang-debug with two provenance violations, for
  `src/core/async/DetachedTask.hpp` and `src/core/async/ParkedWork.hpp` — the async lane's in-flight
  files, no row in `.agent/reference/provenance.md` yet. Not `core::net`, not this round.
- `core-cpp.tui` fails on clang-debug in the same tree (it passes on cl-debug and asan/ubsan). The
  tui lane is live in this checkout; not this round.
- `core-cpp.async-link-smoke` and every `tests/` check remain registered with a bare `add_test` and
  so unbounded. Structural, outside `core_cpp_add_test`'s remit (see N3 for the comment nit).

## What I ran

| Command | Result |
|---|---|
| `ctest -LE hygiene` in `out/build/cl-debug` (Windows) | 14/14 passed; `core-cpp.tui` **9.78 s**, `core-cpp.net` 0.87 s, rest ≤ 0.02 s |
| `ctest -R '^core-cpp\.(tui\|net\|net_tls)$'` in WSL `out/build/clang-asan-ubsan` | 3/3 passed; tui **11.10 s**, net 1.01 s, net_tls 0.55 s |
| `ctest` in WSL `out/build/clang-debug` | 18/20; failures `core-cpp.tui` and `core-cpp.cmake-hygiene`, both other lanes' (above). `vendor-selftest` 95.3 s, `cmake-hygiene-selftest` 54.1 s |
| `core-cpp-net-test '[http]'` | 112 assertions / 24 cases, passed |
| `core-cpp-net-test 'a listener keeps accepting across sequential connections' -s` | passed, 0.147 s, INFO printed as quoted above |
| `core-cpp-net-test '[net]'` / `core-cpp-net_tls-test` | 603/109 and 40/6, passed |
| `grep TIMEOUT` over the generated `CTestTestfile.cmake` of three trees | 12/12 `core_cpp_add_test` registrations bounded; 300 default, 120 net, 60 cli |

Not run: `cl-release-tls` (no OpenSSL on this machine, as the report notes), `clang-format --check`
and a full `ctest -L hygiene` sweep (other lanes hold uncommitted edits; the hygiene scanner did run
in clang-debug and flagged nothing in `cmake/CoreCppTargets.cmake` or `src/core/net/`).
