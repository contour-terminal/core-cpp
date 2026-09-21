# Task A11 re-review, round 1 — the second hygiene hole and ruling R57

Base `af392a6`, head `origin/master`. Scope: `tests/cmake/check-cmake-hygiene.cmake` and its
self-test (`review-A11-fix.diff`), and the `src/core/tui/completer/` rename
(`f28462f`, `a713187`, `cfff6ee`). `src/core/async/` has zero commits and zero diff between the
two SHAs, so item 4 is vacuously clean.

### Finding Verdicts

- **The second hygiene hole** — ADDRESSED. `tests/cmake/check-cmake-hygiene.cmake:162-169`
  assembles `expectedNamespace` from every segment of `CMAKE_MATCH_1` (the whole path under
  `src/core/`), skipping any segment in `CORE_CPP_HYGIENE_PRIVATE_DIRECTORIES`
  (`tests/cmake/check-cmake-hygiene.cmake:138`: `backend bsd darwin detail emscripten linux posix
  windows`). `src/core/platform/testing/` now requires `core::platform::testing`, not
  `core::platform`. Self-test cases for both shapes exist in
  `tests/cmake/check-cmake-hygiene-selftest.cmake:29,31` (clean tree: `foo/posix/Impl.cpp` →
  `core::foo`, `foo/testing/Fake.hpp` → `core::foo::testing`) and `:64` (case: `foo/testing/Fake.hpp`
  declaring `core::foo` must be refused). Traced by hand against the pre-fix rule
  (`^src/core/([^/]+)/` → first segment only): the `testing/Fake.hpp` case would have computed
  `core::foo` as expected and passed uncaught — a genuine RED without the fix.
- **Ruling R57** — ADDRESSED. All eight `src/core/tui/completer/*` files now declare
  `namespace core::tui::completer` (`cfff6ee`, e.g. `src/core/tui/completer/Completer.hpp:16`,
  `src/core/tui/completer/FuzzyMatch.hpp:7`). The eight `namespace-directory` allowlist rows are
  gone: `grep -n core_cpp_hygiene_allow tests/cmake/check-cmake-hygiene.cmake` shows no
  `namespace-directory` row left at all (only `diagnostic-pragma`, `source-glob`,
  `unprefixed-*`/`global-cmake-variable` rows for `CompileCache.cmake`, `FetchTransferBound.cmake`,
  `CoreCppTopLevel.cmake` and the consumer-smoke trees, none for `tui/completer`). `docs/modules/tui.md:60`
  names `core::tui::completer`. `cfff6ee`'s commit message ends `Closes core-cpp#30`.

### What the Rule Now Accepts

1. `src/core/net/posix/Foo.cpp` declaring `core::net` — **passes**. Path segments `net, posix`;
   `posix` is skipped as layout, `expectedNamespace = core::net`, matches.
2. `src/core/net/testing/Fake.hpp` declaring `core::net` — **refused**. Segments `net, testing`;
   `testing` is not in the private list, `expectedNamespace = core::net::testing`; `core::net`
   is neither equal nor a prefix-match of it, so `namespace-directory` fires. Directly proven by
   the self-test's `foo/testing/Fake.hpp` case.
3. `src/core/net/testing/posix/Impl.cpp` declaring `core::net::testing` — **passes**. Segments
   `net, testing, posix`; `posix` skipped regardless of position, `expectedNamespace =
   core::net::testing`, matches. The self-test has no synthetic case for this *combined*
   two-level shape (only the public-only and private-only cases separately), but the real tree
   exercises it directly: `src/core/net/testing/posix/InMemoryTransport.cpp:15` and
   `src/core/net/testing/windows/InMemoryTransport.cpp:17` both declare `core::net::testing` and
   are part of every `ctest -L hygiene` run, which the report's CI evidence shows green. Minor gap
   in test isolation, not a functional problem — worth a follow-up self-test case, not a blocker.
4. A file declaring a second, differently-cased namespace later in the file — **refused**. The
   loop's `firstNamespaceSeen` flag (`tests/cmake/check-cmake-hygiene.cmake`, main loop) confines the
   directory check to the first declaration but checks every later one with
   `elseif(declared MATCHES "[A-Z]")`. Self-test case
   `tests/cmake/check-cmake-hygiene-selftest.cmake:63` (`namespace core::foo { namespace Detail
   {} }`) exercises exactly this and predates this diff (part of the already-approved first-hole
   fix) — still present and still passing after this round's changes.

**Exempt-list check:** `posix, windows, linux, bsd, darwin, emscripten, detail` all match real
private-layout directories in the tree (`src/core/{log,net,platform,tui}/{posix,windows,linux,bsd,
detail}`, confirmed by `find src/core -type d`) and the `cmake/CoreCppTargets.cmake:22-24` comment
naming exactly this set for `core_cpp_add_module()`'s private-header SOURCES list. One item is
speculative: `backend` is not in that CoreCppTargets.cmake list, no `src/core/**/backend/`
directory exists yet in the tree, and neither the design spec nor the task plan names a literal
`backend/` path (the plan puts backend files straight in `posix/`, `linux/`, `bsd/`, `windows/`,
per `docs/superpowers/plans/2026-09-18-core-cpp.md:806`). It is harmless today (nothing exercises
it) and is called out honestly in both the rule's comment and the CHANGELOG entry, so it isn't
hidden — but it's an ungrounded addition to a rule that's supposed to track
`cmake/CoreCppTargets.cmake` exactly. Not a defect against either finding; flagging for whoever
plans B7's `net/` backend layout to confirm the directory name before it's load-bearing.

### New Breakage in the Fix Diff

None. `src/core/async/` has no commits and no diff in this range. The `using namespace
core::tui::completer` question (review focus #3): checked all nine type names the module exports
(`Completer`, `CompletionConfig`, `CompletionItem`, `CompletionProvider`, `FuzzyConfig`,
`FuzzyMatch`, `FuzzyMatchResult`, `SmartCaseConfig`, `SmartCaseMatch`) against the rest of
`core::tui` — none collide with a distinct declaration elsewhere. Every function that looked like
a free function from its name (`match`, `complete`, `suggest`, `quality`, `priority`,
`adjustScore`, etc.) is actually a `static` member of one of those nine types
(`src/core/tui/completer/FuzzyMatch.hpp:76-114`, `SmartCaseMatch.hpp:40-78`), always called
through explicit `ClassName::` qualification, so the using-directive can't affect their
resolution. The apparent hits for those words elsewhere in `core::tui` (`Theme.hpp`,
`KeyBindings.hpp`, `GhostTextHelper.hpp`, etc.) are comment prose, unrelated member-function calls,
or a local lambda parameter (`GhostTextHelper.hpp:57`'s `suggest` parameter) — not competing
namespace-scope declarations. Also confirmed the standard's own rule for two sibling
using-directives issued from the same external scope (here, five test files' file scope): a real
name collision would be a hard ambiguity error at the point of use, not a silent pick — so even if
one existed, it would show up as a build failure, not as a latent bug. The report's own green
build (1010/1010 clang, 1014+1 skipped MSVC) plus this check together rule it out.

Spot-checked the mechanical rename in `src/core/tui/CommandPalettePopup.cpp` and
`src/core/tui/TestHelpers.hpp`: consistently `completer::`-qualified, no stray unqualified use, no
include-path changes, matches the report's description of ~360 references becoming five `using`
lines plus explicit qualification at the thirteen `core::tui`-internal call sites.

### Verdict

**Fix round:** All findings addressed, no new Critical/Important breakage.

- The second hygiene hole: ADDRESSED (`tests/cmake/check-cmake-hygiene.cmake:162-169`, self-tested).
- Ruling R57 (completer rename): ADDRESSED (`cfff6ee`, eight allowlist rows deleted, docs/CHANGELOG/provenance updated, core-cpp#30 closed).
