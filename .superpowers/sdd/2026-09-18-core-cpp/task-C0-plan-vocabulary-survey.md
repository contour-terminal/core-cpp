# Survey: is the plan's vocabulary stale?

**No. I applied zero substitutions, and I believe applying them would have damaged the document.**

The counts are right. The count is not the measure: every occurrence of every stale-looking term is
in a position where the **old name is the correct name**.

Zero re-scope passages found. The spec has the same shape and is *less* exposed than the plan. One
real defect found, and it is in `task-B6-dispatch.md`, not in the plan or the spec — details at the
end, and it is the item worth acting on tonight.

All reads from `git show HEAD:<path>` (R87); `docs/superpowers/` had no uncommitted edits from any
lane, and I made none.

## The counts, re-measured

| term | plan | spec | your count |
|---|---|---|---|
| `core::coro` | 11 | **0** | 11 |
| `src/core/coro` | **7** | **0** | 6 |
| `EventSource` (all forms) | **17** | 6 | 10 |
| `DefaultEventSource` | 2 | 1 | 2 |
| `makeDefaultEventSource` | 1 | 1 | 1 |
| `FdInterest` | 1 | 1 | 1 |
| `IReactor` | 3 | 1 | — |
| `NetErrorCode::Other`, `BadFileHandle` | 1, 1 | 1, 1 | — |

Two differ from yours: `src/core/coro` is 7, and `EventSource` is 17 if compounds
(`DefaultEventSource`, `TerminalEventSource`, `EventSourceParity_test`, `MockEventSource`) are
counted, 10 if not. Neither difference changes the answer.

## Where each occurrence actually sits

Mapping every occurrence to its `### Task` section puts all 43 into four classes, and none of them
is "an instruction to a future task naming a thing that no longer exists".

| class | example | count |
|---|---|---|
| **The record of a completed task** | Task A5 `Create src/core/coro/{Task,...}.hpp`; Task B3 `Delete the EventSource/DefaultEventSource/*EventSource files` | 20 |
| **A mapping statement, old name on the left of `→`** | `EventSource`/`makeDefaultEventSource`/`FdInterest` → `IoBackend`/`makeDefaultBackend`/`Interest`; `IReactor` → `EventLoop`; `NetErrorCode::Other` → `SystemError` | 8 |
| **A consumer's own symbol, which keeps its name** | endo's `tui/runtime/{EventSource,PollEventSource}.hpp`, `TerminalEventSource`; tuidu's `src/{coro,platform,testing,tui}` | 12 |
| **An upstream artefact name** | fastcached `IReactor_test`, `TestReactor_test` | 3 |

The `coro` block deserves naming separately: **all 18 `core::coro` / `src/core/coro` occurrences are
inside Tasks A4, A5 and A5b**, and Task A5b *is* the rename — its heading reads *"Rename `core::coro`
to `core::async`"* and it carries the before/after table. Substituting there produces
"Rename `core::async` to `core::async`" and destroys the record of the decision.

## Your example, checked against the mechanism

> *A brief telling a fresh agent to create `src/core/coro/Task.hpp` is a brief that is wrong about
> the tree it will land in.*

That line is **Task A5's**, and A5 has run. `brief.sh` extracts, per task:

```sh
awk -v id="$id" '... /^### Task / { intask = ($0 ~ ("^### Task " id "[:( ]")) } ... intask { print }'
```

— only the named task's own section, plus the `## Phase B` (or `C`) preamble. So A5's text cannot
reach a B or C agent. The shared preamble is 13 lines and contains none of these terms; its one
match on `reactor` is the title of a section in **fastcached's** rulebook
(`§Dialing and the reactor`), which is that document's name for its own thing.

## The proposed gate would have done the damage

> *After the update, no term in the plan should match the `from` of any `delivered` row.*

Run against the plan, that gate demands **197 rewrites across 36 delivered rows**:

| would rewrite | occurrences | to |
|---|---|---|
| `net` | 59 | `core::net` |
| `coro` | 37 | `core::async` |
| `tui` | 37 | `core::tui` |
| `Read` / `Write` / `Push` / `Stop` / `Release` / `Handle` / `Threads` | 23 | their camelBack forms |
| `crispy::cli`, `logstore`, `platform::IClock`, ... | 41 | their `core::` forms |

Nearly every one is correct text. **The table's `from` side is the consumer's spelling, and the plan
is a document *about* consumers** — it says what endo, contour, tuidu and fastcached name things
today, because that is what the migration tasks have to find. A gate that forbids the old names
forbids the plan from describing its own subject.

That is the same shape as this session's other near-misses: a check whose input resembles the thing
it should read. `rewrite.py` is anchored for C++ source and would not have fired on most of these
anyway — `(?<![\w:])coro::` does not match inside `core::coro::` — so the table could not have
driven the substitution even where one was wanted.

## Re-scope passages: zero

I looked for passages wrong about *what to build* rather than *what to call it*, and found none in
the plan. Recorded explicitly because you asked for the number including zero.

## The spec: same shape, less exposed — no separate ruling needed on these grounds

All 12 of its occurrences fall in the same four classes: endo's own names (`tui/runtime/EventSource`,
`TerminalEventSource`), the four mapping lines (`IReactor → EventLoop`,
`NetErrorCode::BadFileHandle → BadHandle`, `NetErrorCode::Other → SystemError`, the EventSource
triple), and tuidu's own `src/{coro,...}` directory list.

It has **zero** `core::coro` and **zero** `src/core/coro`, so on the axis that looked worst in the
plan the spec is clean — the plan's 18 are all in A5/A5b, which the spec has no equivalent of.
A12's label-list edit and this are not in conflict, and I did not open the file for writing.

## The one real defect, and it is in a dispatch

**`task-B6-dispatch.md:15` tells B6 that `core::async::asTask(aw)` *exists*. It does not exist
anywhere in `src/`.**

The plan is right and the dispatch is wrong:

- plan `:860` (B6's checklist) — *"Implement per Part I §2 item 7. `ISocket::read/write` return
  awaitables. **Add `core::async::asTask`.**"*
- plan `:163` and spec `:144` (§2 item 7) — *"`core::async::asTask(aw)` for callers that store a
  Task"*, a statement of what the layer provides when item 7 is done. Correct.
- dispatch `:15` — *"`core::async::asTask(aw)` **exists** for callers that must store one."*

B6's dispatch is already written. A fresh agent reading it will look for a helper it is in fact
supposed to write, and the most likely outcomes are that it wastes time looking, or concludes B1
under-delivered. One word.

This is a claim about the tree rather than a rename or a scope change, so per your constraint I have
changed nothing — and it is yours in any case, since dispatches are.
