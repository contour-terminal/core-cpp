# Task A4 re-review, fix round 1

Range: fix base `48aae7b` to head `629629e`. Controller commit `0cf5bf7` is ignored. This was a
read-only review, and I did not re-run any suite. The evidence is CI run 35365981481 on
`629629e51bdc5555e76edfb6bf748e203b1940fe`, which concluded **success** with all 21 jobs,
`ci-ok` included. Among them: both Emscripten legs (`core-cpp.platform` passed under node), macOS,
all Windows legs, TSan, ASan/UBSan and clang-tidy.

### Finding Verdicts

1. **WallClockRef retainer rows: Addressed.**
   - `Clock_test.cpp` adds a test-local `Retainer`, with an explicit constructor that takes and
     stores a `WallClockRef`.
   - It also adds a multi-argument `Forwarder(ManualClock&, WallClockRef, int)`, which hands the
     borrow on to two `Retainer`s.
   - Paired `is_constructible` rows show that an lvalue is accepted and an rvalue is refused
     (`SystemWallClock`, `ManualWallClock`), both into `Retainer` directly and through
     `Forwarder`.
   - For contrast, `OverloadRetainer`/`OverloadForwarder` show that a deleted
     `IWallClock const&&` overload lets a temporary through a forwarding constructor.
   - A runtime case checks that both hops still answer as the borrowed clock.
   - Together these back the claim at `docs/modules/platform.md:60`.

2. **Public native factories: Addressed.**
   - `nativeEnvironmentProvider()` and `nativeFileInfoProvider()` return
     `std::unique_ptr<Interface>`. They are declared in `EnvironmentProvider.hpp` and
     `FileInfoProvider.hpp`, which are both in the `HEADERS` FILE_SET; `posix/` and `windows/`
     are in none.
   - Every platform has a real default:
     - Windows gets `WindowsEnvironmentProvider` and `WindowsFileInfoProvider`.
     - Linux, macOS, the BSDs (`SOURCES_POSIX`) and Emscripten (`SOURCES_EMSCRIPTEN`) share
       `PosixEnvironmentProvider` and `PosixFileInfoProvider`.
   - endo's Linux provider became `posix/PosixFileInfoProvider`. It was already generic lstat(2)
     code and is not a stub. Provenance, CHANGELOG and the migration row record the rename.
   - Tests: each factory has a case that checks the dynamic type and then uses the provider for
     real (set/export/unset, or listing a directory). It runs in every test job. The lstat
     cases now run on Linux, macOS and Emscripten.
   - Docs: the consumer-migration row is present. `docs/modules/platform.md` has a factory
     table, and `.agent/rules/platform.md` updates the subset list.

3. **Zero-argument user paths only through fakes: Addressed.**
   - The zero-argument overloads are gone. `homeDirectory` and `configHome` now take
     `core::Environment const& = core::LiveEnvironment {}`. A zero-argument call therefore runs
     the same body the `FakeEnvironment` cases run.
   - The case that compared against the live environment is removed.
     `static_assert(requires { homeDirectory(); })` and the matching one for `configHome()`
     check that the call exists without making it.
   - Seven `TestEnvironmentProvider` cases also cover `EnvironmentProvider::homeDirectory()` and
     `configHome()`, which had no test before.

4. **"the shell"/"Endo" wording: Addressed.**
   - Every flagged spot was reworded for a general library, and none was simply deleted:
     - `PathUtils.hpp:184`: "A program that accepts POSIX device paths".
     - `PathUtils.hpp:27`: "so a program mirrors", and the example is now `Project-to`.
     - `SignalHandler.hpp:14` and `:114`.
     - `FileSystem.hpp:35`.
     - `ScopedTempDir.hpp`: "a command language that treats a backslash as an escape".
   - The same pass also covered `SignalHandler.cpp`, `SystemInfo.hpp`, `EnvHelper.hpp`,
     `InMemoryFileSystem.hpp`, `WindowsEnvironmentProvider.*` and `FileSystem_test.cpp`.
   - At `629629e`, `git grep -i shell src/` finds only generic uses: "shell glob", `pw_shell`,
     `SHELL`, and an example in a test comment.

5. **`SystemPipe::read` returns `std::expected<ChannelResult, PlatformError>`: Addressed.**
   - `ChannelResult` has three states: empty (the default), `bytes(n)` and `endOfStream()`. The
     end of stream is its own state, not a zero count. `bytes(0)` equals empty, and
     `static_assert` rows pin that.
   - POSIX mapping: n>0 is bytes, 0 is end of stream, EAGAIN/EWOULDBLOCK/EINTR are empty, and
     anything else is `IoError`.
   - Windows mapping is the same, with `WSAEWOULDBLOCK` as empty. The `recv()` length is clamped
     to `INT_MAX`.
   - A zero-byte read returns empty without touching the channel.
   - The contract is written in the class doc and the `read()` doc.
   - Tests on every platform:
     - data;
     - empty, which now runs on Windows too;
     - end of stream via `shutdown` of the write direction: the bytes first, then an end that
       stays;
     - failure: on POSIX a `dup2` of `/dev/null` over the read end gives ENOTSOCK; on Windows
       `SD_RECEIVE` gives WSAESHUTDOWN;
     - the zero-byte read.
   - Nothing in core-cpp calls `read()`. The platform page, migration rows, CHANGELOG and
     provenance are updated.

6. **Environment.cpp polish: Addressed, with a correctness gap in the short-circuit (see New
   Breakage, Minor 1).**
   - An identical write returns before publishing (`Environment.cpp:137`), and a new test pins
     the unchanged `environ` pointer.
   - The `environmentMutex()` comment now says it serialises the writer too.
   - `Environment.hpp:119` says the writer is not for use between `fork()` and `exec()`, and
     `docs/modules/base.md` repeats it.

7. **Emscripten EOF: Addressed.**
   - The `platformRead()` doc in `Types.hpp` and the "Under Emscripten" section of
     `docs/modules/platform.md` describe the -1/EAGAIN behaviour and how code can learn the
     writer is done. The explanation matches `library_pipefs.js`, whose comment reads "behave as
     if the read end is always non-blocking".
   - The test is no longer compiled out. Under `__EMSCRIPTEN__` it checks
     `second == -1 && errno == EAGAIN`, and it passed under node on emsdk 3.1.56 and latest.

8. **Exceptions rule: Addressed.**
   - `cpp-guidelines.md:73-98` now reads: recoverable errors return `std::expected`; exceptions
     are for unrecoverable conditions, plus `core::coro::OperationCancelled`. A function that
     throws says so with `@throws`.
   - It defines unrecoverable: "no caller between the failure and `main()` has a meaningful
     alternative".
   - Its examples are Wakeup's refused channel, `ScopedTempDir` and `ScopedWorkingDirectory`
     when they cannot set up, and `std::bad_alloc`.
   - It lists what is recoverable, ending "when in doubt, it is recoverable". Precondition
     violations stay assertions.
   - `design-principles.md` and `AGENT.md` say the same.
   - Both fixtures already carry `@throws` (`ScopedTempDir.hpp:49`,
     `ScopedWorkingDirectory.hpp:24`).

9. **core-cpp#14 closed: Addressed.**
   - `Wakeup.hpp` documents `@throws std::runtime_error` on the constructor and explains why the
     condition is unrecoverable. `docs/modules/platform.md` says the same and links the rule.
   - At `629629e`, `git grep` over the whole tree finds no `core-cpp/issues/14`, no
     `core-cpp#14` and no bare `#14` outside the fastcached links. The rules' "Open work"
     section and both platform pages are clean.

### New Breakage in the Fix Diff

No Critical or Important issues.

**Minor**

1. **`src/core/Environment.cpp:129`: the identical-write short-circuit checks the last duplicate
   entry, but readers see the first.**
   - The loop reassigns `unchanged` for every entry that matches the name. Readers take the
     first match: `lookupInEnviron()` (`:68-79`, used by `LiveEnvironment`) and `getenv()`.
   - Duplicate entries can be inherited through `execve`. Suppose `environ` holds `X=old` and
     later `X=same`. Then `setProcessEnvironmentVariable("X", "same")` returns success without
     publishing, and `X` still reads `old`.
   - At `48aae7b` the same call removed every duplicate and published `X=same`. This is
     therefore a regression, though an edge case, and no test covers duplicate entries.
   - One-line fix: base `unchanged` on the first match only, and clear it on any later match:
     `unchanged = !removed && value && …; removed = true;`. A second match then forces a
     publish, which removes the duplicates as before.
2. **`.agent/rules/platform.md:94` now contradicts the list the fix extended.**
   - The line still says "Anything else in the module may use threads, sockets and the
     filesystem freely; anything on that list may not."
   - The list now includes `PosixFileInfoProvider` (lstat(2) and `std::filesystem` over
     Emscripten's virtual filesystem).
   - Fix: drop "the filesystem", or name what the subset really forbids: threads, blocking
     waits and sockets.

**Nits**

- `docs/modules/platform.md:46` states without qualification that under Emscripten a relative
  symlink target reads resolved against the link's directory. `FileInfoProvider.hpp:61` hedges
  "(3.1.56 at least)", and the tests accept either form, so the page should hedge the same way.
- `Environment.hpp:130`: the doc of `unsetProcessEnvironmentVariable()` says "with the
  guarantees of `setProcessEnvironmentVariable()`". A fork()/exec() restriction is not a
  guarantee, so a reader of the unset declaration alone may miss it. `base.md` covers both
  functions.

### Out-of-Scope Observations

- `design-principles.md` still cites "the design spec, Part I §2" for the exceptions rule, and
  the spec states the old rule. Concern 2 of the report says the controller is amending it.
- The plan's A4 file list still names `linux/LinuxFileInfoProvider`, which the rename
  superseded. The plan is controller-owned.
- `core::cli` throws `ParserError` for malformed input, which the new rule calls recoverable.
  core-cpp#13 already tracks it.
- The Windows `SystemPipe::write` still uses `static_cast<int>(size)` with no clamp, unlike the
  new `read`. This predates the fix.

### Verdict

**Fix round:** All findings addressed, no new Critical/Important breakage.
