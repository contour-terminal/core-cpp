# Task A10 report: Phase A gate, pass 2

Fifteen defects: thirteen in `core::platform` (including its `testing/` doubles) and two in
`core::testing`'s environment helper. None turned out to be in `core::Generator` itself — the
generator only appears here as `FileSystem::walkDirectoryRecursive()`'s return type, which no
finding touched.

Each defect was reproduced by a test that fails before the fix and passes after it, except where
the "Test" column says otherwise and explains why.

Branch `master`, seventeen commits interleaved with the other agents' on the same branch — the
last three being the two fix-forwards and ruling R53's seam:

```
ed1e9fe platform: the FileSystem interface names its two modes instead of spelling them bool
b5fb155 platform: the model's read-write stream stops handing out stale pointers
3789935 platform: MessageQueue guards its wakeup pointer like every other member
8c471af platform: SignalHandler::restore deregisters the interrupt wakeup too
8c599c5 platform: the Windows EnvironmentProvider reads the environment through core::LiveEnvironment
d317971 testing: setTestEnv sets an empty value instead of removing the variable
5e7b681 platform: the Windows SystemPipe never stalls a producer, and never truncates a count
e560751 platform: isExecutableFile classifies a symlink by what it points at
36d8bc2 platform: a glob's bracket arm is reached for the character it exists to match
090ddd1 platform: the path helpers keep a spelling the ANSI code page cannot hold
1b26450 platform: NativeFileSystem reports the failure it actually had
f010ff0 platform: TestEnvironmentProvider lives in the namespace its directory names
8c2f1f2 docs: the changelog and the provenance table record the gate's second pass
46df80c platform: the gate's fixes pass the pinned clang-tidy
3cdb0ce test(platform): the recase case skips where two spellings cannot both exist
cc27c2a test(platform): the two-hop recase has a case that runs on every volume
a518402 platform: NativeFileSystem takes its rename primitive, so the two-hop recase is testable
```

(`1ab4f67`, with a near-identical subject, is the other agent's pass-1 changelog commit, not mine.)

## RED/GREEN per finding

| # | What | Test (RED before, GREEN after) | Where |
|---|---|---|---|
| 1 | `MemoryIOBuf` use-after-free, and writes that always append | `the model's read-write stream survives a write that reallocates the file` — **AddressSanitizer `heap-use-after-free`, READ of size 1**, plus `CHECK((*stream)->gcount() == 0)` failing `1 == 0` without a sanitizer; and `the write streams answer alike in the model and on the real filesystem`, which runs one script over `InMemoryFileSystem` and `NativeFileSystem` and compares | `FileSystem_test.cpp` |
| 2 | `MessageQueue::_wakeup` data race | `MessageQueue.setWakeup_is_guarded_like_every_other_member` — **ThreadSanitizer `data race`, read of size 8 by T3 / previous write by main thread** | `MessageQueue_test.cpp` |
| 3 | `SignalHandler::restore()` leaves a dangling `Wakeup*` | `SignalHandler::restore deregisters the interrupt wakeup` — `CHECK_FALSE(isSignalled(wakeup))` failed `!true` | `SignalHandler_test.cpp` |
| 4 | Windows `EnvironmentProvider::get()` reports an empty variable as unset | `the native EnvironmentProvider reads an empty variable as set, not as absent` — on Windows, `CHECK(provider->get(Name) == "")` failed `{?} == ""` | `EnvironmentProvider_test.cpp` |
| 5 | `setTestEnv(name, "")` deletes the variable on Windows | `setTestEnv sets an empty value rather than removing the variable` and `ScopedEnv restores a previous value that was empty` — both red on Windows | `EnvHelper_test.cpp` |
| 6 | Windows write socket stays blocking; `WSAEWOULDBLOCK` reported as `IoError` | `SystemPipe reports a write into a full channel as done`, now cross-platform and on a thread with a bounded wait — on Windows `CHECK(producerReturned)` failed `false` after 10 s (the producer was parked in `send()`) | `SystemPipe_test.cpp` |
| 7 | `write()` casts the byte count to `int` with no clamp | **Compile-time only**: five `static_assert`s on `clampToTransferSize` (0, 1, `INT_MAX`, `INT_MAX+1`, `SIZE_MAX`). A runtime case needs a buffer over 2 GiB, which no test may allocate. | `SystemPipe.cpp` |
| 8 | `makeLoopbackPair()` accepts whoever connects | **Guard, not a red**: `SystemPipe's two ends are connected to each other` asserts the property the fix guarantees, and passes before it too. Provoking the race needs an adversary that knows the ephemeral port between the `listen()` and the `accept()`; nothing in-process can learn it, and guessing it is not a test. | `SystemPipe_test.cpp` |
| 9 | A symlink to a directory reported executable | `isExecutableFile classifies a symlink by what it points at` — `CHECK_FALSE(backend.isExecutableFile(dir / "to-dir"))` failed `!true` | `FileSystem_test.cpp` |
| 10 | The bracket arm unreachable for `[` | Six rows in the table — `[ against [[]` and `[x against [[]x` failed `false == true` | `GlobMatch_test.cpp` |
| 11 | `generic_string()` narrows to the ANSI code page | `stripTrailingSeparator keeps a path the native narrow encoding cannot spell`, `isCaseOnlyRename compares paths the native narrow encoding cannot spell`, `the model tells apart two paths the native narrow encoding cannot` — on Windows all three **threw**: `No mapping for the Unicode character exists in the target multi-byte code page` | `PathUtils_test.cpp`, `FileSystem_test.cpp` |
| 12 | `createDirectory()` says "No such file or directory" for an existing directory | `createDirectory says which of the two reasons it failed for` — `CHECK(again.error().contains(file_exists.message()))` failed | `FileSystem_test.cpp` |
| 13 | `rename()` throws away `recaseError` | `rename reports why the recase failed, not why the first attempt did` — reported "Directory not empty" where the retry had failed with "File name too long" | `FileSystem_test.cpp` |
| 14 | `bool` parameters in the public `FileSystem` interface | Compilation: the new cases name `WriteMode::Truncate`/`WriteMode::Append` at the call site, and the whole tree plus the consumer smoke projects build | `FileSystem.hpp` and its two implementations |
| 15 | `TestEnvironmentProvider`'s namespace | Compilation: `EnvironmentProvider_test.cpp` now spells `using core::platform::testing::TestEnvironmentProvider;`, which does not compile before the move | `TestEnvironmentProvider.hpp` |

### How each was fixed

1. `MemoryIOBuf` carries one position for reading and writing, as `std::filebuf` does; it
   overwrites from where the stream stands, extends only past the end, re-establishes the get area
   after every write and seek, and implements `seekoff()`/`seekpos()`. `MemoryOutputBuf`'s
   `_writePos`, which nothing read, is gone.
2. `setWakeup()` takes the queue's mutex, and `push()`/`shutdown()` signal *under* it. Reading the
   pointer under the lock and signalling after it would still leave the window the finding
   describes; this way, once `setWakeup(nullptr)` returns, nothing is inside `signal()`.
3. `restore()` stores `nullptr` into `interruptWakeup`, next to the line that clears the callback.
4. `get()`, `unset()` and `exportVariable()` delegate to `core::LiveEnvironment`,
   `core::unsetProcessEnvironmentVariable()` and `core::setProcessEnvironmentVariable()`, as the
   POSIX provider already did — which is why that one never drifted. The 32 KiB-per-lookup
   allocation goes with it.
5. On Windows an empty value is written to the Win32 block through
   `core::setProcessEnvironmentVariable()` after `_putenv_s()`, and `unsetTestEnv()` removes it from
   there explicitly (`_putenv_s` only reaches the Win32 block for a variable the CRT's copy knows).
   `EnvHelper.hpp` is a public header, so it cannot include `<windows.h>`; the project's own writer
   is the seam.
6. The write socket is put into non-blocking mode with `ioctlsocket(FIONBIO)`, and `write()`
   answers `WSAEWOULDBLOCK` as done, the way the POSIX branch answers `EAGAIN`.
7. One `clampToTransferSize()` for both directions.
8. `areConnectedToEachOther()` compares `getsockname`/`getpeername` in both directions;
   `makeLoopbackPair()` retries up to 16 times, each on a fresh ephemeral port.
9. The followed `fs::status()` decides on its own: a regular file (through a symlink or not) with an
   execute bit. A directory, a symlink to one, and a dangling symlink are all false.
10. The `[` arm is tested before the literal one, and only when a `]` closes the expression — so an
    unterminated `[` stays the literal `[` that `fnmatch(3)` reads it as. A bare swap, as the
    finding suggested, would have turned that into a failed match.
11. `stripTrailingSeparator()` and `isCaseOnlyRename()` go through `normalizePath()`
    (`generic_u8string`), and `isCaseOnlyRename()` splits the normalized strings on the last `/`
    rather than rebuilding a `std::filesystem::path`, which on Windows reads a narrow string back in
    the ANSI code page. `createTempFile()` is `wchar_t` end to end on Windows (`_wmktemp_s`), and the
    prefix goes in as UTF-8 on every platform.
12. `create_directory()` answering false with no error becomes `std::errc::file_exists`.
13. `renameViaTemporary()` returns a `RecaseResult` (renamed / error / stranded path), `rename()`
    reports that error, and where the rollback failed too it names the `<name>.recase-N` the entry
    was left under. The temporary name is built in UTF-8, not through `path::string()`: a mangled
    candidate would rename the entry to a name nobody asked for, not merely misreport one.

## Finding 14: the names chosen

```cpp
enum class WriteMode : std::uint8_t       { Truncate, Append };
enum class OverwritePolicy : std::uint8_t { Refuse, Replace };
```

`openWrite(path, WriteMode = WriteMode::Truncate)` and
`copyFile(from, to, OverwritePolicy = OverwritePolicy::Refuse)`, mirrored through
`NativeFileSystem` and `InMemoryFileSystem`. `Overwrite { No, Yes }`, which the task offered, was
rejected: `No`/`Yes` name the representation again, which is the thing the rule is about.
`OverwritePolicy::Replace` says what happens to the destination.

The defaults match the old `false`, so no existing call site needs an edit; only an implementation
of the interface outside core-cpp does. Both are in `CHANGELOG.md` under **Breaking** with the
migration.

## Finding 15: the hygiene rule has a hole

`tests/cmake/check-cmake-hygiene.cmake` does carry a `namespace-directory` rule, and it did not
catch this. Its expected namespace comes from the **first** directory segment under `src/core/`
only:

```cmake
if(kind STREQUAL "cpp" AND path MATCHES "^src/core/([^/]+)/")
    set(expectedNamespace "core::${CMAKE_MATCH_1}")
```

and it then accepts that namespace *or anything nested in it*. So every file below
`src/core/platform/` is held to `core::platform` or a descendant, and a file in
`platform/testing/` declaring `core::platform` passes. The rule is one level deep where the
tree is not.

Not widened here, as instructed. Widening it means deriving the expected namespace from the whole
relative directory and deciding what to do about `src/core/Base64.hpp` → `core::base64`, which is a
namespace the directory does *not* name and which the rule allows today by the same
nesting clause. That is a change of its own, with its own self-test rows.

## The recase path's coverage, on the volumes it is written for

Asked by the controller after the macOS failure, and worth recording: the two-hop recase existed
with **no test that could run on a case-insensitive volume at all**, which is the only kind of
volume it was written for.

- `isCaseOnlyRename()`'s cases in `PathUtils_test.cpp` are purely lexical — no filesystem, so no
  assumption about the volume. They run everywhere and always did.
- `NativeFileSystem::rename()`'s recase path had exactly one case, mine for finding 13, and it
  reaches the branch *artificially*: on a case-sensitive volume it makes the direct rename fail
  over a non-empty destination. That scenario cannot be built on APFS, which is what the macOS
  jobs showed.

So the success path of `renameViaTemporary()` — a `foo` → `Foo` rename actually taking effect —
was untested on every platform. `cc27c2a` adds `rename changes the lettercase of a name, whatever
the volume`, which runs on both kinds: on a case-sensitive volume the direct rename does the work,
on a case-insensitive one whichever path the OS forces, and either way the entry must end up
spelled with the capital, exactly once, with its content intact. The "exactly once" also catches a
temporary stranded under `<name>.recase-N`.

One honest limit remained after that: on ext4, APFS, NTFS and UFS the OS performs a case-only
rename natively, so even there the direct `fs::rename` succeeds and the two-hop implementation
stays unexercised. It is reachable only on volumes that refuse such a rename (FAT, some SMB
mounts), which no machine this project builds on has.

**Ruling R53** closed that: inject the rename primitive and test the implementation. Done in
`a518402`.

### The seam (ruling R53)

`NativeFileSystem` now takes its rename primitive at construction:

```cpp
using RenameFunction = std::function<void(std::filesystem::path const& from,
                                          std::filesystem::path const& to, std::error_code& ec)>;
[[nodiscard]] RenameFunction nativeRename();

explicit NativeFileSystem(RenameFunction rename = nativeRename());
```

It stayed inside the "modest" bound you set: no new virtual, no change to the `FileSystem`
interface, and no source break — `instance()` is the only construction site in the tree, and the
defaulted constructor keeps `NativeFileSystem {}` working. The header gains `<functional>`, one
`using`, one factory, one constructor and one private member. Recorded under **Added**, not
Breaking.

Three cases drive it over a `ScriptedRename` (a table of per-call outcomes: an error to report, or
`nullopt` to perform the rename for real; state shared through a `shared_ptr` because a
`std::function` copies its callable):

| Case | Script | Asserts |
|---|---|---|
| `the recase retry renames when the volume refuses a direct case-only rename` | refuse, then real | success; exactly three calls (the refused attempt plus both hops); one entry left, spelled `Foo`, content intact |
| `the recase retry reports the second hop's reason and rolls back` | refuse, real, second hop fails | the message carries the second hop's reason and **not** the direct attempt's; no `.recase-` in it; the entry is back under `foo`, intact |
| `a recase whose rollback also fails says where the entry is` | refuse, real, second hop fails, rollback fails | the message names both spellings and the temporary; and the entry really is at that temporary, with its content |

The middle case is finding 13 driven through the path that produces it rather than around it, and
it is genuinely discriminating: reverting the finding-13 fix locally (`recase.error.message()` →
`ec.message()`) turns it red on both assertions, reporting `File exists` (the direct attempt) in
place of `Permission denied` (the second hop). Restored and re-verified green.

The volume-independent case from `cc27c2a` stays as well: it pins the contract on real
filesystems, where the scripted one pins the implementation.

## clang-tidy in `src/core/platform/`

impl-A9's report of about ten findings in this module is cleared by `46df80c`: `performance-enum-size`
on the two new enums, `readability-suspicious-call-argument` on the rollback's arguments,
`readability-avoid-nested-conditional-operator` and `modernize-return-braced-init-list` on the new
`seekoff()` — which is roughly that many diagnostics once counted per translation unit. Re-checked
at the end by touching all 25 platform and `platform/testing` sources and re-running the pinned
`clang-tidy` preset over them: zero findings. (A full-tree tidy run currently stops earlier, in
`src/core/async/WhenAny.hpp`, which is another session's work in flight.)

## Deliberately left

- **Error messages across `NativeFileSystem.cpp` still use `path.string()`** (about twenty sites).
  That is the same narrowing as finding 11, and on Windows MSVC *throws* on a path the ANSI code
  page cannot spell — inside a function whose contract is to return the reason as a value. I fixed
  it only where the finding named it, and where a mangled string would drive a real filesystem
  operation (`renameViaTemporary`'s candidate name). Changing the rest is mechanical
  (`path.string()` → `normalizePath(path)`) but it changes every error message on Windows from
  backslashes to forward slashes, which consumers may assert on. Worth its own ticket.
- **`InMemoryFileSystem::isExecutableFile()` still reads the *link's* own permissions** and does not
  resolve symlinks, so it answers `true` for a dangling symlink carrying an execute bit, where
  `NativeFileSystem` now answers `false`. An existing case (`isExecutableFile follows the execute
  bit of a symlink entry`) asserts the model's behaviour, so changing it is a decision about what
  the double should model, not a defect on the list.
- **`InMemoryFileSystem::createDirectory()` succeeds for a directory that already exists**, where
  `NativeFileSystem` now reports `File exists`. Finding 12 was about the *message*, so I did not
  change either backend's success/failure answer; the two differ, and which one is right is a
  decision.
- **Windows sockets are still inheritable.** The POSIX `SystemPipe` ends are `FD_CLOEXEC`; the
  Windows ones are created with `socket()`, which produces an inheritable handle. Not on the list,
  and the fix (`WSASocketW` with `WSA_FLAG_NO_HANDLE_INHERIT`) touches the creation path finding 8
  already rewrote — I did not want to fold an unreviewed change into it.
- **The `_values` map of `WindowsEnvironmentProvider` is still case-insensitive while the process
  block is matched case-insensitively by Win32 itself.** Unchanged behaviour, no finding.

## Not mine, observed

- `src/core/log/LogSink.cpp` was in a non-compiling state in this shared checkout for a while
  (`LogSink.cpp(245): error C3861: 'isStdErrTty': identifier not found`), so I built only my own
  targets on Windows until it was fixed. It is fine now; reported rather than touched.
- One ASan run of the full suite failed a single test once, immediately after a rebuild while the
  other agent's `Utils.hpp`/`LogSink.cpp` were being edited in the tree. Three further runs were
  clean, and it was not in `core-cpp.platform`. Recorded, not chased.
- `src/core/log/posix/TerminalQuery.cpp` is being added without a row in
  `.agent/reference/provenance.md`, which `ctest -L hygiene` refuses by name. Whoever is adding it
  will hit it themselves; noting it here in case they push first.
- Commit `1556119` ("ioctlsocket takes the command signed, as clang-cl insists") amends my
  `SystemPipe.cpp` from another session, because `5e7b681` broke `clangcl-release`: `FIONBIO` is an
  unsigned constant and `ioctlsocket` takes a signed command, which `-Wsign-conversion` rejects
  under clang-cl and MSVC does not warn about at all. It is correct and I left it. My mistake was
  verifying that commit on `cl-debug` only; from `090ddd1` on I built `clangcl-release` too, and
  every push since has been checked on both Windows compilers.
- The repository's working-tree copies of `CHANGELOG.md` and `.agent/reference/provenance.md`
  pick up CRLF whenever a tool rewrites them on Windows. The committed blobs are LF
  (`.gitattributes` normalizes), so nothing is wrong, but a Python `write_text()` in this tree
  produces a working copy git reports as touched until it is checked out again.

## Verification

Local, all green:

| Preset | Result |
|---|---|
| `clang-debug` (WSL) | 12/12 |
| `gcc-release` (WSL) | 12/12 |
| `clang-asan-ubsan` (WSL) | 12/12, four consecutive runs |
| `clang-tsan` (WSL) | 12/12 |
| `clang-tidy` (WSL) | clean |
| `cl-debug` (Windows) | 14/14 |
| `clangcl-release` (Windows, `--clean-first`) | 14/14 |
| `ctest -L hygiene` | 7/7, provenance included |
| `python scripts/clang-format.py --check` | 359 files formatted |
| `mkdocs build --strict` | built |

Twelve new cases in `core-cpp.platform` and two in `core-cpp.testing`, plus six rows in the glob
table, and the full-channel `SystemPipe` case now runs on Windows as well as POSIX. The platform
suite ends at 147 cases on Linux and 127 on Windows.

## CI

| Run | Workflow | Commit | Result |
|---|---|---|---|
| `35524698669` | Build | `a518402` (final) | watched |
| `35524002244` | Portability (dispatched) | `cc27c2a` | **success** (FreeBSD, system clang) |
| `35523953911` | Build | `cc27c2a` | **success** |
| `35522418404` | Build | `3cdb0ce` | **success**, all 24 jobs |
| `35523249853` | Portability (dispatched) | `3cdb0ce` | **success** (FreeBSD, system clang, 5m55s) |
| `35522267147` | Portability (dispatched) | `46df80c` | success (FreeBSD, system clang) |
| `35521713530` | Docs | `46df80c` | success |
| `35521713519` | Build | `46df80c` | **failure**, `macos (appleclang)` and `macos (llvm-22)` only |
| `35521290104` | Build | `1556119` | failure: `clang-tidy` and the same two macOS jobs |

Two CI failures, both mine, both fixed forward:

- `35521290104` — `clang-tidy` on `performance-enum-size` (the two new enums),
  `readability-suspicious-call-argument` (the rollback's arguments),
  `readability-avoid-nested-conditional-operator` and `modernize-return-braced-init-list` (the new
  `seekoff()`). Fixed in `46df80c`; the same findings had already appeared in my local `clang-tidy`
  preset run and I pushed before finishing it.
- `35521713519` — the finding-13 recase case on macOS, whose default volume format is
  case-insensitive, so the two directories the case needs cannot both exist and the second
  `createDirectory()` answered `File exists` (correctly, since my own finding-12 fix). Fixed in
  `3cdb0ce` with a `SKIP` that names the reason.

The final Build matrix, all green: `linux (clang-22, clang-22-arm64, clang-22-cxx26,
clang-22-tracy, gcc-14, gcc-15)`, `macos (appleclang, llvm-22)`, `windows (cl-debug, cl-release,
cl-release-tls, clangcl-release)`, `sanitizers (clang-asan-ubsan, clang-tsan)`, `emscripten (emsdk
3.1.56, emsdk latest)`, `consumer-smoke (cpm, vendored, wasm)`, `clang-tidy`, `style`, `coverage`,
`compile-cache`, `ci-ok`.

### One caveat on the last local run

A full local `ctest --preset cl-debug` at the very end reported `core-cpp.cli` (timeout, 300 s) and
`core-cpp.cmake-hygiene` (`src/core/log/posix/TerminalQuery.cpp` has no provenance row) as failing.
Neither is mine: both come from the other agent's work in flight in this shared checkout at that
moment — `TerminalQuery.cpp` was still untracked, alongside uncommitted edits to `Base64.hpp`,
`Escape.hpp`, `Times.hpp`, `Utils.hpp`, `cli/CLI.cpp` and `log/*`. CI on `3cdb0ce`, which carries my
work without those edits, is green including both `core-cpp.cli` and the hygiene checks.

---

# Fix round 1 (Ruling R54)

Two Important items from the review, both in `InMemoryFileSystem`, both the same shape: the fix
went in through one door and another was left open. Ruling R53's rename seam is above, in
`a518402`, and shipped before these.

## RED/GREEN

| Item | Test | RED before | GREEN after |
|---|---|---|---|
| 1. Keys round-trip | `the model's listings round-trip a name the narrow encoding cannot spell` | **Windows, `cl-debug`: 8 of 11 assertions fail**, throwing `No mapping for the Unicode character exists in the target multi-byte code page` out of `listDirectory()`, the walk and `weaklyCanonical()` | 136 cases pass on `cl-debug` and `clangcl-release` |
| 2a. Write under an open stream | `a write through the model reaches a stream open on the same file` | **ASan `heap-use-after-free`, READ of size 1** | passes |
| 2b. Remove under an open stream | `a stream outlives a remove of the file it was opened on` | **ASan `heap-use-after-free`, READ of size 1** | passes |
| 2c. Rename under an open stream | `a stream follows the file across a rename` | **ASan `heap-use-after-free`, READ of size 1** | passes |

Each red was produced by restoring the previous `InMemoryFileSystem.{cpp,hpp}` from git under the
new tests, so the tests are the only difference.

## Item 1 — one way in, one way out

`normalizePath()` was already the one way *into* the key space. `pathFromKey()` is now the one way
out: it builds the path from a `std::u8string_view`, which is UTF-8 by definition, instead of
`std::filesystem::path`'s narrow constructor, which reads the platform's native narrow encoding.

Routed: the three parent-walk loops (`ensureParentDirectories`, `createDirectories`,
`addDirectory`, which also emitted keys through `path::string()`), `createDirectory`'s parent
check, `weaklyCanonical`, the three `DirectoryEntry` constructions in `listDirectory` and the
three in `walkDirectoryRecursive`, `createTempFile` and `setCurrentPath`.

**Two sites beyond the ten listed**, same defect, found while routing the rest: `addSymlink()`
stored its target through `path::string()` (narrowed on the way *in*), and `resolveSymlinks()`
converted it back implicitly (narrowed on the way out) — so a symlink to a non-ASCII name
resolved to nothing on Windows. `FileEntry::content` for a symlink entry went the same way. Fixing
only the ten would have left the listing case red for a link. The test covers it.

## Item 2 — ownership made real, as instructed

I agree with the ruling and did not take the documented-contract option. `_files` is now
`std::map<std::string, std::shared_ptr<std::string>>`, and the streams hold their own reference.

That alone fixes `remove()` and `rename()` — and it makes the fake *more* faithful rather than
less: an open stream now survives an unlink and follows a rename, which is what a POSIX file
descriptor does, because a rename moves the name and not the contents. One `fileAt()` helper is
the only place a file's storage is created, and it never replaces the string a key already has,
so a writer cannot detach a stream that is already open.

Shared ownership alone does **not** fix the write-under-an-open-stream case: `writeFile()`
assigns through the shared string, which can still reallocate it, and the read-write buffer's get
area pointed inside that string. So `MemoryIOBuf` now caches no pointer into the file at all — it
keeps an integer position, leaves the get area empty, and serves every read through
`underflow()`, `uflow()` or `xsgetn()`, re-reading where the data is now. A get area spanning the
string is read directly by `sgetc()`/`sbumpc()` without entering the class, so a reallocation
between two reads could not otherwise be noticed. `showmanyc()` is overridden to match.

`MemoryOutputBuf` needed only the shared pointer: it appends to the string and holds no pointer
*into* it. `MemoryInputBuf` already owned a copy and was never at risk.

## One thing I did not change, and why

`openRead()` still snapshots the file into the stream, so a read-only stream does not see writes
made after it was opened — whereas `openReadWrite()` now does. On a real filesystem both see
them. Making the read stream share the storage too is a two-line change, but it alters observable
behaviour for every consumer whose tests read through this fake, and neither review item asked
for it. That is a semantics decision rather than a defect, so it is reported rather than taken.

## One scare, and what it was

A `gcc-release` run of the platform suite aborted with glibc's `double free or corruption (out)`
and SIGABRT, while `clang-debug`, `clang-release`, `clang-asan-ubsan`, `clang-tsan`, `cl-debug`
and `clangcl-release` were all green on the same sources. Chased it down rather than shipping
past it:

- the parent commit was green under `gcc-release`, so it was mine;
- every `[FileSystem]` case passed on its own, and the whole tag passed under `--order decl`;
- a `pathFromKey()` reproducer was clean under `g++-14` at `-O0` and `-O2`;
- a **from-scratch** rebuild of the `gcc-release` platform tree passed: 542 assertions, 155 cases.

It was a mixed-object build of my own making. Demonstrating the reds meant swapping
`InMemoryFileSystem.{cpp,hpp}` between the old and new versions in a shared checkout, and this
round changes the class's layout (`_files`' mapped type, plus a new private member). An object
compiled against one version and linked against the other is exactly a `double free or
corruption`. Worth recording as a hazard: a RED demonstration by file swapping is safe for a
behaviour change, but for a layout change the tree has to be rebuilt from scratch afterwards, and
a compiler cache makes the stale object easy to miss.

## Verified

`clang-debug`, `clang-release`, `clang-asan-ubsan`, `clang-tsan` and a from-scratch `gcc-release`
(155 cases), `cl-debug` and `clangcl-release` (136 cases), `clang-tidy` clean over the module,
`python scripts/clang-format.py --check` (363 files), `mkdocs build --strict`.

`ctest -L hygiene` fails on three `src/core/log/**/ProcessId.*` files that have no provenance row.
Those are another session's, untracked in the tree at the time, and none of my files is named —
the same shape as the `TerminalQuery.cpp` note above.

Commit `ab623bc`, on `origin/master` (pushed as part of another session's push from the shared
checkout). CI: Build `35528458513` on `f28462f`, which contains it.

---

# Fix round 1, part 2 (Ruling R56): `openRead()` shares too

The one thing I had left as a reported divergence. The ruling overrode it, and I agree with the
reasoning: the fake's own write streams had just been made to survive a remove and follow a
rename, so a read stream frozen at a snapshot was the odd one out inside one class.

| Item | Test | RED before | GREEN after |
|---|---|---|---|
| `openRead()` reads live | `a read stream sees a write that lands after it was opened` | `CHECK(byte == 'b')` fails `'a' == 'b'`, and the seek past the old end fails — the stream is still holding the content the file had when it was opened | 156 cases pass |

The red was produced by restoring snapshot semantics exactly — handing the stream
`std::make_shared<std::string>(*it->second)`, a private copy — with nothing else changed.

**How.** The read half of the buffer became its own class, `MemoryReadBuf`, which `MemoryIOBuf`
now derives from rather than duplicating: it holds the file's storage as a `shared_ptr`, keeps an
integer position and caches no pointer into the string, so every read re-reads where the data is.
`MemoryIStream` uses it directly; the old `MemoryInputBuf`, which owned a copy, is gone. So a read
stream sees a write, sees an append and walks into it, and outlives a remove of its name — all
asserted in the one case.

It is its own commit (`9f7925c`), touching only `InMemoryFileSystem.cpp` and the test plus their
changelog and provenance lines, so it reverts alone if a consumer turns out to depend on the
snapshot.

## Issue 27 and the rulebook

- [core-cpp#27](https://github.com/contour-terminal/core-cpp/issues/27) now carries a divergence
  list in two halves: what the fake models like the native backend (shared content, survives
  `remove()`, follows `rename()`, `openRead()` live, in-place `openReadWrite()`, UTF-8 keys both
  ways) and what it still does not (`isExecutableFile()` on a dangling symlink,
  `createDirectory()` on an existing directory, symlink resolution generally, directory
  semantics). The last two were added so the table is the whole picture rather than half of it;
  the first two remain the issue's original, undecided scope.
- `.agent/rules/testing.md` gains **"Showing a red by swapping files is safe for behaviour, not
  for layout"** (`1240035`), with the `double free` episode as its origin and the tell to
  recognise it by: a failure only one toolchain sees, in code the diff did not touch, that moves
  or vanishes when cases are run individually.

## Verified (R56)

From-scratch `gcc-release` (my own new rule applied), `clang-debug`, `clang-asan-ubsan`,
`clang-tsan` — 552 assertions in 156 cases each; `cl-debug` and `clangcl-release` — 468 in 137;
`clang-format --check` over 366 files; `mkdocs build --strict`.

---

# Fix round 2 (Ruling R58) — the last one

One Important, which the previous round introduced, and five Minor.

## Item 1 (Important) — `unget()` and `putback()` set `badbit`

The irony is exact: leaving the get area empty is *why* a read stream can notice a file that
changed under it, and it is also why `std::streambuf` can never satisfy a put-back itself. Every
`unget()` and `putback()` reached `pbackfail()`, whose default refuses — so the round whose
purpose was fidelity gave every stream the fake hands out a `badbit` where `std::ifstream` and
`std::fstream` succeed.

**What the real streams do**, since the ruling asked me to decide and match rather than document.
A standalone probe under `g++-14`/libstdc++ answered all three cases:

```
ifstream: read 'a' | unget good=1 then read 'a' | putback(same) good=1 then read 'a' | putback('X') good=1 then read 'X'
fstream : read 'a' | unget good=1 then read 'a' | putback(same) good=1 then read 'a' | putback('X') good=1 then read 'X'
file after: abcdef
```

So a put-back of a *different* character succeeds, the next read returns it, and **the file is
unchanged** — `std::filebuf` keeps such a character in a one-character slot of its own
(`_M_pback`) rather than writing it. The MSVC-built probe would not run standalone (it died with
`STATUS_STACK_BUFFER_OVERRUN` before producing output), so I settled that half the way this file
already settles such questions: the case runs one script over **both** backends and compares, and
it passes on `cl-debug` and `clangcl-release` — which is MSVC answering the same as libstdc++,
through the project's own working build rather than a probe I could not get to run.

**And that was wrong of me** (ruling R59). libc++ *refuses* a put-back of a character the file
does not hold, so the case was red on macOS and FreeBSD. The standard is with libc++
([streambuf.virt.pback]): one put-back is guaranteed at all, and a different character need not
be accepted. I had measured two implementations and written their agreement down as a contract.

The lesson, which is the general one: **a probe of two implementations measures those two, not
the standard.** Where a test states behaviour as a contract, the statement has to come from what
is guaranteed; where the standard leaves it open, there is nothing to compare a fake against, and
asserting the native answer only encodes whichever library happened to run. The case now compares
model against native for `unget()` and `putback()` of the character just read — satisfied out of
the get area, answered alike everywhere — and states the model's own answer for the
different-character case, which stays permissive because a memory buffer with an exact position
can always satisfy a put-back. core-cpp#27 carries it as a divergence, naming which libraries
differ.

**The fix.** `pbackfail()` with a one-character slot, matching filebuf: `unget()` steps back;
`putback(c)` where the file holds `c` there needs no slot; `putback(c)` where it does not fills
the slot, so the next read returns `c` and the file still reads as it did. A second put-back of a
character the file does not hold is refused, as filebuf's single slot refuses it — but refused
*before* moving the position, where filebuf leaves it moved on a failure the standard does not
describe. A seek or a write through the stream discards a pending put-back. `underflow()`,
`uflow()` and `xsgetn()` all deliver the slot first.

| Test | RED before | GREEN after |
|---|---|---|
| `unget and putback answer alike in the model and on the real filesystem` | with `pbackfail()` disabled: the assertions about the *native* backend pass and every model-against-native comparison fails — native succeeds, the model at none | 157 cases pass on every preset, on libc++ as well after R59 |

The case states the real streams' answers outright as well as comparing, so two backends wrong in
the same way could not agree and pass, and it compares field by field so a regression names which
answer moved.

## The five Minor

| # | What | Done |
|---|---|---|
| 2 | The walk's sort key still converted out of a path narrowly — the one conversion the key-space sweep missed | `normalizePath(e.path)` |
| 3 | `copyFile()` and `addFile()` replaced a key's string, detaching an open stream and falsifying what `fileAt()` promises | routed through `fileAt()`, as preferred — so `copyFile()` onto an open destination now overwrites in place, which is also what the native backend does. `createTempFile()` too. The two remaining `_files[...] = std::move(...)` are `rename()`'s, which *move* the existing string and so preserve identity deliberately: that is POSIX rename, and the destination's old content surviving for whoever holds it open is `unlink`-on-overwrite |
| 4 | Three behaviour changes filed under `Added` | the two fixes moved to **Fixed**, and the lifetime change — streams surviving `remove()`, following `rename()`, `openRead()` reading live, `copyFile()` overwriting in place — collected into one **Breaking** entry with the migration a consumer needs |
| 5 | A formally out-of-range pointer in `xsgetn`, and a stale `<sstream>` | the copy is guarded so the pointer is not formed when there is nothing to copy; `<sstream>` replaced by the `<istream>`/`<ostream>`/`<optional>` this file actually uses |

## Issue 27

Two rows added to the "modelled" half: `unget()`/`putback()` including a character the file does
not hold, and `copyFile()` onto an open destination. The undecided half is unchanged.

---

# Fix round 3 (Ruling R61) — closing A10

Three leftovers.

## 1. The changelog was the last place still telling the refuted story

The code, the test and core-cpp#27 were all corrected when libc++ refuted my claim; the changelog
entry was not, so it was the one document still telling a consumer that real streams accept a
`putback()` of a character the file does not hold, and that the fake matches them. It now says
what the standard guarantees, names which libraries differ (libstdc++ and MSVC accept, libc++
refuses), and says the fake is deliberately the permissive one, pointing at core-cpp#27.

Worth naming the pattern, because it is the second time in this task: **a correction is not
finished until every document that carried the wrong version carries the right one.** The fix, the
test and the issue were all updated in the same hour; the changelog was written earlier and not
re-read, and that is exactly why it was invisible — it had been written before the question arose.
When a claim changes, grep the tree and the issues for *the claim*, not for the file you were
editing.

This report is gitignored and task-scoped, so it is not in front of the next task. The sentence
is therefore also a rule, under "Public API and versioning" in
[`.agent/rules/library-hygiene.md`](../../../.agent/rules/library-hygiene.md), next to the
requirement that every public change has a CHANGELOG entry — which is the rule it protects.

## 2. `copyFile()` had no test in either backend

| Test | RED before | GREEN after |
|---|---|---|
| `copyFile answers alike in the model and on the real filesystem` | — (new coverage; both backends already agreed) | passes on every preset |
| `the model copies onto an open destination in place` | `CHECK(byte == 'n')` fails `'o' == 'n'` with `copyFile()` restored to replacing the key's string: the stream is detached and still reading the old content | passes |

The comparison case covers the three answers `[fs.op.copy.file]` pins down — onto a free name,
refused over an existing one with the destination left untouched, replaced when asked — stated as
well as compared. The in-place case is asserted of the model alone, because `[fs.op.copy.file]`
says the contents are copied, not whether the destination is truncated in place or recreated: the
same reason the put-back case states rather than compares.

## 3. `unget()` after a put-back character has been read — recorded, not matched

**My call: record it.** `std::filebuf` hands the put-back character out again, because it is still
sitting in filebuf's own one-character slot and `sungetc()` simply steps `gptr()` back into it.
Mine clears the slot when the character is read, so `unget()` steps back to the file's own byte.

I did not match it, for the reason the previous round taught me the hard way: nothing pins this
down. The standard describes neither the slot nor what `unget()` does afterwards, and **libc++
refuses the put-back that creates the situation at all** — so there is no portable native answer,
and matching would mean copying libstdc++'s internals and calling it a contract. That is exactly
the mistake R59 corrected. It is recorded at the code site, where someone reading `pbackfail()`
will meet it, and in core-cpp#27's list.

## Verified

`clang-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan` — 590 assertions in 159 cases;
`cl-debug` and `clangcl-release` — 506 in 140; `clang-tidy` clean over the module;
`clang-format --check`; `ctest -L hygiene`; `mkdocs build --strict`.

Commit `ec7c41d`.

## Note for the record

The controller mentions the diff package handed to the re-review was missing its CHANGELOG hunks
and the reviewer regenerated them. Nothing on my side changed as a result — but it is worth
noting that item 1 above, a stale changelog, is precisely the kind of thing a review reading a
diff without changelog hunks would be most likely to miss, and it was caught anyway.
