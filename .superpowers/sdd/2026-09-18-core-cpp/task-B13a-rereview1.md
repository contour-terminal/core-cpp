# Task B13a, re-review of fix round 1 (`002af92`)

Scope: the items of `task-B13a-fixround1.md` only — R80, the two Importants, Minors 4–8, and R82.
Nothing else in `src/core/tui` was re-reviewed, and `src/core/net/`, `src/core/async/`,
`tools/migrate/` and `scripts/` are other lanes'.

**Verdict: all nine items ADDRESSED.** One new **Important** defect introduced by this round —
`LanguageId::Last`'s new documentation names a compile-time guard that does not fire, which is the
same class of defect as the Critical it was written alongside. Measured, both halves.

---

## How this was verified

Nothing in `D:\core-cpp` was edited. Every mutation ran in a private detached worktree
(`<scratchpad>/wt-rereviewB13a`, at `549822b`, which carries `002af92`), so the shared checkout
never held a mutated file. Two build trees, both mine:

| Tree | Use |
|---|---|
| `out/build/rereviewB13a-clang-debug` (WSL, clang 22.1.2, `clang-debug`, `-DCORE_CPP_WITH_IMAGES=ON`) | reading the shipped behaviour of the tree under review |
| `<worktree>/out/build/rereviewB13a-mut` (same preset) | every mutation below |

Baseline, both trees: `core-cpp-tui-test` **3870 assertions in 1033 test cases, all passing**
(the round added exactly 10 cases over the 1023 the review measured), `core-cpp-tui_output-test`
34/34 with 1 skip.

| Command | Result |
|---|---|
| `python -m mkdocs build --strict` (from the worktree, `-d <scratchpad>/site`) | clean; the `!!! warning` admonition renders — I read it out of `site/modules/tui/index.html` |
| `python scripts/clang-format.py --check` on the five touched C++ files | `5 file(s) are formatted with clang-format 22.1.8` |
| `ctest -R 'layering|migrate-renames'` | both **Passed** (`migrate-renames` is green now, unlike last round) |
| `git grep` for every phrasing of the retracted claim, tree-wide | nothing outside the review/report documents themselves |

Seven mutations, each applied to the worktree, built, run, and reverted (`diff -q` against a
pristine copy after each):

| # | Mutation | Suite |
|---|---|---|
| A | `{ ".endo-format", Yaml }` back into `FilenameLanguageTable` | **red**, 1 case: `GenericSyntaxHighlighter.filename_table_is_the_shipped_set` (`_test.cpp:968`) |
| B | un-thread `StyledText.cpp:473` alone | **red**, 1 case: `StyledText.fromMarkdown.a_registered_fence_tag_highlights_its_code_block` (`StyledText_test.cpp:191`) |
| C | un-thread `MarkdownRenderer.cpp:831` alone | **red**, 1 case: `MarkdownRenderer.stream.a_registered_fence_tag_highlights_its_code_block` (`MarkdownRenderer_test.cpp:1592`) |
| D | un-thread `MarkdownRenderer.cpp:855` alone | **red**, same case, same assertion as C |
| E | un-thread `StyledText.cpp:440` alone (not asked; the review said it was held only by `-Werror`) | **red**, same case as B — it is now held by a test |
| H1 | append `Rust` after `LanguageId::Last`, no switch case | **does not compile** — `-Werror,-Wswitch` at `GenericSyntaxHighlighter.cpp:2328` |
| H2 | H1 **plus** the switch case the compiler demands, no `BuiltinLanguageTable` row | **compiles, green, 3870/1033** — see the new finding |

---

## The items

### Critical (R80) — retract the claim, keep the identity, document the precondition

**1. Does the new test exercise two registries? — ADDRESSED.**
`src/core/tui/GenericSyntaxHighlighter_test.cpp:1393-1430`. It builds `toys` (toy first) and
`others` (doll first) and asserts the wrong-language outcome, not the retracted one. I ran it with
`-s` rather than trusting the source:

```
_test.cpp:1420: PASSED: REQUIRE( *doll == *toy )
_test.cpp:1422: PASSED: CHECK( highlightLine("let x", *toy, HighlightState::Normal, &others).first
                               == HighlightMap(5, Cat::String) )
with expansion:  { 3, 3, 3, 3, 3 } == { 3, 3, 3, 3, 3 }
```

`3` is `String`, doll's category, reached through toy's id — the review's probe, now an assertion.
The empty-registry case survives at `:1404-1407`, but it is now labelled as a consequence rather
than as the guarantee, which is the correct relationship between the two.

**2. Is the built-in half asserted? — ADDRESSED** (`:1426-1429`): `.cpp` resolves identically in
`toys`, in `others` and in an empty registry (`== LanguageId::Cpp`, an absolute value, not just an
equality), and `highlightLine("int x;", Cpp)` agrees across both registries and the no-registry free
function. One weakness, recorded as Minor N2 below: the two highlight assertions are equalities, so
they would also hold if all three paths returned plain text.

**3. Did the retraction reach all four places? — ADDRESSED, and a fifth.** I grepped for the claim
rather than for the files:

| Place | Now says |
|---|---|
| `GenericSyntaxHighlighter.hpp:81-92` (`isRegisteredLanguage`) | `@warning` with the precondition, the cost, the `std::vector::iterator` analogy, and the portable built-in half |
| `GenericSyntaxHighlighter.hpp:235-237` (class `@note`) | belongs to its registry; points at `isRegisteredLanguage()` |
| `GenericSyntaxHighlighter.hpp:278-282` (`SyntaxHighlighterRegistry::highlightLine`) | the fourth site the ruling did not name; "resolves to whatever sits in the same position here" |
| `CHANGELOG.md:605-613` (+ the snippet comment at `:565-566`) | a paragraph of its own; the snippet warns to hold one registry |
| `docs/modules/tui.md:125-135` | `!!! warning`, verified rendered |
| `tools/migrate/renames.json`, `core::tui::LanguageId::Endo` note | "it belongs to that registry -- hold one registry per program, or keep each id with the registry that issued it" |

`git grep -i` for "plain text rather than as some other language", "means nothing to another
registry", "only means something to the registry", "meaningful only to", "unstyled" and "degrade"
over `src/`, `docs/`, `.agent/`, `CHANGELOG.md` and `renames.json` returns **no** surviving
statement of the old guarantee. The two hits that remain are true: `GenericSyntaxHighlighter.cpp:2321`
documents the internal `highlightBuiltin` helper (where any non-built-in id genuinely is plain
text), and `.hpp:280` is the corrected sentence.

### Important — the third table

**ADDRESSED.** `FilenameLanguageTable` is public at `GenericSyntaxHighlighter.hpp:453-462`, with the
golden copy and case at `_test.cpp:988-1013`.

I put `{ ".endo-format", LanguageId::Yaml }` back myself (mutation A). The suite went **red** with
one failing case, `GenericSyntaxHighlighter.filename_table_is_the_shipped_set`, at
`REQUIRE(shipped.size() == golden.size())` — `9 == 8`. That is the row that left the suite green at
3540/1023 last round.

Coverage is not weaker than the other two tables: the same `checkTableMatchesGolden` (`_test.cpp:966`,
`REQUIRE` on size, `CHECK` on token and language per row), **plus** a loop that resolves every row
through `detectLanguageFromPath()` bare and under a directory (`_test.cpp:1005-1012`), which the
extension and fence-tag cases do not do. The one cosmetic difference — the golden copy is local to
the `TEST_CASE` rather than file-scope like its two siblings — costs nothing.

Also checked the method, not just the result: `git grep -n "to_array<LanguageToken>\|to_array<LanguageName>"
-- src/core/tui` now returns four shipped tables (three token tables plus `BuiltinLanguageTable`)
and three golden copies, with no table left in a `.cpp`.

### Important — three uncovered call sites

**ADDRESSED**, with one nuance worth recording.

Un-threaded **one at a time**, each turns the suite red:

- `StyledText.cpp:473` → `StyledText.fromMarkdown.a_registered_fence_tag_highlights_its_code_block`
  (`StyledText_test.cpp:177`, failing at `:191`).
- `MarkdownRenderer.cpp:831` → `MarkdownRenderer.stream.a_registered_fence_tag_highlights_its_code_block`
  (`MarkdownRenderer_test.cpp:1571`, failing at `:1592`).
- `MarkdownRenderer.cpp:855` → **the same case, the same assertion line**.

So every one of the three sites is individually covered — none can be forgotten silently, which is
what the finding asked for. The nuance: `:831` and `:855` are not *distinguished*; one case fails
for either, so it tells you the streaming path broke, not which of its two lines. That is Minor N1,
not a re-opening — the sites are in the same function pair and a failure points at both.

Bonus, unasked: mutation E shows `StyledText.cpp:440` is now held by a test too, not only by
`-Werror=unused-parameter` as the review measured.

### Minors 4–8 — decided and recorded?

| # | Verdict | Where, and what convinced me |
|---|---|---|
| 4 `.editorconfig` shadowing | **ADDRESSED — refuse** | `GenericSyntaxHighlighter.cpp:2423-2426` checks each extension against `FilenameLanguageTable` and refuses with `TokenInUse`; test SECTION at `_test.cpp:1300-1313` also asserts the built-in still answers for the path afterwards. The check can only ever fire for the three dotfile rows, since every other row fails `starts_with('.')` first — minimal and correct. |
| 5 dotless extension | **ADDRESSED — refuse** | `.cpp:2414-2417` (`size() < 2 \|\| !starts_with('.')`) and `:2431-2434` (empty fence tag), new `LanguageRegistrationError::MalformedToken` at `.hpp:209`. Three SECTIONs at `_test.cpp:1263-1298` cover dotless, bare `"."` and the empty fence tag, each asserting the error *and* the token. |
| 6 `CapacityReached` | **ADDRESSED** | `SyntaxHighlighterRegistry.the_reserved_id_range_runs_out` (`_test.cpp:1317-1349`): 128 registrations, each id asserted as `128 + i`, the 129th refused, and a full registry still answering for `.lang0`, `.lang127`, `.cpp` and not for the refused token. Ran it: the three new registry/table cases are 312 assertions. |
| 7 `LanguageId::Last`'s documentation | **ADDRESSED as written, but the new text is wrong** — see New Finding 1 | `.hpp:62-69`, against `src/core/net/NetError.hpp:46-51`. The shape now matches the sibling; the guard it names does not exist. |
| 8 "a consumer edits nothing" | **ADDRESSED** | `CHANGELOG.md:557-564`: "recompiles unchanged" plus both exceptions — the address-of case (a default argument is not part of a function type) and the forward-compatibility `TokenInUse` on a later built-in. Both are accurate. |

### R82 — the two `renames.json` rows

**ADDRESSED.** Both rows exist at `HEAD` (I read them out of `git show HEAD:tools/migrate/renames.json`,
not only the working tree, which another lane is regenerating):

- `core::tui::LanguageId::Endo` — carries the full `registerLanguage({...})` call, the corrected
  `std::expected<LanguageId, LanguageRegistrationFailure>` return, the `*result` dereference, the
  one-registry precondition, and why there is no rewrite row.
- `core::tui::registerEndoHighlighter` — prose: own a registry, fill it at startup, pass it to the
  four entry points, each trailing and defaulting to `nullptr`.

`ctest -R migrate-renames` passes, which is also the assertion that both symbols are absent from
this tree. Minor N4 below: only the first note carries code a consumer can paste.

---

## New findings

### Important (new) — `LanguageId::Last`'s new doc names a guard that does not fire

`src/core/tui/GenericSyntaxHighlighter.hpp:62-69`. The text written this round says:

> One appended after `Last` still satisfies the switch in highlightLine() and still leaves `Last`
> looking like a count, and everything that walks `[0, Last)` — BuiltinLanguageTable, name() and the
> golden tests — would miss it; **builtinLanguageTableIsIndexedByLanguageId() is what refuses that,
> at compile time.**

Both halves are wrong, and I measured each:

- **H1** — append `Rust` after `Last`, nothing else: the build **fails**,
  `GenericSyntaxHighlighter.cpp:2328:17: error: enumeration value 'Rust' not handled in switch
  [-Werror,-Wswitch]`. So an appended enumerator does *not* "still satisfy the switch"; the switch is
  the first and only thing that notices it.
- **H2** — append `Rust` after `Last` **and** add the `case LanguageId::Rust:` the compiler just
  demanded, without a `BuiltinLanguageTable` row: **the build is clean and the suite is green,
  3870 assertions in 1033 test cases.** `builtinLanguageTableIsIndexedByLanguageId()`
  (`.hpp:349-360`) compares `BuiltinLanguageTable.size()` with `LanguageId::Last`, and appending
  *after* `Last` moves neither, so the `static_assert` cannot fire. It fires only if the author also
  adds the table row — i.e. only when the mistake is already half corrected.

This is the Critical finding's own shape: a public header sentence that promises a guard which is
not there, and a maintainer who trusts it. It is milder — the victim is a future core-cpp author, not
a consumer — but it was introduced by the very edit that was supposed to bring this enumerator up to
`NetErrorCode::Last`'s standard, and `NetErrorCode::Last` is scrupulous about exactly this: it says
the appended code "breaks nothing that would otherwise catch it… **Measured**: with such a code in
the enumeration the other twelve cases pass green", and then names a *runtime* test that does catch
it (`NetError_test.cpp:118`, "No code hides above Last", scanning `[CodeCount, 256)` through
`toString`).

`src/core/tui` has no equivalent: `git grep -n "Last" src/core/tui/GenericSyntaxHighlighter_test.cpp`
is **empty** — no test names `LanguageId::Last` at all.

Two ways to close it, and I would take both:

1. Say what is true: `-Wswitch` on `highlightBuiltin`'s switch is what refuses an appended
   enumerator, and the `static_assert` refuses it only once it has a table row. The sibling's
   `toString()` carries a paragraph telling the next author not to add a `default:`;
   `highlightBuiltin`'s switch (`.cpp:2328`) still carries none, which is the other half of the
   review's M4 and is what makes the first sentence load-bearing.
2. Mirror the sibling's test: scan `[to_underlying(Last), FirstRegisteredLanguageId)` and assert each
   value has an empty `name()` and highlights as plain text. That is what would have turned H2 red.

### Minor

- **N1 — one case covers two call sites.** `MarkdownRenderer.cpp:831` and `:855` both fail
  `MarkdownRenderer.stream.a_registered_fence_tag_highlights_its_code_block` at the same assertion
  (`MarkdownRenderer_test.cpp:1592`). Each site is covered; neither is distinguished. Asserting the
  detected language separately from the highlighted spans would split them.
- **N2 — the portable-built-in half is asserted by equality.** `_test.cpp:1428-1429` compares
  `toys.highlightLine("int x;", Cpp)` with `others`' and with the free function. If built-in
  highlighting regressed to plain text in all three paths the assertions would still hold. One
  `CHECK` that the map is not uniformly `Default` would make it assert what it claims. (Other cases
  in the suite would catch that regression, which is why this is Minor.)
- **N3 — `docs/modules/tui.md` never mentions `MalformedToken`.** The page still describes refusal
  as "saying which name, extension or fence tag was already claimed" (`:106-109`). The CHANGELOG
  (`:597-601`) carries the new class of refusal — a dotless or empty extension, an empty fence tag —
  and the docs page does not, though a consumer writing `.extensions = { "wob" }` meets it on the
  first run. The file-name shadowing half *is* on the page (`:141-142`).
- **N4 — only one of the two `renames.json` notes carries pasteable code.** The
  `registerEndoHighlighter` note describes the registry pattern in prose; its sibling carries the
  literal `registerLanguage({...})` call. Since both rows are read by the same migrating consumer at
  the same moment, the prose one is adequate, but "text a consumer could paste" is true of one row
  and not the other.

### Nits (no action expected)

- `docs/modules/tui.md:137` says "The three built-in tables the module ships", where
  `BuiltinLanguageTable` is a fourth (it maps names, and has its own golden case). "Three token
  tables" would be exact.
- `LanguageRegistrationError::MalformedToken`'s doc (`.hpp:209`) says "an extension without its
  leading dot, or an empty extension or fence tag"; a bare `"."` is also refused and is neither,
  unless you read it as "an extension that is empty after the dot".
- `docs/modules/tui.md:88-89` still sells "two parts of one program can hold different ones" three
  paragraphs above the warning that says two registries are the hazard. The warning does address it
  head on, so this is a reading-order observation, not a contradiction.
- `firstCharacterHighlighter()` is now defined twice, in `StyledText_test.cpp:141-175` and
  `MarkdownRenderer_test.cpp:1478-1509`. Both are in anonymous namespaces, so this is duplication
  between test TUs, not an ODR problem.

---

## For the controller

One thing to rule on: **whether New Finding 1 is a fix-round item or a follow-up ticket.** It is a
false claim in a public header, which is the category this round exists to eliminate, and the fix is
two sentences plus a ~10-line test. It does not affect any consumer, and the committed behaviour is
correct — only the comment is wrong. Everything the round was asked to do, it did.
