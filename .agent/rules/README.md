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
([fastcached#957](https://github.com/LASTRADA-Software/fastcached/issues/957)); until core-cpp
has that check ([core-cpp#12](https://github.com/contour-terminal/core-cpp/issues/12)),
keeping entries true is a review question here.

## Do not `@`-import these

`CLAUDE.md` imports `AGENT.md`, and Claude Code resolves `@` imports recursively. An
`@`-prefixed reference to a file in this directory, anywhere in `AGENT.md`, would pull every
one of them into every session and undo the point of the split. Link them as plain markdown.
Origin: [fastcached `.agent/rules/README.md`](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/rules/README.md).

## Open work

- **[core-cpp#12](https://github.com/contour-terminal/core-cpp/issues/12)** — a check that reads
  every `## Open work` entry and refuses one whose leading reference is not a core-cpp issue, or
  whose issue has closed.
