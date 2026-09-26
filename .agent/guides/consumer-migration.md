# Migrating a consumer onto core-cpp

How each consuming project moves from its own copy of the shared code onto core-cpp. The plan is
the design spec's,
[Part I §2 (rename map) and §7 (consumer migration)](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md);
this guide is the working checklist. Where each consumer keeps its pin afterwards is in
[`../reference/consumers.md`](../reference/consumers.md).

## Order, and the one rule about it

Merge first, migrate once: core-cpp imports and merges everything (Phases A and B) and tags
`v0.1.0`; then each consumer migrates in a single pull request pinned to that tag. Consumer pull
requests open as drafts. **contour's pull request merges last**, after endo's and tuidu's,
because both fetch code from contour's `master` today and would break if contour dropped it
first.

## Common steps

1. Work in a worktree created from `origin/master`, never in the main checkout:
   ```sh
   git -C <repo> fetch origin
   git -C <repo> worktree add <repo>-worktrees/core-cpp -b <branch> origin/master
   ```
2. Pin core-cpp with `GIT_TAG v0.1.0` (CPM) or vendor that tag (contour). For local iteration
   against a core-cpp checkout: `-DCPM_core-cpp_SOURCE=/path/to/core-cpp`.
3. Rewrite the code with the migration tooling (`tools/migrate/`, Task C0): the mechanical pass,
   then the semantic one. The two commands a consumer pull request runs are below.
4. Delete the consumer's copy in the same pull request. A copy left beside core-cpp is the drift
   this project exists to end.
5. Build and test with the consumer's own presets on Windows and Linux; the pull request body
   carries "Consumer impact" from core-cpp's side and the consumer's CI result.

## The two commands

Both read one table, `tools/migrate/renames.json`, and neither invents a rename: what is not in
the table is a hand edit.

```sh
# 1. The mechanical pass: includes, namespaces, symbols, members and macros, in place, under the
#    path you give it and nowhere else. --dry-run reports without writing.
python /path/to/core-cpp/tools/migrate/rewrite.py --profile endo src

# 2. The semantic pass: members whose name is too common to rewrite by text (Read, Write, Run,
#    Stop). Needs libclang's Python bindings (python -m pip install libclang) and one or more
#    compile databases -- pass every platform's, because each sees files the others do not.
python /path/to/core-cpp/tools/migrate/semantic_rename.py --profile fastcached \
    --compile-db build/linux/compile_commands.json build/windows/compile_commands.json \
    --decl-paths src/FastCache/Net src/FastCache/Async
```

`rewrite.py` is **idempotent**: a second run changes nothing, and a half-converted tree converges,
so re-run it after a rebase rather than merging its output by hand. It reports what it changed per
file, so the diff is reviewable row by row.

What it deliberately does not do, and what is therefore yours:

- **A namespace definition** (`namespace tui { ... }`, `namespace net { ... }`). A consumer's own
  namespace and the moved one are the same token. endo has five such forward declarations.
- **A namespace alias** (`namespace cli = crispy::cli;`), for the same reason from the other side:
  the token being defined is the consumer's, the token on the right is the one that moved, and only
  a person can say whether the alias should follow the move, be re-pointed at `core::cli`, or go
  away entirely because the qualified uses it stood in for are now spelled out. Grep each profile
  for `^\s*namespace\s+\w+\s*=` before you start; the count is small and the decisions are not
  mechanical.
- **A string or character literal.** A codemod may change what the code says, never what the
  program sends. Comments *are* rewritten, because a comment documents the code beside it.
- **`crispy::` as a prefix.** contour keeps crispy's renderer half, so the table renames crispy
  symbol by symbol; the same holds for `endo::platform::`, which keeps `Process` and `Pipe`, and
  for `endo::testing::`, which keeps `InjectedShell`.
- **Every row whose `apply` is `manual`** — a shape change rather than a rename, such as
  `SleepUntil{&reactor, tp}` becoming `loop.sleepUntil(tp)`. Each carries a `note` saying what to
  write instead; `python -c` over the table lists them for your profile.

### What each row's `kind` means, and which tool consumes it

A table whose kinds are discoverable only by reading the checker is a table people misuse, so:

| `kind` | What the row's `from` matches | Applied by |
|---|---|---|
| `include` | an `#include` directive, either spelling | `rewrite.py` (always emits the angle form) |
| `namespace` | a qualified use `from::`, and `using namespace from;` — never a namespace *definition* | `rewrite.py` |
| `symbol` | one fully qualified name, anchored so a longer one never matches | `rewrite.py` |
| `member` | `.from(` and `->from(` | `rewrite.py`, or `semantic_rename.py` where `apply` is `semantic` |
| `macro` | the bare identifier | `rewrite.py` |
| `removed` | **nothing** — core-cpp no longer has this symbol | **nobody**; see below |

`apply` picks the consumer: `text` (`rewrite.py`), `semantic` (`semantic_rename.py`), `manual` (a
human, following the row's `note`), `none` (a `removed` row, which no tool may touch).

**A `removed` row runs the gate backwards.** It names a symbol core-cpp deleted, has no `to` and no
`target`, and `check-renames.py` asserts the symbol is **absent** from the delivered headers — so a
re-introduction is refused. It exists for two reasons: a removal that changes the *shape* of a call,
not just its name, must stay a compile error at the call site rather than become a codemod that
rewrites it into something that compiles and is wrong; and the `note` is where the migration
instruction lives, beside every other rename the same pull request applies. The schema refuses a
`removed` row that carries a `to`, a `target`, or any `apply` but `none`, so no rewrite tool is ever
handed one.

### Proving a mechanical pass was mechanical

Six migrations are about to rewrite hundreds of files each. "I read the diff and it looked right"
does not survive the first hundred, and the reviewer of a 53-call-site rename does not want to
spot-check either. Prove the pass was **pure by construction** instead: for every file the commit
modified, take the pre-image, apply the substitutions *you* assert the pass should have made, and
compare with the post-image.

```bash
# The substitutions you assert this pass made. Written out here, by hand, from the rename rows the
# pull request applies -- see below for why they are not read back from the tool.
substitutions() {
    sed -e 's/NetErrorCode::Other/NetErrorCode::SystemError/g' \
        -e 's/@c Other/@c SystemError/g'
}

# Every file the commit MODIFIED. --diff-filter=M is load-bearing: a file the commit added has no
# pre-image and one it deleted has no post-image, so neither is a transform this can check, and
# asking git for the missing side prints `fatal:` and counts the file as impure. A consumer
# migration adds and deletes by the dozen, so without the filter the proof fails on its first
# attempt, every time, and gets abandoned as broken. -z and `read -d ''` keep a path with a space
# or a non-ASCII byte in it one path rather than two or a shell-quoted string git then cannot find.
list=$(mktemp) expected=$(mktemp) actual=$(mktemp)
git diff -z --name-only --diff-filter=M HEAD~1 HEAD -- src > "$list"

# Derive the post-image yourself and compare, whitespace stripped. The loop reads from a file, not
# from a pipe, so that its counters survive it -- in a pipeline the `while` runs in a subshell and
# both counts come back zero, which reads exactly like a clean run.
pure=0 impure=0
while IFS= read -r -d '' file; do
    git show "HEAD~1:$file" | substitutions | tr -d '[:space:]' > "$expected"
    git show "HEAD:$file"                   | tr -d '[:space:]' > "$actual"
    if cmp -s "$expected" "$actual"; then
        pure=$((pure + 1))
    else
        impure=$((impure + 1)); echo "NOT PURE: $file"
    fi
done < "$list"
echo "pure: $pure   not pure: $impure"
rm -f "$list" "$expected" "$actual"
```

Run against core-cpp's own `Other` -> `SystemError` rename (`8a88ce0`, 53 call sites) this answers
`pure: 16   not pure: 2`, and names the two files as the ones carrying work that was not the
rename -- which is the method doing both of its jobs at once: proving purity where purity is
claimed, and refusing it where hand work happened.

**What the filter leaves out, you review as itself.** An added, deleted or renamed file is outside
this proof, and in a consumer migration that is most of the commit: the consumer's own copy of the
code is deleted and nothing replaces it in that tree. That is a diff a person reads, and it reads
easily -- a deletion has no bytes to be subtly wrong about. The proof covers the part that does:
the files whose contents a codemod rewrote in place.

Anything the pass did that the substitutions do not account for breaks the comparison and is named
by file. That catches the two failure modes a diff read does not:

- **An unintended rewrite** -- a call site that should have become something else being swept into a
  catch-all row, or a stray reformat riding along.
- **A hand edit smuggled into a mechanical commit** -- the one that looks innocent in review and is
  invisible six months later. Keep hand edits in their own commit; then this check stays meaningful.

**Write the substitution list yourself; do not derive it from the codemod's report, and do not
re-run the codemod to produce the expected side.** Both are the same mistake and it is an easy one,
because the automatic version cannot drift and looks like an improvement: you would be using the
tool's account of what it did -- or the tool itself -- to verify what it did, so a tool that is
wrong about a row is wrong identically on both sides and proves itself correct. The entire value of
this method is that it is **independent of the tool**. Assert what should have changed, derive the
post-image from that assertion, and compare. `rewrite.py`'s per-row report is still worth keeping in
the pull request -- as the thing being checked, not as the check.

**Strip whitespace before comparing.** This is not a convenience; it is what makes the proof usable
at all. A rename changes identifier lengths, clang-format re-wraps the lines it lands in, and that
reflow is the *one legitimate difference* a mechanical pass produces. Measured on `8a88ce0`: across
the sixteen purely mechanical files, **34 changed lines do not contain the renamed token at all** --
they moved because clang-format re-wrapped around an identifier six characters longer. Without the
strip the proof reports a false difference on every one of them, and is abandoned the first time it
is used in anger, which is worse than never having had it.

**And write its cost down, because a proof with an undocumented blind spot is one people
over-trust:** a stripped comparison cannot see a whitespace-only change. `auto const x = 1;` and
`auto  const   x=1;` are the same string to it. What covers exactly that gap is the pinned
`clang-format --check` over the same commit, which sees nothing but whitespace. The two together
are complete; neither is complete alone, so run both and say in the pull request that you did.

**Its real value is the files you cannot compile**, which is most of them for most consumers: the
Windows paths from a Linux machine, the BSD and Apple ones from anywhere, the Emscripten-only ones.
There a spot-check is a sample and CI is a round trip measured in tens of minutes -- this is a
complete answer in seconds, before the push.

What it does **not** catch is a *correct* rewrite to the *wrong target* -- every byte as the table
says, and the table wrong. That is what `check-renames.py` is for, and the two are complementary:
this proves the pass did only what you assert, the gate proves the assertion matches the delivered
API. It also pairs with the codemod's idempotence, which the tests assert: idempotence says running
*twice* changes nothing, and this says running *once* changed nothing but what was intended.

### Read the output, never the summary

The failure this whole section defends against has a name, and it is not "the tool has a bug": it is
**a mechanical check believed from its summary rather than its output.** Three instances from
building these tools, each a different mechanism:

- **The summary hid a gap in scope.** A rename reported 59 replacements -- a plausible number, and
  correct -- while silently skipping every attribute access.
- **The tool answered a smaller question than it was asked.** An `awk` reported no over-long lines;
  its input had been truncated by a `head` further up the pipe.
- **The answer was true, and then stopped being true.** A `git diff` CRLF warning described a past
  state and was read as a present one; a set of mutation transcripts cited line numbers from a
  working copy that had since been reformatted.

In each the number was real and the thing it was taken to mean was not. A codemod's replacement
count is a summary in exactly that sense, which is why this check reads the output instead: read
what the check printed, against the tree as it is *now*, and re-run it after anything reformats the
code it cites. A count, a percentage or a green tick is a claim about output nobody has looked at.

### The table is checked against the delivered headers

`tools/migrate/check-renames.py` runs in every build as ctest `core-cpp.migrate-renames`
(label `hygiene`). For every row naming a core-cpp symbol it asserts that the symbol and its public
header exist in `src/core/`; for a row whose target a Phase B task still owes (`"status":
"pending"`) it asserts the opposite, so the row cannot rot in either direction. **A task that
renames a public symbol updates `renames.json` in the same commit** — the gate fails otherwise, and
says which row.

Its cases, and the codemods', are stdlib `unittest`, not pytest:

```sh
python -m unittest discover -s tools/migrate -p '*_test.py'
```

## CPM snippet

```cmake
CPMAddPackage(
    NAME core-cpp
    GITHUB_REPOSITORY contour-terminal/core-cpp
    GIT_TAG v0.1.0
    SYSTEM YES              # core-cpp headers never trip your -Werror
    EXCLUDE_FROM_ALL YES    # build only what you link
    OPTIONS "CORE_CPP_WITH_TUI ON" "CORE_CPP_WITH_TLS OFF")
target_link_libraries(myapp PRIVATE core::async core::net core::tui)
```

## Per consumer

| Consumer (branch) | Mechanism | What its pull request does |
|---|---|---|
| endo (`build/core-cpp`) | CPM | Stops fetching anything from contour (vtparser is unused); deletes `src/tui`, the generic half of `src/platform` and the `src/testing` helpers; rewrites includes and namespaces; drops OpenSSL |
| tuidu (`build/core-cpp`) | CPM | Deletes `src/{coro,platform,testing,tui}` and the crispy fetch; rewrites 43 files; adapts to the API drift since its June snapshot (`EventSource` mocks, `crispy::cli`'s type names) |
| fastcached, PR A (`claude/<n>-core-cpp-tui`) | CPM | Deletes `vendor/` and its vendor checks; its TUI adapter moves to `core::tui` |
| fastcached, PR B (`claude/<m>-core-cpp-async-net`) | CPM | Deletes its async and networking layers and the core files that moved; a staged semantic rename; a benchmark gate (GET throughput within 5%) |
| Lightweight (`feat/dbtool-core-tui`) | CPM, only under `LIGHTWEIGHT_BUILD_TOOLS` | `dbtool`'s progress output and `main.cpp` use `core::tui_output` |
| contour (`build/vendor-core-cpp`) | a verbatim `vendor/core-cpp` (base, log, cli, platform, async, net, testing) | Deletes `src/{coro,net}` and crispy's generic half; a link-what-you-include commit first; vtparser's includes become `<core/...>`; turns `CORE_CPP_WITH_TLS` on and links `core::net_tls` where it used `net`'s TLS (the daemon); stays a draft until endo and tuidu merge |
| morph, PR 1 (`build/core-cpp`) | CPM, replacing FetchContent | Its timeout scheduler becomes one wrapper over core-cpp's event-loop timers (native: its own thread; WebAssembly: the host-driven backend); base64 and the wakeup pipe come from core-cpp |
| morph, PR 2 (`feat/coroutines`) | CPM | An awaitable `Completion<T>` and `core::async::Task<R>` model handlers on the model's strand, with stop-token cancellation for execute deadlines |

## Renames

### Namespaces and includes (contour, endo, tuidu)

| From | To |
|---|---|
| `crispy::` (generic half) | `core::` |
| `crispy::cli::` | `core::cli::` |
| `crispy::App` | `core::cli::App` |
| `crispy::base64::` | `core::base64::` |
| `crispy::testing::FakeEnvironment` | `core::testing::FakeEnvironment` |
| `logstore::` | `core::log::` |
| `coro::`, `endo::coro::` | `core::async::` |
| `net::` | `core::net::` |
| `endo::platform::` | `core::platform::` |
| `endo::testing::` | `core::testing::` |
| `tui::` | `core::tui::` |
| `endo::Generator`, `<platform/Generator.hpp>` | `core::Generator`, `<core/Generator.hpp>` |
| `<platform/X.hpp>` (endo's generic platform layer) | `<core/platform/X.hpp>` |
| `<testing/ScopedTempDir.hpp>`, `<testing/ScopedWorkingDirectory.hpp>`, `<testing/EnvHelper.hpp>` | `<core/testing/...>` |
| the compatibility aliases in `namespace endo` (`endo::NativeHandle`, `endo::FileSystem`, `endo::SignalHandler`, `endo::TestEnvironment`, ...) | the `core::platform::` names; `endo::TestEnvironment` is `core::platform::testing::TestProcessEnvironment`, and its working directory `core::platform::testing::TestWorkingDirectory` (core-cpp 0.5.0) |
| `endo::containsGlobChars`, `endo::globMatchFilename` | `core::platform::containsGlobChars`, `core::platform::globMatchFilename` |
| `net::IClock`, `net::SteadyClock`, `net::ManualClock`, `net::defaultSteadyClock`, `net::SteadyTimePoint`, `net::SteadyDuration`, `<net/platform/Clock.hpp>` | the same names in `core::platform`, `<core/platform/Clock.hpp>` |
| `net::NativeHandle`, `net::InvalidHandle`, `net::platformRead`/`platformWrite`/`platformClose`, `<net/platform/NativeHandle.hpp>` | the same names in `core::platform`, `<core/platform/Types.hpp>` (there is no `NativeHandle.hpp`) |
| `net::ensureWinsockInitialized`, `<net/platform/WinsockInit.hpp>` | `core::platform::ensureWinsockInitialized`, `<core/platform/WinsockInit.hpp>` |
| `net::createSystemPipe`, `net::SystemPipe`, `<net/platform/SystemPipe.hpp>` | `core::platform::createSystemPipe`, `core::platform::SystemPipe`, `<core/platform/SystemPipe.hpp>` (see the `read()` delta below) |
| `net::testing::TempDir`, `<net/testing/TempDir.hpp>` (contour's `vthost` tests) | `core::testing::ScopedTempDir`, `<core/testing/ScopedTempDir.hpp>`: the prefix has no default, and a directory it cannot create throws rather than failing a `REQUIRE`; `path()` and `operator/` are the same |
| `<net/platform/PeerAddress.hpp>`, `<net/platform/WindowsLoopback.hpp>`, `<net/WaitChunking.hpp>`, `<net/EpollEventSource.hpp>`, `<net/KqueueEventSource.hpp>` | private in core-cpp (`detail/PeerAddress.hpp`, `windows/WindowsLoopback.hpp`, `linux/EpollBackend.hpp`, `bsd/KqueueBackend.hpp`; `WaitChunking.hpp` went with the WFMO backend in 0.5.0); nothing outside contour's `src/net` included them, and `<net/PollEventSource.hpp>` joined them in Task B3 (`posix/PollBackend.hpp`; its Windows half, the WFMO backend, was removed in 0.5.0). A program gets a backend from `core::net::makeBackend(BackendKind)` or `makeDefaultBackend()` |
| `<crispy/X.hpp>` for Assert, Base64, Deferred, Defines, Environment, Escape, FNV, Flags, Overloaded, Times, UserInfo, Utils | `<core/X.hpp>` |
| `<crispy/LogStore.hpp>`, `<crispy/LogSink.hpp>` | `<core/log/LogStore.hpp>`, `<core/log/LogSink.hpp>` |
| `<crispy/CLI.hpp>`, `<crispy/App.hpp>` | `<core/cli/CLI.hpp>`, `<core/cli/App.hpp>` |
| `<crispy/testing/Environment.hpp>` | `<core/testing/Environment.hpp>` |
| `<coro/X.hpp>`, `<net/X.hpp>`, ... | `<core/async/X.hpp>`, `<core/net/X.hpp>`, ... |

### API deltas (contour, endo, tuidu)

| From | To |
|---|---|
| `net::IClock` | `core::platform::IClock` |
| contour's `net` target, which built `Tls.hpp`/`Tls.cpp` into itself and always required OpenSSL | `<core/net/Tls.hpp>` is `core::net_tls`, a target of its own that exists only with `CORE_CPP_WITH_TLS`: configure core-cpp with `CORE_CPP_WITH_TLS ON` (the CPM snippet above has it OFF) and link `core::net_tls`, which links `core::net`, and OpenSSL PRIVATE |
| `IListener::localPort` | `boundPort` |
| `NetErrorCode::Other` | `SystemError` |
| `EventSource`, `makeDefaultEventSource`, `FdInterest` | `IoBackend`, `makeDefaultBackend`, `Interest` (Task B3; the full table is in that release's CHANGELOG entry and every row is in `tools/migrate/renames.json`). A backend DISPATCHES: `attach(handler)` then `setInterest(handler, interest)`, and `wait()` calls back rather than answering a `WaitOutcome` of tokens |
| `gsl::not_null<T*>` | a reference, or an asserted pointer |
| `crispy::fatal(...)`, from `<crispy/Assert.hpp>` | `core::log::fatal(...)`, from `<core/log/Assert.hpp>` |
| `SoftRequire(...)`, from `<crispy/Assert.hpp>` | the same macro, from `<core/log/Assert.hpp>`; `Require` and `Guarantee` stay in `<core/Assert.hpp>` |
| `CRISPY_PACKED`, `CRISPY_REQUIRES`, `CRISPY_CONSTEVAL`, `CRISPY_CONSTEXPR`, `CRISPY_CONCEPTS_SUPPORTED` | `CORE_PACKED`, `CORE_REQUIRES`, `CORE_CONSTEVAL`, `CORE_CONSTEXPR`, `CORE_CONCEPTS_SUPPORTED` |
| the global `Overloaded` of `<crispy/Overloaded.hpp>`, and `crispy::Overloaded` of `<crispy/Utils.hpp>` | `core::Overloaded`, in `<core/Overloaded.hpp>` (which `<core/Utils.hpp>` includes) |
| `logstore::SourceLocationCustom` | removed: `core::log::SourceLocation` is `std::source_location` |
| `crispy::views::enumerate`, a function object | `core::views::enumerate`, a function template: `enumerate(r)` is unchanged, but it cannot be passed as a value |
| `net::createSystemPipe()` returning `std::expected<..., NetError>`, and `SystemPipe::write` returning `IoResult` | `core::platform::createSystemPipe()` and `write` report a `core::platform::PlatformError` (`PipeCreationFailed`, `IoError`); the non-blocking behaviour is contour's |
| `SystemPipe::read` returning `IoResult` (contour: a count, 0 at the end of the stream, a would-block as `NetError`) or `std::expected<std::size_t, PlatformError>` (endo: a count, 0 at the end of the stream) | `std::expected<core::platform::ChannelResult, PlatformError>`: test `isEndOfStream()` where the code tested for 0, and `empty()` where it tested for a would-block error; `bytesRead()` is the count. A `PlatformError` is a real failure only |
| endo's `SystemPipe`, blocking on POSIX | non-blocking and close-on-exec on both ends: a write the full channel refuses reports done, and a read of an empty channel returns an empty `ChannelResult` instead of blocking |
| endo's `<platform/Types.hpp>` including `<windows.h>`, `<io.h>` and `<fcntl.h>` on Windows, and defining `STDIN_FILENO`/`STDOUT_FILENO`/`STDERR_FILENO` and `SIGINT`/`SIGTERM`/`SIGKILL`/`SIGTSTP`/`SIGCONT`/`SIGCHLD` there | `<core/platform/Types.hpp>` includes none of them and defines none: endo's process code (which stays in endo) includes what it uses and defines its own Windows fallbacks |
| `homeDirectory()`, `configHome()` from `<platform/UserPaths.hpp>`, reading `std::getenv()` | the same calls, reading `core::LiveEnvironment` (on Windows the operating system's block, not the CRT's copy); each takes a `core::Environment const&`, which defaults to that |
| a composition root that includes `<platform/posix/PosixEnvironmentProvider.hpp>`, `<platform/windows/WindowsEnvironmentProvider.hpp>`, `<platform/linux/LinuxFileInfoProvider.hpp>` or `<platform/windows/WindowsFileInfoProvider.hpp>` and picks one with `#ifdef` (endo's `Shell.cpp`, `Prompt.cpp`, `Registration.cpp`) | `core::platform::nativeProcessEnvironment()`, `nativeWorkingDirectory()` and `nativeFileInfoProvider()` (`std::unique_ptr` to the interface), from the public `<core/platform/ProcessEnvironment.hpp>`, `<core/platform/WorkingDirectory.hpp>` and `<core/platform/FileInfoProvider.hpp>`; the implementation headers are private in core-cpp (in no FILE_SET), and endo's `LinuxFileInfoProvider` is `PosixFileInfoProvider` there, the provider on every POSIX system. The factory makes a new provider where `PosixEnvironmentProvider::instance()` handed out one singleton, so the composition root owns it and injects it |
| `ENDO_GENERATOR_FORCE_FALLBACK` | `CORE_GENERATOR_FORCE_FALLBACK`; `Generator` is the fallback on libstdc++ now as well (endo's picked by include order) |
| `endo::testing::setTestEnv()`/`ScopedEnv` over `setenv()`/`getenv()` | `core::testing::setTestEnv()`/`ScopedEnv`, over `core::setProcessEnvironmentVariable()`/`core::LiveEnvironment` on POSIX, `_putenv_s()` on Windows as before |

### fastcached (PascalCase to camelBack)

The full table has 44 rows and is seeded into `tools/migrate/renames.json` (Task C0). Examples:

| From | To |
|---|---|
| `<FastCache/Async/X.hpp>`, `<FastCache/Net/X.hpp>` | `<core/async/X.hpp>`, `<core/net/X.hpp>` |
| `IsReady`, `Native`, `Release` | `done`, `handle`, `release` |
| `SyncRun` | `syncRun` |
| `IReactor`, `PlatformReactor`, `TestReactor` | `EventLoop`, `PlatformLoop`, `testing::TestLoop` |
| `Submit`, `Schedule`, `CancelPending`, `Run`, `Stop`, `Clock` | `submit`, `schedule`, `cancelPending`, `run`, `stop`, `clock` |
| `SleepUntil{&reactor, tp}` | `loop.sleepUntil(tp)` |
| `InterruptibleSleepUntil` | `interruptibleSleepUntil(&loop, token, tp)` |
| `CancellationSource`, `CancellationToken` | `StopSource`, `StopToken` |
| `ISocket::Read`, `Write`, ... | `read`, `write`, ... |
| `*Listener::Bind(...)` | `listen(loop, ListenOptions)` |
| `NetErrorCode::BadFileHandle` | `BadHandle` |
| `IClock::Now`, `Refresh` | `now`, `refresh` |
| `<FastCache/Core/Clock.hpp>`, `FastCache::IClock`, `SteadyClock`, `CachedClock`, `ManualClock`, `IWallClock`, `SystemWallClock`, `ManualWallClock`, `WallClockRef` | `<core/platform/Clock.hpp>`, the same names in `core::platform` |
| `FastCache::TimePoint`, `FastCache::Duration` | `core::platform::SteadyTimePoint`, `core::platform::SteadyDuration` |
| `ManualClock::Advance`, `SetNow`; `ManualWallClock::Advance`, `SetNow` | `advance`, `setNow` |
| `IWallClock::Now`, `WallClockRef::Now`, `WallClockRef::Get` | `now`, `now`, `get` |
| `FastCache::DefaultSystemWallClock()` | `core::platform::defaultSystemWallClock()` |
| `FC_ZONE_*`, `FC_FRAME_MARK*`, `FC_THREAD_NAME`, `FC_PLOT`, `FC_TRACY_ENABLED` | `CORE_ZONE_*`, `CORE_FRAME_MARK*`, `CORE_THREAD_NAME`, `CORE_PLOT`, `CORE_CPP_WITH_TRACY` (0 or 1 in `<core/Config.hpp>`) |
| `<FastCache/Core/Profiling.hpp>`, `<FastCache/Core/Ranges.hpp>` | `<core/Profiling.hpp>`, `<core/Ranges.hpp>` |
| `FastCache::FindOrNull`, `FastCache::FindIfOrNull` | `core::findOrNull`, `core::findIfOrNull` |
| `FastCache::Ranges::Iota`, `FoldLeft`, `Ranges::Detail::*` | `core::ranges::Iota`, `FoldLeft`, `core::ranges::detail::*` |
| `FC_RANGES_FORCE_FALLBACK` | `CORE_RANGES_FORCE_FALLBACK` |

## What a migration must not do

- **Edit core-cpp's code from the consumer's side.** A fix goes to core-cpp, gets a release, and
  the consumer moves its pin ([`../rules/library-hygiene.md`](../rules/library-hygiene.md)).
- **Pin a branch.** Pin a tag, or temporarily a full SHA.
- **Keep a compatibility alias** (`namespace endo { using core::platform::...; }`). The
  migration happens once; aliases make it happen never.

## Checks that the old copies are gone

| Repository | Command | Expected |
|---|---|---|
| endo | `rg -l "namespace (coro\|net\|crispy\|tui)\b" src` | nothing |
| fastcached | `rg "FastCache/(Async\|Net)/"` | nothing |
| tuidu | `rg "endo::(coro\|platform)"` | nothing |
| contour | `rg "^namespace (coro\|net)\b" src` | nothing |
