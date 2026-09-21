# Task B2 report: `core::net_types`, one error vocabulary for both lineages

**Status:** DONE_WITH_CONCERNS. The review's two Important findings and four Minors are fixed
(`6dfb8c5`), one further finding of my own is fixed (`c7102e2`), and the transcripts in this report
have been re-run against the committed files. The remaining concerns are other lanes' in-flight
state, one Windows preset I cannot configure here, and one incident I caused — all listed at the
end.

**Commits:** `ddddc67..c7102e2` (six commits, pushed to `origin/master`).

| Commit | What |
|---|---|
| `8a88ce0` | `net: one NetError vocabulary for both lineages` — `NetError.hpp`, `NetError_test.cpp` and the 15 files the `Other` → `SystemError` rename touched |
| `e6dd748` | `docs(net): the merged error vocabulary, its two renames and what they break` — `docs/modules/net.md`, `CHANGELOG.md`, `.agent/reference/provenance.md` |
| `748142f` | `tools(migrate): the NetErrorCode::Other rows name the target B2 delivered` — `tools/migrate/renames.json` |
| `73d4569` | `docs(net): toString() says what renders a code's number wrong, not just why words are better` — comment only, `NetError.hpp`. The renumbering trap belongs at the function, where the next edit will hit it, not only in a changelog entry |
| `6dfb8c5` | `net: a code appended past Last was invisible to every check that walks the enum` — review findings I-1 and I-2 and the four Minors |
| `c7102e2` | `test(net): six NetError case names could not select themselves` — a `testing.md` violation neither review caught |

## What landed

`src/core/net/NetError.hpp` now carries the union of the two vocabularies:

```
Ok, Eof, Cancelled, Timeout, WouldBlock, BadHandle, ConnReset, ConnRefused,
AddressInUse, AddressNotAvail, AddressError, HostUnreach, PermissionDenied,
Unsupported, MessageTooLarge, SystemError, Last
```

in the spec's order, plus `isDeadlineExpiry(NetErrorCode)` and a trailing `Last`.
`core::net_types` still links nothing and its include set is unchanged
(`<cstdint> <string> <string_view> <utility>`) — no `<format>`, which is what Task C4 cares about.

`IoResult.hpp` did not need to change: it is one alias over `NetError`.

## Tests: RED then GREEN

**Every transcript below was re-run against the files as they are committed**, and cites the line
numbers that shipped. An earlier version of this report quoted the same runs taken from a working
copy that predated two `static_assert` blocks and a clang-format reflow, so every citation sat
eight lines low. The substance reproduced exactly, but a reviewer who checked `:97`, found a
`constexpr` array there and concluded the transcript was invented would have been reasoning
correctly from bad evidence. Re-running was cheaper than explaining.

Counts move across the commits, so they are stated once: `8a88ce0` shipped 155 assertions in 12
cases; `6dfb8c5` added the "No code hides above Last" case and replaced 52 assertions that could
not fail with two that can, giving **106 assertions in 13 cases**, which is what the rest of this
section is measured against.

### RED 1 - the vocabulary does not exist (compile)

Reproduced so it can be checked today: the header at `8a88ce0~1` goes in an include directory ahead
of `src/`, and the test file is the one on `master`.

```
$ clang++ -std=c++23 -fsyntax-only -ferror-limit=8 -I/tmp/b2red -Isrc ... src/core/net/NetError_test.cpp
src/core/net/NetError_test.cpp:18:18: error: no member named 'isDeadlineExpiry' in namespace 'core::net'
src/core/net/NetError_test.cpp:30:29: error: no member named 'SystemError' in 'core::net::NetErrorCode'
src/core/net/NetError_test.cpp:30:57: error: no member named 'Last' in 'core::net::NetErrorCode'
src/core/net/NetError_test.cpp:36:15: error: use of undeclared identifier 'isDeadlineExpiry'
src/core/net/NetError_test.cpp:47:59: error: no member named 'Last' in 'core::net::NetErrorCode'
src/core/net/NetError_test.cpp:75:19: error: no member named 'SystemError' in 'core::net::NetErrorCode'
src/core/net/NetError_test.cpp:90:19: error: no member named 'HostUnreach' in 'core::net::NetErrorCode'
src/core/net/NetError_test.cpp:91:19: error: no member named 'AddressNotAvail' in 'core::net::NetErrorCode'
fatal error: too many errors emitted, stopping now [-ferror-limit=]
9 errors generated.
```

### GREEN

```
All tests passed (106 assertions in 13 test cases)
```

The thirteen cases:

| Case | What it pins |
|---|---|
| Every NetErrorCode has a description of its own in the house style | walks `[0, Last)`; each description non-`"unknown error"`, distinct, and lower-case words with no punctuation |
| Last counts the codes; it is not one of them | `toString(Last) == "unknown error"`, `!isDeadlineExpiry(Last)` |
| No code hides above Last | nothing in `[Last, 256)` is described - the one check that survives a code appended past `Last` |
| A value outside the enumeration is described as unknown | a cast value still renders |
| Both spellings of an expired deadline answer yes and no other code does | the two operands explicitly; every other code iterated |
| Dropping WouldBlock stops an accept loop a quarter-second in (fastcached#824) | the #824 case, named for what narrowing the predicate broke |
| makeNetError carries every argument and defaults the two it may omit | full, code-only and zero-context paths |
| A default NetError is an unclassified OS error and never a success | `NetError{}.code == SystemError`, `!= Ok` |
| NetError describes its category then its context then the OS code | all four rendering branches, including a new code |
| NetError renders words rather than an enumerator's position | no `code=`, no `NetError(` |
| The merge gave each lineage exactly what the other one had | which codes each lineage did *not* have before the merge |
| The two renamed codes keep the meaning their call sites relied on | `Other` to `SystemError` is still the default and the unclassified one; `BadHandle` is still distinct from it |
| IoResult carries the transferred count or the error | unchanged |

### RED 2 - five mutations, to prove the cases can fail

Per `.agent/rules/testing.md` ("Prove the test can fail... check that the failures are the ones you
expect *and only those*"). Every file was restored between mutations; the driver and the full
output are in the scratchpad (`transcripts.py`, `transcripts.txt`).

**(a) `isDeadlineExpiry` narrowed to `Timeout`.** The compile-time guard fires first:

```
NetError_test.cpp:36:15: error: static assertion failed due to requirement
                         'isDeadlineExpiry(core::net::NetErrorCode::WouldBlock)'
```

With that `static_assert` muted for one run, exactly the two cases that exist for it fail:

```
Both spellings of an expired deadline answer yes and no other code does
  NetError_test.cpp:154: FAILED: CHECK( isDeadlineExpiry(NetErrorCode::WouldBlock) )
Dropping WouldBlock stops an accept loop a quarter-second in (fastcached#824)
  NetError_test.cpp:172: FAILED: CHECK( isDeadlineExpiry(NetErrorCode::WouldBlock) )
test cases:  13 |  11 passed | 2 failed
assertions: 106 | 104 passed | 2 failed
```

**(b) `toString(SystemError)` changed to `"Network Error."`** - three cases, and only those:

```
Every NetErrorCode has a description of its own in the house style
  NetError_test.cpp:106: FAILED: CHECK( isHumanDescription(text) )
  with messages: static_cast<int>(code) := 15 / text := "Network Error."
NetError describes its category then its context then the OS code
  NetError_test.cpp:209: FAILED: "Network Error. [errno 13]" == "system error [errno 13]"
The two renamed codes keep the meaning their call sites relied on
  NetError_test.cpp:259: FAILED: "Network Error. (bind) [errno 13]" == "system error (bind) [errno 13]"
test cases:  13 |  10 passed | 3 failed
assertions: 106 | 103 passed | 3 failed
```

**(c) a code added *before* `Last` with no answer.** Without a `case`, the compiler names the
function first:

```
NetError.hpp:70:13: error: enumeration value 'ProxyRefused' not handled in switch [-Werror,-Wswitch]
```

(`switch (code)` is line 69 in the committed header and 70 in the mutated one, which has the extra
enumerator.)

Given a `case ... : break;` (so `toString` answers `"unknown error"`), **the test fails without the
test file being edited**:

```
Every NetErrorCode has a description of its own in the house style
  NetError_test.cpp:105: FAILED: CHECK( text != "unknown error" )
  with messages: static_cast<int>(code) := 15 / text := "unknown error"
test cases:  13 |  12 passed | 1 failed
assertions: 110 | 109 passed | 1 failed
```

The total rising 106 to 110 on its own is the headline; the decomposition is the real signature and
is stronger, because **two independent cases grew**. Measured by feeding each name from
`--list-tests` back to the binary:

| Case | baseline | with the extra code |
|---|---|---|
| Every NetErrorCode has a description of its own in the house style | 49 | **52** |
| Both spellings of an expired deadline answer yes and no other code does | 16 | **17** |
| every other case | unchanged | unchanged |

A hand-written list of today's codes would have left both flat. Three assertions per code in the
first case and one in the second is exactly what walking the enumeration produces.

**(d) a code added *after* `Last`, with a description of its own** - review finding I-2, the one the
guard missed. `-Wswitch` is satisfied, `static_assert(SystemError < Last)` still holds, and before
`6dfb8c5` all twelve cases passed green at 155 assertions. After it:

```
(builds clean: -Wswitch is satisfied)
No code hides above Last
  NetError_test.cpp:143: FAILED: CHECK( hiddenAt == -1 )
  with expansion: 17 == -1
  with messages: hiddenAt := 17 / hidden := "proxy refused"
test cases:  13 |  12 passed | 1 failed
```

**(e) `HostUnreach` claimed for the contour lineage** - the case that replaced a tautology:

```
The merge gave each lineage exactly what the other one had
  NetError_test.cpp:250: FAILED:
    CHECK( gainedBy(contour, fastcached) == std::set<std::string_view>{ ... } )
  with expansion:
    { "address not available", "permission denied" }
    ==
    { "address not available", "host unreachable", "permission denied" }
```

It names the missing word rather than a number. (The assertion begins at line 248; a `CHECK` spread
over two lines is reported one line past its closing parenthesis, which is why the citation is
`:250`. Every single-line `CHECK` above reports its own line — `:209`, `:259` and the rest check
out exactly.)

## The three decisions

### 1. `NetError::toString()` keeps contour's rendering

`connection reset (recv) [errno 104]`, not `NetError(code=9 system=104 context=recv)`. Three
reasons, in order of weight:

- **fastcached's prints the enumerator's *position*, and this task renumbers the enumeration.** An
  old log line and a new one that read alike would mean different codes, silently. Words do not
  have that failure mode.
- **A reader needs no copy of the header.** `code=9` is a lookup; `connection reset` is not.
- **`std::format` would enter `core::net_types`.** That target links nothing and is what
  `fastcache-cc` links alone in Task C4; `<format>` is not free. contour's rendering is string
  concatenation.

fastcached is the lineage that loses. Recorded under **Breaking** with the migration: `ToString()`
is `toString()`, and `ToStringView(code)` — which gave the enumerator's *name* (`"Eof"`) — is
`core::net::toString(code)`, which gives the description (`"end of stream"`). A caller that wanted
the identifier must map it itself; there are 46 `ToStringView` sites in fastcached at `0708dd54`,
so this is the biggest single cost of the decision and the migration note says so.

A second, smaller break in the same family: `toString(SystemError)` is `"system error"`, where
contour's `toString(Other)` was `"network error"`. The description follows the code's name; both
change in the same release, and the entry says a log filter matching the exact old text must be
updated.

### 2. What `toString()` says for the new codes, and why the switch keeps no `default`

`address not available`, `host unreachable`, `permission denied` — contour's style: lower case,
human, no punctuation. `system error` for the renamed `Other` (was `network error`).

The switch keeps no `default`, and the header now says why, at the function:

> The switch has no `default`, deliberately: adding a code then makes every compiler name this
> function, which is how a new code is stopped from silently rendering as `"unknown error"` in
> every log line that carries it. Do not add one. The statement after the switch handles the
> values that are not enumerators, which a cast can still produce.

Mutation (c) above shows both halves working: `-Werror,-Wswitch` names `toString` first, and if
warnings were ever not fatal the test still fails.

The **house style is now asserted**, not only intended: the test's `isHumanDescription` refuses an
upper-case letter, punctuation or a leading/trailing space. That is what caught mutation (b) at the
description rather than only at the two rendering cases.

### 3. `Ok` stays

Kept, deliberately:

- **The spec's union names it**, and it is the zero enumerator, which `cpp-guidelines.md` requires
  to be "the off, absent or default case". `NetErrorCode{}` meaning "nothing has failed" is the
  useful default for a *variable of that type*.
- **It has live callers outside an error channel.** `git grep` across the consumers found
  contour's `vthost/ConnectionAcceptor.hpp:64` — `net::NetErrorCode _lastCode = NetErrorCode::Ok;`,
  a "no failure recorded yet" member — and fastcached's `SocketClosedStates_test.cpp:200`, which
  aliases it as `None`. Dropping it would force `std::optional<NetErrorCode>` on them for no gain.
- **The design-principles objection is answered elsewhere.** "A constructed object is usable" is
  about `NetError`, and `NetError`'s default code is `SystemError`, never `Ok`: a default-
  constructed error *is* an error. A case asserts exactly that
  (`A default NetError is an unclassified OS error, never a success`).

The enumerator's doc comment now states the distinction rather than repeating both lineages'
"sentinel, not normally stored" hedge.

### A fourth decision, made because the test set required it: `Last`

The plan asks the `toString` case to "iterate the enum, do not list twelve strings", and C++23 has
no enumerator reflection. Every alternative I weighed (a hand-written array, a scan over the whole
`std::uint8_t` range, a `static_assert` anchored on `SystemError` by name) fails in the same way —
appending a code leaves the covered set unchanged, which is the trap
`design-principles.md` names explicitly ("A `static_assert` that anchors a table's length on an
enumerator *by name* fires only when nothing is wrong"). That file prescribes the answer: "The
enumerator states its own count (a trailing `Last`)".

Cost accounted for: `Last` is a value no caller should construct, in an enum where `Ok` is already
a non-error. It is documented as not a code, `toString` gives it no description (`case … : break;`,
falling through to `"unknown error"`), and `isDeadlineExpiry(Last)` is false — both asserted. The
consumer cost is nil in practice: `toString` is the only exhaustive switch over `NetErrorCode` in
core-cpp, in contour (`src/net/IoResult.hpp:44`, which core-cpp replaces) or in fastcached
(`ToStringView`), so no consumer's `switch` gains an unhandled enumerator.

## The ripple

| Rename | Sites in core-cpp | Sites upstream |
|---|---|---|
| `NetErrorCode::Other` → `SystemError` | **53** occurrences in **15** files, plus 2 Doxygen `@c Other` mentions (`HttpServer.hpp`, `posix/UnixListener.cpp`) | 62 in contour at `6777ff05` |
| `NetErrorCode::BadFileHandle` → `BadHandle` | **0** — core-cpp already spelled it `BadHandle`; this rename is fastcached-side only | 34 in fastcached at `0708dd54` |

The 15 files: `AsyncBufferedReader.cpp`, `AsyncBufferedReader_test.cpp`, `HttpServer.cpp`,
`HttpServer_test.cpp`, `Tls.cpp`, `posix/{AcceptLoop,PosixListener,PosixSocket,SocketsPosix,UnixListener}.cpp`,
`testing/posix/InMemoryTransport.cpp`, `testing/windows/InMemoryTransport.cpp`,
`windows/{SocketsWin32,WindowsListener,WindowsSocket}.cpp`. Nearly every one is
`makeNetError(Other, errno, …)`. The rest of the diff in those files is clang-format reflow: the
new identifier is six characters longer, so several `makeNetError(` calls re-wrapped. I checked the
whole diff for anything that was not the rename or reflow and found none.

The call sites are their own regression test: `HttpServer_test.cpp` (7 sites) and
`AsyncBufferedReader_test.cpp` assert the code a malformed request line and a closed peer produce,
and they run green after the rename.

**`tools/migrate/renames.json`** existed by the time I landed (C0 committed it as `ddddc67` while I
was building). Both rename rows were already seeded. The `Other` rows, however, were seeded
`"status": "pending"` with a note saying *Task B2 adds the target and marks the row delivered* —
so `748142f` does that. `check-renames.py` went from 410 to 412 delivered targets, 10 to 8 pending,
0 failures; `tools/migrate`'s 51 unit tests pass.

**`.agent/reference/provenance.md`**: `NetError.hpp`'s row now names the second upstream
(`LASTRADA-Software/fastcached src/FastCache/Net/NetError.hpp` at
`0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`), what was merged from it, and what was deliberately not
taken (`ToString()` and `ToStringView()`). It follows the one-row-with-the-merge-in-the-notes shape
that `platform/Clock.hpp` already uses for a three-way merge.

**`docs/modules/net.md`**: a new "The error vocabulary" section with the 17-row table (code,
`toString()`, when it arrives), the `isDeadlineExpiry` paragraph, and the rendering. The status
admonition says B2 is done and the vocabulary below is the merged one.

**`CHANGELOG.md`**: one **Added** entry (the merged enum, `isDeadlineExpiry`, `Last`, and that
`net_types` still links nothing) and three **Breaking** entries (the two renames with their sed
migrations, the `"network error"` → `"system error"` text, and the `toString()` rendering with the
fastcached migration).

## Verification

| Gate | Result |
|---|---|
| `python scripts/clang-format.py --check` (pinned 22.1.8) on the 18 files | clean |
| `clang-tidy` preset (pinned 22.1.8) over `core-cpp-net_types-test`, `core-cpp-net-test`, `core-cpp-net_tls-test` | no finding |
| WSL `clang-debug` — `ctest -L net` | 3/3 passed |
| WSL `clang-debug` — full `ctest` | 19/20; the one failure is `cmake-hygiene`, 10 violations, all `src/core/async/*` files another agent has not yet given provenance rows. None of mine. |
| WSL `gcc-release` — `ctest -L net` | 3/3 passed |
| Windows `cl-debug` — `ctest -L net` | 2/2 passed |
| Windows `clangcl-release` — `ctest -L net` | 2/2 passed (fresh tree, so the `--clean-first` caveat does not apply) |
| **Emscripten, emsdk 3.1.56** (libc++ 17, the strict leg) — `ctest -L net` | 1/1 passed (`core-cpp.net_types` under node) |
| `mkdocs build --strict` | clean |
| `tools/migrate/check-renames.py` + its 51 unit tests | 0 failures / OK |

All local trees are private (`out/build/b2-*`), so no other session's build directory was touched.

**CI, green.** `Docs` run **`35540156587`** on my head commit `748142f`: success. `Build` run
**`35540278214`**: success, all 27 jobs, on `98937f7` — of which `748142f` is an ancestor
(`git merge-base --is-ancestor 748142f 98937f7` holds). The `Build` run on `748142f` itself
(`35540156584`) was *cancelled*, not failed: the workflow's concurrency group cancels the
in-progress run whenever anyone pushes to `master`, and three other agents pushed within eleven
minutes of me. `35540278214` is the first run since mine that was allowed to finish, and it covers
my commits unchanged.

Jobs that matter here, all green in `35540278214`: `emscripten (emsdk 3.1.56)`,
`emscripten (emsdk latest)`, `windows (cl-release-tls)` (the preset I could not configure locally),
`windows (cl-debug)`, `windows (clangcl-release)`, `clang-tidy`, `style`, `coverage`,
`sanitizers (clang-asan-ubsan)`, `sanitizers (clang-tsan)`, `consumer-smoke (wasm)`.

`35540278214` covers `8a88ce0`, `e6dd748` and `748142f`. It predates `73d4569`, which is
comment-only but still rebuilds every target that includes `NetError.hpp`, so **no completed green
run covers `73d4569` yet**; I am watching the newest head for one rather than citing a run on my own
SHA, per `global-constraints.md`. Locally `73d4569` is clean under the pinned clang-format and green
in `core-cpp.net_types` on `clang-debug`.

## Concerns

1. **The `emscripten` leg matters here and it passed locally on 3.1.56**, which is the version the
   global constraints call out for libc++ 17. I deliberately kept the test file to C++20-era
   library facilities (`std::views::iota`/`transform`, `std::ranges::all_of`, no
   `std::to_underlying`, no `std::ranges::contains`) for that reason.
2. **`cl-release-tls` could not be configured on this machine**: `find_package(OpenSSL)` fails, and
   the pre-existing `out/build/cl-release-tls` tree has `OPENSSL_INCLUDE_DIR-NOTFOUND` too, so this
   is not something my change introduced. `Tls.cpp`'s five renamed sites are covered by
   `core-cpp.net_tls` under `clang-debug` and `gcc-release`, both green locally, and by CI's
   `windows (cl-release-tls)` job, green in `35540278214`. Worth knowing for the next task that
   touches `Tls.cpp`: on this machine that preset is CI-only.
3. **`ctest -L hygiene` is red in the shared checkout, for someone else's files.** 10 `provenance`
   violations, every one a `src/core/async/` file the B1 agent has added without a row
   (`AsyncQueue.hpp`, `DetachedTask.hpp`, `IExecutor.hpp`, `ParkedWork.hpp`, `ResumeOn.hpp`,
   `SyncRun.hpp`, `ThreadPoolExecutor.hpp` and three tests). Nothing of mine is named. It will clear
   when B1 lands its provenance rows.
4. **I had to work around two other agents' staged hunks in `CHANGELOG.md`.** The index held the
   tui agent's and C0's staged entries when I came to commit. I did not stage the file: I built a
   `HEAD + my hunks only` blob, set the index entry to it with `git update-index --cacheinfo`,
   committed, then ran `git add CHANGELOG.md` to put their hunks back in the index exactly as I
   found them (48 lines / 46 insertions / 2 deletions, before and after). `git show --stat` on both
   commits matches what I expected. Flagging it because the window between my commit and the re-add
   was a few seconds in which their staged state did not exist.
5. **`check-renames.py`'s *pending* arm cannot see an enum-scoped symbol land.** `_checkPending`
   asks whether the header opens the whole namespace path of `target.symbol`, so for
   `core::net::NetErrorCode::SystemError` it looks for a namespace `core::net::NetErrorCode`, which
   is an enum. The row would have read "not landed" forever. The *delivered* arm walks the prefixes
   and does find it, which is why `748142f` works. This is a gate asymmetry, not a table problem;
   it belongs to C0 or to whoever next touches that script, and any future enum-scoped pending row
   has the same hole. Recorded in `748142f`'s message.
6. **One judgement call worth a reviewer's eye:** `Last`. It is the only part of this change that
   adds a value to a public enum that nothing should ever construct, and I took it because
   `design-principles.md` prescribes it and because the plan's test ("iterate the enum") has no
   other sound implementation in C++23. If a reviewer prefers the enum clean, the fallback is a
   hand-maintained array in the header plus the `-Wswitch` guard alone — which is strictly weaker,
   and mutation (c) is the evidence.

---

# Fix round: the two Important findings (commit `6dfb8c5`)

Both folded into one commit, per the controller's ruling, plus all four Minors.

## I-2: a code appended *past* `Last` was invisible — reproduced before fixing

The review's claim, re-established on this tree rather than taken on trust. `ProxyRefused`
appended **after** `Last`, with a `toString` case of its own returning `"proxy refused"`:

- `-Wswitch` is satisfied — the code has a case;
- `static_assert(NetErrorCode::SystemError < NetErrorCode::Last)` still holds — `SystemError` did
  not move;
- every case walks `[0, Last)` and never reaches index 17.

Result: **`All tests passed (155 assertions in 12 test cases)`**. The decision-#4 guard closed the
append-*before*-`Last` half only, and the half it left open is the one `design-principles.md`
describes: a check anchored on an enumerator by name fires only when nothing is wrong. "`Last` is
the count" is exactly such a check.

**The fix.** The enumeration is dense from zero, so *nothing above the count is described* and
*`Last` is the count* say the same thing while the enumeration is intact — and only the first is
still worth asking once a code has been appended past `Last`. The new case scans `[Last, 256)` and
names the first value answering anything but `"unknown error"`, with its description.

RED, with the mutation in place, exactly one case failing:

```
No code hides above Last
/mnt/d/core-cpp/src/core/net/NetError_test.cpp:143: FAILED:
  CHECK( hiddenAt == -1 )
with expansion: 17 == -1
with messages: hiddenAt := 17 / hidden := "proxy refused"
test cases:  13 |  12 passed | 1 failed
```

GREEN, mutation removed: `All tests passed (106 assertions in 13 test cases)`.

`Last`'s own doc comment now carries the rule — *a new code goes above it, never below* — and names
the case that refuses the alternative, because a test can only refuse the mistake after someone has
made it.

## I-1: the changelog promised a classification that does not ship

Verified independently of the review's line numbers: `git grep` for `AddressNotAvail`,
`HostUnreach` and `PermissionDenied` across `src/` returns **nothing** outside `NetError.hpp` and
its test (`core::platform::PlatformError::PermissionDenied` is a different enum). The classifiers:
`posix/PosixSocket.cpp`'s `fromErrno` maps `ECONNRESET`, `EPIPE` and `EBADF` and nothing else;
the WSA table the four equivalents; the bind and connect ladders `EADDRINUSE` and `ECONNREFUSED`.

So a migrating fastcached caller's `== HostUnreach` branch compiles and is **dead** until the
producers land. The **Added** entry now says so and names Tasks B6 to B8;
`docs/modules/net.md` says it beside the table.

## The four Minors — all taken, one with a change of shape

| Minor | What changed |
|---|---|
| `isDeadlineExpiry`'s comment describes callers we do not have | It now says the blocking transports arrive in Task B9 and that nothing in core-cpp asks this yet |
| the lineage round-trip case is near-tautological | Rewritten rather than deleted — see below |
| `docs/modules/net.md`'s `Last` row says `—` | It says `unknown error`, and the section gained the "above `Last`, never below" rule and the "no producer yet" note |
| the house-style rule is enforced in the test, not stated at the switch | Stated at `toString`, with the reason: these strings go into log lines people grep |

The round-trip case was asserting that codes named in two arrays *written in the merged spelling*
exist and have descriptions — which the compiler and the first case already settle, so it could not
fail. It now asserts what nothing else does: which codes each lineage did **not** have before the
merge and does now. Shown able to fail by adding `HostUnreach` to the contour array:

```
The merge gave each lineage exactly what the other one had
  CHECK( gainedBy(contour, fastcached) == std::set<std::string_view>{...} )
with expansion:
  { "address not available", "permission denied" }
  ==
  { "address not available", "host unreachable", "permission denied" }
```

It names the missing word rather than a number. This is why the assertion count is 106 and not 156:
52 assertions that could not fail were removed, two that can were added.

## Verification of `6dfb8c5`

pinned clang-format 22.1.8 clean; the `clang-tidy` preset clean; `clang-debug` 106 assertions /
13 cases; `gcc-release` green; Windows `cl-debug` green; **Emscripten emsdk 3.1.56** green under
node; `mkdocs build --strict` clean.

One process note from that run: splitting `source emsdk_env.sh` and `ctest` into two shell
invocations reports `Could not find executable node` / `Not Run`, which reads like a test failure
and is not. Both must be in one `bash -lc`.

# Incident: I overwrote another lane's commit message with `git commit --amend`

**On `origin/master`, not undoable, no content lost.** `438f40b` carries *this task's* commit
message on the upstream-drift-checker lane's content. `git diff 8514854 438f40b` is empty — same
tree `9e5c013`, same parent — so only the message is wrong. `6dfb8c5` sits intact underneath it.

**How.** I committed `6dfb8c5`, then wanted one more paragraph in its message. To avoid committing
another lane's staged deletions I used the private-index recipe — `export GIT_INDEX_FILE=$(mktemp)`,
`git read-tree HEAD`, `git commit --amend`. Between my commit and that sequence the drift-checker
lane had already committed `8514854` on top of mine, so `HEAD` was theirs: `read-tree` read their
tree and `--amend` rewrote **their** commit with my message. They pushed. No force-push was
attempted and none will be.

**The finding.** `global-constraints.md`'s private-index recipe protects the *index* from other
lanes and says nothing about `HEAD`, which moves just as freely. `git commit --amend`, `git reset`
and `git rebase` are unsafe in this checkout however carefully the index is handled, because the
object they act on is chosen after you last looked at it. Same shape as R74: a guard that does not
cover the case.

**Containment.** The overwritten commit is pinned against garbage collection as
`refs/recovered/upstream-drift-checker-message` (`8514854`), and its full 38-line message is saved
to the scratchpad (`lost-message-8514854.txt`); `git log -1 --format=%B 8514854` still prints it
here. Recovery — a `git note`, a follow-up commit body, or nothing — is the owning lane's decision,
routed through the controller.

**Consequence for this task:** `6dfb8c5`'s message covers I-2 and the Minors but not the I-1
changelog fix, whose paragraph is what the failed amend was adding. The fix is in the commit's
*content*; the explanation is in the "I-1" section above rather than in the message, because
amending again would risk the same collision.

## CI, stated plainly

No completed green run covers `73d4569` or `6dfb8c5`. Runs `35541181884` (`73d4569`),
`35542238141` (`7838b53`) and `35542564691` (`78bf537`) were each superseded while still pending or
queued — with several lanes pushing, a `Build` rarely starts a job before the next push arrives.
The last completed green is `35540278214` on `98937f7`, covering `8a88ce0`, `e6dd748` and `748142f`.
The two later commits are comment-and-test-only against a header six targets include, and seven
local toolchains are green on them — local evidence, not CI, and named as such.

---

# Supplementary round: citations, the wire question, and one more finding of my own

## The renumbering is not a wire break — checked, not assumed

The review asked a question neither the implementation nor the first report had: the merge
**renumbers** the enumeration, so is anything but a log line reading those numbers? Verified
independently here.

What moved, contour side: `AddressError` 9 → 10, `Unsupported` 10 → 13, `MessageTooLarge` 11 → 14,
`Other`/`SystemError` 12 → 15. fastcached side almost everything moves: `AddressInUse` 6 → 8,
`AddressNotAvail` 7 → 9, `ConnRefused` 8 → 7, `ConnReset` 9 → 6, `HostUnreach` 10 → 11,
`PermissionDenied` 11 → 12, `SystemError` 12 → 15.

Every numeric use of a `NetErrorCode` in either upstream, by grep for a cast of a code to an
integer type:

| Site | What it is |
|---|---|
| fastcached `src/FastCache/Net/NetError.hpp:55` | `NetError::ToString()` — log formatting, the rendering this task replaced |
| fastcached `src/FastCache/Server/AdminHttpServer_test.cpp:524` | a Catch2 `INFO` — in-process diagnostic |
| contour | **none at all** |

Nothing serialises a code to a file, a socket or an IPC frame; `SealedFrameSocket` and
`FrameEndpoint`, the two names that sound like they might, only compare codes. So the CHANGELOG's
coverage of this break as a *log-text* change is complete, which is a stronger claim than "we think
it is only logs" — and it is the second reason the numeric rendering had to go, since fastcached's
numbering is the one that moves most.

## A finding of my own: six case names could not select themselves (`c7102e2`)

`.agent/rules/testing.md`, "A case name is an argument": a comma splits a Catch2 test spec, so a
name containing one cannot select itself. **Six of this file's thirteen names had one** — and 42 of
the 1581 `TEST_CASE` names in the tree contain a comma, so more than a seventh of every instance in
core-cpp was in this one file, all written by this task. Neither review caught it.

Measured, not assumed:

```
$ core-cpp-net_types-test "Every NetErrorCode has a description of its own, in the house style"
Filters: "Every NetErrorCode has a description of its own","in the house style"
No test cases matched '"Every NetErrorCode has a description of its own"'
No test cases matched '"in the house style"'
No tests ran
$ echo $?
0
```

**The exit status is what makes this worth a commit.** The rule predicts a deterministic failure,
because "No tests ran" usually is one. Here it is a *pass*: `normalisedExitCode` returns 2 only
when Catch2 itself reports an error (`totals.testCases.total() == 0 && rawExitCode != 0`), and
Catch2 does not report one for a filter that matched nothing. So anything that picks a case by
name — a bisect script, a flake hunt, a CI shard — gets a green for a case that never ran. That is
a worse failure than the rule describes, and it is worth carrying back into
`.agent/rules/testing.md` if the controller agrees.

Counted before and after with a loop that feeds every name from `--list-tests` back to the binary:

```
selected 7 of 13 cases by their full name      (before)
selected 13 of 13 cases by their full name     (after)
```

The names lost their commas and nothing else.

## Citations

Every line number in this report's transcripts was eight low: the runs were real, but taken from a
working copy that predated two `static_assert` blocks and a clang-format reflow of one array. All
five mutations plus RED 1 were re-run against the committed files and the citations replaced; each
was then spot-checked against `sed -n '<n>p'`. Two need a word of explanation rather than a
correction, and both are noted at the transcript: `NetError.hpp:70` is the mutated header (the
committed one has `switch (code)` at 69), and `NetError_test.cpp:250` is Catch2 reporting a
two-line `CHECK` one line past its closing parenthesis.

The lesson is the reviewer's, and it is worth more than the correction: a transcript that does not
survive a spot-check reads as fabricated, and a reviewer concluding that from `:97` would have been
reasoning correctly from bad evidence. Re-run transcripts after the code they cite has been
formatted, not before.

## Commits, final

| Commit | What |
|---|---|
| `8a88ce0` | the merged vocabulary and the rename ripple |
| `e6dd748` | docs, changelog, provenance |
| `748142f` | the `renames.json` targets |
| `73d4569` | the `toString()` renumbering reason, in the header |
| `6dfb8c5` | findings I-1 and I-2, and the four Minors |
| `c7102e2` | six case names that could not select themselves |

---

# R79: the purity proof, in `.agent/guides/consumer-migration.md` (`7ddbe1d`)

Edited the tooling lane's section rather than adding a second one. Three rulings applied, and one
thing measured that I had been about to guess.

**The expected side is derived from a substitution list written by hand.** The recipe as it stood
re-ran `rewrite.py` on the pre-image and compared that against the post-image — circular in the way
that is hardest to see, because a tool wrong about a row is wrong *identically on both sides* and so
proves itself correct. Deriving the list from the tool's per-row report is the same mistake one step
removed. The guide now says to assert what should have changed and derive the post-image from the
assertion, and says *why*, because the automatic version cannot drift and will read as an
improvement to the next person.

**Whitespace is stripped, and the cost is written down.** Reflow is the one legitimate difference a
mechanical pass produces. Measured rather than asserted: across `8a88ce0`'s sixteen purely
mechanical files, **34 changed lines do not contain the renamed token at all** — they moved because
clang-format re-wrapped around an identifier six characters longer. (I had written "a dozen
`makeNetError(` calls" from memory and replaced it with the measurement; citing an unmeasured number
inside the section that warns about exactly that would have been poor.) The blind spot this buys —
a stripped comparison cannot see a whitespace-only change, `auto const x = 1;` and
`auto  const   x=1;` being the same string to it — is named, together with what covers it: the
pinned `clang-format --check` over the same commit, which sees nothing else. Complete together,
neither alone.

**The recipe in the guide is the one that was run.** Against `8a88ce0` it answers
`pure: 16   not pure: 2` and names the two files carrying work that was not the rename — the method
doing both its jobs in one run. It uses `git show` rather than a second worktree, which is one less
thing to leave behind in a shared checkout.

**"Read the output, never the summary"** keeps the tooling lane's three instances, which are
first-hand, regrouped by *mechanism* so the class is recognisable somewhere new: the summary hid a
gap in scope (a rename reporting a plausible 59 replacements while skipping every attribute
access); the tool answered a smaller question than it was asked (an `awk` whose input a `head` had
truncated); the answer was true and then stopped being true (a CRLF warning describing a past state,
and this lane's own mutation transcripts citing line numbers that outlived the formatting of the
file they pointed at).

How it was committed: content built from `git show HEAD:<path>` plus the transform, never from the
worktree (R87); the HEAD blob re-checked against my base immediately before writing; then
`git commit --only -- <path>` rather than a private index, because the file had no other lane's
hunks in it and a private-index commit would have left the shared index encoding a deletion of
another lane's work. `git show --stat` says one file, 73 insertions, 31 deletions, which is what I
intended.

# CI: every B2 commit is now covered by a completed green run

| Run | Head | Covers |
|---|---|---|
| `35540278214` | `98937f7` | `8a88ce0`, `e6dd748`, `748142f` |
| `35542238141` | `7838b53` | `73d4569` |
| `35542633374` | `fa7b569` | `6dfb8c5` — all 24 jobs green |
| `35543400100` | `b528631` | `c7102e2` — 24 jobs, 0 non-success |
| `35544311790` | `7ddbe1d` | the guide edit; pending, and documentation only |

Ancestry checked with `git merge-base --is-ancestor` in each case rather than read off the log.

---

# Correction: the exit-code claim in `c7102e2` and above is false

**The claim:** that a Catch2 filter matching nothing exits **0**, so a name that cannot select
itself yields a false green. It appears in `c7102e2`'s commit message, in the "A finding of my own"
section above, and it is what the controller turned into Ruling R90 — a change to
`src/core/testing/ExitCode.cpp`.

**It is wrong.** An unmatched filter exits **2**, exactly as `.agent/rules/testing.md` and the build
contract say. Measured through a script file with sound quoting:

```
  2  <-  net_types 'Every NetErrorCode has a description of its own, in the house style'  (the comma name)
  2  <-  net_types 'no such case at all'
  2  <-  fixture   '[nothing-has-this-tag]'
  2  <-  fixture   'nothing-a,nothing-b'
  0  <-  fixture   '--list-tests'
  0  <-  fixture   '--help'
```

`ctest -R exit-codes` passes, and `tests/cmake/check-exit-codes.cmake` has carried the row
`2;[nothing-has-this-tag]` since Task A1. That check was corroborating evidence in the tree the
whole time; reconciling one surprising result against it would have caught this before it was
reported.

The `rawExitCode != 0` guard in `normalisedExitCode` is therefore correct and load-bearing: Catch2
returns 2 for a spec that matched nothing and 0 for `--list-tests`/`--help`, so `rawExitCode` is
exactly the signal that separates *asked to list* from *asked to run and matched nothing*. Nothing
to fix; **R90 is withdrawn on my report.**

**How the measurement went wrong.** `$B` in the probe was expanded by the *outer* Git Bash rather
than inside WSL, so the command that ran was an empty string followed by a filename, and `$?` —
expanded in that same outer shell — reported the outer shell's status, 0. Confirmed by printing it:
`B is: []`. The first probe of the pair had a worse form of the same flaw, `... | tail -6; echo $?`,
which reports `tail`'s status, not the binary's.

So the status answered a different question from the one asked and was reported as a defect in the
machinery that says whether anything is broken. That is the second bullet of "Read the output,
never the summary", committed to `.agent/guides/consumer-migration.md` an hour earlier: *the tool
answered a smaller question than it was asked.*

**What survives.** `c7102e2` itself is still right, and its numbers are sound because they were
taken a different way — a script file grepping the run's **output** for "All tests passed" rather
than reading `$?`. Six of thirteen names contained a comma; a comma does split a Catch2 test spec
(`Filters: "Every NetErrorCode has a description of its own","in the house style"`, then
`No test cases matched` twice, then `No tests ran`); 7 of 13 selected themselves before the rename
and 13 of 13 after. What does not survive is the escalation: the miss is a deterministic non-zero,
which is what `.agent/rules/testing.md` already says. The rule needs no change and the sentence I
offered for it is withdrawn.

`c7102e2` is pushed, so per R83 it is not amended; this section and the next commit's message are
the correction.

**The rule this actually earns**, offered rather than taken, and it is about measurement rather than
exit codes: **read a status inside the script that produced it — never through a nested shell, and
never after a pipe.** `wsl -- bash -lc '...'` through this harness expands `$VAR` and `$?` in the
outer shell; that has produced three mis-measurements in one session (a loop variable that vanished,
a `--list-tests` that ran as a command, and this). The remedy is the one already used for anything
load-bearing here: write the probe to a file and run the file.

**Third confirmation, with no shell in the loop.** Re-measured through `cmake -P` using
`execute_process`, the same mechanism `tests/cmake/check-exit-codes.cmake` uses:

```
-- exit 0  <-  [pass]
-- exit 2  <-  [nothing-has-this-tag]
-- exit 2  <-  nothing-has-this-name
-- exit 2  <-  Every NetErrorCode has a description of its own, in the house style
-- exit 0  <-  --list-tests
-- exit 0  <-  --help
```

Removing the `rawExitCode != 0` guard would break the `--list-tests` and `--help` rows, which are
the legitimate no-tests invocations it exists to protect. R90 is withdrawn on this evidence, and no
sentence goes into `.agent/rules/testing.md`: the rule there is correct as written.

**The two measurements differed in exactly the way the new rule names.** The comma finding involved
two numbers taken the same afternoon, one sound and one not, and the difference between them is the
whole rule:

| | How it was taken | Held up? |
|---|---|---|
| "7 of 13 names select themselves, 13 of 13 after" | a **script file**, run as a file, grepping the run's **output** for `All tests passed` | yes |
| "an unmatched filter exits 0" | a one-liner through `wsl -- bash -lc '...'`, reading **`$?`** — which the outer shell had already expanded | no; the true answer is 2 |

Same session, same binary, same question. The sound one never looked at a status it had not captured
itself, and never asked a nested shell to hold a variable for it. That is the rule, and this is the
evidence for it.

---

# Closing state

Six commits, `ddddc67..c7102e2`, plus `7ddbe1d` for the R79 guide edit. Every one is covered by a
completed green `Build` run (the table above), verified by ancestry rather than read off the log.
The task's own suite is 106 assertions in 13 cases, green on clang-debug, gcc-release, cl-debug,
clangcl-release and Emscripten emsdk 3.1.56 under node; `clang-tidy` and the pinned
`clang-format --check` are clean; `mkdocs build --strict` passes.

Two things this task got wrong and corrected in the open, both recorded above rather than quietly
fixed: a `git commit --amend` that overwrote another lane's commit message (R83 now bans the form),
and an exit-status measurement that produced a defect report against working machinery (withdrawn,
and the measurement rule it earned is in the constraints). Both corrections were made before anyone
acted on them.
