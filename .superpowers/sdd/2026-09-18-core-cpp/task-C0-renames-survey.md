# Survey: does `renames.json` still describe Phase B?

**Answer: yes — 0 missing rows across Phase B — and the zero is load-bearing rather than empty,
because the probe reports a symbol the moment its row is deleted, on all three row kinds.**

One real defect found, and it is not a missing row: **nothing requires a `removed` row to be
qualified, and a badly spelled one is silently inert rather than wrong.** Details below.

Surveyed at `HEAD` = `870d12b`, table = the working tree's 484 rows.

## What was surveyed

| Task | State | Commits | Public headers D/R/M | Public symbols vanished | Covered | Uncovered |
|---|---|---|---|---|---|---|
| B1 async grafts | COMPLETE | 6 | 5 | 5 | 0 | **5** (all `detail::`) |
| B2 NetError union | CLOSED | 7 | 2 | 1 | 1 | 0 |
| B3 IoBackend | DONE_WITH_CONCERNS | 5 | 8 | 22 | 20 | **2** |
| B13a highlighter seam | CLOSED | 5 | 3 | 2 | 2 | 0 |

B4 is in flight; its three rows are `pending` and carry their targets (below). B5–B12 have not
started. B12b parts 1–2 were surveys and delivered no public API.

## The method, and why it is this one

The question is **not** "do the 484 rows validate" — `check-renames.py` answers that on every
build, and it answers it about the rows that *exist*. The question is whether a row is **missing**:
a symbol a consumer writes today that Phase B renamed or deleted, with nothing pointing at its
replacement. That failure is silent by construction, because a gate over rows cannot see a row
nobody wrote.

Per Phase B commit:

1. every public header the commit **deleted, renamed or modified** (an added file has no pre-image
   and so cannot have lost a symbol);
2. the qualified symbols its **pre-image** declared **at namespace scope**;
3. which of those no longer exist anywhere in `src/core` today;
4. for each vanished symbol, whether **any** row mentions it — as a `removed` row's `from`, any
   row's `to`, any row's `target.symbol`, or any row's `from` **in the consumer's spelling**.

Step 4's last clause is the one that matters and the one I got wrong first. **A row's `from` is the
consumer's spelling — `net::EventSource`, not `core::net::EventSource`** — so asking the table about
the core-cpp name alone answers "no row" for nearly every symbol that has one. The table's own
`namespace` rows are the map between the two, so the probe reverses them: for a vanished
`core::net::X` it also asks about `net::X`, for `core::async::X` about `coro::X` and `endo::coro::X`,
and so on.

### Two wrong answers on the way, both from the probe being too generous

The first run reported **83** uncovered, including `core::net::stop_requested`,
`core::net::testing::push_back` and `std::erase_if`. The extractor's free-function arm matched any
`identifier(` and qualified it with the enclosing namespace, so every **member call** became a
namespace-scope symbol. Fixed by tracking brace depth: a name counts only where the depth equals
the depth of its enclosing namespace, so a member, a local or a nested class's body is excluded.
That took 83 to 22.

The second run reported **22**, because `mentions()` only asked about the core-cpp spelling. Adding
the reversed namespace rows took 22 to **7**.

Both errors were over-reporting, which is the safe direction and was deliberate — under-extraction
would have hidden a real gap, and step 3 filters anything that still exists. But it is worth
recording that the first number I could have reported was wrong by an order of magnitude.

## The control

A zero that cannot become non-zero proves nothing. The probe was run against mutated tables with a
known-covering row deleted, one row-kind at a time, and required to name **exactly** the affected
symbols and nothing else:

| Row dropped | Expected newly uncovered | Got | |
|---|---|---|---|
| `net::EventSource -> core::net::IoBackend` (a **rename** row) | `core::net::EventSource` | same | **PASS** |
| `core::net::FdToken` (a **removed** row) | `core::net::FdToken` | same | **PASS** |
| `net::EventSourceKind -> core::net::BackendKind` (an **enum** row) | the enum **and its 3 enumerators** | same | **PASS** |

The third case also proves the enumerator-inheritance path: dropping the parent's row surfaces all
three enumerators, so the probe is not silently crediting them.

## The 7 uncovered symbols, and why each is correctly absent

Decisive test: **is the name written anywhere a consumer keeps?** A name used only inside the files
core-cpp replaces needs no row — the migration deletes those files. Checked by `git grep` at each
consumer's pin, read-only.

| Symbol | Lost in | Named in consumers | Verdict |
|---|---|---|---|
| `core::async::detail::WhenAllRunner` | B1 `b21229d` | contour `src/coro/WhenAll.hpp`, tuidu `src/coro/WhenAll.hpp` — **only the file being replaced** | correctly absent |
| `core::async::detail::WhenAllState` | B1 | same | correctly absent |
| `core::async::detail::WhenAnyRunner` | B1 | contour `src/coro/WhenAny.hpp` only | correctly absent |
| `core::async::detail::makeWhenAllRunner` | B1 | contour + tuidu `WhenAll.hpp` only | correctly absent |
| `core::async::detail::makeWhenAnyRunner` | B1 | contour `WhenAny.hpp` only | correctly absent |
| `core::net::FdRegistration` | B3 `e7963de` | contour `src/net/{EventLoop.hpp,EventSource.hpp,PollEventSource.cpp,README.md}` — **all replaced** | correctly absent |
| `core::net::WaitFdAwaiter` | B3 `e7963de` | contour `src/net/{EventLoop.cpp,EventLoop.hpp,README.md}` — **all replaced** | correctly absent |

**The endo hits are a different type with the same name, and that is the interesting part.** endo
names `FdRegistration` in `src/tui/runtime/EventSource.hpp` and `WaitFdAwaiter` in
`src/tui/runtime/TuiRuntime.{hpp,cpp}` — but those are `core::tui::runtime::` names, not
`core::net::` ones. `src/core/tui/runtime/EventSource.hpp` still declares `FdInterest`, `FdToken`,
`WaitOutcome`, `FdRegistration` **and** `FdRegistry` — five of the exact names the `removed` rows
carry under `core::net::`. They survive until B12 and are covered by rows[156]
(`tui/runtime/EventSource.hpp -> core/tui/runtime/EventSource.hpp`).

**For B12's brief:** when the TUI runtime moves onto `core::net::EventLoop`, endo's uses of all five
of those names need rows in the same commit, and those rows will be renames or removals of
`core::tui::runtime::` names — a *second* set of rows for names the table already carries once
under `core::net::`.

## The two beliefs, tested rather than confirmed

### Belief 1: `removed` rows are fully qualified — TRUE, but the reason is the opposite of expected

All 8 are qualified: `core::tui::LanguageId::Endo`, `core::tui::registerEndoHighlighter`,
`core::net::{FdToken, WaitOutcome, FdRegistry, PollEventSource, EpollEventSource,
KqueueEventSource}`. 0 bare.

The stated risk was a collision with `core::tui::runtime`'s identically named types. **The collision
is live** — those five names all still exist under `core::tui::runtime`. But testing it rather than
reasoning about it inverts the conclusion. Rewriting rows[478]'s `from` to each spelling:

| `from` spelled | Gate |
|---|---|
| `core::net::FdToken` (as delivered) | **silent** — correct, the symbol is gone |
| `FdToken` (bare) | **silent** |
| `net::FdToken` (the consumer's spelling) | **silent** |
| `core::tui::runtime::FdToken` (a name that *does* exist — the control) | **FIRES**: *"is a removed symbol, but src/core/tui/runtime/EventSource.hpp declares it again"* |

A bare row does **not** false-positive against the TUI runtime. `qualified_failure()` with a single
component only looks for a `#define`, so it reports absent and the row passes. A two-component
`net::FdToken` needs a namespace called `net`, which nothing opens, so it also passes.

**So a badly spelled `removed` row is not wrong — it is inert.** It reads as a guard on every
review and guards nothing, in either direction, forever. That is strictly worse than a false
positive, and it is this session's recurring failure class.

### FINDING: nothing enforces the qualification

`renames.py::_row()` checks that a `removed` row has a `from`, no `to`, no `target`, a `note`, an
`apply` of `none` and a `status` of `delivered`. **It does not check that `from` is qualified**, and
the mutated tables above loaded without complaint. The 8 rows are qualified by discipline, not by
construction, and a 9th written bare would pass the schema, pass the gate, and protect nothing.

The trap is sharper than it looks, because **`removed` is the only kind whose `from` is a *core-cpp*
name.** Every other kind's `from` is the consumer's spelling. Writing `net::FdToken` — the spelling
every neighbouring row uses — produces an inert row that no check can distinguish from a working one.

I did not fix this: the table and the loader are mine, but a schema change lands with its own RED
and a CHANGELOG line, and you asked for a survey. Say the word and it is a short round: require
`"::"` in a `removed` row's `from`, and require its first component to be `core`.

### Belief 2: `pending` rows carry their `target` since R92 — TRUE, 4 of 4

| Task | Row | `target.symbol` |
|---|---|---|
| B6 | `localPort -> boundPort` | `core::net::IListener::boundPort` |
| B4 | `FastCache::IReactor -> core::net::EventLoop` | `core::net::EventLoop::submit` |
| B4 | `FastCache::PlatformReactor -> core::net::PlatformLoop` | `core::net::PlatformLoop` |
| B4 | `FastCache::TestReactor -> core::net::testing::TestLoop` | `core::net::testing::TestLoop` |

The B4 `IReactor` row watches `EventLoop::submit` rather than `EventLoop`, because `EventLoop`
already exists — the row would resolve on day one and never fire. That is the right choice and it
is the only symbol row in the table whose `to` deliberately differs from its `target.symbol`.

### `FastCache::SyncRun`: nothing is owed

rows[440] is **delivered**, not pending, with `target = (core/async/SyncRun.hpp,
core::async::syncRun)`. B1 landed `SyncRun.hpp` and the row was updated with it. The async lane owes
nothing; R92 closed a door on a gap that had already been walked through once and repaired.

## Two smaller findings

### The `to` side is unchecked for every kind but `include`

`_check_delivered` compares `row.target` — the string the codemod actually **writes into a
consumer's source** — with the tree only for `kind: "include"`. For `symbol`, `namespace`, `member`
and `macro` rows, `to` is never compared with anything. What holds the line today is that `to` and
`target.symbol` agree for **237 of 238** symbol rows, and the one exception is B4's deliberate one
above. So the hole is real, currently has exactly one occupant, and that occupant is intentional —
but a symbol row whose `to` is a typo would rewrite a consumer into a name core-cpp does not have
and pass every gate here.

### Two `macro` rows have no `target.symbol`, so only their header is checked

`CORE_GENERATOR_FORCE_FALLBACK` and `CORE_RANGES_FORCE_FALLBACK` name a `target.header` but no
symbol, and `_check_delivered` returns as soon as `target.symbol` is absent. The header is asserted
to exist and be public; that the macro is still *defined* in it is not. `defines_macro()` already
exists and would close it.

## Why the zero is meaningful

Three things separate this from "the probe found nothing":

1. **It found something when a row was taken away** — three times, on three different row kinds,
   naming exactly the affected symbols including the enumerators that ride on a parent's row.
2. **It over-reported twice before it under-reported never.** 83, then 22, then 7, each reduction
   traced to a named defect in the probe rather than to a filter I wanted.
3. **The 7 survivors were each decided by evidence outside the table** — `git grep` at four
   consumer pins — rather than by a rule that would have let them through.

What it does **not** cover: a symbol that was renamed *within* a header the probe reads as modified
but whose old name still exists elsewhere in `src/core` (step 3 drops it); anything behind an
`#if`; and the `from` side, since consumers' own renames are not in this tree. And it says nothing
about B5–B12, which have not run.

---

# Addendum: the short round, and one correction

## Is the `to` / `target.symbol` agreement enforced, or merely observed?

**Merely observed.** Nothing in `renames.py::_row()` or `check-renames.py::_check_delivered()`
compares them for any kind but `include`. The agreement holds for 237 of 238 symbol rows because
every author so far has written the same string twice, and the one divergence is B4's deliberate
`EventLoop::submit` watch.

So it is another thing true by discipline. It is **not** in this round, because closing it needs a
way for B4's row to say "these differ on purpose", which is a schema field and a design decision
rather than a check — and inventing an opt-out to enforce a rule that has never been broken is the
wrong trade tonight. It is written down here so C1 does not discover it.

## Delivered in this round

| Change | RED | Now |
|---|---|---|
| A `removed` row's `from` must be `core::`-rooted and qualified | 2 cases the loader accepted | refused, with the reason in the message |
| A `macro` row with no `target.symbol` is checked against its header | 1 case the gate passed | the header must still *name* the macro |
| The inertness mechanism pinned, so the rule cannot be simplified back out | — | `test_an_unqualified_name_is_inert_rather_than_wrong` |

102 cases in `tools/migrate`, up from 98. Predicted 3 failures before running; got exactly those 3.

## Correction: my first macro check was wrong, and the rows said so

The first version asserted the header **defines** the macro. Run over the tree it immediately
failed both real rows of that shape:

```
rows[376]: src/core/Generator.hpp defines no macro CORE_GENERATOR_FORCE_FALLBACK
rows[377]: src/core/Ranges.hpp defines no macro CORE_RANGES_FORCE_FALLBACK
```

Those rows are correct. The macro is one a **consumer** defines; core-cpp only asks whether it is
set (`#if defined(__cpp_lib_ranges_fold) && !defined(CORE_RANGES_FORCE_FALLBACK)`). That is why they
carry no `target.symbol` — and **their `note` says exactly that**: *"a macro the consumer defines,
not one core-cpp defines, so the gate checks the header alone."*

I had read those two rows' `kind`, `to` and `target` while writing the survey and had not read their
`note`. The answer was in the row I was reporting on. Same failure class as the rest of the evening:
the repository had already written down what I went on to assert against it.

The corrected check is **consults**, not **defines**: the header must define the macro or test it
(`defined(X)`, `#ifdef`, `#ifndef`, `#cmakedefine`). That still closes the real hole — a header that
survives while the macro it names is renamed — and a mention in a comment does not count, because
`_stripped()` blanks comments before the scan. Four cases pin the distinction, including one
asserting the two real rows pass, so the next person cannot tighten it back to `defines`.

## The gate's live reds are B4's, not this round's

`check-renames.py` reports 3 failures on the current tree, identical before and after this round
(verified by running `HEAD`'s copy of the gate against the same table):

```
rows[437]: core::net::EventLoop::submit now exists ... mark the row delivered (it waits on task B4)
rows[438]: core::net::PlatformLoop now exists ...
rows[439]: core::net::testing::TestLoop now exists ...
```

B4 has landed `PlatformLoop.hpp` and `TestLoop.hpp`. This is the pending arm doing precisely its
job, and the three rows flip to `delivered` in B4's commit, not mine.
