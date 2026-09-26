# The rulebook

Each file here holds the load-bearing constraints of one part of core-cpp.
[`AGENT.md`](../../AGENT.md) carries the index and one tripwire line per file; the reasoning
lives here.

## What a rule in this directory is

**Every rule here has already been a bug**, in core-cpp or in one of the projects its code
came from. That is the entry criterion, and it is why the prose keeps the failure a rule
prevents rather than stating the rule alone. A rule with no consequence attached is one the
next reader argues away, usually correctly, because the simpler design really does look
better until you know what it does.

Most of them are *silent* failures:

- a test binary whose every case skipped, reported as broken, or one whose four failures
  were reported as skipped;
- a sanitizer, a compiler cache or an analyser that is configured, announced and inert;
- a coroutine frame that is parked, never resumed and never freed, with no diagnostic;
- a vendored copy that drifted from its upstream while every build stayed green.

If a rule's failure mode is loud, it usually does not need writing down: the build or the
test suite already says it.

Rules carried from another repository cite their origin as a full URL, so the history
behind them stays one click away. A bare issue number (a `#` followed by digits) in this
repository would link to the core-cpp issue of that number, which is a different issue.

## Files

| File | Governs |
|---|---|
| [`cpp-guidelines.md`](cpp-guidelines.md) | Every C++ source: language baseline, naming, forbidden constructs, the pinned tools. The canonical text; the documentation site includes it |
| [`design-principles.md`](design-principles.md) | How a class, module or fallible API is shaped: DI, configuration at construction, data-driven design, `std::expected`, `enum class` over `bool`, RAII |
| [`library-hygiene.md`](library-hygiene.md) | What makes core-cpp safe to consume: no global state, prefixes, vendoring, the graduation rule, public API and versioning |
| [`build-and-toolchain.md`](build-and-toolchain.md) | What differs between compilers, standard libraries, hosts and tool versions; the compiler cache; CI gates |
| [`testing.md`](testing.md) | How tests are registered, what they may assume, and the ways a suite has reported a defect as something else |
| [`async-and-net.md`](async-and-net.md) | `src/core/async/`, `src/core/net/`: layering, sockets, dialling, and socket and coroutine lifetime |
| [`platform.md`](platform.md) | `src/core/platform/`: OS seams and Windows specifics |
| [`tui.md`](tui.md) | `src/core/tui/`: the terminal UI, its output leaf and its runtime |

Neighbours: [`../guides/`](../guides/) holds how-tos rather than rules (team runs,
profiling, consumer migration, releasing), and [`../reference/`](../reference/) holds the
annotated source tree and the consumer table.

## Adding a rule

Add it to the file that governs the code it constrains, under the existing headings, and
state three things:

1. what the rule is;
2. what breaks when it is violated;
3. how you know: the test, the measurement, or the CI failure that proved it.

Then add a one-line tripwire to that file's entry in `AGENT.md` if a reader could plausibly
break the rule without noticing.

Deferred work does **not** belong here. Open a GitHub issue and link it from the file's
`## Open work` section: a residual recorded only in prose is one nobody diffs. An *accepted
trade-off* is different and does belong here, under `## Accepted trade-offs`, so that nobody
"fixes" it without reopening the argument.

An `## Open work` entry is a top-level bullet whose **leading** reference is the issue:

```
- **[core-cpp#123](https://github.com/contour-terminal/core-cpp/issues/123)** — what is left.
```

Further issue links inside a bullet's prose are citations, and a citation may name a closed
issue. The leading one may not: an entry whose issue has closed is a rule that has gone
false, and the expensive version of that is an entry saying something *cannot* be done,
which tells the next session not to try. Delete the heading when its last entry goes. The
grammar is fastcached's, where a ctest reads and resolves it
([fastcached#957](https://github.com/LASTRADA-Software/fastcached/issues/957)), and
`scripts/check-open-work.py` holds core-cpp to it
([core-cpp#12](https://github.com/contour-terminal/core-cpp/issues/12)): the ctest
`core-cpp.open-work` refuses an entry that does not lead with a core-cpp issue and a heading with no
entries, and CI's `style` job also runs it `--online`, which refuses an entry whose issue has
closed. So close an issue and delete its entry in the same change.

## Every principle names the step that carries it

**A rule that states a principle with no executable step is a sentence people agree with and do
not carry out.** `build-and-toolchain.md`'s "a gate that does not report reads as a gate that
passed" was correct, precise and already written when four silent gates happened in one hour
beside it. So each principle here names what carries it: a check that refuses the violation, a
procedure with a step a reader performs, or -- where neither exists -- a statement that no
mechanical step does, so the principle is known to rest on review. The audit below was made for
v0.1.0 (Task B13); a new principle joins it in the same change.

| Principle | File | Carried by |
|---|---|---|
| Anything that touches an ambient resource is reached through a seam | `design-principles.md` | **check**: `core-cpp.ambient-reads` refuses a direct clock or environment read outside its seam (added by this audit); the filesystem, sockets and the terminal have no textual check |
| Configuration is fixed at construction | `design-principles.md` | **review**: a setter that reconfigures is not textually distinct from one that is state |
| Behaviour is a table | `design-principles.md` | **review**, with the golden tables where a table exists (`GenericSyntaxHighlighter_test.cpp`, `NetError_test.cpp`, the socket-error tables) |
| A recoverable error is `std::expected` | `design-principles.md` | **review**: an error signalled by `bool` or a sentinel has no spelling to find |
| No `bool` in an API where an `enum class` says what it means | `design-principles.md` | **procedure**: a pull request lists every `bool` parameter it adds to a public header, with the reason it is not an `enum class`; a scan would flag every predicate |
| RAII for every handle | `design-principles.md` | **check**: clang-tidy (`cppcoreguidelines-owning-memory`), fatal |
| Namespace is directory; no global CMake state; every name prefixed; no NOLINT, no diagnostic pragma, no C-style `for` | `library-hygiene.md`, `cpp-guidelines.md` | **check**: `core-cpp.cmake-hygiene`, with its self-test |
| An include across modules is an edge of the module table | `library-hygiene.md` | **check**: `core-cpp.layering`, links and (since Task B13) includes |
| Every file has a provenance row | `library-hygiene.md` | **check**: `core-cpp.cmake-hygiene`'s `provenance` rule; drift by `core-cpp.upstream-drift` |
| An OS difference is an implementation, never an `#ifdef` in logic | `platform.md` | **check** for source selection (`core-cpp.platform-sources`); **review** for an `#ifdef` inside a function body |
| A test asserts what distinguishes | `testing.md` | **procedure**: predict the RED, neuter the fix, run, and see exactly the predicted failures -- stated there as "prove the test can fail"; no mechanical step can tell a distinguishing assertion from a vacuous one |
| Every wait is bounded and says what it waited for | `testing.md` | **check** for the bound only: every registration carries a `TIMEOUT` (`core_cpp_add_test`); the message is **review** |
| `SKIP`, never `SUCCEED`, where a case could not run | `testing.md` | **check**: `core_cpp_add_test`'s exit-code contract (77 all skipped, `core-cpp.exit-codes`); a `SUCCEED` in a skipped path is **review** |
| A gate that does not report reads as passed | `build-and-toolchain.md` | **check**: `core-cpp.preset-coverage`, `core-cpp.tree-level-coverage` (which also holds `style` in `ci-ok`'s needs), and `scripts/tidy-record.py`, which refuses a clang-tidy result that carries no instrument record |
| Every tracked text file is clean UTF-8 | `build-and-toolchain.md` | **check**: `core-cpp.text-encoding` |
| Every transport declares `cancelRead`; every `read` guards its buffer; every loop-thread-only member refuses a second thread | `async-and-net.md` | **check**: `core-cpp.cancel-read-declared`, `core-cpp.read-buffer-guard`, `core-cpp.loop-affinity-canary.*` |
| A member that files work asks for the turn that runs it | `async-and-net.md` | **check**: the parameterised case in `HostDrivenLoop_test.cpp` |
| A platform socket error is classified in one table | `async-and-net.md` | **check** for the table's rows (`SocketErrors_test.cpp`); a second private switch is **review** |
| An Open work entry leads with an open core-cpp issue | `README.md` | **check**: `core-cpp.open-work` for the grammar, and the `style` job's `--online` run for the issue's state |
| A public header change is a CHANGELOG entry | `library-hygiene.md` | **review**: the release checklist in `.agent/guides/releasing.md` |

## Do not `@`-import these

`CLAUDE.md` imports `AGENT.md`, and Claude Code resolves `@` imports recursively. An
`@`-prefixed reference to a file in this directory, anywhere in `AGENT.md`, would pull every
one of them into every session and undo the point of the split. Link them as plain markdown.
Origin: [fastcached `.agent/rules/README.md`](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/rules/README.md).
