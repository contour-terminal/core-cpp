# Design principles

How a class, a module or a fallible API is shaped in core-cpp. These principles are
load-bearing: follow them unless there is a very strong reason not to, and state that reason
in the code and in the pull request. How the code is *written* (naming, forbidden constructs,
tools) is in [`cpp-guidelines.md`](cpp-guidelines.md).

They come from the projects core-cpp's code comes from:

- [contour `AGENT.md`](https://github.com/contour-terminal/contour/blob/6777ff05014f8ff163b071e8b0e942830119db80/AGENT.md):
  `enum class` over `bool`, configuration at construction, testability.
- [endo `AGENT.md`](https://github.com/contour-terminal/endo/blob/f774a210ce989e5947b8f61d715068b1dc96088c/AGENT.md):
  dependency injection by constructor, data-driven design, `std::expected`.
- [fastcached `AGENT.md`](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/AGENT.md):
  RAII for every handle, and caching only where staleness degrades safely.

Their examples were about terminals, shells and caches. Here they are restated against the
seams core-cpp itself carries. Most of those seams arrive with the module imports (Tasks A3 to
A7) and the merged async layer (Phase B); the design spec,
[Part I](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md),
names each one.

## Dependency injection

**Anything that touches I/O, time, randomness, the filesystem, the network, the environment,
the terminal or any other ambient resource is reached through an interface that the object is
given, never through a concrete type, a singleton, or a free function with hidden state.**

| Resource | Seam | Test double |
|---|---|---|
| Time | `core::platform::IClock`, `IWallClock` | `ManualClock`, `ManualWallClock` |
| Readiness and completion | `core::net::IoBackend` | `testing::ScriptedBackend`, `testing::NullBackend` |
| The browser's event loop | `core::net::IHostScheduler` | `testing::ManualHostScheduler` |
| An event loop | `core::net::EventLoop` (an `IExecutor`) | `testing::TestLoop` |
| A connection | `core::net::ISocket`, `IListener`, `IConnector` | the in-memory transport |
| Name resolution | `IAsyncAddressResolver` | an injected resolver |
| Files | `core::platform::FileSystem`, `FileInfoProvider` | `testing::InMemoryFileSystem`, `testing::MockFileInfoProvider` |
| The environment | `core::Environment` to read, `core::platform::ProcessEnvironment` to write | `core::testing::FakeEnvironment`, `testing::TestProcessEnvironment` (both seams) |
| The working directory | `core::platform::WorkingDirectory` | `testing::TestWorkingDirectory` |
| Terminal output | `core::tui::TerminalOutput` | `MockTerminalOutput` |

- **Define the interface first, then inject it**, by constructor, as a reference or a
  `std::unique_ptr`. If you want a global, a mutable `static`, or a direct `::read()`,
  `std::getenv()` or `std::chrono::steady_clock::now()` in logic, that is the signal to
  introduce or reuse a seam. *(endo, fastcached)* **The clock and the environment are held by a
  check**, `scripts/check-ambient-reads.py` (`core-cpp.ambient-reads`): a direct read under
  `src/core/` outside the seam that owns it is refused by file and line, and every read it
  excuses -- the seams themselves, and the clock reads the tui widgets brought from endo -- is
  named in its allow list with the reason, so the debt is visible rather than absorbed.
- **Each module's test doubles live in its `testing/` subdirectory, are public, and are
  compiled into the module**, so a consumer's tests use the same doubles core-cpp's do.
  *(core-cpp; the design spec, Part I §1)*
- **A seam is where the platform split happens.** An OS difference is an implementation of an
  interface under `posix/`, `linux/`, `bsd/`, `darwin/`, `windows/` or `emscripten/`, chosen at
  the composition root;
  never an `#ifdef` in logic. See [`platform.md`](platform.md). *(endo)*
- **Deviate only with a stated reason**, for example a pure leaf computation with no
  environment coupling, or a measured hot path where the indirection costs too much. *(endo,
  fastcached)*

## Configuration at construction time

**A constructed object is a usable object.** Everything a class needs to do its job
(collaborators, policy, limits, tuning) is given to its constructor and fixed thereafter. No
`init()` or `setup()` second phase, no default constructor followed by setters, no static knob
set from elsewhere at startup. `EventLoop(IoBackend&, IClock&, EventLoopOptions)` is the shape.
*(contour)*

- **Configuration is not state.** A setter that changes the domain state an object exists to
  manage is fine; a setter that changes how the object was *set up* is not. Ask: would two
  differently configured instances be two different objects, or one object in two states?
  Different objects means the constructor.
- **Omit the default constructor** when there is nothing sensible to default to.
- **Configuration members are private and have no setter.** Prefer that over `const` members:
  a `const` member deletes assignment and quietly breaks types held in containers.
- **A long constructor is a fact about the data**, not a reason to add setters: group related
  parameters in an options struct (`EventLoopOptions`, `DialOptions`). A builder is only for
  optional, order-independent parameters.
- **Fallible setup is a static factory returning `std::expected<T, E>`**, never a constructor
  that leaves the object half-built.
- **When you cannot**, document why at the declaration: live reconfiguration that is the
  feature, geometry the OS decides, a framework that default-constructs, or one `attach` call
  to break a construction cycle (one, not a sequence).
- **How to check it:** how many calls must a caller make before this object is usable? The
  answer must be zero. It is also why the principle pays off in tests: a fully constructed
  object is built with test doubles in one expression.

## Data-driven design

**Behaviour is described by data; code interprets that data.** Adding a backend, an error
code, an option, a warning flag or a sanitizer should be adding a row to a table, not editing
logic in several places. *(endo, fastcached)*

- **One source of truth per concept.** core-cpp's CMake is built this way: the module table
  (`cmake/CoreCppModules.cmake`), the dependency table (`cmake/CoreCppDependencies.cmake`), the
  pedantic, sanitizer and coverage tables (`cmake/CoreCppToolchain.cmake`), the platform source
  table (`cmake/CoreCppTargets.cmake`) and the hygiene rules and allowlist
  (`tests/cmake/check-cmake-hygiene.cmake`).
- **No naive repetition.** Branches that differ only by a value, and copied blocks that differ
  only in constants, names or types, are a table in disguise: lift the value into a descriptor
  and write the logic once.
- **A table indexed by an enumerator proves its order.** The enumerator states its own count
  (a trailing `Last`), the table takes its extent from that, and one `static_assert` checks
  the extent and every row's position. A `static_assert` that anchors a table's length on an
  enumerator *by name* fires only when nothing is wrong: append an enumerator and forget its
  row, and it still compiles and the lookup reads past the end. Origin:
  [fastcached `build-and-toolchain.md`, "Language and ABI pitfalls"](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/rules/build-and-toolchain.md).
- **The test:** if a sixth case showed up tomorrow, how many places would you edit? More than
  one means the design is not data-driven enough yet.

## Error handling: `std::expected<T, E>`

**Fallible APIs return `std::expected<T, E>`**, and callers chain them with `and_then`,
`or_else`, `transform` and `transform_error` rather than nested `if`s. *(contour, endo,
fastcached, Lightweight)*

- **Each module grows its own error type as the need arises** (`core::net::NetError`,
  `core::platform::PlatformError`); do not invent a taxonomy up front. *(contour)*
- **Capture the reason where it is still in scope.** A `bool` result lets a caller say no
  more than "it failed", which the operator already knew; the `errno`, the `error_code` or
  the Windows error is only available at the point of failure. Origin:
  [fastcached `wire-and-protocol.md`, "Sockets"](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/rules/wire-and-protocol.md).
- **Classify a platform error in one place.** Two copies of an `errno` table drift, and the
  copy that lacks a row turns a specific cause into an unclassified one that no caller can
  match on. Same origin.
- **Exceptions are for unrecoverable conditions and for cancellation.** A recoverable error is
  a value (`std::expected`). `core::async::OperationCancelled` is thrown when a coroutine's own
  stop token cancels it, and a condition the program cannot continue past throws; see
  `cpp-guidelines.md` for what counts as unrecoverable. A cancellation that comes from the
  resource (`close()`, `cancelRead()`) is `NetErrorCode::Cancelled`, a value. A precondition
  violation is an assertion, not an error code: there is no result it could return that would be
  true.
  *(core-cpp; the design spec, Part I §2)*

## `enum class` over `bool`

**A `bool` in an API is an anonymous enum whose two values are named after their
representation instead of their meaning.** A `bool` parameter, a `bool` result that reports
success or failure, and two or more `bool` members in one type are findings; the replacement is
a purpose-named `enum class`, such as `IdlePolicy { Block, Return }` or `FdWakePolicy`.
*(contour)*

- **The call site loses the meaning.** `open(path, true, false)` tells a reader nothing.
- **The compiler stops helping.** `bool` accepts pointers, integers and characters by
  implicit conversion, and two adjacent `bool` parameters can be exchanged with no diagnostic.
  An `enum class` converts from nothing.
- **A third case rewrites every signature.** An `enum class` gains an enumerator, and `switch`
  exhaustiveness names every place that must now handle it.
- **A `bool` result is right when the function's name asks the question** (`empty()`,
  `contains()`, `running()`, `isOnWorkerThread()`). When it reports success or failure, it is
  really `std::expected<void, E>`, which carries the reason.
- **Several `bool` members are usually a state machine hiding in flags.** Where some
  combinations cannot occur, the states are one `enum class`. Flags that genuinely combine are
  one bitmask type (`Interest { None = 0, Read = 0b01, Write = 0b10 }`).
- **When you cannot** (a signature you do not own, a standard concept or comparator, generic
  code threading a `bool` template argument to `if constexpr`), say so at the declaration.
- **How to check it:** at the call site, can you tell what `true` means without opening the
  header?

## RAII for every handle

**Every resource has an RAII owner:** file descriptors, sockets, listeners, Windows handles,
coroutine handles (`UniqueCoroHandle`), timers and registrations with an event loop. A
`Task<T>`'s awaiter takes ownership of the coroutine it awaits when it is constructed, so a
temporary `Task` cannot destroy the coroutine across a suspension point. *(endo, fastcached)*

The destruction *order* of RAII owners is part of the design, and is where the lifetime bugs
live; see [`async-and-net.md`](async-and-net.md) for what an event loop requires of the
objects registered with it.

## Testability of every code area

**Every code area is testable, and new code lands with tests.** Code that is hard to test is
a design smell: inject the dependency and extract the decision into a pure function, rather
than skipping the test. A decision with no I/O in it (`selectReadinessCallback`, a timeout
computation, a retry budget) is a pure function tested over a table of cases. *(contour,
endo, fastcached)*

## Caching an expensive answer

**An answer that costs a spawn, a syscall or a walk, and is asked for more than once, is
cached only where staleness degrades safely.** *(fastcached)*

- **Staleness that costs a refusal, a miss or a retry is safe to cache**: the stale answer
  fails closed and heals itself.
- **Staleness that produces a wrong answer that looks right is not cacheable**, however
  expensive the probe. Expense is not the criterion; what a stale answer *does* is. Origin:
  [fastcached#188](https://github.com/LASTRADA-Software/fastcached/issues/188).
- **Refresh on an interval, never on a miss**: a miss-triggered refresh gives a remote peer a
  free way to force the expensive probe on every request.
- **Reach the cache through an injected seam with an injected clock.** A cache with a hidden
  clock cannot be tested.
- **Prefer not needing the cache:** computing a value once and returning what you already have
  beats caching it.
