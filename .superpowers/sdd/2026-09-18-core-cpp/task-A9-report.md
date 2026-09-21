# Task A9 report: Phase A gate, pass 1

Every finding was worked test-first: the failing case was written and seen to fail, then the
defect was fixed and the case seen to pass. Where a defect's symptom is platform-specific the RED
was reproduced on that platform, by temporarily reverting the fix in a warm build tree; those
reverts were never committed.

Branch: `master`, on top of `b505db8`. The commits are listed at the end.

## RED/GREEN per finding

### Crash and corruption

**1. `CLI.cpp:611` — unsigned underflow makes `--help` segfault.** RED / GREEN.
New case `CLI.helpText.narrow-margin` renders a help text at margins 0, 1, 8, 20, 40, 79, 80.
Before the fix, at margin 0 (clang-debug, WSL):

```
/mnt/d/core-cpp/src/core/cli/CLI_test.cpp:156: FAILED:
due to a fatal error condition:
  margin 0
  SIGABRT - Abort (abnormal termination) signal
...string_view:260: Assertion '__pos < this->_M_len' failed.
```

libstdc++'s hardened `operator[]` traps on the out-of-bounds index before the read happens, so
this is an abort rather than an AddressSanitizer report. Re-run under `clang-asan-ubsan` with the
fix reverted, it aborts there identically, from the same assertion — that preset uses the same
standard library, so the check fires before the sanitizer would see the read. On MSVC the same
index trips `__msvc_string_view.hpp(1623): Assertion failed: string_view subscript out of range`.

The fix computes the room left on the line as `margin > cursor ? margin - cursor + 1 : 1u`,
which is also the guard the dead `if (rightMargin <= 0)` was meant to be. While there: a word
longer than the line had no whitespace to shift to and produced an *empty* chunk, and the caller
loops on chunks, so it never terminated — it now cuts hard, which guarantees progress.

**2. `CLI.cpp:841` — a verbatim placeholder longer than the longest option wraps to a ~4 GB
allocation.** RED / GREEN.
New case `CLI.helpText.verbatim-longer-than-the-options`. Before the fix (clang-debug), the
`assert(columnWidth > leftSize)` fires: `SIGABRT`. Under NDEBUG the same input reaches
`spaces(4294967281)`.
The options column now takes the verbatim placeholder's width into account, so the row it
shares the column with fits, and both paddings saturate instead of asserting.

**3. `CLI.cpp:597` — help text starting with a newline reads `text[SIZE_MAX]`.** RED / GREEN.
New case `CLI.helpText.leading-linefeed`; `SIGABRT` from the same hardened `operator[]` before
the fix. The chunk is now counted down from the line feed rather than up from `linefeed - 1`.

**4. `Utils.hpp:303` — `splitKeyValuePairs()` reads past the view.** RED / GREEN.
New case `utils.splitKeyValuePairs.bounded`, in two sections. The value section fails
deterministically everywhere:

```
CHECK( result.at("foo") == "bar"sv )
with expansion:  "bar:trailing=junk" == "bar"
```

and the second section, whose view ends at the end of its heap block, is a genuine
AddressSanitizer report under `clang-asan-ubsan`:

```
==790==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x6e12a95e1cd7 ...
READ of size 8 at 0x6e12a95e1cd7 thread T0
```

Fixed with `text.substr(iBeg)`.

**5. `Utils.cpp:32` — `threadName()` on Windows does `resize(len - 1)` with `len == 0`.**
Fixed; RED not reproducible. `GetThreadDescription()` succeeding and `WideCharToMultiByte()`
then failing cannot be provoked from a test without hooking the Win32 API, and core-cpp has no
seam there. The function now has one exit past the acquisition, so `LocalFree()` always runs, and
a conversion length of 0 answers empty instead of throwing `length_error`. `utils.threadName`
covers only that the call itself does not throw.

**6. `App.cpp:77` — `screenWidth()` accepts `ws_col == 0`.** Fixed; covered indirectly.
`screenWidth()` is a static function in an anonymous namespace with no seam, so no test calls it
directly — adding one would be the API change this task is told not to make. Its consequence is
covered by `CLI.helpText.narrow-margin`'s margin-0 case (finding 1), which was RED. The clamp is
`ioctl(...) != -1 && ws.ws_col > 0`.

### Wrong answers

**7. `Flags.hpp:84` — `operator&=` clears the flag instead of intersecting.** RED / GREEN.
`Flags_test.cpp` is new and checks every operator against its non-compound form. Before the fix:

```
/mnt/d/core-cpp/src/core/Flags_test.cpp:100: FAILED:
  CHECK( intersected == (both & Fruits { Fruit::Apple }) )
/mnt/d/core-cpp/src/core/Flags_test.cpp:101: FAILED:
  CHECK( intersected == Fruits { Fruit::Apple } )
```

`operator&=` now intersects, and an overload taking a `Flags` was added so the pair is symmetric
with `operator|`/`operator|=`. (A consumer that relied on the old spelling wants `disable()`; no
consumer does — `grep` over contour, endo, tuidu and morph finds no `&=` on a `Flags`.)

**8. `Base64.hpp:159` — `decodeLength()`'s scan never stops.** RED / GREEN.
New case `base64.decodeLength`:

```
CHECK( base64::decodeLength("YWJj"sv) == base64::decodeLength("YWJj!!!!!!!!"sv) )
with expansion:  3 == 9
CHECK( base64::decodeLength("YWJj"sv) == base64::decodeLength("YWJj\n\n\n\n"sv) )
with expansion:  3 == 6
```

The sentinel is now a named constant, `detail::LastDigitIndex`, and `decode()` tests against the
same one.

**9. `Escape.hpp:35` — escape and unescape do not round-trip, and the printable range is off by
one.** RED / GREEN. `Escape_test.cpp` is new, and walks all 256 byte values through both numeric
styles and back. Before the fix:

```
CHECK( core::escape('~') == "~" )          with expansion: "\x7e" == "~"
CHECK( core::unescape("\\\""sv) == "\"" )  with expansion: "\""   == """
CHECK( core::unescape("\\101"sv) == "A" )  with expansion: "\101" == "A"
REQUIRE( restored.size() == 1 )            byte 0x22 escaped as "\""
```

Three defects: 0x7E was outside the printable range; `escape()` writes a quote as `\"` and
`unescape()` had no case for it; and an octal escape is three digits of which only those below
`\100` begin with a zero, but the reader keyed the sequence on `'0'`. The reader now opens an
octal sequence on any octal digit and consumes exactly three, which reads the `\0dd` form it used
to accept identically (a leading zero is octal-neutral).

**10. `FNV.hpp:61` — the trivially-copyable overload hashes padding bytes.** RED / GREEN.
The RED was a runtime case hashing two `Padded` objects with equal members and different padding:

```
CHECK( fnv(fnv.basis(), a) == fnv(fnv.basis(), b) )
with expansion:  4151431055 (0xf771cf8f) == 103124936 (0x6258fc8)
```

After the fix that call does not compile at all — which is the point — so the permanent test is
the constraint itself (`STATIC_CHECK(!HashableBytewise<Padded>)`) plus a compile-time hash, which
the old `reinterpret_cast` made impossible. The overload now requires
`std::has_unique_object_representations_v` and reads the bytes with `std::bit_cast`.
No consumer is affected: contour's and endo's `FNV` uses all go through the `char`, `uint8_t` or
`string_view` overloads.

**11. `Utils.hpp:550` — `toLower()`/`toUpper()` pass a plain `char` to `tolower()`/`toupper()`
(and `CLI.cpp:555` to `isalpha()`).** Fixed; RED not observable.
New cases `utils.toLower/toUpper.non-ascii` and `CLI.helpText.non-ascii-help-text` assert the
correct behaviour (bytes with the high bit set pass through unchanged, and a UTF-8 help text
still yields its hyperlink), but they pass before the fix as well: glibc's table has a negative
margin and the UCRT tolerated the negative index here too. It is undefined behaviour that happens
to work on these two platforms — verified by temporarily reverting the fix on MSVC and running
the case. Both call sites now widen through `unsigned char`, via a `core::detail::asCTypeArgument`
helper that leaves wider character types alone.

**12. `Utils.hpp:582` — `readFileAsString()` returns trailing NULs and cannot open a non-ASCII
path.** RED (Windows) / GREEN.
New case `utils.readFileAsString`, three sections. On POSIX the function was already correct; on
Windows, with the fix reverted in the `cl-debug` tree:

```
  "line one\r\nline two\r\n" == "line one\nline two\n\0\0"    (CRLF translation; trailing NULs)
CHECK( core::readFileAsString(path) == content )
due to unexpected exception with message:
  No mapping for the Unicode character exists in the target multi-byte code page.
```

It now takes the `std::filesystem::path` itself, opens binary, and truncates to `gcount()`.

### Silent misbehaviour

**13. `App.cpp:293` — `installLogging()` called twice silently sends every later log line to
stdout.** RED / GREEN. `App_test.cpp` is new. Before the fix:

```
CHECK( &core::log::errorLog.sink() != beforeAnyInstall )     — the second install put it back
CHECK( &core::log::errorLog.sink() == beforeAnyInstall )     — and left a dangling sink after the app died
CHECK( core::readFileAsString(tmp / "second.log").contains(Marker) )   with expansion: false
```

The previous output is now released *before* the replacement is created — not after, which is
what the assignment did. Resetting after `create()` would not have been enough: the replacement
snapshots the categories as it installs itself, so the previous one must be gone first. The cost
is that a destination that then fails to open leaves logging on the console rather than on
whatever was installed before; the caller is told, and has nothing to fall back to either way.
This is noted in the code.

**14. `LogSink.cpp:277` — `isStdErrTty()` returns `true` unconditionally on Windows.**
RED (Windows) / GREEN.
`core::log` now answers both questions in one place — `isStdOutTerminal()` and
`isStdErrTerminal()` — and `ScopedOutput`'s private `isStdErrTty()` is gone, as are
`core::cli::App`'s two copies of the same `#ifndef _WIN32` branch in `helpStyle()` and
`customizeLogStoreOutput()`. The platform branch now lives in exactly one function per stream,
which is what `.agent/rules/platform.md` asks for; it cannot move into `core::platform`, because
`platform` depends on `log` and the module table forbids the reverse edge.

New case `a redirected standard stream is not a terminal` redirects the descriptor to a file with
`dup`/`dup2` and asks. With the Windows branch put back to `return true`:

```
D:\core-cpp\src\core\log\LogSink_test.cpp(496): FAILED:
  CHECK( !core::log::isStdErrTerminal() )
with expansion:  false
```

(both sections failed; standard output the same way).

**15. `App.cpp:224` — `cli::parse()` throws although its contract says it returns
`std::nullopt`.** RED / GREEN.

**What I chose, and why.** I made the documentation true for `cli::parse()` and the *return* true
for its two bool-returning callers, rather than making `parse()` swallow. `parse()`'s declaration
now states that it throws `ParserError` for a value of the wrong type and `std::invalid_argument`
for a missing required option, and returns `std::nullopt` when the arguments do not form a
complete command. `reparseParameters()` and `parseParametersForTesting()` — both documented
"false on failure" — catch, report the message on standard error the way `App::run()` already
does, and answer `false`.

The reason for that split rather than catching inside `parse()`: the exception is the only place
the *reason* exists. `App::run()` prints `Unhandled error caught. {what}`; if `parse()` returned
`nullopt` instead, every caller would print "Failed to parse command line parameters." and the
user would never learn which option was wrong. Catching at the boundary whose contract is a value
keeps both the contract and the diagnostic. This is deliberately not core-cpp#13's `std::expected`
conversion, which stays at the end of the plan and will carry the reason in the return type.

Before the fix (`App_test.cpp`):

```
CHECK( !app.parseParametersForTesting(3, argv) )
due to unexpected exception with message: Integer value expected but something else specified.
CHECK( !app.parseParametersForTesting(2, argv) )
due to unexpected exception with message: Not enough arguments specified.
```

`CLI_test.cpp`'s `CLI.parse.failure-modes` pins the two throwing paths, so the declaration and
the code cannot drift apart again.

## Lower-severity items

Fixed (each a line or two, and covered by a case where one was possible):

- `CLI.cpp:383-384`: an assignment duplicated by the `setOption()` on the next line.
- `CLI.cpp:428-429`: a re-set of the command's own flag to the value set at the top of the same
  function.
- `CLI.cpp:977`: `if (com.children.empty())` inside the `else` of that same test — dead.
- `Base64.hpp`: `decode()`'s index lambda captured the 256-byte table by value, copying it per
  call; it captures by reference now.
- `Utils.hpp`: `eachElement<T>()`'s end iterator was `max + 1` computed in `int` and cast back,
  which for a type narrower than `int` wraps onto `begin()` (so `eachElement<uint8_t>()` was an
  *empty* range) and for one as wide as `int` overflows. RED: `count == 0` where 256 was
  expected, for `uint8_t`, `int8_t` and `uint32_t`. The end iterator now sits on the maximum and
  carries an `exhausted` flag.
- `LogSink_test.cpp`: "an explicit filter never silences the error category" disabled the
  process-wide `errorLog` with no restore; a failing `REQUIRE` below it would have left it off
  for every later case in the binary. It now takes a `ScopedCategoryState` guard.
- `App.cpp`: `listDebugTags()` sorted `core::log::get()` — the process-wide registry, whose order
  is its construction order — in place. It sorts a copy.

Deliberately left, for a ruling (both are API decisions, as the task says). **Both have since been
ruled on by the controller** and come as one round together with the review's findings, so the
tree is built and verified once rather than twice — `Times2D::operator[]` returns the type its
`value_type` declares, and `joinHumanReadableQuoted`'s separator stops being a deducible template
parameter so its default becomes usable. Both are public-API changes that cost nothing before
v0.1.0 and would be breaking after it; per the unreleased-CHANGELOG rule neither needs a Breaking
entry.

- **`Times2D::operator[]`** (`Times.hpp:177`) returns `second[i % second.size()]`, the inner
  element only, although iterating a `Times2D` yields a tuple of both coordinates. Making the
  subscript agree with the iterator changes its return type, which is public API.
- **`joinHumanReadableQuoted`** (`Utils.hpp:224`) declares `typename Separator` with
  `Separator sep = ", "`, so the default can never be taken: `Separator` is deducible and the
  call with one argument does not compile. The one-line repair is
  `typename Separator = std::string_view`, but choosing between that and matching
  `joinHumanReadable`'s plain `std::string_view sep` is an API call. Nothing in core-cpp or any
  consumer calls it today.

Also left, as neither a line or two nor clearly right:

- `printOption(option, nullopt, style)` is still rendered twice per option per help run (once in
  `longestOptionText()`, once for the left column), plus once coloured. Removing the duplication
  means caching the rendered strings across the two loops, which is a restructure, not a fix.
- `layOut()`'s fixed 8-space continuation indent still does not line up once a prefix (timestamp,
  pid) is present. The correct indent is the rendered prefix's width, which the formatter does not
  currently measure.

## Verification

Local, all green after the fixes:

| Preset | Host | Result |
|---|---|---|
| `clang-debug` | WSL Ubuntu 26.04 | build clean, `ctest` 19/19 |
| `clang-asan-ubsan` | WSL | build clean, `ctest` all pass |
| `gcc-release` | WSL | build clean, `ctest` all pass |
| `cl-debug` | Windows, VS dev shell | build clean, `ctest` 21/21 |
| `clangcl-release` | Windows | build clean, `ctest` 21/21 |
| `clang-tidy` | WSL | clean for every file this task touched |

`python scripts/clang-format.py --check` clean (359 files, clang-format 22.1.8).
`ctest -L hygiene` 7/7 (the provenance rule needed rows for the three new files).
`mkdocs build --strict` clean.

CHANGELOG entries are under `[Unreleased]` → `Fixed` (one per defect) plus an `Added` line for
`core::log::isStdOutTerminal()`/`isStdErrTerminal()`. `.agent/reference/provenance.md` has a note
on every file that now diverges from contour's crispy, and rows for the three new files
(`Escape_test.cpp`, `Flags_test.cpp`, `App_test.cpp`).

## Consumer impact

- **`core::Flags::operator&=`** changes behaviour. No consumer uses it (checked contour, endo,
  tuidu, morph); one that did and wanted the old meaning wants `disable()`.
- **`core::FNV`**'s byte-wise overload no longer accepts a type with padding. No consumer
  instantiates it with one.
- **`core::readFileAsString`** now returns the file's exact bytes. contour reads a CA PEM
  (`ContourGuiApp.cpp:266`) and a forced-DPI file through it; both were getting trailing NULs on
  Windows.
- **`core::base64::decodeLength`** now returns the size of the base64 prefix. endo sizes a buffer
  with it (`GeminiProvider.cpp:326`) and was over-allocating for anything after the payload.
- **`core::log`** gains `isStdOutTerminal()`/`isStdErrTerminal()`; nothing is removed from its
  public surface (`ScopedOutput::isStdErrTty()` was private).
- **`cli::parse()`**'s documented contract changed, not its behaviour; `App::reparseParameters()`
  now answers `false` where it used to let an exception through, which is what its callers
  already expected.

## Note on the working tree

Another session is committing to `master` in `D:\core-cpp` at the same time (the `platform:`
commits interleaved with mine, and uncommitted edits under `src/core/platform/`). I touched none
of its files; everything above was built and tested with its work in the tree.

One of its commits blocked the push for a while: `5e7b681` made
`src/core/platform/SystemPipe.cpp:388` fail under clang-cl,

```
error: implicit conversion changes signedness: 'unsigned long' to 'long' [-Werror,-Wsign-conversion]
    if (::ioctlsocket(pair[1], FIONBIO, &nonBlocking) == SOCKET_ERROR)
```

which `cl-debug` does not catch and which CI's Windows clang-cl job would have failed on. I
reported it to the team lead rather than editing another session's file; that session fixed it
itself in `1556119`, and `clangcl-release` is green on top of that. The 10 clang-tidy findings
that remain in the tree are in `src/core/platform/` as well and are not mine.

## Commits

- `3af2245` fix(base): escape and unescape round-trip every byte
- `5c617f0` fix(base): Flags::operator&= intersects instead of clearing
- `29feb11` fix(base): base64 decodeLength measures the base64 prefix
- `0bcecc1` fix(base): FNV hashes only a type whose bytes are its value
- `758256a` fix(base): Utils stays inside the bounds it was given
- `0480624` fix(log): a redirected standard stream is not a terminal
- `128e3db` fix(cli): the help text stays inside the text it lays out
- `08b285f` fix(cli): App keeps the contracts it documents
- `1ab4f67` docs: the changelog and the provenance table record the gate's fixes
- `b8eb7f6` test(log): the redirect fixture opens a descriptor, not a FILE
- `2e68c34` style: the new base code and cases satisfy the pinned clang-tidy

(`master` also carries the other session's `platform:` and `testing:` commits, interleaved with
these; pushing `master` published both sets, which is unavoidable with one checkout and one
branch.)

## CI

Pushed as `b505db8..1556119`. Runs on `1556119`, in
[contour-terminal/core-cpp](https://github.com/contour-terminal/core-cpp/actions):

| Workflow | Run id | Result |
|---|---|---|
| Build | `35521290104` | green except the two macOS jobs — see below |
| Docs | `35521290076` | success |
| Portability (`gh workflow run portability.yml`, FreeBSD) | `35521296538` | success |

Green in the Build run: `style`, Linux (`clang-22`, `clang-22-cxx26`, `clang-22-arm64`,
`clang-22-tracy`, `gcc-14`, `gcc-15`), Windows (`cl-debug`, `cl-release`, `cl-release-tls`,
`clangcl-release`), `sanitizers (clang-asan-ubsan)`, `sanitizers (clang-tsan)`,
`emscripten (emsdk 3.1.56)`, `emscripten (emsdk latest)`, `coverage`, `compile-cache`, and the
three `consumer-smoke` jobs. Both Emscripten jobs green is what the Global Constraints ask of a
task that touches the WebAssembly subset, which `base`, `log` and `cli` all are.

Three jobs red, none of them on a file this task touched — all three are `src/core/platform/`,
the other session's work:

- `macos (appleclang)` and `macos (llvm-22)`, on one case-insensitive-filesystem (APFS) case,
  which is why no Linux or Windows job sees it:

  ```
  core-cpp-platform-test:
  rename reports why the recase failed, not why the first attempt did
  src/core/platform/FileSystem_test.cpp:472: FAILED:
    REQUIRE( backend.createDirectory(dir / name).has_value() )
  with expansion:  false
  147 test cases | 146 passed | 1 failed
  ```

- `clang-tidy`, on seven findings, every one of them in `src/core/platform/FileSystem.hpp`,
  `NativeFileSystem.cpp` or `testing/InMemoryFileSystem.cpp` (`performance-enum-size`,
  `readability-suspicious-call-argument`, `readability-avoid-nested-conditional-operator`,
  `modernize-return-braced-init-list`). None in `core::base`, `core::log` or `core::cli`.

Both were reported to the team lead. That session pushed its own clang-tidy fix as `46df80c`
shortly afterwards, which re-runs the whole suite — every commit of this task included — as Build
run `35521713519`. `clang-tidy` is green there; the macOS `core::platform` case fails again at
the same `FileSystem_test.cpp:472`, so it is still open and still that session's. It is the
setup `REQUIRE` that fails, not the assertion the case is about: the fixture cannot create its
directory on APFS.

Nothing in `core::base`, `core::log` or `core::cli` is red in either run.

---

# Fix round 1 (Ruling R52)

The review verdicted all 15 gate findings FIXED and found one Critical regression, three Important
items and five Minor ones; Ruling R51's two deferred API items came in the same round, so the tree
was built and verified once. Worked test-first again, on top of `3cdb0ce`.

## Critical

**1. `CLI.cpp:598-604` — `--help` hangs forever on a space before a line feed.** RED / GREEN.

This was mine: the round that fixed findings 1 and 3 introduced it. The chunk that ends at a line
feed is trimmed of its trailing spaces, but the caller advanced its index by the chunk's *length*.
The two differ by exactly what was trimmed, so the trimmed space stayed in front of the index —
and the loop that skips what a chunk stopped at skips line feeds, not spaces. The same empty chunk
came back for ever.

New case `CLI.helpText.space-before-linefeed`, five sections. Before the fix, killed by a
20-second timeout:

```
/mnt/d/core-cpp/src/core/cli/CLI_test.cpp:271: FAILED:
due to a fatal error condition:
  SIGTERM - Termination request signal
exit=124  (124 = timed out)
```

and after it, `All tests passed (8 assertions in 1 test case)`.

The fix makes "what was emitted" and "what was consumed" two numbers instead of one: the helper
returns a `WrappedChunk { text, consumed, trimLeadingWhitespaces }`, the caller adds `text.size()`
to the cursor (which counts columns) and `consumed` to the index (which counts input). Progress no
longer depends on the trimming: a chunk that ends at a line feed consumes up to but not including
it, and the caller's skip loop consumes the line feed itself, so at most one turn can be empty.

**The same shape elsewhere.** I went through every index in the wrapper and its callers:

- `stylizer()`'s hyperlink scan (`CLI.cpp:550-575`) advances `a = right`, where `right` is derived
  from the *found* position and is strictly greater than `a`. Progress, and no trimming.
- `printOption(..., cursor)` (`CLI.cpp:739`) advances the cursor by the *uncoloured* length while
  emitting the coloured string. Deliberate and correct: the cursor is a column count and an SGR
  escape has no width. Not the same shape.
- `printOptions()` and `usageText()` hold no index of their own.

The one place was `wordWrapped()`.

## Important

**2. The CHANGELOG recorded five breaking changes as `Fixed`.** Done.
They are under `Breaking` now, each with a migration note: `Flags::operator&=`'s silent reversal
(the prose existed, in the wrong section), `FNV`'s narrowed constraint — with the migration the
review asked for, and stating that it now rejects `float` and `double` as well, whose
representations have padding bit patterns — `decodeLength()`'s and `readFileAsString()`'s changed
answers, and the `[[nodiscard]]` on `unescape()` and `readFileAsString()`, which stops a consumer
building with `-Werror`. What is left under `Fixed` for those entries is only what does not change
what a consumer sees (`FNV`'s `std::bit_cast`, `decode()`'s table capture).

**3. `Utils.hpp:599-603` — the Doxygen stated a contract the function did not keep.** RED / GREEN.
`file_size()` throws for a path that is not there, which is the ordinary failure: contour reads a
CA certificate through this. It uses the `std::error_code` overload now and answers empty, as the
`@return` already claimed. New section in `utils.readFileAsString`; with the fix reverted:

```
utils.readFileAsString
  a file that is not there reads as empty, as the contract says
/mnt/d/core-cpp/src/core/Utils_test.cpp:420: FAILED:
  CHECK( core::readFileAsString(tmp / "no-such-file.txt").empty() )
due to unexpected exception with message:
  filesystem error: cannot get file size: No such file or directory
```

**4. `LogSink.cpp:275-292` — the platform split (the controller's ruling).** Done.
`isStdOutTerminal()` and `isStdErrTerminal()` keep their single declaration in `LogSink.hpp`;
`src/core/log/posix/TerminalQuery.cpp` and `src/core/log/windows/TerminalQuery.cpp` implement
them, listed in `SOURCES_POSIX` and `SOURCES_WINDOWS`. No `#ifdef` is left in either. Emscripten
never compiles `SOURCES_POSIX` and serves `isatty()` over its own libc, so the POSIX file is named
in `SOURCES_EMSCRIPTEN` too — without that the module would link with two undefined symbols there.
Confirmed in the generated build graphs: the `clang-debug` tree compiles `posix/TerminalQuery.cpp`
and the `cl-debug` tree `windows/TerminalQuery.cpp`.

The ruling is right and my earlier reasoning was half the rule. I have left one `#ifdef` in that
file that the ruling did not name: `processId()`'s `getpid()`/`_getpid()` (`LogSink.cpp:31-39`).
It predates this task, it is a private helper in an anonymous namespace, and splitting it is a
second file pair for one line — worth a ruling of its own rather than a silent decision here.

## Minor

**5. `Base64.hpp:45-47` — the Doxygen described the sentinel, the constant is the last index.**
Corrected: it now says it is the largest index a digit maps to, and that every other byte is 64,
one past it.

**6. `Utils.hpp:553-562` — `asCTypeArgument` left the wide-character case undefined.**
Taken the second of the two options the review offered: `detail::toLowerChar`/`toUpperChar` send a
one-byte character through `std::tolower`/`std::toupper` widened as `unsigned char`, and anything
wider through `std::towlower`/`std::towupper`. No width is silently truncated into the narrow
functions' domain any more, and the helper no longer hands out a value its caller has to cast.

**7. `Escape.hpp:121-126` — `\1` through `\7` read differently now.** Recorded: the CHANGELOG
entry says they used to come back as the two literal characters and now open a three-digit octal
run, that this is correct for anything `escape()` produced, and that hand-written or third-party
escaped text meaning a literal backslash before a digit has to spell the backslash `\`.

**8. `App.cpp:299-307` — the new failure mode was in a comment only.** Recorded in the CHANGELOG
entry for finding 13.

**9. `Escape.hpp:24` — `escape()` changed behaviour without gaining Doxygen.** It has a `///`
block now: what passes through, which bytes get a named escape, and which get a numeric one.

I did **not** give `escape()` the `[[nodiscard]]` the review says is "as warranted on one as on
the other". The ruling's item 9 asks only for the documentation, and this same round is recording
the two existing `[[nodiscard]]`s as a breaking change for consumers building with `-Werror`;
adding a third is a call for the controller, not a silent one for me. It is one attribute and one
CHANGELOG clause whenever you want it.

**Review Minor #6, which the ruling's list does not number** — the undocumented layout change.
`if (available >= text.size()) return text;` corrects an off-by-one, so text that reaches exactly
to the margin is no longer broken onto a second line. That is an improvement but it changes
rendered `--help` output, and it is now a clause in the CHANGELOG entry.

## The two API items ruled earlier (R51)

**10. `Times2D::operator[]` returns the type its `value_type` declares.** RED / GREEN.
It answered `second[i % second.size()]` — the inner coordinate alone — although the iterator's
`value_type` is `std::tuple<T1, T2>`, so subscripting and iterating disagreed on what an element
of a `Times2D` is.

**I changed `operator[]`, not `value_type`,** as the ruling prefers, and the call sites agree:
`*it` already yields the tuple, `times2D.iterator::value_type` already says tuple, and the one
existing case (`times2D.iterator.post_increment_answers_the_prior_position`) asserts against
`std::tuple { 0, 0 }`. Redefining `value_type` to the inner type would have made the iterator lie
instead. Nothing in core-cpp or in contour, endo, tuidu or morph subscripts a `Times2D`, so no
call site needed fixing. `Times2D` also gained a `value_type` of its own, aliasing the iterator's,
so the two cannot drift apart again.

New case `times2D.subscript_answers_the_same_element_iteration_does` walks the whole grid and
compares `grid[i]` against `*it` at every position, plus the four corners by hand. With the fix
reverted it does not compile, which is the honest RED for a type mismatch:

```
Times_test.cpp:85: error: no member named 'value_type' in 'core::detail::Times2D<int, int, int>'
Times_test.cpp:86: error: static assertion failed due to requirement
  'std::is_same_v<int, std::tuple<int, int>>':
  std::is_same_v<decltype(grid[0]), decltype(*grid.begin())>
Times_test.cpp:92: error: invalid operands to binary expression
  ('int' and 'value_type' (aka 'std::tuple<int, int>'))
```

A latent defect came with it: `Times::size()` and `Times::operator[]` had never been instantiated
by anything in the repository, so the conversions their arithmetic implies had never been
compiled. The new case instantiates both, and `-Wsign-conversion`/`-Wshorten-64-to-32` rejected
them; they spell their conversions out now.

**11. `joinHumanReadableQuoted`'s separator stops being a deducible template parameter.**
RED / GREEN. It is a `std::string_view sep = ", "`, matching `joinHumanReadable` beside it. New
case `utils.joinHumanReadableQuoted` covers the default, an explicit separator, an empty range,
and that each element is still escaped. Nothing calls it yet, in core-cpp or in any consumer.
With the fix reverted, every call that leaves the separator out fails to compile:

```
Utils_test.cpp:495: error: no matching function for call to 'joinHumanReadableQuoted'
Utils_test.cpp:497: error: no matching function for call to 'joinHumanReadableQuoted'
Utils_test.cpp:501: error: no matching function for call to 'joinHumanReadableQuoted'
```

Both are recorded under `Changed`, not `Breaking`, per the ruling.

## Verification (fix round 1)

| Preset | Host | Result |
|---|---|---|
| `clang-debug` | WSL Ubuntu 26.04 | build clean, `ctest` all pass |
| `clang-asan-ubsan` | WSL | build clean, `ctest` all pass |
| `gcc-release` | WSL | build clean, `ctest` all pass |
| `clang-tidy` | WSL | exit 0, no findings anywhere in the tree |
| `emscripten` (emsdk in WSL) | WSL, tests under node | build clean, `ctest` 16/16 |
| `cl-debug` | Windows, VS dev shell | build clean, `ctest` 21/21 |
| `clangcl-release` | Windows | build clean, `ctest` 21/21 |

`python scripts/clang-format.py --check` clean; `ctest -L hygiene` passes (inside the runs above);
`mkdocs build --strict` clean.

The `emscripten` preset was run locally rather than left to CI, because the platform split is the
one change in this round that could fail only there: `SOURCES_POSIX` is not compiled under
Emscripten, so naming `posix/TerminalQuery.cpp` in `SOURCES_EMSCRIPTEN` as well is what keeps
`core::log` from linking with two undefined symbols.

**A note on what a full-suite run means in a shared checkout.** The other session edits
`src/core/platform/`, `src/core/tui/` and `src/core/async/` in this same working tree while I
build, so a `ctest --preset ...` over all 20 tests picks up whatever half-edited state those
modules are in at that second: one run reported `core-cpp.async` and `core-cpp.async-fallback`
failing at `WhenAny_test.cpp:308` while that session was editing `Task_test.cpp`, and a later one
reported `core-cpp.platform` failing at `FileSystem_test.cpp:723/741` with `FileSystem_test.cpp`
modified in the working tree. Neither is reproducible once that session lands its work, neither
module is this task's, and nothing in this round touches either.

So the results above are of two kinds. The Windows and Emscripten runs happened to fall in a
quiet moment and are whole-suite numbers. For the three Linux presets I also ran, from the
committed state, exactly what is mine plus every hygiene check — `core-cpp.base`, `core-cpp.log`,
`core-cpp.cli` and `ctest -L hygiene` — and all three are 3/3 and 8/8 on `clang-debug`,
`gcc-release` and `clang-asan-ubsan`. CI on a clean checkout is the arbiter for the rest.

## Commits (fix round 1)

- `1968931` fix(cli): the help wrapper advances by what a chunk consumed — Critical #1
- `325d28f` fix(base): Utils keeps three contracts its header states — Important #3, Minor #6, R51 #11
- `d7a1299` fix(base): Times2D subscripts the element it iterates — R51 #10
- `fb50e64` refactor(log): the terminal query is an implementation per platform — Important #4
- `bd69fda` docs: the changelog separates this round's breaking changes from its fixes — Important #2, Minor #5/#7/#8/#9
- `49d4c36` docs: the changelog names the one layout change the wrapper fix brings — review Minor #6

## CI (fix round 1)

The other session pushed while this round was being verified, which carried the first five of
these commits up with its own. That push, `a518402`, ran **fully green**: Build `35524698669`,
Docs `35524698689` and Portability `35524743889` all `success`, with no failing job — the macOS
`core::platform` case that was red through the first round included, which that session has since
fixed.

`49d4c36` went up as `a518402..af392a6`. Runs on `af392a6`: Build `35526193943`,
Docs `35526193941`, Portability `35526198133`.

## Left for the controller

- **`escape()`'s `[[nodiscard]]`**, which review Minor #5 says is as warranted as the one on
  `unescape()`. The ruling asked only for the Doxygen, and this round already records two
  `[[nodiscard]]`s as a breaking change; adding a third is a call to make deliberately. One
  attribute, one CHANGELOG clause.
- **`LogSink.cpp`'s remaining `#ifdef`**, `processId()`'s `getpid()`/`_getpid()`
  (`LogSink.cpp:31-39`). It predates this task and the ruling did not name it; it is a private
  helper in an anonymous namespace, and splitting one line into a second `posix/`+`windows/` file
  pair is a judgement call rather than an obvious win.
- The two items already reported and still open from the first round: the duplicate
  `printOption` rendering, and `layOut()`'s fixed 8-space continuation indent.

---

# Fix round 2 (Ruling R55)

Both leftovers from round 1, ruled the same way, plus finishing what item 10 uncovered.

**1. `escape()` gains `[[nodiscard]]`.** Done — and the whole header rather than the one overload
whose behaviour changed. `escape()` has three spellings (a byte, an iterator pair, a
`std::string_view`) and `escapeMarkdown()` two; marking only the first would have left exactly the
inconsistency the ruling objects to, and `escape(text)` is the spelling most likely to be called
for its return value alone. `unescape()` already had it. This is one name wider than the ruling
literally asked for — `escapeMarkdown()` — which I flagged to the controller and which was
confirmed: one Breaking entry covers a family as easily as a member.

It went into the existing `Breaking` entry, as instructed, which now reads "every function in
`<core/Escape.hpp>` … and `core::readFileAsString()`" with the same migration.

**2. `LogSink.cpp`'s `processId()` `#ifdef` goes the same way.** Done.
`src/core/log/posix/ProcessId.cpp` and `src/core/log/windows/ProcessId.cpp`, sharing one
declaration in `src/core/log/detail/ProcessId.hpp`. That header is **private**: listed in
`SOURCES`, not `HEADERS`, so it is in no file set and cannot become public API — the shape
`core::tui_output` already uses for `detail/XtVersion.hpp`. The terminal queries stay declared in
the public `LogSink.hpp` because `core::cli::App` asks them too; nothing outside the module asks
for the process id, so it does not belong there.

`core::log` now has no `#ifdef` in its logic. The three new files have provenance rows — the
platform agent flagged the round-1 pair mid-flight, before that commit landed; both had their rows
in the same commit as the split, and these three do too.

**3. `Times::size()` and `Times::operator[]`.** The conversions were already spelled out in
round 1's `d7a1299` (the new `Times2D` case instantiated them through `Times2D::size()` and
`operator[]`, and `-Wsign-conversion`/`-Wshorten-64-to-32` rejected them, which is how they were
found). What this round adds is the part that keeps them from rotting: `times.size_and_subscript`
instantiates **both, directly, for every shape `times()` has** — the count-only form,
start/count/step, a negative step, an unsigned value type, a narrow one (`std::uint8_t`),
subscript checked against iteration at every position, and two constant evaluations. Before, the
only thing instantiating them was one 2-D case reaching them indirectly.

## Verification (fix round 2)

| Preset | Host | Result |
|---|---|---|
| `clang-debug` | WSL Ubuntu 26.04 | `core-cpp.{base,log,cli}` 3/3, `ctest -L hygiene` 8/8 |
| `cl-debug` | Windows, VS dev shell | build clean, `ctest` 22/22 |
| `clangcl-release` | Windows | build clean, `ctest` 22/22 |

`python scripts/clang-format.py --check` clean; `mkdocs build --strict` clean.
The Windows runs exercise `windows/ProcessId.cpp`, which no other host compiles; the generated
build graphs confirm each host picks exactly one — `src/core/log/posix/ProcessId.cpp` in the
`clang-debug` tree, `src\core\log\windows\ProcessId.cpp` in the `cl-debug` one.

`LogSink_test.cpp` keeps its own `processId()` helper rather than calling the module's. That is
deliberate: the case asserts that the `[PID]` field carries this process's id, and checking the
implementation against itself would assert nothing. An independent spelling of the same question
is what makes it a test.

## Commits (fix round 2)

- `c524cac` refactor(log): the process id is an implementation per platform too
- `336234a` fix(base): every function in Escape.hpp is nodiscard
- `41cede6` test(base): the Times cases instantiate size() and operator[]
- `d893594` docs: the changelog and the provenance table record fix round 2

---

# Fix round 3 (Ruling R62)

Five Minor items from the re-review, which verdicted every finding and both rulings ADDRESSED.

**1. `LogSink.cpp:42`'s `localtime_s`/`localtime_r` `#ifdef`.** Done, the same way as the other
two: `src/core/log/posix/LocalTime.cpp` and `src/core/log/windows/LocalTime.cpp`, sharing one
declaration in the private `src/core/log/detail/LocalTime.hpp`. POSIX spells the reentrant
conversion `localtime_r` and Windows `localtime_s`, with their two arguments the other way round,
which is exactly the kind of difference that should not be a branch inside a function that formats
a log line. Both zero the `std::tm` before converting, so a failed conversion answers the same
thing on either platform — `localtime_r` leaves the buffer alone where `localtime_s` zeroes it.

**`grep -c 'ifdef\|ifndef'` is now 0 for every file at `core::log`'s root** — `LogSink.cpp`,
`LogStore.cpp`, `LogSink.hpp`, `LogStore.hpp`, `Assert.hpp` — so the three claims that said so are
true. Thank you for catching that they were not.

While there I found that **nothing asserted the timestamp's content**, only that it can be
suppressed: a conversion that failed would have rendered `0000-00-00 00:00:00` and passed every
case in the suite. That matters more now that the conversion is a platform implementation, so
`LogSink_test.cpp` gained a case for it, which reads the local time on both sides of the emitted
line and accepts either (a minute ticking over between them cannot make it flap) and spells the
conversion out itself rather than calling the one under test. RED-probed by making `localTime()`
answer a zeroed `std::tm`:

```
LogSink_test.cpp:288: FAILED:
  CHECK( (capture.contains(before) || capture.contains(after)) )
with message:
  expected 2026-09-20 22:46 or 2026-09-20 22:46, got: [1900-01-00 00:00:00.770140] [test.formatter] hello
```

**2. The two new `[[nodiscard]]`s in `Times.hpp` sat outside the entry that enumerates.** Folded
in: that Breaking entry now reads "every function in `<core/Escape.hpp>` … plus
`core::readFileAsString()`, `core::detail::Times::operator[]` and
`core::detail::Times2D::operator[]`".

**3. The trailing-space rendering change is recorded** — and it is larger than I would have
guessed, so I measured it rather than describing it from the code. Lifting both versions of the
wrapper pair out and running them side by side over a set of inputs:

```
DIFF  input="First_line._\nSecond_line."
        old="First_line.\n_____\n____Second_line."      (three lines)
        new="First_line.\n____Second_line."             (two)
DIFF  input="abc___\nxyz"
        old="abc\n_____\n_____\n_____\n____xyz"         (five lines)
        new="abc\n____xyz"                              (two)
```

The old renderer consumed a trailing space per turn and emitted a line break plus a continuation
indent for each one. So the CHANGELOG now says both rendering changes: the line that reaches
exactly to the margin, and this. (The same measurement is where round 1's termination argument
came from, so the harness was already written.)

**4. `Breaking` versus `Changed`.** Moved: `Times2D::operator[]` and
`joinHumanReadableQuoted()`'s separator are under `Breaking` now, each with a migration —
`std::get<1>(grid[i])` or a structured binding for the first, and formatting a non-string
separator yourself for the second, since the old signature rendered it with `std::format` and so
accepted an `int` or a `char`. The `Changed` section stays, because the async session's entries
are in it; only mine moved.

**5. The `cli` test has a `TIMEOUT`.** 60 seconds, set in `src/core/cli/CMakeLists.txt` next to
`core_cpp_add_test`, the way `core::testing` sets its `ENVIRONMENT` there. The suite takes
hundredths of a second, so the bound is only ever a hang, with room for a sanitized build on a
loaded runner. Verified in the generated `CTestTestfile.cmake`: `TIMEOUT "60"`.

**The systemic version** was ruled in (R63), ran into another session's uncommitted work, and was
then handed to that session (R64) — they have the file open and the default is four lines on top
of the keyword they are already adding. The measurement below went to them verbatim. What follows
is the record of it:
that session is adding a `TIMEOUT` *keyword* to `core_cpp_add_test()` in the working tree right
now — the per-test override half of R63, opt-in, so a test that does not pass it stays unbounded.
The two halves fit together; they just land in the same twenty lines. I am not editing a file
somebody else has uncommitted changes in, so this is reported and waiting on the controller.

The measurement the default needs is done. The slowest binary `core_cpp_add_test()` registers is
`core-cpp.tui`: **9.4 s** on `clang-debug`, **11.1 s** under `clang-asan-ubsan`, **9.7 s** on
`cl-debug`. Every other binary it registers is under one second.
`core-cpp.vendor-selftest` at 87 s is slower still, but `tests/` registers it, not this function,
so a default here does not reach it.

That makes **300 seconds** the defensible default: about 27x the slowest legitimate binary
measured under a sanitizer, so a 5x slowdown on a loaded runner still leaves 5x of headroom, while
a hang is named in five minutes rather than ctest's own 1500. `cli` keeps its tighter 60.

The two do not fight, whatever order they land in: `cli`'s bound is a `set_tests_properties()`
after `core_cpp_add_test()` returns, so it overwrites whatever default the function set, and a
default arriving later needs no edit here.

**On why the gap mattered.** The controller's point is worth recording: nothing asserted the
timestamp's content, so a conversion that failed would have rendered zeroes and passed the whole
suite. That is the same shape as the other things this gate keeps turning up — a case that pins
the mechanism but not the answer, which cannot fail for the reason it exists. `eachElement()`
was one (the range was empty and every use of it iterated nothing), `decodeLength()` was another
(its scan never stopped and the size it returned was never checked against what `decode()` wrote).
A test that exercises a path without asserting its result is a test that reports the path is still
there, and nothing more.

## Verification (fix round 3)

| Preset | Host | Result |
|---|---|---|
| `clang-debug` | WSL Ubuntu 26.04 | `core-cpp.{base,log,cli}` 3/3, `ctest -L hygiene` 8/8 |
| `gcc-release` | WSL | `core-cpp.{base,log,cli}` 3/3, hygiene 8/8 |
| `clang-asan-ubsan` | WSL | `core-cpp.{base,log,cli}` 3/3, hygiene 8/8 |
| `emscripten` (emsdk in WSL) | WSL, tests under node | build clean, `ctest` 16/16 |
| `clang-tidy` | WSL | exit 0, no findings anywhere in the tree |
| `cl-debug` | Windows, VS dev shell | build clean, `ctest` 22/22 |
| `clangcl-release` | Windows | build clean, `ctest` 22/22 |

The Windows runs are what actually exercise `windows/LocalTime.cpp` and its swapped arguments:
the new timestamp case passes there, which no other host can show. The generated build graphs
confirm each host selects exactly its own three platform sources — `posix/{LocalTime,ProcessId,
TerminalQuery}.cpp` in the `clang-debug` tree, the `windows/` three in the `cl-debug` one.
`python scripts/clang-format.py --check` clean; `mkdocs build --strict` clean.

## Commits (fix round 3)

- `96d09ee` refactor(log): the local-time conversion is an implementation per platform
- `6988689` test(log): the timestamp is checked for what it says
- `56ebf47` build(cli): the cli test has a timeout
- `78824d1` docs: the changelog says what breaks in one place
- `3961733` docs: the migrated entries join the Breaking list rather than starting a new one

## On the diff package

Noted, and no harm done from this side: the omissions were in what the reviewer was handed, not
in what was committed — `CHANGELOG.md`, `Base64.hpp` and `Utils_test.cpp` were all in the pushed
commits, which is why reading them from git recovered them.

## CI (fix round 3)

Round 3's commits went up carried by the other session's push, as rounds 1 and 2 did — this is a
shared branch and a shared checkout, and `origin/master..master` was empty by the time I looked.
`50c3c2d` is the first pushed commit that contains all five of them (`96d09ee`, `6988689`,
`56ebf47`, `78824d1`, `3961733`); the green runs before it, on `4f7fd11` and `73ec045`, do not.

Runs on `50c3c2d`: Build `35537221014`, Docs `35537221002`, Portability `35537251076`
(dispatched by me on that head, so the FreeBSD job covers this round rather than an earlier one).
