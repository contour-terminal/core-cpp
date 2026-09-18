# log

Categorised logging. Namespace `core::log`, directory `src/core/log/`, target `core::log`. It
depends on [base](base.md) and builds for WebAssembly.

Imported from contour's crispy `LogStore` and `LogSink` at `6777ff05`, where the namespace was
`logstore`; `gsl::not_null` is replaced by a reference.

| Header | What it has |
|---|---|
| `<core/log/LogStore.hpp>` | `Category`, `Sink`, `MessageBuilder`; the registry (`get()`), `configure()`, `enable()`, `setSink()`, `setFormatter()`; `errorLog` and the `errorLog()` macro |
| `<core/log/LogSink.hpp>` | the standard and error formatters, `unmatchedFilters()`, `ScopedOutput` (a process-wide destination, to standard error or appended to a file) and `ScopedCapture` (for tests) |
| `<core/log/Assert.hpp>` | `core::log::fatal()`, which logs to the always-enabled `fatal` category and aborts, and `SoftRequire()`, which logs to `error`, asserts in a debug build, and answers `false` |

## Categories and the filter

A `core::log::Category` is an object with a name, usually at namespace scope, that registers
itself when it is constructed:

```cpp
auto inline netLog = core::log::Category { "net", "Sockets and the event loop." };

netLog()("connected to {}", peer);   // formatted with std::format, written if enabled
```

`core::log::configure(filter)` enables the categories a filter names and disables every other
one, except `error`, which stays enabled so that asking for detail never hides a failure. The
filter is `all`, or a comma-separated list of names, each of which may end in `*` to match a
prefix: `net,tui.*`. `unmatchedFilters()` names the patterns that match no category, so a typo
can be reported.

Every core-cpp test binary applies `LOG` this way before it runs; see
[testing](testing.md#the-log-filter).

## Sinks

A category writes a finished line to its `Sink`. The default, `Sink::console()`, writes to
standard output and is disabled until a program enables it. `ScopedOutput` installs one
destination for every category, serialises writers from several threads under a mutex, and puts
the previous sinks and formatters back when it is destroyed. A category refers to its sink
without owning it, so a sink must outlive every category that uses it, or be replaced first.
