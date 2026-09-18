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
3. Rewrite the code with the migration tool (`tools/migrate/`, Task C0): the mechanical pass
   (`rewrite.py --profile <consumer>`: include paths and namespaces), then the semantic pass for
   renamed members (`semantic_rename.py` over the compile database).
4. Delete the consumer's copy in the same pull request. A copy left beside core-cpp is the drift
   this project exists to end.
5. Build and test with the consumer's own presets on Windows and Linux; the pull request body
   carries "Consumer impact" from core-cpp's side and the consumer's CI result.

## CPM snippet

```cmake
CPMAddPackage(
    NAME core-cpp
    GITHUB_REPOSITORY contour-terminal/core-cpp
    GIT_TAG v0.1.0
    SYSTEM YES              # core-cpp headers never trip your -Werror
    EXCLUDE_FROM_ALL YES    # build only what you link
    OPTIONS "CORE_CPP_WITH_TUI ON" "CORE_CPP_WITH_TLS OFF")
target_link_libraries(myapp PRIVATE core::coro core::net core::tui)
```

## Per consumer

| Consumer (branch) | Mechanism | What its pull request does |
|---|---|---|
| endo (`build/core-cpp`) | CPM | Stops fetching anything from contour (vtparser is unused); deletes `src/tui`, the generic half of `src/platform` and the `src/testing` helpers; rewrites includes and namespaces; drops OpenSSL |
| tuidu (`build/core-cpp`) | CPM | Deletes `src/{coro,platform,testing,tui}` and the crispy fetch; rewrites 43 files; adapts to the API drift since its June snapshot (`EventSource` mocks, `crispy::cli`'s type names) |
| fastcached, PR A (`claude/<n>-core-cpp-tui`) | CPM | Deletes `vendor/` and its vendor checks; its TUI adapter moves to `core::tui` |
| fastcached, PR B (`claude/<m>-core-cpp-async-net`) | CPM | Deletes its async and networking layers and the core files that moved; a staged semantic rename; a benchmark gate (GET throughput within 5%) |
| Lightweight (`feat/dbtool-core-tui`) | CPM, only under `LIGHTWEIGHT_BUILD_TOOLS` | `dbtool`'s progress output and `main.cpp` use `core::tui_output` |
| contour (`build/vendor-core-cpp`) | a verbatim `vendor/core-cpp` (base, log, cli, platform, coro, net, testing) | Deletes `src/{coro,net}` and crispy's generic half; a link-what-you-include commit first; vtparser's includes become `<core/...>`; stays a draft until endo and tuidu merge |
| morph, PR 1 (`build/core-cpp`) | CPM, replacing FetchContent | Its timeout scheduler becomes one wrapper over core-cpp's event-loop timers (native: its own thread; WebAssembly: the host-driven backend); base64 and the wakeup pipe come from core-cpp |
| morph, PR 2 (`feat/coroutines`) | CPM | An awaitable `Completion<T>` and `core::coro::Task<R>` model handlers on the model's strand, with stop-token cancellation for execute deadlines |

## Renames

### Namespaces and includes (contour, endo, tuidu)

| From | To |
|---|---|
| `crispy::` (generic half) | `core::` |
| `crispy::cli::` | `core::cli::` |
| `crispy::base64::` | `core::base64::` |
| `logstore::` | `core::log::` |
| `coro::`, `endo::coro::` | `core::coro::` |
| `net::` | `core::net::` |
| `endo::platform::` | `core::platform::` |
| `endo::testing::` | `core::testing::` |
| `tui::` | `core::tui::` |
| `endo::Generator` | `core::coro::Generator` |
| `<crispy/X.hpp>`, `<coro/X.hpp>`, `<net/X.hpp>`, ... | `<core/X.hpp>`, `<core/coro/X.hpp>`, `<core/net/X.hpp>`, ... |

### API deltas (contour, endo, tuidu)

| From | To |
|---|---|
| `net::IClock` | `core::platform::IClock` |
| `IListener::localPort` | `boundPort` |
| `NetErrorCode::Other` | `SystemError` |
| `EventSource`, `makeDefaultEventSource`, `FdInterest` | `IoBackend`, `makeDefaultBackend`, `Interest` |
| `gsl::not_null<T*>` | a reference, or an asserted pointer |

### fastcached (PascalCase to camelBack)

The full table has 44 rows and is seeded into `tools/migrate/renames.json` (Task C0). Examples:

| From | To |
|---|---|
| `<FastCache/Async/X.hpp>`, `<FastCache/Net/X.hpp>` | `<core/coro/X.hpp>`, `<core/net/X.hpp>` |
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
| `FC_ZONE_*`, `FC_TRACY_ENABLED` | `CORE_ZONE_*`, `CORE_CPP_WITH_TRACY` |

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
