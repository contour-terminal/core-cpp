# Dependency injection

Everything in core-cpp that touches time, I/O, the network, the file system, the environment or
the terminal is reached through an interface that an object is given at construction, and every
such interface has a test double in its module's `testing/` directory. That is what lets a
consumer test its own code over core-cpp without a real socket, a real clock or a real terminal.

| Resource | Interface | Test double |
|---|---|---|
| Time | `core::platform::IClock`, `IWallClock` | `ManualClock`, `ManualWallClock` |
| Readiness and completion | `core::net::IoBackend` | `testing::ScriptedBackend`, `testing::NullBackend` |
| The browser's event loop | `core::net::IHostScheduler` | `testing::ManualHostScheduler` |
| An event loop | `core::net::EventLoop` (an `IExecutor`) | `testing::TestLoop` |
| A connection | `core::net::ISocket`, `IListener`, `IConnector` | the in-memory transport |
| Files | `core::platform::FileSystem`, `FileInfoProvider` | `testing::InMemoryFileSystem`, `testing::MockFileInfoProvider` |
| The environment | `core::platform::EnvironmentProvider` | `testing::TestEnvironmentProvider` |
| Terminal output | `core::tui::TerminalOutput` | `MockTerminalOutput` |

These arrive with the module imports (Tasks A4 to A7) and the merged async layer (Phase B).

## How an object is built

- **Collaborators and configuration go to the constructor**, and are fixed afterwards. A
  constructed object is a usable one: no `init()`, no setters to call first.
  `EventLoop(IoBackend&, IClock&, EventLoopOptions)` is the shape.
- **Options that belong together are a struct** (`EventLoopOptions`, `DialOptions`), not a long
  parameter list and not setters.
- **Fallible construction is a static factory** returning `std::expected<T, E>`.
- **A platform difference is an implementation of an interface**, chosen once where the program
  is composed, never an `#ifdef` in logic.

The rules, with the reasons and the exceptions, are in
[`design-principles.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/design-principles.md).
