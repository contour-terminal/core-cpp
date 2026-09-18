# log

Categorised logging. Namespace `core::log`, directory `src/core/log/`, target `core::log`.

!!! note "Status"
    Not imported yet. Task A3 of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md)
    imports contour's `LogStore` and `LogSink` (namespace `logstore` today) at `6777ff05`, with
    `gsl::not_null` replaced by a reference or an asserted pointer.

Depends on [base](base.md). Builds for WebAssembly.
