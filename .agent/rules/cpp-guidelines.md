# C++ coding guidelines

This is core-cpp's canonical C++ rule set. It is self-contained: nobody needs a global note, a
personal rules file or another repository's guidelines to write code here. The documentation
site includes this file verbatim, and [`AGENT.md`](https://github.com/contour-terminal/core-cpp/blob/master/AGENT.md)
carries a short list that points here.

The rules merge the guidelines of the projects core-cpp's code comes from. Each one names its
origin, so the history behind it is one click away:

| Short name | Source, at the commit core-cpp imported from |
|---|---|
| contour | [contour `AGENT.md`](https://github.com/contour-terminal/contour/blob/6777ff05014f8ff163b071e8b0e942830119db80/AGENT.md) |
| endo | [endo `AGENT.md`](https://github.com/contour-terminal/endo/blob/f774a210ce989e5947b8f61d715068b1dc96088c/AGENT.md) |
| fastcached | [fastcached `AGENT.md`](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/AGENT.md) |
| Lightweight | [Lightweight `.agent/cpp-guidelines.md`](https://github.com/LASTRADA-Software/Lightweight/blob/f57dc2e0704d885a3c642a63675873919fc2d128/.agent/cpp-guidelines.md) |
| tuidu | [tuidu `AGENT.md`](https://github.com/contour-terminal/tuidu/blob/30107fbab72310fde5db89e7882eab288f6b541e/AGENT.md) |

How a class, a module or a fallible API is *shaped* (dependency injection, configuration at
construction, data-driven design, `std::expected`, `enum class` over `bool`) is in
[`design-principles.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/design-principles.md).
This file is about how the code is *written*.

## Language baseline

- **C++23, with `CXX_EXTENSIONS OFF`.** Prefer what C++23 offers: `constexpr`, `std::ranges`
  and range views, `std::format`/`std::print`, `std::expected` with its monadic methods,
  `std::span`, structured bindings, coroutines. *(contour, endo, fastcached, Lightweight)*
- **The WebAssembly subset is held to libc++ 17.** Code in a module or file that builds
  under single-threaded Emscripten (the table in the
  [module overview](https://contour-terminal.github.io/core-cpp/modules/)) checks the
  `__cpp_lib_*` feature-test macro before it uses a library facility newer than that, and
  uses no `std::thread`, `std::jthread` or blocking wait. emsdk 3.1.56 ships libc++ 17.0.4
  (`_LIBCPP_VERSION` 170004), and it is a CI leg. *(core-cpp; the design spec, Part I §1)*
- **Use `std::span` for arrays and contiguous sequences** in an API, never a pointer and a
  length. *(contour, endo, fastcached, Lightweight)*
- **Use `auto` where it reads better**, and structured bindings for tuple-like results.
  *(contour, endo, fastcached, Lightweight)*
- **`const` correctness throughout:** references, pointers, member functions, and parameters
  that are not mutated. *(contour, endo, fastcached, Lightweight)*

## Forbidden constructs

Each of these has a mechanical check or a review question behind it, and none has an
exemption short of an allowlist row that states its reason.

- **No C-style `for (init; cond; step)` loops.** Use a range-based `for` over a range
  (`std::views::iota(0, n)`, `std::span{argv, argc}.subspan(1)` for `argv`) or an algorithm.
  `tests/cmake/check-cmake-hygiene.cmake` refuses the three-clause form.
  *(contour, endo, fastcached, tuidu)*
  - **Convert a loop by reading its body, not its head.** A loop whose body advances the
    variable, whose callee advances it through a `std::size_t&`, whose bound is inclusive or
    compound, or whose variable outlives it is not a mechanical conversion. The details, and
    the wrong cache key one of them produced, are in
    [`build-and-toolchain.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/build-and-toolchain.md).
    Origin: [fastcached#1452](https://github.com/LASTRADA-Software/fastcached/issues/1452).
- **No raw owning pointers.** Ownership is a `std::unique_ptr` or `std::shared_ptr`, and every
  resource has an RAII owner. *(contour, endo, fastcached, Lightweight)*
- **No `NOLINT`.** Fix what clang-tidy reports. Where a check is wrong for the whole tree, the
  answer is a documented exception in `.clang-tidy`; names the standard library binds by
  spelling (`value_type`, `begin`, `await_ready`, `promise_type`, ...) are covered by
  `.clang-tidy`'s `IgnoredRegexp`, so they need no comment either. The hygiene scan refuses
  the token. *(contour, endo, fastcached, Lightweight, tuidu)*
- **No diagnostic-muting pragmas** (`#pragma warning`, `#pragma clang diagnostic`,
  `#pragma GCC diagnostic`), and no widening of `-Wno-*` without a row in
  `cmake/CoreCppToolchain.cmake` that says why the finding cannot be fixed in core-cpp's code.
  The one allowlisted pragma is `SuppressWindowsDialogsAtStartup.cpp`'s `init_seg(lib)`, whose
  reason is on its row in the hygiene scan. *(contour's zero-warning policy)*
- **The ban is on the act, not on the spelling: it covers every language's suppression comment.**
  A `# noqa` is the Python `NOLINT`, and so is a `# type: ignore`, a `// eslint-disable`, a
  `# ruff: noqa` or a `#[allow(...)]`. Enabling ruff's linter over `tools/migrate/` retired three
  of them and none needed to stay: two were inert once a real rule set existed, and the third was
  an `E402` on an `import` under a `sys.path.insert` -- which went away for a **better reason than
  suppression**, because Python already puts a script's own directory first on `sys.path`, so the
  surgery the import needed excusing for was never needed at all. **A suppression comment is
  usually a fix nobody looked for.** *(Task C0)*
- **No new third-party dependency** without an option that gates it, a row in
  `cmake/CoreCppDependencies.cmake` saying how it is found or fetched, and a CHANGELOG entry.
  See [`library-hygiene.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/library-hygiene.md).
  *(contour, endo, fastcached, Lightweight)*
- **Recoverable errors return `std::expected`; exceptions are for unrecoverable conditions.**
  A failure its caller can act on (report it, retry, fall back, take another path) is a value:
  the operation returns `std::expected`, or a result type such as
  `core::platform::ChannelResult` where an outcome is no failure at all. An exception is for a
  condition the program cannot continue past where it happens, and for cancellation:
  `core::async::OperationCancelled`, which a coroutine throws when its own stop token cancels it.
  A function that throws says so (`@throws`) and names the condition.
  *(core-cpp; endo and tuidu reserve exceptions for the same cancellation path)*
  - **Unrecoverable** means that no caller between the failure and `main()` has a meaningful
    alternative, so a value handed back would only be passed on by each of them, or ignored.
    Examples:
    - `core::platform::Wakeup`'s constructor, when the operating system refuses the eventfd,
      self-pipe or event because descriptors, handles or kernel memory are exhausted: no event
      loop can run without its wakeup channel.
    - `core::testing::ScopedTempDir` and `ScopedWorkingDirectory`, when they cannot set up: a
      fixture that cannot set up must fail its test, and Catch2 reports a throw from a
      constructor as exactly that. An empty path handed back instead would put the test's files
      in the working directory.
    - Memory exhaustion, which the standard library reports as `std::bad_alloc`.
  - **Recoverable**, and so a value: anything a caller can expect in normal operation and handle.
    A file that does not exist, a permission refused, malformed input, a peer that closed, a
    channel with nothing to read yet, a timeout, a full buffer, a busy resource. When in doubt,
    it is recoverable: a caller that cannot go on can still treat a value as fatal, while a
    throw costs every other caller a `try` block.
  - **A precondition violation is neither**: it is an assertion, because there is no result the
    function could return that would be true.

## Zero warnings

**The tree is warning-free, and a warning is a build break.** Every preset turns
`CORE_CPP_WERROR` on, and the pedantic warning set is the union of endo's and fastcached's
lists (`cmake/CoreCppToolchain.cmake`). Fix the cause of a warning; never silence it. clang-tidy
findings are treated the same way (`WarningsAsErrors: '*'`). *(contour)*

`CORE_CPP_WERROR` decides *fatality*, not *which warnings exist*: a `-Wno-<x>` row belongs
beside the flag that makes it necessary, under the same condition, never under the WERROR
switch. [`build-and-toolchain.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/build-and-toolchain.md)
has the measurement that made this a rule.

## Naming

`.clang-tidy` is authoritative and wins over any prose here; this is what it enforces.
*(contour, endo, fastcached)*

| Kind | Spelling | Example |
|---|---|---|
| Types, type aliases, enums, enumerators | `CamelCase` | `EventLoop`, `NetErrorCode::ConnReset` |
| Functions, methods, variables, parameters, local `const` values | `camelBack` | `runOnce`, `nextDeadline` |
| Private and protected data members | `_camelBack` | `_clock` |
| `constexpr` variables and class constants, including descriptor tables | `CamelCase`, **no prefix** | `SkipExitCode`, `DefaultBackends` |
| Macros (a convention; `.clang-tidy` does not check it) | `CORE_` or `CORE_CPP_` + `UPPER_CASE` | `CORE_ZONE_SCOPED` |
| Files (a convention) | the type they hold, `CamelCase` | `EventLoop.hpp`, `EventLoop_test.cpp` |

- **No `k` prefix and no `g_` prefix**, on anything. A constant is `CamelCase`; a mutable
  file-scope or `thread_local` name is `camelBack` like any other variable. The prefix is a
  substitute for a naming convention rather than one, and it makes ambient state read as
  normal; if a bare name looks wrong at its use, that is the dependency-injection rule
  telling you something. *(endo, fastcached, tuidu)*
- **A surviving `bool` reads as a predicate:** `_isVisible`, not `_visible`. *(contour)*
- **A header's outermost namespace is `core::<directory>`.** Headers directly in `src/core/`
  are `core`; nested helper namespaces (`detail`, `testing`, `base64`, `views`, `ranges`) are
  allowed inside. Spell it `namespace core::<directory>`, not `namespace core { namespace <directory>`.
  The hygiene scan (`namespace-directory` in `tests/cmake/check-cmake-hygiene.cmake`) refuses
  a source under `src/core/` whose first named namespace is anything else. *(core-cpp; the
  design spec, Part I §1)*

## Files and headers

- **Every source file starts with `// SPDX-License-Identifier: Apache-2.0`** (`#` in CMake),
  on its first line. The hygiene scan refuses a file without it. *(core-cpp)*
- **Headers are `.hpp` and carry `#pragma once`**, never an include guard. *(contour)*
- **Headers sit next to their sources**, under `src/core/<module>/`, and a test sits next to
  what it tests: `Foo.hpp`, `Foo.cpp`, `Foo_test.cpp`. *(fastcached, endo)*
- **Every header is self-contained:** it compiles on its own, with no precompiled header and
  nothing included before it. Every `.cpp` includes what it uses. *(Lightweight, fastcached)*
- **Private headers stay private.** `detail/`, `posix/`, `linux/`, `bsd/`, `darwin/`,
  `windows/`, `emscripten/` and `tui/platform/` are in no `FILE_SET`, and a consumer must not
  include them.
  *(core-cpp; the design spec, Part I §1)*
- **Include with angle brackets from the `src/` root:** `#include <core/net/EventLoop.hpp>`.
  `.clang-format` groups and orders the `<core/...>` includes. *(contour)*

## Functions and types

- **`[[nodiscard]]` on every value-returning function where ignoring the result would be a
  bug**, which includes everything that returns `std::expected`, every builder step whose
  result must be terminated, and every query. *(contour, endo, fastcached, Lightweight)*
- **Coroutine parameters are taken by value.** A reference or a view parameter names storage
  the caller may destroy while the coroutine is suspended. clang-tidy's
  `cppcoreguidelines-avoid-reference-coroutine-parameters` catches the reference half and
  cannot see a `std::string_view` or `std::span`, so take an owning type (`std::string`,
  `std::vector`) where the coroutine outlives the call expression. *(tuidu; fastcached
  measured the view half:
  [fastcached `wire-and-protocol.md`, "Dialing"](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/rules/wire-and-protocol.md))*
- **A value returned from a decoder owns its bytes.** A struct that borrows (`std::string_view`,
  `std::span`) from the buffer it was decoded from is named `*View`; anything else owns.
  `decode(encode(x))` is the obvious spelling and a use-after-free the moment one member
  borrows. Origin: [fastcached#366](https://github.com/LASTRADA-Software/fastcached/issues/366),
  [fastcached#395](https://github.com/LASTRADA-Software/fastcached/issues/395).
- **An `enum class` has an explicit underlying type** (`: std::uint8_t`), and its zero
  enumerator is the off, absent or default case, so a zero-initialised value means what
  `false` would have meant. *(contour)*
- **Every `bool` and byte-wide enum in a struct sits in one run.** One byte between two
  eight-aligned members costs seven bytes of padding, and clang-tidy's padding check fails the
  build on the sum. Origin: [fastcached `build-and-toolchain.md`, "Language and ABI
  pitfalls"](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/rules/build-and-toolchain.md).
- **Two functions that differ only in their return type are one symbol on Linux.** The
  Itanium ABI does not mangle a free function's return type, so they collide at link time and
  every caller of one silently reaches the other, while MSVC, which mangles it, links both
  and stays green. Give them different names. Same origin as above.

## Documentation

- **Doxygen `///` on every public function, class, struct and member**, stating what it does,
  each parameter and the return value:

  ```cpp
  /// Short description of what it does.
  /// @param name What it is.
  /// @return What it returns.
  ```

  The API reference at <https://contour-terminal.github.io/core-cpp/api/> is generated from
  these comments. *(contour, endo, fastcached, Lightweight)*
- **Be factual.** Documentation states what the code does, its limits and its current state,
  with no marketing prose. *(endo, Lightweight)*

## The pinned tools

- **clang-format and clang-tidy run at the version CI pins**: `.clang-format-version` and
  `.clang-tidy-version` each name one PyPI release (22.1.8). Successive LLVM releases format
  the same file differently, and an older clang-tidy is silent about checks it does not have,
  so a tree clean under whatever binary is on `PATH` can still be rejected by CI.
  Install the pins with `python scripts/tool-versions.py --install`; format with
  `python scripts/clang-format.py <paths>` (or `--all`), which refuses every other build, and
  refuses a bare run rather than rewriting a tree somebody else is working in. *(fastcached)*
- **Never run `clang-format -i` with another version.** As a checker an older binary is worth
  something; as a formatter it rewrites code the pinned one already accepted, and the diff is
  invisible in review because every line of it is "just formatting". Run any other build with
  `--dry-run` only. *(fastcached)*
- **clang-tidy runs through the `clang-tidy` preset**, which points `CORE_CPP_CLANG_TIDY_EXE`
  at the pinned binary and warns when it is another version. *(fastcached)*

## Checklist before pushing

1. `python scripts/clang-format.py --all --check` is clean.
2. The `clang-tidy` preset builds with no finding, and no `NOLINT` was added.
3. `clang-debug` and `gcc-release` build without a warning and pass `ctest` (on Windows:
   `cl-debug` and `clangcl-release`).
4. New and changed behaviour has a test next to the code, and the test was seen to fail
   without the change.
5. New public API has Doxygen comments and `[[nodiscard]]` where ignoring it is a bug.
6. A public header change has a CHANGELOG entry under `[Unreleased]`, and the pull request
   says what it means for each consumer ("Consumer impact").
7. `/simplify` was run; findings outside the change were either addressed or reported.

*(Lightweight's checklist, adapted to core-cpp's presets)*

## Open work

- **[core-cpp#10](https://github.com/contour-terminal/core-cpp/issues/10)** — document the
  public API the Doxyfile selects, then set `WARN_AS_ERROR = FAIL_ON_WARNINGS` so an
  undocumented declaration fails the Docs workflow.
- **[core-cpp#31](https://github.com/contour-terminal/core-cpp/issues/31)** — nothing enforces
  "every header is self-contained". The rule is stated above and `check-cmake-hygiene.cmake` has
  eighteen rules, all of which read text; **none of them compiles anything**, so a header that
  needs another included first passes every check this tree has. It only breaks for whoever
  includes it first, which in a library is a consumer. The gap surfaced the way an unenforced
  rule usually does — Task B1's dispatch told that lane to exclude a header from "the header
  self-check", an instruction it could satisfy by doing nothing, because there is no such check
  to be excluded from. Enforcing it means compiling each `FILE_SET HEADERS` entry as a
  translation unit of its own, which is a build-time check rather than a `cmake -P` scan, so it
  is a per-leg test and not a `tree-level` one: whether a header is self-contained is a question
  for each toolchain.
