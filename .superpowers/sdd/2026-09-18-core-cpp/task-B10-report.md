# Task B10: the helpers on the awaitable ISocket

**Status: DONE.** One commit, **`974d8ef`**, on `99acaa6`. It is not pushed. Worktree:
`D:/core-cpp-wt-b1011`.

## 1. What the task turned out to be

B6 had already moved the helpers onto the awaitable signatures. `SplitSocket`, the two test doubles
and `withTimeout` compiled against them, and the plan's line "Expect FAIL after B6's signature
change until they are adapted" no longer described the tree. So this task was about what the
contract **requires** of the helpers, not about signatures. Each item below has a case that pins
it.

| Item | What changed | Evidence |
|---|---|---|
| `SplitSocket::close()` | It now checks a liveness token between its two retirements. **This was a defect**, below | RED, a SIGSEGV |
| `serve()` | Before it reads a request, it awaits each connection's `handshakeIfNeeded()`. It drops a connection whose handshake fails, reading nothing and writing nothing | RED |
| `WriteQueue` | No code change. A case confirms there is never more than one write in flight and that each frame is exactly one `write` | GREEN on arrival; a mutation fails it |
| `withTimeout` | No code change. Three cases run it over a **real** socket read | GREEN on arrival |
| `AsyncBufferedReader` | The refill goes through a member buffer instead of a 4 KiB array in the coroutine frame, and it asserts `contract::requireReadBuffer`. Its scripted double asserts it too | The existing suite, plus the half-line timeout case |

### The defect: `SplitSocket::close()`

The old code was `_readHalf->close(); _writeHalf->close();`. Closing the read half completes a
parked read. That resumes a coroutine which may own the `SplitSocket` and destroy it, both halves
included, before the first `close()` returns. The second call then goes through a freed
`unique_ptr`. The case is `SocketDecorator_test.cpp`, "A decorator's close() touches nothing once a
completion has run".

**Prediction:** a crash in plain Debug. glibc's tcache overwrites the first 16 bytes of the freed
chunk (the vptr and `_readHalf`), which leaves `_writeHalf` null.

**RED, verbatim (clang-debug):**
```
A decorator's close() touches nothing once a completion has run
  backend=poll
/mnt/d/core-cpp-wt-b1011/src/core/net/SocketDecorator_test.cpp:192: FAILED:
  {Unknown expression after the reported line}
due to a fatal error condition:
  SIGSEGV - Segmentation violation signal
test cases: 1 | 1 failed
```

Changing the order of the two calls cannot fix this, because whichever half closes first can
destroy the object. The fix is a `shared_ptr<void const>` token, checked through a `weak_ptr` after
the first retirement. If the object is gone, its destructor has already destroyed the other half,
and a destroyed socket ABANDONS its parked operation, so nothing is left unretired. The rule is
recorded in `.agent/rules/async-and-net.md` under "Socket and coroutine lifetime".

### `serve()` and the handshake

The case is `HttpServer_test.cpp`, "serve completes the transport handshake before it reads a
request". It uses a socket whose `handshakeIfNeeded()` fails, behind a one-shot listener.
**Predicted:** 1 case, 1 assertion (the reader reads once, gets EOF, and answers nothing).

**RED, verbatim:**
```
/mnt/d/core-cpp-wt-b1011/src/core/net/HttpServer_test.cpp:739: FAILED:
  CHECK( reads == 0 )
with expansion:
  1 == 0
test cases: 1 | 1 failed
assertions: 3 | 2 passed | 1 failed
```

### core-cpp#35: a decision, stated

I followed the brief's ruling: **importing `LingeringClose` is out of scope.** `HttpServer` still
closes every connection **through its destructor**, with no explicit `close()` or `shutdownWrite()`.
The adaptation adds no close of either kind, so the reader question the brief raised does not
come up. The latent defect is now written on `serve()` itself, in `HttpServer.hpp`: a 413 written
over an unread body can be destroyed by the reset the close sends. The CHANGELOG entry says the
same and links #35.

### `WriteQueue`: one writer, confirmed

The case is "WriteQueue never has two writes in flight, and writes one frame per write". It uses a
`PermitSocket` double that parks every write until the test grants a permit, and counts the writes
it holds at once. Frames are enqueued before the drain starts, while a write is parked, between two
writes of the same drain, and after the drain has ended. Both checks passed on arrival:
`maxInFlight == 1`, and each frame was exactly one write.

**The mutation that proves the case has teeth.** With the `draining` guard in `enqueue` removed
(`if (true)`), the prediction was that `maxInFlight` would exceed 1 and the frames would be out of
order. Verbatim:
```
CHECK( socket.maxInFlight() == 1 )    with expansion:  3 == 1
CHECK( socket.writes() == ... )       with expansion:  { "one", "four", "five", "six" } == ...
```
The first version of the case failed at a settle predicate rather than at the assertion. I loosened
the predicates so that the mutation now reports its real reason.

### `withTimeout` over a socket (`WithTimeout_test.cpp`, new)

Each case runs on every backend the platform builds:

- a read that loses the race gives back the read slot: `parkedWaiterCount() == 0`, and the next
  read on the same socket returns the late bytes;
- a read that wins the race leaves no timer behind;
- a `readLine` that times out after receiving half a line keeps exactly those bytes, and the next
  `readLine` completes the line intact.

All three were GREEN on arrival.

## 2. Gates

The commit was amended once, for a missing provenance row for `WithTimeout_test.cpp` that
`cmake-hygiene` caught. The amended commit changes only that one row in `provenance.md`. The WSL
legs that ran **before** the amend (clang-debug, gcc-release and asan-ubsan) each show that
hygiene failure and nothing else. `ctest -L hygiene` was then run again on the amended tree. The
tsan and tidy legs ran **after** the amend.

| Gate | Build exit | Result |
|---|---|---|
| `clang-format --all --check` (22.1.8) | – | 462 files clean |
| `clang-tidy` preset (tree deleted first; `build.ninja` names clang-tidy 2070 times) | 0, 563 steps | 38/38, 0 skipped, 0 diagnostics |
| WSL `clang-debug` | 0 | 37/38 before the amend (the hygiene row); `-L hygiene` on `974d8ef`: **18/18**. `core-cpp-net-test`: **1731 assertions in 240 cases** |
| WSL `gcc-release` | 0, 563 steps | 37/38 (the same row), 7 skipped (3 hostdriven and 4 socket-contract canaries, `NDEBUG`) |
| WSL `clang-asan-ubsan` | 0, 563 steps | 37/38 (the same row), 0 skipped |
| WSL `clang-tsan` | 0, 563 steps | **38/38**, 0 skipped |
| Windows `cl-debug --clean-first` | 0, **561 steps** | **38/38**, 0 skipped |
| Windows `clangcl-release --clean-first` | 0, **561 steps** | **38/38**, 6 skipped (3 hostdriven, 2 iocp, 1 dialog canary) |
| `mkdocs build --strict` | exit 0 | |

## 3. Concerns

1. The `SplitSocket` case is in `SocketDecorator_test.cpp`, which is registered POSIX-only because
   `WindowsSocket` has no `cancelRead`. The fix is platform-independent, but no Windows leg runs
   the case.
2. `sizeof(AsyncBufferedReader)` grows by 4 KiB. That is the price of removing the per-refill
   frame allocation, and it is recorded in the CHANGELOG under Changed.
3. `WithTimeout_test.cpp`'s `send(ISocket*, std::string_view)` takes a view as a coroutine
   parameter. It is always called with a literal and awaited in the same full-expression, which is
   the exception the guideline states itself ("where the coroutine outlives the call expression").
   It is not the owning type the rule prefers.
