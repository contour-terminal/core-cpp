# core-cpp#31: a header self-check, before v0.1.0

`.agent/rules/cpp-guidelines.md` requires every `.hpp` to be self-contained. You established that
**all eighteen hygiene rules read text and none compiles anything**, so the requirement is
unenforced: a header needing a neighbour included first passes every check this tree has, and
breaks only for whoever includes it first — **which in a library is a consumer**.

Six consumer migrations are about to include these headers in orders nobody here has tried. That
is why this lands before the tag rather than after it.

## What to build

A check that compiles **each public header as the first and only include of a translation unit**,
and fails naming the header and what it needed. Public means the module's `FILE_SET HEADERS` —
derive the list from the build, not from a glob, so a header added tomorrow is covered without
anyone remembering.

**It is per-leg, not `tree-level`** — your own distinction, and it is the reason this cannot ride
the `style` job: whether a header stands alone is a question for each toolchain, and MSVC, clang,
GCC, AppleClang and emcc will not all answer it the same way.

## The cost question, which is the real design question

~155 public headers means ~155 translation units per leg. **Measure before you commit to a
shape**: build the whole set on `clang-debug` and on `cl-debug`, with fastcache-cc as it normally
runs, and report the added wall-clock. Then choose:

- one generated `.cpp` per header, all in one object library — thorough, and the cache makes the
  second build cheap;
- or a cheaper shape you argue for, if the measurement says the first is not affordable.

If it is expensive, say so with the number and propose the trade rather than quietly narrowing the
set. A check that covers half the headers and says it covers them all is the failure this
repository has spent the evening finding.

## Constraints that bear on it

- **The WebAssembly subset**: headers excluded from the Emscripten `FILE_SET` must be excluded
  here too, and `ThreadPoolExecutor.hpp` `#error`s under single-threaded Emscripten by design.
- **Private headers are not in scope** — `detail/`, `posix/`, `linux/`, `bsd/`, `windows/`,
  `emscripten/` and the tui `platform/` directories hold implementation headers that are allowed
  to assume their includer.
- **Self-test it**, in the shape the other checkers use: a header that needs a neighbour must be
  refused by name, and a clean set must pass. **Red against `HEAD`'s checker first**, and predict
  the failure count before you run.
- Expect it to **find real defects on its first run**. That is the point; report them rather than
  fixing them silently, because some will belong to other lanes' in-flight modules.

## Then

`ctest -L hygiene` and the per-leg run green; `ruff` clean; a CHANGELOG line under `Added`
(a consumer does not observe the check, but they observe the guarantee it makes true, so this one
does earn an entry — say which you chose and why). Close core-cpp#31 in the commit message, and
delete its `## Open work` entry in the same commit, since an entry outliving its issue is the
rot the rulebook's own grammar exists to prevent.

Report to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-hdr-selfcheck-report.md`: the
measurement, the shape you chose and why, the self-test REDs, and every real defect the first run
found with whose module it belongs to.
