# Task A12 — re-review of fix round 2 (scoped)

Scope: the two commits of round 2, `dda4050` and `98937f7` (35 lines across
`cmake/CoreCppTargets.cmake`, `src/core/net/Socket_test.cpp`,
`src/core/net/posix/UnixSocket_test.cpp`). Nothing else in `core::net` was reviewed.

Verdicts: items 1, 2, 3, 4, 5 all **ADDRESSED**.

---

## Item 1 — the sixth give-up site: **ADDRESSED**

`src/core/net/posix/UnixSocket_test.cpp:129-133`

```cpp
if (auto const wrote = co_await sock->write(bytes); !wrote.has_value())
{
    listener->close();
    co_return;
}
```

Checked against `echoOnce` (`:85-101`), the sibling `connectAndProbe` runs against: it is a
`while (!*served)` loop whose `!got.has_value() || *got == 0` branch is `continue`, not
`co_return`, so it goes straight back into `accept()`. Before this fix, the client's plain
`co_return` on a failed write left that accept parked forever once the client's own socket
(destroyed on return) delivered the EOF that drove `echoOnce` back to `accept()` rather than out of
the loop. `listener->close()` on this branch is exactly the shape `unixProbe` already uses in
`Socket_test.cpp:405-423` against the identical `echoOnceDraining` pairing, and I traced why it
works: `UnixListener::close()` (`src/core/net/posix/UnixListener.cpp:119-135`) sets `_closed = true`
and wakes the parked wait before the fd is closed; `acceptOne` (`AcceptLoop.cpp:20-60`) then loops
back, sees `*closed`, and returns `Cancelled` rather than parking again — which is what lets
`echoOnce` see `!accepted.has_value()` and `co_return` with `*served` still false, ending the loop
instead of re-entering it.

The comment at `:123-128` states the corrected reasoning ("a sibling's shape ... decides whether an
early return is safe") in place of the old, wrong one, and the commit message for `dda4050` says
what it corrects rather than quietly fixing it: it quotes `50c3c2d`'s claim ("every early return
that remains ... each now says so") and says it "was wrong at this one site, and saying it is what
made the site look examined." That satisfies the round's instruction on this point.

Both call sites of `connectAndProbe` (`:163`, `:227`) go through the one function definition, so
both are covered by the one fix.

### The RED/GREEN claim — reasoned from code, not reproduced

I did not rebuild to reproduce this. The affected binary (`core-cpp-net-test`, POSIX-only via
`UnixSocket_test.cpp`) only builds under WSL/Linux presets, the tree is shared with other lanes
whose builds are currently red for unrelated reasons (`gcc-release` on `IExecutor.hpp`,
`cmake-hygiene` on async provenance rows), and per my own instructions I must not touch or rebuild
that shared tree. Reasoning it through instead:

- **RED (pre-fix, forced branch):** client's write fails → old code `co_return`s → `sock` (the
  client's socket) destructs, closing the fd → `echoOnce`'s parked `read()` on the accepted
  connection returns `0` → `continue`, not `co_return` → back into `accept()` with `*served` still
  false, listener never closed → nothing ever wakes that `accept()` → `whenAll` never resolves →
  `blockOn` never returns. Matches the report's "ran until killed" observation exactly.
- **GREEN (post-fix, same forced branch):** client's write fails → `listener->close()` runs first →
  `_closed = true` and the parked wait is woken *while the fd is still valid*
  (`UnixListener.cpp:130-135` closes the wait path before the syscall close, per the ordering
  comment there) → `acceptOne`'s next loop iteration sees `*closed` and returns `Cancelled` →
  `echoOnce` sees `!accepted.has_value()` and `co_return`s with `*served` still `false` → both arms
  of `whenAll` finish → `blockOn` returns → `REQUIRE(served)` fails immediately. Matches the
  report's 0.107 s figure in kind (a `REQUIRE` failing on the very next scheduler turn after both
  arms unwind, not a wait of any kind).

The mechanism is the same one already verified with a real run in `task-A12-rereview1.md` for the
analogous `unixProbe`/`echoOnceDraining` pairing (Item 3 there), and the code at this sixth site is
textually identical to that pairing after the fix. I'm confident in the claim on that basis; I did
not run it myself. Say so, as asked.

---

## Item 2 — `unixEcho`'s discarded write: **ADDRESSED** (claim verified independently)

`src/core/net/Socket_test.cpp:471-473` (no listener close needed) — verified by reading
`echoServer`'s actual shape rather than taking the report's word:

```cpp
// Socket_test.cpp:76-89
Task<void> echoServer(core::net::IListener* listener, bool* served)
{
    auto accepted = co_await listener->accept();
    if (!accepted.has_value())
        co_return;
    auto conn = std::move(*accepted);

    auto buffer = std::array<std::byte, 64> {};
    auto const got = co_await conn->read(buffer);
    if (!got.has_value() || *got == 0)
        co_return;                    // <-- co_return, not continue
    ...
}
```

`echoServer` is a **one-shot** accept, unlike `echoOnce`/`echoOnceDraining`: on `*got == 0` (the EOF
that the client's socket destructing produces) it `co_return`s rather than looping back into
`accept()`. `unixEcho`'s sibling composition is `whenAll(echoServer(listener, served),
client(loop, listener, path, matched))` (`:480`), so when the client's write fails and it plain
`co_return`s, the destroyed `sock` delivers EOF to `echoServer`'s parked `read()`, which ends the
server arm on its own. Both arms finish, `whenAll` resolves, no hang. The claim holds — this is
genuinely "the same rule, opposite answer" because the sibling's shape is opposite, not because the
reachability is thin (it also is thin, as the report says, but that's not why it's safe).

---

## Item 3 — the `tests/`-only claim in `CoreCppTargets.cmake`: **ADDRESSED**

`cmake/CoreCppTargets.cmake:19-23` (commit `98937f7`) now names `core-cpp.async-link-smoke`
alongside `tests/` as a bare `add_test()` the 300 s default does not reach. Confirmed
`src/core/async/CMakeLists.txt:124` registers it with a bare `add_test(NAME
core-cpp.async-link-smoke ...)`, no `TIMEOUT` — the claim is accurate, and the file itself is
untouched, matching the instruction not to edit the async lane's CMakeLists.txt.

FYI, not part of this round: a further commit `f90aacb` (already on `master`, authored after
`98937f7`) rewords this same comment again, to state the rule ("a bare `add_test()` elsewhere keeps
ctest's default unless it sets a `TIMEOUT`... adding one means deciding its bound with it") rather
than a fact with an expiry date ("is unbounded today"). That's a strict improvement over what item 3
asked for and doesn't change this verdict; flagging only so it isn't mistaken for drift.

---

## Item 4 — "three orders of magnitude" → measured figure: **ADDRESSED**

`src/core/net/Socket_test.cpp:250-253`: now reads "both sections together measure 0.147s against
this 10s budget — about 70x". `10000 / 0.147 ≈ 68`, so "about 70x" is the honestly-rounded measured
figure, consistent with the 0.147 s the previous re-review measured independently for this same
case. No remaining "orders of magnitude" language at this site.

---

## Item 5 — the report's superseded paragraph: **ADDRESSED**

`task-A12-report.md:278-281` now reads:

> **Superseded by `a13070c` (Ruling R64).** This paragraph originally said the bound was
> "deliberately not a project-wide default." It is one now... See the R64 addendum below.

placed directly under the paragraph it corrects, rather than replacing it silently. `a13070c`'s CI
(`Build 35538763899`, all 24 jobs) is confirmed green per the report and matches what I see on
`origin/master`'s history for that commit.

---

## New findings in these 35 lines

None — Critical, Important or Minor. The functional change (the added `listener->close()` block in
`UnixSocket_test.cpp`) is a straight copy of the already-reviewed `unixProbe` pattern, idempotent
against `UnixListener::close()`'s own `_closed` guard, and touches no state after the socket that
owns the write (`sock`) — no new lifetime issue. The other two files' changes are comments/prose
only.

## Note for the team lead, not a defect in the diff

CI coverage for this round's actual commits is not yet a clean, completed green as of this writing:

- `dda4050` never triggered its own CI run — it and `98937f7` were evidently pushed together, so
  GitHub's `on: push` fired once, against head `98937f7`. Nothing wrong with that; the `98937f7` run
  covers both commits' combined tree.
- That `98937f7` Build run (`35540278214`) was still `in_progress` as of this review, not yet
  concluded either way.
- A later, out-of-round commit `f90aacb` triggered its own Build run, which was itself `cancelled`
  (superseded by a still-later, unrelated tui-lane push) before completing.

So neither commit in this round has a completed CI conclusion in hand yet, only the local build/test
results the report lists (WSL clang-debug 21/22 with the one known other-lane failure, gcc-release
11/11, clang-asan-ubsan 3/3, Windows cl-debug 9/9, clangcl-release 2/2). Worth watching to green
before calling the round closed, the same way round 1's item 5 was.

## Pre-existing, not this round

Per the dispatch, not reported as findings: `core-cpp.cmake-hygiene` failing on ~10 async-lane
provenance rows, and `gcc-release` failing to build `src/core/async/IExecutor.hpp`.
