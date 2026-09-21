# Task A11 report — Phase A gate, pass 3 (`core::async`)

All twelve gate findings fixed, plus the second hole in the namespace gate that the platform agent
raised afterwards — each test-first. Fourteen commits, all on `origin/master`. The three findings
deferred to Task B1 were not touched.

| Commit | Findings |
|---|---|
| `ccdec61` | 1 (Critical), 9 |
| `33dc64f` | 3 |
| `378e157` | 10 |
| `a3463a6` | 4, 6 |
| `26a820a` | 8 |
| `75c11c7` | 7, 12 |
| `d47a913` | 2 |
| `6af78b8` | 5 |
| `70e2198` | 11 |
| `af392a6` | CHANGELOG, `docs/modules/async.md`, provenance |
| `f28462f` | 5, second hole (the first directory segment only) — added after the first round |
| `c4c3153` | CHANGELOG follow-up |
| `d70c31e` | `docs/modules/tui.md` named a namespace the code does not have |
| `a713187` | the completer allowlist rows name core-cpp#30 |
| `cfff6ee` | ruling R57: the completer is renamed and the eight allowlist rows are deleted |

## The Critical one: the ownership rule

**Rule chosen:** *the race state is reference-counted, and every call into it that can run foreign
code holds a reference for the duration of that call.*

`WhenAnyState` is a `std::shared_ptr`, held by the awaiter, by every runner promise and by the
parent→child cancel bridge. Both `request_stop()` sites take a local copy of that pointer before
they call, so the state — and the `StopSource` inside it — outlives the call even when the call
destroys the awaiter.

Why it holds on **both** `StopToken` branches: it no longer asks the stop facility to keep its own
state alive across a request, which is not something either branch promises. Keeping a stop state
alive across one's own `request_stop()` is the caller's job; the fallback's
`StopSourceFallback::request_stop()` happens to take a `shared_ptr` copy of its state, which is an
implementation detail of ours and not a guarantee, and `std::stop_source::request_stop()` on
libstdc++ and MSVC reaches its heap state through a raw pointer. The fix removes the dependency on
either, so the two branches behave the same by construction rather than by coincidence.

Two things it deliberately does **not** change, because they are already safe and the reasons are
worth recording:

- The `StopCallback` object itself is still an awaiter member and is still destroyed from inside
  its own callback. The standard permits exactly that (`~stop_callback` does not block when the
  callback runs on the current thread), libstdc++ implements it with its `_M_destroyed` flag, and
  our fallback's `deregisterCallback()` returns at once when the requester is the current thread.
- A child's own registrations cannot dangle in the list the request is walking. They are locals of
  the child's body, so they are deregistered as that body unwinds — before the runner's final
  suspension, which is the only thing that can end the race. The only thing the race's completion
  could free out from under `request_stop()` was the state, which is what is now held.

### RED / GREEN

`WhenAny_test.cpp`, *"whenAny survives children resumed from inside the cancel bridge's own
callback"*. Two children park on `StopCallbackEvent`, an awaitable that registers a `StopCallback`
and resumes the parked coroutine **from inside it** — the existing `ManualEvent` registers none, so
every cancellation it delivers is observed on a later manual resume with no callback on the stack.

Each child also holds a registration of its own (`NoteStop`) while parked, registered before the
event's. That is load-bearing: libstdc++'s `_M_request_stop()` has `if (__last_cb) return true;`,
so it never touches its state after the *last* callback — and with one callback per child the race
always ends on the last one. The extra registration, which sits behind the event's in the list,
makes the request come back to its own state afterwards. Without it the case passes even unfixed.

- **RED** (`clang-asan-ubsan`, `core-cpp-async-test`, `std::stop_token`):
  `AddressSanitizer: heap-use-after-free ... READ of size 4` — the `_M_value.load()` of
  `_Stop_state_t::_M_lock()`, on the state the awaiter's destruction had just freed.
- **GREEN**: both `core-cpp-async-test` and `core-cpp-async-fallback-test`, 40 cases, ASan/UBSan
  and TSan clean.
- On the **fallback** binary the case passes before *and* after, which is the finding's point: the
  fallback's accidental `shared_ptr` copy masks it. It is kept there as a regression guard.

## The link one (finding 2)

`core::async` now links `Threads::Threads` (INTERFACE) on `CORE_CPP_USE_THREADS`, the same
condition `core::base` uses and the one that is off exactly under single-threaded Emscripten.

**Honest limit on the RED.** The link cannot be made to fail on any host this session has: pthread
is part of libc on all of them (glibc ≥ 2.34, the Windows CRT, libSystem), so `Threads::Threads`
resolves to nothing there and the program links either way. So there are two proofs:

- **RED, reproduced:** dropping `PUBLIC_LIBS` fails the configure by name —
  `[core-cpp] core::async does not link Threads, although <core/async/StopToken.hpp>'s fallback
  uses std::mutex, std::condition_variable and std::this_thread.` The gate is in
  `src/core/async/CMakeLists.txt`, next to the fix, so the dependency cannot be dropped silently.
- **The link a consumer makes:** `core-cpp-async-link-smoke` (`StopTokenLinkSmoke.cpp`), which
  links `core::async` and nothing else with the fallback forced, and runs as ctest
  `core-cpp.async-link-smoke`. It is not a `core_cpp_add_test()`, because that links
  `core::testing_main` → `core::base` → Threads, which is what hid this. It builds and runs green
  on FreeBSD (Portability run `35527475488`) and macOS/AppleClang — the two platforms the finding
  names as taking the fallback.

`AGENT.md`, `docs/modules/index.md`, `docs/modules/async.md` and the CHANGELOG said "std only";
they now say what the module links.

## Findings 3–12

| # | RED | GREEN |
|---|---|---|
| 3 | *"whenAny keeps a winner that already ran when the flow is cancelled after it"* failed on `REQUIRE_FALSE(threwCancelled)`: the completed child's index was thrown away. | Only a child that **completed** latches the win; `await_resume()` throws `OperationCancelled` only where nothing won. |
| 4 | `static_assert(!WhenAllTakes<Task<void>&>)` and `!WhenAnyTakes<Task<void> const>` both failed to compile with `remove_cvref_t` in the constraint. | Constraint deduced without stripping the reference; an lvalue and a const rvalue are "no matching overload" at the call. |
| 5 | Two holes, two REDs — see [Finding 5 in full](#finding-5-in-full-two-holes-in-the-namespace-gate). | Both closed (`6af78b8`, `f28462f`); 25 self-test cases, all refused by name; the tree scans clean. |
| 6 | — (`<type_traits>` missing from `WhenAll.hpp`). | Added; `Awaitable.hpp`'s unused `<utility>` removed. |
| 7 | — (`<stdexcept>` came from Catch2). | Added; unused `<string>` removed. |
| 8 | — (`Awaiter` and `HasStopToken` unused). | All five `if constexpr (requires { awaiting.promise().stopToken(); })` are `HasStopToken<Promise>`; `Awaitable_test.cpp` asserts both concepts over the module's types and over near misses. `Awaitable.hpp` moved below `Cancellation.hpp` (it includes `StopToken.hpp` alone). |
| 9 | — (allocation + indirect call). | `WhenAnyCancelBridge`, a named functor; `<functional>` gone. Part of the Critical fix. |
| 10 | — (public API). | `whenAny()` resolves to `std::optional<std::size_t>`; `detail::WhenAnyNoWinner` is gone. Recorded under **Breaking**. |
| 11 | — (config). | The four stop-token names are a *method* exemption only. The `ClassMethod` style is gone with its duplicate of the regex; a static member function falls through to the `Method` style, checked with the pinned clang-tidy on `Static_Method` (still refused) and `get` (still exempt). |
| 12 | Standalone reproduction with GCC 14.3 and the old `__OPTIMIZE__` condition: `-Og` → rc 139, `-O1` → rc 139, `-O2` → ran. | `src/core/async/CMakeLists.txt` reads the last `-O` off the build's own flags, defines `CORE_ASYNC_SYMMETRIC_TRANSFER_IS_TAIL_CALL` only at `-O2` or better, and reports in the configure log. Verified: `gcc-release` (`-O3`) runs the case, `gcc-debug` (`-O0`) skips, `-Og` skips. |

## Finding 5 in full: two holes in the namespace gate

`6af78b8` closed the first, `f28462f` the second (the second was raised by the platform agent after
the first round and carried the same rule).

1. **Only the first namespace in a file was inspected at all.** The rule says every segment of
   every namespace is lowercase, but `expectedNamespace` was cleared after the first declaration.
   `namespace core::async { namespace Detail { ... } }` passed clean.
   **RED**: the new self-test case reported `namespace-directory: src/core/foo/Foo.cpp was not
   refused`. **GREEN**: the directory half still applies to the first declaration only (a separate
   latch says which one that was); the case half applies to every one.
2. **The expected namespace came from the first directory segment under `src/core/` only.** Every
   file below a module directory was therefore measured against the module namespace, so
   `src/core/platform/testing/` declaring `core::platform` passed although its neighbours declare
   `core::platform::testing` — how `TestEnvironmentProvider.hpp` got in.
   **RED**: with the first-segment version restored, the new `testing/` case reported
   `namespace-directory: src/core/foo/testing/Fake.hpp was not refused`. **GREEN**: the expected
   namespace is assembled from every segment, skipping the directories that are layout rather than
   namespace.

**Which directories are layout:** `posix`, `windows`, `linux`, `bsd`, `darwin`, `emscripten`,
`detail` — the ones `core_cpp_add_module()` holds private headers in
(`cmake/CoreCppTargets.cmake`) — plus `backend`. That keeps the behaviour the tree already has:
`src/core/net/posix/` is `core::net`, `src/core/net/detail/` is `core::net` or `core::net::detail`,
`src/core/tui/runtime/posix/` is `core::tui::runtime`, `src/core/net/testing/posix/` is
`core::net::testing`. Every other segment is a namespace: `testing/`, `runtime/`.

Self-test cases added for both shapes the message asked for: `src/core/foo/posix/Impl.cpp`
declaring `core::foo` sits in the **clean** tree (must pass), and
`src/core/foo/testing/Fake.hpp` declaring `core::foo` is a **case** (must be refused), beside the
clean `Fake.hpp` that declares `core::foo::testing`.

### What the deeper rule found in the tree, and why it is an allowlist row

Exactly one thing, in one module: **`src/core/tui/completer/`'s eight files declare `core::tui`**
where the directory says `core::tui::completer`. Nothing else in `src/core/` moved — I checked
every nested directory and its first namespace before changing the rule.

It is recorded in the allowlist with its reason rather than fixed, because **both ways out are a
change to `core::tui`'s public API, which is the tui owner's call**:

- rename the namespace to `core::tui::completer` — which `docs/modules/tui.md:60` **already says**
  the module offers, and which the sibling directories `runtime/` and `testing/` do. That changes
  every consumer naming `core::tui::Completer`, `CompletionItem`, `CompletionProvider`,
  `fuzzyMatch` or `smartCaseMatch`, and ~370 lines across 13 files inside `core::tui` itself
  (`CommandPalettePopup`, `CompletionPopup`, `FuzzyPickerPopup`, `InputField`, `TestHelpers` and
  four test files); or
- flatten the directory into `src/core/tui/`, which changes the include paths of five public
  headers (`<core/tui/completer/Completer.hpp>`).

The files came from endo (`src/tui/completer/`, `f774a210`), which declares a flat `namespace tui`
for all of its TUI, so the **import is faithful** and it is core-cpp's own namespace-equals-
directory rule that they do not meet. `core::tui` is unreleased, so either fix is free now and
breaking after v0.1.0.

### Resolved: ruling R57 — renamed, and the rows deleted

The lead ruled option A. The eight files now declare `core::tui::completer`; inside `core::tui`
the call sites name them `completer::X`, and the five test files that had `using namespace
core::tui` have `using namespace core::tui::completer` beside it, which is what turns ~360
unqualified references into five lines rather than 360. No include path changes and no behaviour
does. The eight allowlist rows are deleted in the same commit, and the tree scan is clean with
nothing exempting them — which is the point of the corrected rule.

Nothing in the rename was ambiguous or read wrong once qualified, so there was nothing to stop
and ask about. Verified: `core-cpp-tui-test` 1010/1010 on clang, 1014 passed + 1 skipped on MSVC,
clang-tidy clean over `core::tui`, hygiene scan clean (no allowlist), self-test 25/25,
`mkdocs --strict` clean.

The CHANGELOG entry is under **Breaking**, not **Changed** as the ruling first said: renaming a
public namespace is an API break, the file's preamble puts every break there with a migration
note, and finding 10's `whenAny` rename already sits in that section. Raised with the lead, who
agreed and amended the ruling — a consumer scanning **Changed** would have missed the one entry
that fails their build.

`docs/modules/tui.md` and the provenance rows of all twenty affected files record it, and
`core-cpp#30` is closed by the commit message.

Finding 11's second half — expressing the ~800-character regexes once — is only half possible.
YAML anchors are out: an anchor plus an alias makes LLVM's parser answer `unknown node kind` and
reject the whole configuration (tested with clang-tidy 22.1.8). Two of the three copies are now
one; `Function` and `Method` differ on purpose, and `TypeAlias`/`Typedef` cannot be merged. Said so
in the file, so the next person does not retry the anchor.

## Left deliberately

- **`core::net` and `core::tui` hand-spell the same stop-token probe in eight more places**
  (`src/core/net/EventLoop.hpp:369,426`, `src/core/tui/runtime/TuiRuntime.hpp:311,350,395,462,499,553`).
  Finding 8 named five sites, all in `core::async`, and those two modules are not this task's
  files. Worth a follow-up so the concept really has one home.
- **`whenAny` over an empty input still allocates its state.** `await_ready()` is true and nothing
  runs, but the `make_shared` happened in the constructor. One allocation in a function that
  otherwise builds N coroutine frames; not worth a lazier shape.
- **The three B1 findings** (`Task.hpp:157`, `WhenAll.hpp:121`, `WhenAny.hpp:60`) are untouched.
- **`src/core/tui/completer/`'s namespace** — see finding 5 above. Allowlisted, not fixed: it is a
  `core::tui` public-API decision, and it is **core-cpp#30**, which each of the eight rows names.

## A process mistake of mine, recorded

Three sessions share one working tree and one index. I used explicit pathspecs throughout and
never `-a` or `add -A`, but that is not enough: naming a *shared file* stages the whole file,
including whatever another session has in flight in it. Twice I took someone else's work into a
commit of mine —

- `26a820a`: nine provenance rows of the log/base session (`Times.hpp`, `Times_test.cpp`,
  `Utils.hpp`, `Utils_test.cpp`, `cli/CLI.cpp`, `cli/CLI_test.cpp`, `log/LogSink.cpp`, and new rows
  for `log/posix/TerminalQuery.cpp` and `log/windows/TerminalQuery.cpp`), when one row of mine was
  what I meant to stage;
- `cfff6ee`: the platform session's provenance row for `platform/testing/InMemoryFileSystem.cpp`
  and their CHANGELOG entry about that fake's POSIX file lifetime.

`d47a913`, `af392a6` and `a713187` are clean. The content survived and nothing was undone —
pushed history on a shared master is not mine to rewrite — but the attribution is wrong in those
two commits, and anyone bisecting or reading them later gets a misleading picture.

The procedure that would have caught it, for whoever reads this next: before staging a shared
file, `git diff -- <file>` and read every hunk; if any is not yours, build a patch of only your
hunks and `git apply --cached` it, which stages them without touching the working tree, then
confirm with `git diff --cached -- <file>`. `git add -p` is the obvious tool and is unavailable
here, being interactive.

## Not mine, seen while verifying

- `core-cpp.cmake-hygiene` fails in a `gcc-release` run against the working tree: three untracked
  files of the `log` session (`src/core/log/detail/ProcessId.hpp`, `posix/ProcessId.cpp`,
  `windows/ProcessId.cpp`) have no provenance row. Transient, theirs.
- `core-cpp.platform` timed out once in the same `gcc-release` run. Theirs.
- Neither appears in CI, which is green.

## Verification

| Where | Result |
|---|---|
| `python scripts/clang-format.py --check` (22.1.8) | clean, 363 files |
| `clang-debug` build + ctest | 20/20 |
| `gcc-release` build + ctest | the two failures above, both other sessions'; everything else passes |
| `clang-asan-ubsan` build + ctest | 20/20 |
| `clang-tsan` build + ctest | 20/20 |
| `clang-tidy` preset, whole tree | clean |
| `cl-debug` build + ctest | 22/22 |
| `clangcl-release`, `--clean-first` (fastcache-cc 0.2.0-739-gd4451c3b predates fastcached `ca8dfc32`) | 22/22 |
| `mkdocs build --strict` | clean |
| `ctest -L hygiene` | included above; the hygiene self-test has 24 cases |
| Both `StopToken` branches | every async case runs in `core-cpp.async` and `core-cpp.async-fallback` |

## CI

- Build `35526193943` — **success**, all 24 jobs on `af392a6` (clang-tidy, style, linux
  clang-22/arm64/cxx26/tracy, gcc-14, gcc-15, macos appleclang and llvm-22, windows cl-debug,
  cl-release, cl-release-tls, clangcl-release, emscripten 3.1.56 and latest, consumer-smoke
  cpm/vendored/wasm, sanitizers asan-ubsan and tsan, coverage, compile-cache).
- Docs `35526193941` — success.
- Portability `35527475488` — success, FreeBSD (system clang), dispatched and watched on `af392a6`.
  (`35526198133`, dispatched by another session on the same SHA, also succeeded.)

After the second round:

- Build `35528458513` on **`f28462f`** — **success**, every job, `ci-ok` green. This is the run
  that covers the second hygiene hole.
- Docs `35529047915` on `c4c3153` — success. Docs `35529188714` on `d70c31e` — success.
- Build `35529047914` (`c4c3153`) and Build `35529188791` (`d70c31e`) were **cancelled by
  concurrency**, each superseded by the next push while still queued. Neither ever ran; `f28462f`
  before them and `a713187` after them are what the Build workflow judged.
- Build `35529338581` and Docs `35529338637` on **`a713187`**, the final commit.
- Portability: `35527475488` (`af392a6`) success; `35529047839` (`c4c3153`); and a dispatch on
  `a713187`.

Lesson for the next round: do not push while a Build run is still queued — GitHub cancels it, and
the cancellation reads like a failure in `gh run list`.
