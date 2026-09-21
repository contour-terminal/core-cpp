# Task A10: Phase A gate, pass 2 findings (`core::platform`, `core::Generator`, the environment writer)

The Phase A gate ran `/code-review` over `6dda244..a92e0e9` (Task A4: `core::platform` imported from endo, `Generator`, the process-environment writer in `core::base`, and the scoped test fixtures). It found 15 defects. They came in with endo's platform layer and are ours now — endo, tuidu, fastcached and contour all link this module.

Fix each one **test-first**: a case that fails before the fix, in the module's existing `*_test.cpp` next to the source. Several of these are in code paths with no test at all, which is why they survived; those need the test more than the fix.

## Memory safety and races

1. **`src/core/platform/testing/InMemoryFileSystem.cpp:99` — use-after-free in the fake filesystem.**
   `MemoryIOBuf` caches get-area pointers into the target `std::string`, then appends to that same string on write, so a reallocation leaves `eback`/`gptr`/`egptr` dangling. Write 4 KB through a read-write stream and then read: heap-use-after-free under ASan, wrong bytes otherwise.
   Also: writes always append regardless of stream position, so `openReadWrite()` can never overwrite in place as the native backend does. Neither `openWrite()` nor `openReadWrite()` has a test.
2. **`src/core/platform/MessageQueue.hpp:161` — data race on `_wakeup`.**
   `setWakeup()` writes it without the lock, while `push()` and `shutdown()` read it outside the lock and dereference it. Every other member is guarded. A teardown that clears the pointer can be missed, leaving `push()` signalling a destroyed object. Must be clean under TSan.
3. **`src/core/platform/SignalHandler.cpp:149` — `restore()` leaves a dangling `Wakeup*` registered.**
   It clears `currentCallback` but never `interruptWakeup`, so after the `Wakeup` is destroyed, Linux's `processSignalFd()` and the Windows console handler can still call `signal()` on it. The asymmetry is invisible because `restore()` looks complete.

## Wrong answers, platform by platform

4. **`src/core/platform/windows/WindowsEnvironmentProvider.cpp:51` — the bug A4 already fixed, in the other reader.**
   `get()` ignores the buffer-too-small return, so a value longer than the buffer yields a string of NUL bytes; and `len == 0` cannot tell "empty value" from "not found", so this provider reports an empty variable as unset while `core::LiveEnvironment::get()` — fixed three commits earlier with the `SetLastError`/`GetLastError` dance — reports `""`. Two readers of the same Win32 block disagree.
   The POSIX provider delegates to `core::LiveEnvironment` and `core::setProcessEnvironmentVariable`; this one hand-rolls it, which is why it drifted. Prefer delegating over re-fixing. Also note it allocates 32 KB per lookup.
5. **`src/core/testing/EnvHelper.hpp:45` — on Windows, restoring an empty value deletes the variable.**
   `setTestEnv(name, "")` and `unsetTestEnv()` are the same call, so `ScopedEnv`'s destructor cannot put back a variable whose previous value was empty — and the environment is process-global, so the damage crosses tests. This contradicts what A4 made `LiveEnvironment::get()` do, which `Environment_test.cpp` asserts.
6. **`src/core/platform/SystemPipe.cpp:305` — the never-stall guarantee holds on POSIX only.**
   On Windows only the read socket is made non-blocking; the write socket stays blocking, so a producer that outruns the loop parks indefinitely — exactly what the POSIX path goes out of its way to prevent. And if the socket ever were non-blocking, `WSAEWOULDBLOCK` falls through to `PlatformError::IoError` instead of the POSIX branch's "a wakeup is already pending, report it as done". `SystemPipe_test.cpp` guards both cases behind `#ifndef _WIN32`, so neither is tested where the bug is.
7. **`src/core/platform/SystemPipe.cpp:177` — `write()` casts size to `int` with no clamp**, while `read()` two lines down clamps with `std::min<std::size_t>(size, INT_MAX)`. A truncating size can make `send()` succeed having written far fewer bytes, and the short count is returned as success.
8. **`src/core/platform/SystemPipe.cpp:257` — `makeLoopbackPair()` accepts whoever connects.**
   Between `listen()` and `accept()` any local process can take the ephemeral port (discoverable, backlog 1), and the returned pipe's two ends are then not connected to each other: every wakeup byte goes to a stranger and the loop never wakes. The usual socketpair emulation compares `getsockname(client)` with `getpeername(server)` and retries; this does not.
9. **`src/core/platform/NativeFileSystem.cpp:71` — a symlink to a directory is reported as an executable file**, against the function's own documented "Directories always return false". A PATH search that trusts it runs the directory and fails with EACCES instead of trying the next entry. A plain directory is correctly rejected; only the symlink slips through the guard.
10. **`src/core/platform/GlobMatch.cpp:74` — a bracket expression is unreachable for the character it exists to match.**
    The literal branch is tested before the `[` branch, so `globMatchFilename("[", "[[]")` — the POSIX way to match a literal bracket — returns false. Swap the two arms.
11. **`src/core/platform/PathUtils.cpp:21` — lossy narrowing the same file documents as wrong.**
    `stripTrailingSeparator()` (`:21,:25`) and `isCaseOnlyRename()` (`:38-39`) go through `path::generic_string()`, which `normalizePath()`'s own comment rejects: on Windows it narrows to the ANSI code page and mangles or throws. `InMemoryFileSystem::normalize()` keys the whole fake filesystem on the first, so distinct paths collapse onto one key; `NativeFileSystem::createTempFile()` (`:452`) has the same problem.
12. **`src/core/platform/NativeFileSystem.cpp:215` — `createDirectory()` reports "No such file or directory" for a directory that already exists**, because `create_directory()` returning false with no error means "already there". The one path where a hard-coded string, not the OS, picks the message gets it wrong.
13. **`src/core/platform/NativeFileSystem.cpp:327` — `rename()` throws away the recase error.**
    When the two-hop case-only rename fails, `recaseError` is never read and the caller gets the first attempt's message, though `renameViaTemporary()`'s comment promises the second hop's. If the rollback also failed, the entry is stranded under `Foo.recase-N` and nothing says so.

## Rules we are breaking in public API

14. **`src/core/platform/FileSystem.hpp:78` — `bool` parameters in a public interface.**
    `openWrite(path, append)` and `copyFile(from, to, overwrite)`. `.agent/rules/design-principles.md` is explicit: a `bool` in an API is an anonymous enum named after its representation, and the replacement is a purpose-named `enum class`. `FileSystem.hpp` is a `FILE_SET HEADERS` file, so this is public API for every consumer: fixing it now costs nothing, later it is a breaking change.
    Replace with `enum class WriteMode { Truncate, Append }` and `enum class Overwrite { No, Yes }` (or better names), and mirror through `NativeFileSystem.hpp:32,45` and `InMemoryFileSystem.hpp:66,80`. This closes the plan's standing bool-parameter item for this module.
15. **`src/core/platform/testing/TestEnvironmentProvider.hpp:11` — namespace does not match directory.**
    It opens `namespace core::platform` while its two neighbours in `platform/testing/` are `core::platform::testing`. AGENT.md's rule is namespace = directory, and a consumer writing the qualified name by analogy gets a compile error. Moving it later is a public-API break.
    Check whether the namespace-directory hygiene rule should have caught this and did not; if it has a hole, say so in the report — do not widen the rule in this task.

## Then

- The presets the constraints name on Windows and WSL, including the sanitizer presets — finding 1 should be an ASan report and finding 2 a TSan report before their fixes.
- `python scripts/clang-format.py --check`, `ctest -L hygiene`, `mkdocs build --strict`.
- CHANGELOG entries under Fixed (and Breaking for finding 14, with the migration note), plus a provenance note per file saying it now diverges from endo and why.
- Push, watch CI and portability to green.
- Write your report to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A10-report.md` with RED/GREEN per finding.

**Another agent is working in this same checkout at the same time**, fixing pass 1's findings in `src/core/{Utils,Flags,Escape,FNV,Base64}*` and `src/core/{cli,log}/`. Your files do not overlap, but the branch does: fetch and rebase before you push (`git pull --rebase origin master`), never force-push, and if a push is rejected, rebase and try again.
