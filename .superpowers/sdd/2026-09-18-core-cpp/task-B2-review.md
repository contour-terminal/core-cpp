# Task B2 review: `core::net_types`, one error vocabulary for both lineages

**Reviewer:** review-B2 · **Range:** `ddddc67..73d4569` (`8a88ce0`, `e6dd748`, `748142f`,
`73d4569`) · **Date:** 2026-09-21

- **Spec compliance: PASS.** The enum, the predicate, the rename and the target's link set all
  match the plan's Task B2 checklist and the spec's rename map, exactly and without scope creep.
- **Task quality: PASS with two Important findings.** Both are in the documentation and the
  test's own guard, not in the shipped header. Neither blocks the next task.

**Verdict: Approved.**

> **Round-2 addendum (see §8).** After this review was first written, `73d4569` landed (comment
> only, assessed in §5) and **all six findings below were answered in the working tree,
> uncommitted**. I re-ran every mutation against that working tree: the `Last` hole is closed, the
> enumeration-walking property survives the restructure, and both files are clang-format clean.
> The findings below are kept as written, against the committed range; §8 records what each one
> now looks like and what remains to do (commit it).

---

## 1. Spec compliance

### The enum

`src/core/net/NetError.hpp:24-49` declares, in order:

```
Ok, Eof, Cancelled, Timeout, WouldBlock, BadHandle, ConnReset, ConnRefused,
AddressInUse, AddressNotAvail, AddressError, HostUnreach, PermissionDenied,
Unsupported, MessageTooLarge, SystemError
```

This is the plan's union (`docs/superpowers/plans/2026-09-18-core-cpp.md:801`) **verbatim, in the
same order**. I extracted the enumerator list from the header with a script rather than reading it,
and compared against the plan's line. Both renames from the spec's map
(`docs/superpowers/specs/2026-09-18-core-cpp-design.md:247,256`) are applied.

Cross-checked against both upstreams, read as blobs:

- contour `6777ff05:src/net/IoResult.hpp` — 13 codes, all present in the merged set
  (`Other` → `SystemError`).
- fastcached `0708dd54:src/FastCache/Net/NetError.hpp` — 13 codes, all present
  (`BadFileHandle` → `BadHandle`).

The union is complete in both directions and nothing extra was invented. `AddressError`,
`Unsupported` and `MessageTooLarge` (contour's, new to fastcached) and `AddressNotAvail`,
`HostUnreach`, `PermissionDenied` (fastcached's, new to contour) are all there.

### `isDeadlineExpiry`

`NetError.hpp:115-118` is `code == Timeout || code == WouldBlock` — **byte-identical in behaviour
to fastcached's `IsDeadlineExpiry` at `0708dd54:src/FastCache/Net/NetError.hpp:110`**. I diffed the
two by reading the upstream blob. The predicate is right; neither operand was dropped.

The reasoning travelled, and travelled *well*. The dispatch asked for the #824 argument rewritten
against core-cpp's callers, with the issue as a full URL and without fastcached's own call-site
inventory. `NetError.hpp:84-114` does exactly that: the two-platform fact, the "both operands are
load-bearing" paragraph with the quarter-second symptom, the "why the obvious edit is inviting"
paragraph, the legitimate narrow-test carve-out, and the "says nothing about whose deadline" note —
with `Server/AdminHttpServer.cpp`, `Consensus/RaftPeerServer.cpp` and
`apps/fastcache-compile-node/FrameEndpoint.cpp` correctly stripped out. The URL is full.

### `core::net_types` links nothing

Verified in a build, not read off the CMake:

- `cmake/CoreCppModules.cmake:193` — `core_cpp_module_target(NAME net_types MODULE net KIND
  INTERFACE PLATFORMS any)`, no `DEPS`.
- `cmake --graphviz` over my configured tree: `node18` (`core-cpp-net_types`) has **zero outgoing
  edges**. The only edges are inbound (`core-cpp-net -> core-cpp-net_types`, and the test).
- Include closure of `NetError.hpp` via `clang++ -H`: 154 headers, depth-1 is `<cstdint>`,
  `<string>`, `<utility>` (`<string_view>` arrives through `<string>` and is correctly declared
  anyway). **`<format>` is absent from the entire closure.** The claim in the report and in the
  `@file` comment is true.

### Test set

The plan names four; all four exist and are real:

| Plan item | Where | Real? |
|---|---|---|
| `toString` for every code | `NetError_test.cpp:98-110`, walks `[0, Last)` | yes, and it fails on a new code — proven below |
| `isDeadlineExpiry` | `:122-145` plus `static_assert` at `:36` | yes, both operands and every non-operand |
| `makeNetError` | `:147-162` | yes, all three argument shapes |
| fastcached round-trips | `:70-94`, `:186-205`, `:207-225` | yes, both lineages listed in merged spelling |

**Nothing is missing and nothing is out of scope**, with one addition: the trailing `Last`
enumerator, which the plan does not name. It is justified in the report, sanctioned by
`.agent/rules/design-principles.md:96-100` ("The enumerator states its own count (a trailing
`Last`)"), and recorded in the CHANGELOG and the docs. I accept it. See finding I-2 for its cost.

---

## 2. The rename: 53 sites, proven pure

This is the defect class the dispatch was most worried about — "a mechanical rename is where a
wrong-but-compiling substitution hides", and the tests cannot see it. I did not spot-check. I
proved it.

For each of the 16 non-`NetError` files in `8a88ce0`, I took the pre-image, applied *only* the
substitutions `NetErrorCode::Other` → `NetErrorCode::SystemError` and `@c Other` → `@c SystemError`,
stripped all whitespace, and compared against the post-image:

```
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/AsyncBufferedReader.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/AsyncBufferedReader_test.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/HttpServer.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/HttpServer.hpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/HttpServer_test.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/Tls.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/posix/AcceptLoop.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/posix/PosixListener.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/posix/PosixSocket.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/posix/SocketsPosix.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/posix/UnixListener.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/testing/posix/InMemoryTransport.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/testing/windows/InMemoryTransport.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/windows/SocketsWin32.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/windows/WindowsListener.cpp
IDENTICAL-MODULO-RENAME-AND-WHITESPACE  src/core/net/windows/WindowsSocket.cpp
```

**No site was swept into the catch-all that should have become one of the new codes, because no
site changed meaning at all.** The Windows paths (`SocketsWin32.cpp`, `WindowsListener.cpp`,
`WindowsSocket.cpp`, `testing/windows/InMemoryTransport.cpp`) are covered by the same proof, which
matters because I could not compile them here. The ternaries that reflowed
(`SocketsWin32.cpp:126-129`, `WindowsListener.cpp:171-174`, `SocketsPosix.cpp:115-118`,
`UnixListener.cpp:180-181`) kept their operands and their argument order.

Counts confirmed: 69 `+` lines carrying `SystemError`, minus 16 in `NetError.hpp`,
`NetError_test.cpp`, `renames.json` and `CHANGELOG.md` = **53 across 15 files**, as reported.
`git grep` finds **zero** remaining `NetErrorCode::Other` or `BadFileHandle` in `src/` outside two
deliberate mentions in the test's own comments.

Upstream counts in the report also check out, by `git grep` against the pinned blobs:
contour `6777ff05` has 62 `NetErrorCode::Other`; fastcached `0708dd54` has 34 qualified
`NetErrorCode::BadFileHandle` (44 including 10 in prose comments) and 46 `ToStringView`.
The `sed` migrations in the CHANGELOG cover every *code* site in both; the 10 fastcached prose
mentions are left stale, which is a comment, not a compile.

---

## 3. What I ran

Private tree only: `out/build/reviewB2-clang-debug` (clang 22, WSL, `-DCORE_CPP_WITH_TLS=OFF
-DCORE_CPP_WITH_TUI=OFF`). I edited nothing in the shared tree and ran no formatter over it. The
mutations below were applied to a **copy** of `NetError.hpp` in my scratchpad, compiled through an
overlay `-I` path, so the shared checkout never held a mutated header for a moment.

| What | Result |
|---|---|
| `core-cpp-net_types-test`, unmutated | `All tests passed (155 assertions in 12 test cases)` — GREEN reproduced |
| `cmake --graphviz`, net_types edges | zero outgoing |
| `clang++ -H` closure of `NetError.hpp` | no `<format>` |
| `python3 scripts/clang-format.py --check` | 7 unformatted files, **none of them B2's** (all B3's in-flight `IoBackend.hpp`, `*Backend.*`, `BackendMatrix.hpp`, `ScriptedBackend.hpp`) |
| `ctest -L hygiene` | 4 failures; `grep -i 'NetError\|net_types\|SystemError\|isDeadlineExpiry\|BadHandle'` over the full output returns **nothing**. All 35 `cmake-hygiene` violations name `src/core/async/` and B3's `src/core/net/*Backend*` files; `migrate-renames` fails on rows 422-424 (`IoBackend`, `Interest`, `makeDefaultBackend`, task B3); `exit-codes` fails only because my reduced configure has no fixture target |
| `git merge-base --is-ancestor 748142f 98937f7` | holds |
| `gh run view 35540278214` | `Build`, `conclusion: success`, `headSha: 98937f7` — the report's CI claim is accurate |

### Mutation testing (7 mutations, all run)

| # | Mutation | Outcome | Verdict |
|---|---|---|---|
| A | `isDeadlineExpiry` narrowed to `Timeout` | **compile error**: `static assertion failed due to requirement 'isDeadlineExpiry(WouldBlock)'` (`NetError_test.cpp:36`) | caught, at compile time — the #824 narrowing cannot land |
| B | `toString(SystemError)` → `"Network Error."` | 3 cases fail (`:106`, `:181`, `:218`), 12→9 passed | caught, and the *style* check at `:106` is what catches it first |
| C1 | new code before `Last`, no `case` | **compile error**: `enumeration value 'ProxyRefused' not handled in switch [-Werror,-Wswitch]` | caught — the no-`default` property is real |
| C2 | new code before `Last`, `case … : break;` | 1 case fails (`:105`), assertions 155→**159** | **caught without the test file being edited** — the dispatch's requirement is genuinely met |
| E | new code before `Last`, description duplicates `"connection refused"` | 2 assertions fail (`:107`, `:109`) | caught |
| D1 | new code **after** `Last`, no `case` | compile error (`-Wswitch`) | caught |
| D2 | new code **after** `Last`, with `return "proxy refused";` | **`All tests passed (155 assertions in 12 test cases)`** | **NOT caught** — see I-2 |

Every claim in the implementer's report about RED/GREEN reproduced exactly, including the assertion
count rising on its own from 155 to 159 under C2. The report is honest.

---

## 4. Findings

### Critical

None.

### Important

#### I-1 — The CHANGELOG promises a classification that does not ship yet

`CHANGELOG.md:246-250`:

> `core::net::NetErrorCode` is the merged vocabulary of both lineages, so a caller of contour's
> `net::NetErrorCode` **or of fastcached's `FastCache::NetErrorCode` has a code for every failure it
> used to distinguish**. From fastcached it gains `AddressNotAvail` …, `HostUnreach` and
> `PermissionDenied` … — three causes that were an unclassified `Other` here and **that no caller
> could match on**.

The three imported codes have **zero producers in core-cpp**. I grepped:

- `src/core/net/posix/PosixSocket.cpp:33-42` (`fromErrno`) maps `ECONNRESET`, `EPIPE`, `EBADF`;
  everything else is `SystemError`. No `EHOSTUNREACH`, no `EACCES`, no `EADDRNOTAVAIL`.
- `src/core/net/windows/WindowsSocket.cpp:12-22` (`fromWsa`) maps `WSAECONNRESET`,
  `WSAECONNREFUSED`, `WSAENOTSOCK`, `WSAEBADF`. Same gap.
- The bind and connect ladders (`posix/PosixListener.cpp:89`, `posix/SocketsPosix.cpp:115`,
  `windows/WindowsListener.cpp:171`, `windows/SocketsWin32.cpp:126`) test only `EADDRINUSE` and
  `ECONNREFUSED`.

Upstream fastcached *does* produce all three, at five sites I read:
`Net/EpollSocket.cpp:53-59`, `Net/KqueueSocket.cpp:53-59`, `Net/IocpSocket.cpp:54-60`,
`Net/BlockingSocket.cpp:75-81`, `Net/ConnectFlow.cpp:44,59`.

**The failure scenario:** a fastcached caller migrating in Phase C keeps its
`if (e.code == NetErrorCode::HostUnreach)` branch. It compiles — the code exists — and it is
silently dead, because core-cpp's socket layer answers `SystemError` for `EHOSTUNREACH`. That is a
behaviour regression the migration guide and the CHANGELOG both currently read as impossible.

This is **not a code defect in B2**: the plan scopes the errno classification to Task B6
(`posix/PosixSocket` rewritten from fastcached's `EpollSocket`/`KqueueSocket`), B7 (IOCP) and B8
(`ConnectFlow`). The change is correctly scoped. The CHANGELOG is what over-promises.

**Fix (one clause):** after "…that no caller could match on", add something like *"The vocabulary
lands now; core-cpp's own sockets still answer `SystemError` for `EHOSTUNREACH`, `EACCES` and
`EADDRNOTAVAIL` until Tasks B6 to B8 bring fastcached's errno classification with them."*
`docs/modules/net.md`'s new table deserves the same note, since it describes `HostUnreach` as "The
network reports the destination as unreachable" with no hint that nothing reports it.

*Found by reading and by `git grep` over both trees; not by running.*

#### I-2 — `Last` is unguarded against the one edit that breaks it, and line 30 is the anti-pattern the rulebook names

`NetError_test.cpp:29-30`:

```cpp
// Last states the count, so it must come after every code it counts.
static_assert(NetErrorCode::SystemError < NetErrorCode::Last);
```

This is precisely the shape `.agent/rules/design-principles.md:96-100` calls out:

> A `static_assert` that anchors a table's length on an enumerator *by name* fires only when
> nothing is wrong: append an enumerator and forget its row, and it still compiles…

**Verified by running (mutation D2):** append `ProxyRefused` *after* `Last`, give it a proper
`case … return "proxy refused";`, and:

- `static_assert(SystemError < Last)` still holds — it says nothing about what comes after `Last`;
- `-Wswitch` is satisfied, because the new code has a `case`;
- `CodeCount` is unchanged, so `allCodes()` never walks the new code;
- **all 12 cases pass, 155 assertions, green.**

The new code is then invisible to every property the suite advertises: its description is never
checked for the house style, never checked for distinctness, and `isDeadlineExpiry` is never asked
about it. The test file's own comment at `:44-46` — *"a code added tomorrow is covered without this
file being edited"* — is false for this edit. The report's decision #4 argues `Last` closes the
append hole; it closes it only for appends *before* `Last`, and moves it one enumerator along.

The report explicitly weighed and rejected "a scan over the whole `std::uint8_t` range" on the
grounds that "appending a code leaves the covered set unchanged". That is true of the *positive*
direction and false of the *negative* one, which is the half that closes this. I wrote the guard
and ran it:

```cpp
TEST_CASE("Nothing above Last is a code", "[net][types]")
{
    auto const beyond = std::views::iota(CodeCount, 256)
                        | std::views::transform([](int v) { return static_cast<NetErrorCode>(v); });
    CHECK(std::ranges::all_of(beyond, [](NetErrorCode c) { return core::net::toString(c) == "unknown error"; }));
}
```

- unmutated header: `All tests passed (156 assertions in 13 test cases)`;
- mutation D2: `Nothing above Last is a code … FAILED`, `13 | 12 passed | 1 failed`.

One assertion, six lines, no new includes (`<ranges>` and `<algorithm>` are already there), and it
uses only C++20-era library facilities so the Emscripten/libc++17 leg is unaffected. I recommend it
replace or accompany the `static_assert` at `:30`.

*Found and fixed by running; both directions proven.*

### Minor

#### M-1 — `isDeadlineExpiry`'s comment describes callers core-cpp does not have

`NetError.hpp:92-95`: "**The callers this exists for are** the accept loops of the blocking
transports, whose listener arms a poll timeout…"

`grep -rn "isDeadlineExpiry" src/` outside the header and its test returns **nothing**. The
blocking transports arrive in Task B9 (`BlockingSocket`, `BlockingConnector`, `TcpClient`). The
present tense reads as a statement about this tree, and a reader who greps for those loops finds
none and starts doubting the rest of the paragraph — which would be a shame, because the rest of
the paragraph is the most valuable text in the file.

Suggested: "The callers this exists for are the accept loops of the blocking transports (Task B9),
whose listener arms a poll timeout…". Two words.

I also ran the census command the upstream comment prescribes,
`grep -rnE '(==|!=) *(NetErrorCode::)?(WouldBlock|Timeout)\b' src/`, and it returns **zero** hits
outside `NetError.*`. So no existing core-cpp site was missed that should have been converted to
the predicate. That part is clean.

#### M-2 — "Every code either lineage had survives the merge" is near-tautological at runtime

`NetError_test.cpp:186-205` walks `ContourLineage` and `FastcachedLineage` asserting
`static_cast<int>(code) < CodeCount` and `toString(code) != "unknown error"`. Both arrays are
written in the *merged* spelling, so the real guarantee is that the two `constexpr` arrays compile
— dropping a code from the union is a compile error, which is the strong half. The runtime body
adds little beyond what the `[0, Last)` walk already covers. Not wrong, and I verified both arrays
are complete and correct against the two upstream blobs (contour 13, fastcached 13). Worth a
sentence in the case saying that the compile is the assertion, so a later reader does not "simplify"
the arrays away.

#### M-3 — `docs/modules/net.md`'s `Last` row says the rendering is `—`

The table row reads `| `Last` | — | Not a code: …`. `toString(Last)` is in fact `"unknown error"`,
which the header's own `@return` at `NetError.hpp:52-53` states and which
`NetError_test.cpp:114` asserts. A one-word correction. Every other row in the table matches the
header exactly — I checked all 16 programmatically, zero mismatches.

#### M-4 — The house style is enforced by the test but not stated at the switch

`isHumanDescription` (`NetError_test.cpp:61-66`) accepts only `[a-z ]`. A contributor adding a code
hits `-Wswitch` first, writes `"TLS handshake failed."`, and then gets a test failure whose rule
lives in a helper's doc comment in another file. The `CAPTURE` makes it diagnosable, so this is
mild — but one line at the switch in `NetError.hpp` ("descriptions are lower-case words, no
punctuation") would close the loop, and would also flag the constraint that no future description
may contain a digit or a hyphen (`"http/2 error"`, `"non-blocking"` would both fail today).

---

## 5. The three decisions

All three were made deliberately and recorded, which is what the dispatch asked. I agree with all
three.

1. **`toString()` keeps contour's rendering.** Correct, and `73d4569` puts the load-bearing reason
   where it belongs. The original header comment argued that words cost a reader no copy of the
   header and that a later code does not change what an older line meant — true, but not the reason
   somebody would stop before reaching for `std::format`. The rewrite (`NetError.hpp:128-137`) says
   the thing that leaves no trace when it bites: `code=` is an index into `NetErrorCode`, **and this
   merge renumbered four of contour's codes** (`AddressError` 9→10, `Unsupported` 10→13,
   `MessageTooLarge` 11→14, `Other`/`SystemError` 12→15), so a line from before and a line from
   after are identical character for character and mean different codes; a reader comparing two runs
   or a filter written against the old numbering is wrong with nothing to notice. `<format>` is
   correctly demoted to "the smaller reason". This is the right ordering, and the commit message
   states why it belongs at the function rather than only in a changelog entry. I verified the
   numbering claim by computing both enumerations from the header and contour's
   `6777ff05:src/net/IoResult.hpp`.

   I also checked whether the renumbering is a *wire* break as well as a log break: it is not.
   `git grep` over both upstreams finds no serialization of a `NetErrorCode` to a file, a socket or
   an IPC frame; the only numeric uses are log formatting (`ToString`) and in-process test atomics
   (`Net/CancelRead_test.cpp`). So the CHANGELOG's coverage of the break — log text only — is
   complete.

2. **The new codes' descriptions.** `address not available`, `host unreachable`,
   `permission denied`, `system error` — all in contour's lower-case, no-punctuation style, all
   matching the docs table. The no-`default` property is preserved, explained at the function
   (`NetError.hpp:55-58`), and **proven to work** by mutations C1 and D1. `case Last: break;` is the
   right way to keep `Last` from being an unhandled enumerator without giving it a description.

3. **`Ok` stays.** The evidence the report cites is real; I verified both call sites in the
   upstreams: `contour 6777ff05:src/vthost/ConnectionAcceptor.hpp:64`
   (`net::NetErrorCode _lastCode = net::NetErrorCode::Ok;`) and
   `fastcached 0708dd54:src/FastCache/Net/SocketClosedStates_test.cpp:200`
   (`constexpr auto None = FastCache::NetErrorCode::Ok;`). The design-principles objection is
   correctly answered by `NetError`'s default being `SystemError`, which
   `NetError_test.cpp:164-171` asserts.

4. **`Last`, the fourth decision.** The consumer-cost claim checks out: `toString` is the only
   exhaustive switch over `NetErrorCode` in core-cpp (`grep`), contour's only one is
   `6777ff05:src/net/IoResult.hpp:44` which core-cpp replaces, and **no other consumer references
   `NetErrorCode` at all** — I grepped endo, tuidu, morph and Lightweight and found nothing. So
   adding `Last` breaks no consumer switch. The cost is finding I-2, not the enumerator itself.

---

## 6. CHANGELOG and provenance

- Placement is correct: the `Added` entry at `:246` sits in `### Added` (12-323); the three
  `Breaking` entries at `:392-418` sit in `### Breaking` (324-538).
- The migrations are executable. `sed -i 's/NetErrorCode::Other/NetErrorCode::SystemError/g'`
  covers all 62 contour sites (every one is qualified). `sed -i
  's/NetErrorCode::BadFileHandle/NetErrorCode::BadHandle/g'` covers all 34 qualified fastcached
  code sites; the 10 unqualified occurrences upstream are all in prose comments, which the sed
  leaves stale but does not break.
- The `ToStringView` → `toString` migration note is the honest one: it correctly warns that
  `ToStringView(code)` gave the *identifier* (`"Eof"`) and `toString(code)` gives the *description*
  (`"end of stream"`), which is a semantic change and not a rename, and names the 46-site cost.
  Confirmed: 46 occurrences at `0708dd54`.
- `.agent/reference/provenance.md:137` takes the one-row-with-the-merge-in-the-notes shape, names
  the fastcached pin in full, and records what was *not* taken. Good.
- `tools/migrate/renames.json`: both `Other` rows now carry a `target` and lost their `pending`
  status; both `BadFileHandle` rows (`5074-5097`) were already delivered by C0.
- The implementer's concern #5 (`check-renames.py`'s pending arm cannot resolve an enum-scoped
  symbol, because it looks for a namespace `core::net::NetErrorCode`) is worth routing to C0. It
  is a gate asymmetry that will bite the next enum-scoped pending row, and the implementer recorded
  it rather than papering over it.

---

## 7. For the controller to rule on

1. ~~I-1 and I-2 as a follow-up commit, or deferred?~~ **Answered:** both, and all four Minors, are
   already fixed in the working tree (§8). What remains is a decision about *how they land* — see
   the one open item below.
2. **Whether `Last` should carry a `[[deprecated]]`-style discouragement or stay as documented.**
   My judgement, asked for explicitly: **documented is enough, now that the guard exists.** A
   caller genuinely can construct `NetError { .code = NetErrorCode::Last }` and compare against it;
   nothing in C++23 prevents an enumerator from being used. But the residual cost is bounded and I
   measured the bound: `toString` is the only exhaustive switch over `NetErrorCode` anywhere in
   core-cpp, contour's only one is `6777ff05:src/net/IoResult.hpp:44` which core-cpp replaces, and
   **endo, tuidu, morph and Lightweight reference `NetErrorCode` zero times**. So no consumer
   switch gains an unhandled enumerator, and a caller that constructs `Last` gets `"unknown error"`
   — wrong, but not silently wrong in a way that outlives the log line. `[[deprecated]]` would fire
   on the test's own legitimate uses (`toString(Last)`, `isDeadlineExpiry(Last)`, `CodeCount`) and
   buy nothing the comment does not. The part that documentation *could not* cover was the
   invariant that `Last` is actually last, and that is now a test rather than a comment.
3. **The one still open:** the six fixes are uncommitted in a checkout three other lanes are
   editing. `CHANGELOG.md` and `docs/modules/net.md` in particular already carry other agents'
   hunks (the implementer hit this once before, report concern #4). Whoever commits these needs the
   same staged-hunk discipline the dispatch prescribes, or another lane's work goes in with them.

---

## 8. Round 2: the prediction, and the state of the tree

### 8.1 The prediction — confirmed in substance, one citation stale

The implementer stated a falsifiable prediction about the `Last` sentinel's third consequence. I
reproduced it against the current header, from a clean harness:

| Predicted | Observed | |
|---|---|---|
| `error: enumeration value 'ProxyRefused' not handled in switch [-Werror,-Wswitch]` at `NetError.hpp:62:13` | `NetError.hpp:62:13: error: enumeration value 'ProxyRefused' not handled in switch [-Werror,-Wswitch]` | **exact, to the column** |
| suite fails with `text := "unknown error"` at `NetError_test.cpp:97` | fails at `NetError_test.cpp:105`, `CHECK( text != "unknown error" )`, `with messages: static_cast<int>(code) := 15 / text := "unknown error"` | **substance exact, line number off by 8** |
| **assertion count rises 155 → 159 on its own** | `assertions: 159 \| 158 passed \| 1 failed`, baseline `155 assertions in 12 test cases` | **exact** |

**The interesting clause holds, and holds more strongly than claimed.** I decomposed the +4 by
running the affected cases individually:

```
                        unmutated   one new code
  description case         49    →      52        (+3: the three CHECKs in the loop body)
  deadline case            16    →      17        (+1: the CHECK_FALSE in the loop)
  "Last counts the codes"   2    →       2        (unchanged — it tests Last itself)
```

**Two independent cases grew, not one.** A hand-written list of today's codes would have left both
at their old size. This is the signature the implementer was pointing at, and it is real.

**The stale citation is a finding, a small one.** The report's RED transcripts cite
`NetError_test.cpp:97` and `:98`; the committed file has those `CHECK`s at `:105` and `:106`, and
`git log` shows the file has not moved since `8a88ce0`. Every quoted line in the report's mutation
transcripts is **consistently 8 lines low**, which means the transcripts were taken from a
working copy that predates the two `static_assert` blocks at `:29-36` and were not re-run against
what landed. The *substance* of every transcript reproduces exactly — the failing expression, the
expansion, the `CAPTURE` values, the counts — so this is a citation-hygiene slip, not a fabricated
result. Worth naming because a reviewer who checked `:97` and found a `constexpr` array there could
reasonably have concluded the transcript was invented.

### 8.2 The other two `Last` consequences, judged by reading

- **`Last` reaches the switch as `case NetErrorCode::Last: break;`** (`NetError.hpp:79`), falling
  through to the `return "unknown error"` after it. Confirmed. Note this is *forced*, not chosen:
  without that case, `-Wswitch` fires on `Last` itself, so the sentinel must be enumerated. A
  `default:` would be one line shorter and would silently absorb every future code — precisely what
  the rule forbids, and what mutations C1 and D1 prove the current shape prevents. The right
  trade-off.
- **A caller can construct and compare against `Last`.** True, unavoidable, and now bounded — see
  §7 item 2.

### 8.3 Every finding is answered in the working tree (uncommitted)

While I was reviewing, fixes for **all six findings** appeared as uncommitted changes to
`NetError.hpp`, `NetError_test.cpp`, `CHANGELOG.md` and `docs/modules/net.md`. I verified each by
running, not by reading the diff:

| # | Fix | Verified |
|---|---|---|
| **I-1** | `CHANGELOG.md`: "**Nothing in core-cpp returns those three yet**… until then a migrated `== HostUnreach` branch compiles and is dead code… (Tasks B6 to B8)". `docs/modules/net.md` gains the same paragraph. | read; it names the right tasks and the right symptom |
| **I-2** | New case `TEST_CASE("No code hides above Last")` scanning `[CodeCount, 256)`, plus `NetError.hpp:45-51` — "**A new code goes above it, never below.**" naming the test as the only thing that refuses it. | **run**: mutation D2 (a code appended *after* `Last`, with a real description) now **fails** at `NetError_test.cpp:143`; it passed green before |
| **M-1** | `isDeadlineExpiry`'s comment now says "Nothing in core-cpp asks this yet: those transports arrive with Task B9, and the predicate is here now because it belongs to the vocabulary rather than to them." | read; exactly the gap I named |
| **M-2** | The lineage case is rewritten from a near-tautology into `gainedBy(contour, fastcached) == {"address not available", "host unreachable", "permission denied"}` and the converse — asserting the merge's *content*, described in words so a failure reads as words. | **run**: green, and this is stronger than what I suggested |
| **M-3** | `docs/modules/net.md` `Last` row is now `unknown error`, not `—`. | **run**: my header↔docs cross-check script reports 16 codes, zero mismatches |
| **M-4** | The house style is stated at `toString` in the header ("lower-case words separated by single spaces, with no punctuation and no capital… `NetError_test.cpp` enforces it rather than trusting the next author to notice the pattern"). | read |

**Full re-verification of the working tree** (private harness, overlay include path, shared tree
never mutated):

```
WORKING TREE, unmutated      All tests passed (106 assertions in 13 test cases)
  + C1 (new code, no case)   COMPILE FAILED: NetError.hpp:70:13 -Wswitch  [caught]
  + C2 (new code, no answer) NetError_test.cpp:105 FAILED; assertions 106 -> 110  [caught, count moves]
  + D2 (code AFTER Last)     NetError_test.cpp:143 FAILED "No code hides above Last"  [NOW caught]
```

The 155→106 drop in the baseline is accounted for: the lineage case shed 52 near-tautological
runtime assertions and gained 2 meaningful ones, and the new guard adds 1 (155 − 52 + 2 + 1 = 106).
The enumeration-walking property survived the restructure intact: C2 still moves the count, by the
same +4 and in the same two cases.

`python scripts/clang-format.py --check`: 13 unformatted files in the tree, **neither of B2's two
among them**. All 13 belong to B3's in-flight backend work.

**Nothing further is required of the implementer beyond committing this**, with the staged-hunk
discipline §7 item 3 describes.

---

## Appendix: what I verified by running vs. by reading

**By running:** the GREEN baseline; all seven mutations (A, B, C1, C2, D1, D2, E) against the
committed header, and four of them again against the working tree; the per-case assertion-count
decomposition (49→52, 16→17, 2→2); the proposed I-2 guard in both directions, and then the
implementer's own version of it; `cmake --graphviz` for the link set; `clang++ -H` for the include
closure; `clang-format.py --check` twice; `ctest -L hygiene`; the docs-table/header cross-check
script twice; the 16-file rename-purity proof; `git merge-base --is-ancestor`; `gh run view`.

**By reading:** the two upstream blobs (contour `6777ff05:src/net/IoResult.hpp`, fastcached
`0708dd54:src/FastCache/Net/NetError.hpp`); the upstream call-site counts via `git grep` over the
pinned trees; the plan's Task B2 checklist and Tasks B6-B9; `.agent/rules/design-principles.md`;
the CHANGELOG, `docs/modules/net.md` and `provenance.md` diffs; `renames.json`'s four rows.

**Not verified:** the Windows and Emscripten compiles (no toolchain in this session). The
rename-purity proof covers the Windows sources' correctness by construction, and CI run
`35540278214` — which I confirmed is green and contains these commits — covers the compiles,
including `windows (cl-release-tls)`, the preset that cannot be configured on this machine.
