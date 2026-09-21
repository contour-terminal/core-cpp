# Task A10 review: Phase A gate, pass 2

Diff reviewed: `b505db8..3cdb0ce`, filtered to `src/core/platform/`, `src/core/testing/` and
`CHANGELOG.md`. Read-only pass; nothing in the working tree, index, HEAD or any branch was touched.
The three items the report files as core-cpp#26/#27/#28 are not re-reported here.

### Finding Verdicts

**1 — `InMemoryFileSystem` `MemoryIOBuf` use-after-free + always-append: FIXED.**
`src/core/platform/testing/InMemoryFileSystem.cpp:89-149`. `xsputn()` reads `position()` *before*
the `resize()` that can reallocate, then indexes the string (`replace(at, count, s, count)`) and
only afterwards re-establishes the get area through `seekTo()` — no stale `eback()`/`gptr()` is
ever dereferenced, including on the "read after a write that grew the string" path the finding
names. `overflow()` routes through `xsputn()`, so the single-char path is covered too; `setp()` is
never called, so `sputc()` always reaches `overflow()`. The second clause is fixed as well: one
position for read and write, `seekoff()`/`seekpos()` implemented, overwrite in place, extend only
past the end. **Red without the fix:** yes — the old append reallocated a 64-byte heap string, so
`(*stream)->read()` reads freed memory (ASan `heap-use-after-free`) and `gcount()` is 1 where
`FileSystem_test.cpp:404` requires 0; the cross-backend script at `FileSystem_test.cpp:365-381`
additionally expects `"xy23456789ABC"` where the old model produced `"0123456789ABCxy"`.

**2 — `MessageQueue::_wakeup` data race: FIXED.**
`src/core/platform/MessageQueue.hpp:48-53, 123-124, 172-176`. Every access is now under `_mutex`,
including the `signal()` call itself. This is the stronger fix, and it is the right one: reading
the pointer under the lock and signalling after it would leave exactly the
`setWakeup(nullptr)`-races-an-in-flight-`push()` window the finding describes. I checked the
deadlock risk that signalling under a lock normally introduces — `Wakeup::signal()` cannot block on
any backend (`linux/LinuxWakeup.cpp:27-36` eventfd `EFD_NONBLOCK`, `posix/PosixWakeup.cpp:40-49`
self-pipe `O_NONBLOCK`, `windows/WindowsWakeup.cpp:25-28` `SetEvent`), so holding `_mutex` across it
is bounded. **Red without the fix:** yes, under TSan only (write in `setWakeup` vs. read in `push`
from two threads, 2000 rounds, `MessageQueue_test.cpp:153-178`).

**3 — `SignalHandler::restore()` dangling `Wakeup*`: FIXED.**
`src/core/platform/SignalHandler.cpp:197`. `interruptWakeup.store(nullptr)` sits in the common tail
after `#endif`, so it runs on Linux, macOS/BSD and Windows alike, and all three reach paths
(`processSignalFd()` at :233, `sigintHandler()` at :83, `consoleCtrlHandler()` at :52) go through
the single `signalInterruptWakeup()` at :31-36. **Red without the fix:** yes —
`SignalHandler_test.cpp:117-124` re-initializes without a wakeup and asserts
`CHECK_FALSE(isSignalled(wakeup))`, which the stale pointer fails. See Minor for the residual
load-then-destroy window.

**4 — Windows `EnvironmentProvider::get()`: FIXED, by delegating (the preferred fix).**
`src/core/platform/windows/WindowsEnvironmentProvider.cpp:48-57` is now
`return core::LiveEnvironment {}.get(name);`, not a re-fix. Equivalence is by construction rather
than by argument: it is literally the code at `src/core/Environment.cpp:159-181`, which sizes the
buffer from the `nSize == 0` probe (buffer-too-small path) and distinguishes empty from unset with
the `SetLastError`/`GetLastError` dance. `unset()` and `exportVariable()` delegate to
`core::unsetProcessEnvironmentVariable()`/`core::setProcessEnvironmentVariable()`. The 32 KiB
per-lookup allocation is gone. **Red without the fix:** yes on Windows for the empty-value case
(`EnvironmentProvider_test.cpp:106-123`); see Minor on the long-value case, which is a guard.

**5 — `setTestEnv(name, "")` deletes the variable on Windows: FIXED.**
`src/core/testing/EnvHelper.hpp:44-51, 60-68`. `_putenv_s` then
`core::setProcessEnvironmentVariable(name, "")` for the empty case, and `unsetTestEnv()` now tells
both copies. `EnvHelper.hpp` stays free of `<windows.h>`, as the platform rule requires. **Red
without the fix:** yes on Windows — `EnvHelper_test.cpp:52-62` and `:64-80`, the second of which is
the `ScopedEnv` destructor case that made the damage cross tests.

**6 — Windows write socket blocking, `WSAEWOULDBLOCK` as `IoError`: FIXED.**
`src/core/platform/SystemPipe.cpp:387-395` (`ioctlsocket(FIONBIO)` on `pair[1]`, which is the write
end) and `:199-207` (`WSAEWOULDBLOCK` → `return size`, byte-for-byte the answer the POSIX branch
gives for `EAGAIN` at `:99-110`). **Red without the fix:** yes on Windows —
`SystemPipe_test.cpp:207-248` now compiles on both platforms and runs the producer on a thread
against a 10 s bound, so a parked `send()` fails the case instead of hanging it.

**7 — `write()` casts size to `int` with no clamp: FIXED.**
`src/core/platform/SystemPipe.cpp:135-147`; one `clampToTransferSize()` for both directions.
**Not red, and the reason holds:** a runtime case needs a real >2 GiB buffer. The five
`static_asserts` (0, 1, `INT_MAX`, `INT_MAX+1`, `SIZE_MAX`) are a compile-time proof of the helper,
which is the best available substitute. Honestly labelled as such in the report.

**8 — `makeLoopbackPair()` accepts whoever connects: FIXED.**
`src/core/platform/SystemPipe.cpp:263-283` (`areConnectedToEachOther()` compares
`getsockname`/`getpeername` in both directions) and `:333-347` (16 attempts, each on a fresh
ephemeral port, bounded so a hammered host reports failure rather than hanging). **Not red, and the
reason holds:** provoking it needs an adversary that learns the ephemeral port between `listen()`
and `accept()`; the new Windows case asserts the property instead and would catch a regression.
Honestly labelled as a guard.

**9 — symlink to a directory reported executable: FIXED.**
`src/core/platform/NativeFileSystem.cpp:71-80`. The followed `fs::status()` decides alone;
`!fs::is_regular_file(status)` rejects a directory, a symlink to one, and (via the `ec`) a dangling
symlink, while a symlink to an executable regular file still answers true. **Red without the fix:**
yes — `FileSystem_test.cpp:438` (`CHECK_FALSE(backend.isExecutableFile(dir / "to-dir"))`).

**10 — bracket expression unreachable for `[`: FIXED, better than asked.**
`src/core/platform/GlobMatch.cpp:59-64, 92-101`. The `[` arm is tested first, but only when
`findBracketEnd()` sees a closing `]`, so an unterminated `[` stays the literal `[` that
`fnmatch(3)` reads it as. The bare swap the finding suggested would have regressed that.
**Red without the fix:** yes — `GlobMatch_test.cpp:50-52` (`[` vs `[[]`, `a` vs `[[]`, `[x` vs
`[[]x`); the three unterminated-`[` rows at `:54-56` are the guard against the over-correction.

**11 — lossy narrowing via `generic_string()`: FIXED.**
`src/core/platform/PathUtils.cpp:21-39` (both `stripTrailingSeparator()` and its root-length
measurement go through `normalizePath()`), `:42-72` (`isCaseOnlyRename()` splits the normalized
UTF-8 strings on the last `/` instead of rebuilding a `std::filesystem::path`), and
`NativeFileSystem.cpp:485-506` (`createTempFile()` is `wchar_t` end to end on Windows via
`_wmktemp_s`, prefix in as UTF-8). **Red without the fix:** yes on Windows — the three cases threw
(`PathUtils_test.cpp:187-196`, `:198-210`, `FileSystem_test.cpp:456-470`); on Linux they pass either
way, which the report states. See Important #1: the read-back half of the same file is not
converted.

**12 — `createDirectory()` misdiagnoses an existing directory: FIXED.**
`src/core/platform/NativeFileSystem.cpp:215-224` — `create_directory()` returning false with no
error becomes `std::errc::file_exists`. **Red without the fix:** yes —
`FileSystem_test.cpp:485` asserted `contains("File exists")` against a hard-coded
"No such file or directory".

**13 — `rename()` throws away `recaseError`: FIXED.**
`src/core/platform/NativeFileSystem.cpp:272-330` — `renameViaTemporary()` returns a `RecaseResult`
(renamed / error / stranded), `rename()` reports that error, and names the `<name>.recase-N` when
the rollback failed too. The temporary name is built in UTF-8, which is the stronger half of the
fix: a mangled candidate would have renamed the entry, not merely misreported it. **Red without
the fix:** yes on a case-sensitive host — `FileSystem_test.cpp:527-528` expects
`filename_too_long` and explicitly rejects `directory_not_empty`, which is what the old code
reported.

**14 — `bool` parameters in the public `FileSystem` interface: FIXED.**
`src/core/platform/FileSystem.hpp:20-36` defines `WriteMode { Truncate, Append }` and
`OverwritePolicy { Refuse, Replace }`; `:93-94` and `:117-120` take them. Both implementations are
converted (`NativeFileSystem.hpp:31, 46-49`, `NativeFileSystem.cpp:202-206, 254-262`,
`InMemoryFileSystem.hpp:65, 79-82`, `InMemoryFileSystem.cpp:325-335, 409-424`). I grepped the whole
tree: **no `bool` overload survives for compatibility**, and no call site outside the new tests
passed the old argument, so nothing else needed an edit. `<cstdint>` is included
(`FileSystem.hpp:6`) for the `std::uint8_t` underlying type. Both are under **Breaking** in
`CHANGELOG.md:236-249` with a migration a consumer can copy-paste in both directions.
**Red without the fix:** compile-time only, as stated.

**15 — `TestEnvironmentProvider` namespace: FIXED.**
`src/core/platform/testing/TestEnvironmentProvider.hpp:11, 81` now open and close
`core::platform::testing`. `EnvironmentProvider_test.cpp:24` spells the qualified name, which does
not compile before the move. `CHANGELOG.md:247-249` records it under Breaking with the migration,
and `.agent/guides/consumer-migration.md:80` already names the new spelling. **The reported hygiene
hole is real** — `tests/cmake/check-cmake-hygiene.cmake:254` derives the expected namespace from
`^src/core/([^/]+)/`, the *first* segment only, and `:272` then accepts that namespace or anything
nested in it, so `platform/testing/X.hpp` declaring `core::platform` passes. Not widened, as
instructed. No new files were added by this task, and the one file it moved now obeys the rule.

### Strengths

- **Finding 4 was fixed the way the finding asked** — by deleting the hand-rolled reader and
  delegating, so the two readers of the Win32 block cannot drift again. Equivalence needs no
  argument; it is the same function.
- **Finding 2 closes the window rather than silencing the tool.** Signalling under the lock is the
  difference between "TSan is quiet" and "once `setWakeup(nullptr)` returns, nothing is inside
  `signal()`", and the choice is justified in the header where the next reader will look. It is
  also safe: every `Wakeup::signal()` backend is non-blocking.
- **Finding 10 rejects the finding's own suggested fix and says why.** A bare swap would have turned
  an unterminated `[` into a failed match; the guard rows at `GlobMatch_test.cpp:54-56` pin that.
- **The two SKIPs are runtime-detected, with reasons.** `FileSystem_test.cpp:516` probes
  `exists(dir / upper)` for case-insensitivity rather than testing `__APPLE__`, and `:523` probes
  whether the filesystem accepts a name past `NAME_MAX`. On a case-sensitive host the case still
  asserts everything it exists to assert, including the negative
  (`CHECK_FALSE(... directory_not_empty ...)`).
- **The full-channel `SystemPipe` case turns a hang into a failure** (`SystemPipe_test.cpp:225-247`):
  bounded wait, `detach()` for the parked thread, shared ownership so what it touches outlives the
  case. That is what the testing rule asks for.
- **CHANGELOG and provenance are complete.** Every one of the fifteen has a Fixed or Breaking entry,
  and every touched file has a provenance note naming the A10 divergence from endo.
- **Findings 7 and 8 are labelled for what they are** — a compile-time proof and a guard — instead of
  being dressed up as reds.

### Issues

#### Critical

None.

#### Important

1. **`src/core/platform/testing/InMemoryFileSystem.cpp:201, 264, 355, 365, 502, 518, 536, 575, 582,
   590` — finding 11 is fixed on the write half of the key space and not on the read half.**
   `normalize()` now produces UTF-8 (good — that is the fix), but ten sites convert a key back with
   the *narrow* `std::filesystem::path` constructor or `path::string()`, which on Windows
   reinterprets those bytes in the ANSI code page. So `listDirectory()` (:502, :518, :536) and
   `walkDirectoryRecursive()` (:575, :582, :590) hand back `DirectoryEntry.path` values whose wide
   spelling is not the one the caller passed in, `weaklyCanonical()` (:264) likewise, and
   `ensureParentDirectories()` (:201), `createDirectory()` (:355) and `createDirectories()` (:365)
   derive parent keys through the same round-trip. *Why it matters:* the fake is meant to answer
   like the native backend — the new `FileSystem_test.cpp:365-381` exists to hold them to that — and
   for a non-ASCII name on Windows it no longer does: `readFile(entry.path)` after a
   `listDirectory()` re-normalizes the mojibake and misses the key. It is **not a regression** (the
   old code threw on such a path), and it is invisible on POSIX and for ASCII names, which is why CI
   is green. *Fix:* one `pathFromKey(std::string const&)` helper returning
   `std::filesystem::path(std::u8string_view{reinterpret_cast<char8_t const*>(k.data()), k.size()})`,
   used at all ten sites, plus one case that lists a directory holding a non-ASCII name and reads an
   entry back through the path it was handed.

2. **`src/core/platform/testing/InMemoryFileSystem.cpp:91, 34, 336-347` — the streams still hold a
   raw `std::string*` into `_files`, so the filesystem's own API can reallocate or free the target
   under a live stream.** Finding 1 asked whether every path that can reallocate the target is safe.
   Every path *through the stream* now is. A mutation *through the filesystem* while a stream is
   open is not: `writeFile()`/`appendFile()` on the same key reallocate the string, leaving the get
   area stale until the stream's next write or seek (the original defect, reached by a different
   door), and `remove()`/`removeAll()`/`rename()` erase the map node, after which `_target` itself
   dangles and the next `xsputn()` writes through a freed pointer. *Why it matters:* this is a test
   double; a fixture that opens a stream and then rewrites the file to set up the next assertion is
   an ordinary thing to write, and the failure is a silent UAF, not a test failure. *Fix:* hold the
   content as a `std::shared_ptr<std::string>` in the map and in the streambuf, or — cheaper — state
   the lifetime contract on `openWrite()`/`openReadWrite()` in `FileSystem.hpp` and assert it in the
   model (refuse a mutating call while a stream is outstanding).

#### Minor

1. **`src/core/platform/SignalHandler.cpp:31-36` vs `:197` — a load-then-destroy window survives on
   macOS/BSD and Windows.** `signalInterruptWakeup()` can load the pointer, then `restore()` store
   `nullptr` and the caller destroy the `Wakeup`, before `signal()` runs — on macOS/BSD from
   `sigintHandler()`, on Windows from the OS console thread. Linux is safe because
   `processSignalFd()` runs on the loop's own thread. Closing it needs refcounted ownership, which
   is out of this task's scope; worth a line in the header saying the pointer must outlive any
   in-flight delivery, so the next reader does not assume `restore()` is a full barrier.

2. **`src/core/platform/EnvironmentProvider_test.cpp:125-137` — the "longer than a page" case is a
   guard, and its comment says otherwise.** The old reader sized a 32767-byte buffer, which is the
   Win32 maximum for a single variable, so the buffer-too-small branch the comment describes was
   unreachable and an 8192-byte value passed before the fix too. Reword to say the branch is
   untestable through this API and that the case guards the delegation instead.

3. **`src/core/platform/FileSystem_test.cpp:530-531` — the comment does not describe the scenario the
   case provokes.** "The rollback put it back under its original name" — in this case the *first*
   hop (`rename(from, candidate)`) fails with `ENAMETOOLONG`, so no rollback runs and the entry was
   never moved. The assertion is right; the comment names a different path.

4. **`src/core/platform/testing/InMemoryFileSystem.cpp:144-159 (seekoff) and 336-347` — two
   divergences the new "answer alike" case does not cover.** `seekoff()` refuses `target > size`
   where `std::fstream` allows a seek past the end, and the model's `openReadWrite()` creates a
   missing file where `std::fstream(in|out)` fails. Either pick them up in `runStreamScript()` or
   say in `FileSystem.hpp`'s `openReadWrite()` doc that the two do not agree there.

5. **`src/core/platform/testing/InMemoryFileSystem.cpp:72-86` — `MemoryOutputBuf` implements no
   `seekoff()`,** so `seekp()` on a model `openWrite()` stream sets `failbit` where the native one
   succeeds. Not in scope for finding 1 (which was about `MemoryIOBuf`), but it is the same "the
   fake diverges from the backend" class and is now the only stream direction without a position.

6. **`src/core/platform/FileSystem_test.cpp:489` — the finding-12 case leaves the missing-parent
   message unpinned.** `CHECK(again.error() != missingParent.error())` passed before the fix as well
   (the formatted paths differ). Asserting `missingParent.error().contains(no_such_file_or_directory)`
   would make the case say what both diagnoses are, not just that they differ.

7. **`src/core/platform/windows/WindowsEnvironmentProvider.cpp:63, 71` — `std::ignore` discards the
   writer's `std::expected`.** The comments are right that `EnvironmentProvider` has no error
   channel, and the old `SetEnvironmentVariableA` was equally silent, so this is not a regression —
   but the interface is the thing that should change eventually, and a `// TODO` or an issue
   reference would keep that visible.

8. **The recase path has no test on the platforms it exists for.** `FileSystem_test.cpp:493-533` is
   `#ifndef _WIN32` and SKIPs on a case-insensitive volume, so `renameViaTemporary()` — written for
   Windows and macOS — is exercised only on Linux, where `isCaseOnlyRename()` still routes through
   it. Acceptable (the logic is platform-independent and the comment says so), but worth a line in
   the report rather than silence.

### Assessment

**Task quality:** Approved.

All fifteen findings are genuinely fixed against the failure scenario, not just against the test:
the three memory-safety items hold up under a walk of the code (the fake's write paths never
dereference a stale get area, `setWakeup(nullptr)` now returns with nothing inside `signal()`, and
`restore()` clears the wakeup on every platform), finding 4 delegates rather than re-fixes as the
finding asked, finding 14 leaves no `bool` overload behind and is recorded under Breaking with a
usable migration, and the macOS skip detects case-insensitivity at runtime while the case still
asserts in full where it can run. The two Important items are incomplete-fix and pre-existing-hazard
follow-ups in the test double rather than defects introduced here — neither is a regression and
neither blocks the gate — but the first should become a ticket before anyone relies on
`InMemoryFileSystem` with non-ASCII paths on Windows.
