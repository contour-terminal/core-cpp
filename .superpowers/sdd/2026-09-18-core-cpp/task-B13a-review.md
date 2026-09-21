# Task B13a review: `core::tui` stops shipping endo's language (core-cpp#24)

Reviewer: task reviewer. Commits under review: `f61d57b`, `d0345d0`, `aa49c53`.
Everything in `src/core/async/`, `src/core/net/`, `tools/migrate/` and `scripts/` is another lane's
and is excluded.

**Spec compliance: met.** The seam answers core-cpp#24 and every bullet of the dispatch, with one
recorded and defensible departure (constraint 1's literal "comments too" reading).

**Task quality: Changes requested.** One Critical and two Important findings, all reproduced by
running. The code is correct for the cases it tests; the defect is that a load-bearing safety claim
in the public API documentation is false, and the test named for that claim asserts only the one
case where it holds.

---

## How this was verified

Private build tree `out/build/reviewB13a-clang-debug` (WSL, clang 22.1.2, `clang-debug` preset,
`-DCORE_CPP_WITH_IMAGES=ON`). Nothing else in the checkout was configured, built or formatted. Every
source mutation below was reverted in the same shell invocation and the tree confirmed clean
(`git status --porcelain src/core/tui/` empty) and green afterwards.

| Check | Result |
|---|---|
| `core-cpp-tui-test`, full | **All tests passed (3540 assertions in 1023 test cases)** — matches the report exactly |
| `core-cpp-tui-test '[tui][highlight]'` | 690 assertions in 79 cases, pass |
| `FuzzyMatchResult.isContiguousSubstring_true_despite_earlier_grapheme` | 12 assertions, pass |
| `python scripts/clang-format.py --check` | 13 files unformatted, **all `src/core/net/`** (another lane). No `src/core/tui/` file is in the list. |
| `git grep -nP '\b(LanguageId::Endo\|registerEndoHighlighter)\b' -- src/core/tui` | empty (exit 1) |
| `git grep -nP '"\.?endo"' -- src/core/tui` | empty (exit 1) |
| `git grep -n "registerEndoHighlighter\|LanguageId::Endo"` tree-wide | only `CHANGELOG.md`, the plan, and another lane's uncommitted `tools/migrate/*_test.py` |
| CHANGELOG placement | entry at `CHANGELOG.md:478-520`, inside `### Breaking` (334-548), not `Changed`. Correct. |
| `mkdocs build --strict` | **not run** — taken on the report's word |

Four mutations were applied and reverted to measure what the tests actually hold:

| Mutation | Suite |
|---|---|
| Un-thread `highlighters` at **both** `StyledText.cpp:440,473` | does not compile — `-Werror=unused-parameter` |
| Un-thread `StyledText.cpp:473` + `MarkdownRenderer.cpp:831,855` | **green**, 3540/1023 |
| Add `.acme-format` to `FilenameLanguageTable` | red, 1 failure (`detectLanguageFromPath`) |
| Add `.endo-format` to `FilenameLanguageTable` | **green**, 3540/1023 |

A scratch program linked against the built `libcore-cpp-tui.a` probed six registry behaviours that
the suite does not; its output is quoted in the findings.

---

## The six questions the brief asked

### 1. Did the seam replace the special case, or shadow it?

**It replaced it.** `SyntaxHighlighterRegistry` holds `std::vector<LanguageDefinition> _languages`
(`GenericSyntaxHighlighter.hpp:280`) and hands out `FirstRegisteredLanguageId + index`. It is a real
n-slot registry, not one named slot.

- **Two languages register independently**: verified by probe — two registrations, distinct ids
  (128, 129), each token resolving to its own language, each highlighter running.
  `SyntaxHighlighterRegistry.a_second_registration_does_not_clobber_the_first`
  (`GenericSyntaxHighlighter_test.cpp:1111`) asserts the same.
- **Order does not matter for detection.** Because every collision is refused before anything is
  stored (`GenericSyntaxHighlighter.cpp:2426-2439`), no two rows can claim the same token, so
  `findByToken`'s registration-order scan cannot produce an order-dependent answer. Probed both
  orders: `a` = toy-then-doll, `b` = doll-then-toy; `find("toy")` resolves in both and both
  highlight as `Keyword`.
- **A registered language cannot displace a built-in.** `detectFromExtension` /
  `detectFromFenceTag` consult the `constexpr` tables first (`.cpp:2476-2490`), and a definition
  naming a built-in name, extension or fence tag is refused (`a_claimed_token_is_refused`, four
  SECTIONs). `find("cpp")` answers `LanguageId::Cpp` even with registrations present.

The one order dependency that remains is **which id** a language gets, and that is where Finding 1
lives.

### 2. Injected or global?

**Injected, honestly.** `registerEndoHighlighter()`'s file-scope `static HighlightFunction` is gone
and nothing replaced it: there is no `static`, no singleton accessor, no `inline` mutable in the
header. A registry is an ordinary copyable value; `MarkdownRenderer` takes it in the constructor
rather than through a setter (`MarkdownRenderer.hpp:68-71`), which is the right call in a class that
already carries a row of `setX()` debt. Copy semantics work: a copied registry keeps its ids
meaningful (probed). `design-principles.md`'s "inject every ambient resource" is satisfied, and no
"what stood in the way" excuse was needed because nothing did.

The report's correction of the dispatch is right: `Buffer.cpp` and `Terminal.cpp` do not call
`highlightLine` / `detectLanguageFrom*` / name `LanguageId` in this repository — verified by grep.
They are Task C1's.

### 3. `LanguageId::Last` as public API

**Sanctioned and contained**, with one documentation gap.

- It reaches exactly one `switch` in the whole tree — `highlightBuiltin`
  (`GenericSyntaxHighlighter.cpp:2350-2367`) — and that switch names it, so `-Wswitch` still fires
  for a forgotten real language. Grep confirms no other `switch` over `LanguageId` exists anywhere
  in `src/`.
- A caller *can* construct it and pass it. Probed: `highlightLine("abc", LanguageId::Last)` returns
  a 3-element map of `Default` (plain text), and `registry.name(LanguageId::Last)` and
  `registry.name(static_cast<LanguageId>(200))` both return an empty view. Nothing reads out of
  bounds; `name()`'s `index < BuiltinLanguageTable.size()` guard (`.cpp:2464`) is what saves it.
- **Consistency with `NetErrorCode::Last`**: same shape, weaker words. See Finding 6.

### 4. The golden tables

**The duplication is genuinely independent** — `ExpectedExtensionTable`
(`GenericSyntaxHighlighter_test.cpp:821-889`) and `ExpectedFenceTagTable` (`:892-962`) are literal
`std::to_array<LanguageToken>` initialisers in the test's anonymous namespace, with their own rows
and their own comments. Nothing derives them from `ExtensionLanguageTable` /
`FenceTagLanguageTable`; `checkTableMatchesGolden` (`:965`) compares element by element with the
size as a `REQUIRE`. The report's RED evidence (60 == 59, 70 == 69) is exactly what this shape
produces.

**Is the two-place edit worth it?** Yes, but for a smaller reason than claimed. It does not make a
new row "a decision taken on purpose rather than one a code review has to catch" — the golden copy
is a verbatim paste of the shipped table, so the natural way to make the build green again is to
paste the same row into the second place in the same motion. What it *does* buy, and what is worth
the 128 rows, is that the row cannot arrive **silently**: something has to be edited twice and the
second edit is in a file called `_test.cpp`, which a reviewer reads differently. Keep it.

**But it covers two of the three built-in tables.** See Finding 2.

### 5. Removal completeness, and the grep

**The report's arithmetic is exactly right — I recounted it.** `git grep -i endo -- src/core/tui`
returns 30 lines. 16 are ordinary English: fourteen `clearToEndOfLine` / `clearToEndOfDisplay`
spellings across `Buffer.{cpp,hpp}`, `MockTerminalOutput.{cpp,hpp}`, `Screen.cpp` and
`TerminalOutput.{cpp,hpp}`, plus two `vendored` in `runtime/WithTimeout.hpp:13,18`. The remaining 14
are all comment lines, and I read each one: five CMake comments recording the import
(`CMakeLists.txt:3,12,57,60,71`), seven `///`/`//` provenance notes
(`Terminal.cpp:19`, `TerminalInput.cpp:9`, `TerminalOutput.cpp:17,291`, `detail/XtVersion.hpp:10`,
`posix/ImageLoader.cpp:15`, `windows/ImageLoader.cpp:9`), one `.clang-tidy:5` justification for the
cognitive-complexity bump, and `Buffer.cpp:263`, which records why core::tui's hyperlink id differs
from endo's. **None of them uses endo as an example of a language**, which is what the dispatch's
constraint 1 was actually aimed at. Refusing to delete them, and saying so, was the right call —
deleting them contradicts the upstream-sync discipline and `.agent/reference/provenance.md`.

The proposed replacement gate is a different matter: see the gate section at the end.

### 6. `d0345d0`'s `FuzzyMatch_test.cpp` rename

**Every property is preserved, and the test passes.** `endo.exe` → `demo.exe` in
`FuzzyMatch_test.cpp:354-380`:

- Eight graphemes, so `REQUIRE(result.positions.size() == 8)` is unchanged.
- Haystack length unchanged (`./build/clangcl-debug/src/shell/` is 32 graphemes either way), so
  `countGraphemes(...)` and the `start + i` loop still describe the same block.
- The regression's actual property — *the pattern's first character occurs earlier in the haystack,
  and the rest of the pattern is still findable as a subsequence after it, so a greedy matcher has
  somewhere wrong to go* — holds: `d` occurs at `./buil**d**` and in `-**d**ebug`, and `emo.exe` is
  a subsequence of what follows either one. Ran it: 12 assertions, pass.
- The two comments disagree cosmetically about *which* earlier `d` ("`./build/clangcl-debug`" above,
  "the stray 'd' in `build`" below). Both are true. Not a finding.

Out of scope for the dispatch, yes — but the same rule, its own commit, and reverting it costs
nothing. I would keep it.

---

## Findings

### Critical

**C1. A registered id passed to a different registry selects a *different language*, not plain text
— and three public documents promise the opposite.**
`src/core/tui/GenericSyntaxHighlighter.hpp:71-78`, `:216-217`; `CHANGELOG.md:486-489`;
`docs/modules/tui.md` ("Registering a language", final paragraph);
test `GenericSyntaxHighlighter_test.cpp:1269`.

The header says:

> A registered id only means something to the registry that produced it; passing one anywhere
> else highlights the line as plain text rather than as some other language.

and the CHANGELOG, the docs site and the report's "Cost, stated plainly" all repeat it. It is the
recorded justification for choosing the reserved-id-range identity over the two alternatives.

**It is false.** Ids are dense from 128 in registration order, with no per-registry discriminator,
so registry `b`'s id 128 is whatever `b` registered first. Probed:

```
2. a.find(toy)=128 b.find(toy)=129  (ids differ across registries: true)
   both highlight as Keyword: true
   a's toy id inside b -> category 3 (Default=0, String=3)
```

`a` registered `toy` then `doll`; `b` registered `doll` then `toy`. Passing `a`'s `toy` id to
`b.highlightLine()` ran **`doll`'s highlighter** — category `String`, not `Default`. It degraded to
a different language, which is precisely the failure mode
`SyntaxHighlighterRegistry::registerLanguage`'s refusal policy exists to prevent, quoted in its own
Doxygen: *"the holder of that id would then get a wrong answer that looks right"*.

Concrete scenario: an application builds one registry for its editor pane (registers its own
language first) and a second for its help viewer (registers, say, a SQL highlighter first) —
exactly the "two parts of one program can hold different ones" the design advertises as the benefit
of injection over a global. A `LanguageId` obtained from `detectLanguageFromPath(path, &editorRegistry)`
and later handed to `helpViewer.highlightLine(...)` paints the line with SQL rules. No diagnostic,
no exception, no `std::expected`; the text is simply coloured wrong.

**The test that names this property cannot fail for it.**
`a_registered_id_without_its_registry_is_plain_text` (`:1269-1284`) checks the id against (a) no
registry and (b) `auto const other = SyntaxHighlighterRegistry {}` — an **empty** one. The empty
registry is the single case where the claim holds, because `index >= _languages.size()` catches it
(`.cpp:2509`). `.agent/rules/testing.md`, "Assert what distinguishes": this assertion does not
distinguish the safe design from the unsafe one.

What convinced me: the probe output above, plus reading `registeredLanguageIndex`
(`.cpp:2338-2341`) and `SyntaxHighlighterRegistry::highlightLine` (`.cpp:2497-2513`) — there is
nothing in the id and nothing in the lookup that could tell one registry's ids from another's.

Three ways out, in increasing cost; **any of them closes this, and one of them must land before
v0.1.0**, because the words are public API and the ids are the identity choice:

1. **Say what happens.** Replace "highlights the line as plain text rather than as some other
   language" with "is unspecified: it may highlight as a different registered language, or as plain
   text if the other registry has fewer languages" in the header, the CHANGELOG and
   `docs/modules/tui.md` — and extend the test to the two-registry case so the words and the
   assertion agree. Cheapest, but it retracts the reason the identity was chosen.
2. **Make the claim true.** Give each registry a cookie (a `std::uint32_t` counter or an address
   hash) stored in the registry and checked in `highlightLine`, with `LanguageId` unchanged — only
   possible if the id carries the cookie, which it cannot in a `std::uint8_t`. So this means a
   second member on the registry plus a `registryCookie()` accessor and a documented "ids are only
   valid against the registry that issued them; passing one elsewhere is a precondition violation" —
   checkable in debug, not in release.
3. **Change the identity**, i.e. the `{ LanguageId, index }` struct the report rejected. Expensive
   and, in my view, not warranted by this alone.

I recommend (1) plus the strengthened test. The identity choice is still the right one; it is the
*advertised* cost that is wrong, and a cost stated accurately is still a good trade.

### Important

**I1. `FilenameLanguageTable` is the third built-in table, and nothing guards it. A consumer's
dotfile can come back, green and invisible to the proposed gate.**
`src/core/tui/GenericSyntaxHighlighter.cpp:2316-2325`.

The two golden tables cover `ExtensionLanguageTable` and `FenceTagLanguageTable`.
`FilenameLanguageTable` — the table that **actually held a consumer's row**, endo's `.endo-format`,
until Task A7's fix round — has no golden copy, because it lives in an anonymous namespace in the
`.cpp` and the test cannot see it. Its only guard is a single hard-coded negative probe,
`CHECK(detectLanguageFromPath(".acme-format") == LanguageId::None)`
(`GenericSyntaxHighlighter_test.cpp:173`).

Measured, both ways:

- Adding `{ .token = ".acme-format", .language = LanguageId::Yaml }` → **red**, 1 failure. That is
  the probe firing on the one name it knows.
- Adding `{ .token = ".endo-format", .language = LanguageId::Yaml }` → **green**, 3540/1023. And the
  report's own gate `git grep -nP '"\.?endo"' -- src/core/tui` **does not match it** (the regex
  requires the closing quote immediately after `endo`), verified: exit 1, no output.

So the exact row this task exists to keep out can be re-added, and neither the durable half of the
gate nor the grep half notices. Fix: give `FilenameLanguageTable` the same treatment as the other
two. Either move it to the header beside them (it is already `constexpr` data of a public type) with
its own golden copy, or expose it through a `[[nodiscard]] auto builtinFilenameTable() ->
std::span<LanguageToken const>` for the test. The first is cheaper and consistent.

**I2. Three of the changed call sites have no test at all.**
`src/core/tui/StyledText.cpp:473`, `src/core/tui/MarkdownRenderer.cpp:831`, `:855`.

`StyledText::fromMarkdown` gained the registry and threads it to two places; the new
`MarkdownRenderer` cases cover neither, and `StyledText_test.cpp` never constructs a
`SyntaxHighlighterRegistry` — grep confirms it names none. `MarkdownRenderer::processStreamBuffer()`
(the `beginStream` / `feedToken` / `endStream` path) is a second copy of the fence-and-highlight
logic and is likewise uncovered for the registry.

Measured: un-threading `StyledText.cpp:473` and both of `MarkdownRenderer.cpp:831,855` leaves the
suite at **3540 assertions in 1023 test cases, all passing**. Had the implementer forgotten any one
of those three lines, nothing would have said so. (`StyledText.cpp:440` is incidentally protected,
but by `-Werror=unused-parameter`, not by a test: un-threading *both* of StyledText's uses fails to
compile, un-threading *one* does not.)

This matters more than the usual coverage nag because `StyledText::fromMarkdown` is endo's likely
entry point — it is the second line of the migration snippet in the CHANGELOG. A consumer following
that snippet exercises code nothing in core-cpp proves.

Fix: two cases. `StyledText.fromMarkdown` with a registry and a ```` ```toy ```` fence, asserting
the spans split (the `SpanRecordingOutput` idiom in `MarkdownRenderer_test.cpp:1478` transfers
directly, or assert over `StyledLine`'s spans). And one streaming case — `beginStream()`,
`feedToken("```toy\n")`, `feedToken("let x\n")`, `endStream()` — asserting the same split as
`MarkdownRenderer.a_registered_fence_tag_highlights_its_code_block`.

### Minor

**M1. `registerLanguage()` does not consult `FilenameLanguageTable`, so a claimed token is accepted
and then silently shadowed.** `src/core/tui/GenericSyntaxHighlighter.cpp:2433-2439`.

The refusal checks run against `detectFromExtension` / `detectFromFenceTag`, which see
`ExtensionLanguageTable` and `FenceTagLanguageTable` but not the filename table. Probed:

```
1. register .editorconfig accepted = true
   detectFromExtension(".editorconfig") -> 128 (registered id = 128)
   detectFromPath("/x/.editorconfig")  -> 13 (Ini = 13)
```

Two of the registry's own accessors disagree about the same file. An application that registers
`.editorconfig`, `.clang-format` or `.clang-tidy` as an extension gets `has_value()` and a working
`detectFromExtension`, and then `detectFromPath` — the accessor it will actually call on a file —
answers the built-in. Narrow (three names), but it is the one hole in a refusal policy whose whole
point is "never a wrong answer that looks right". Fix: check each extension against
`FilenameLanguageTable` too, or document that well-known file names outrank registered extensions
(the header already documents it for `detectFromPath`; `registerLanguage` does not).

**M2. A dotless extension registers successfully and can never match.**
`src/core/tui/GenericSyntaxHighlighter.hpp:179`, `.cpp:2433`.

Probed: `{ .extensions = { "toy" } }` (no dot) → `has_value() == true`, then
`detectFromPath("a.toy")` → `None`. `detectLanguageFromPath` always passes
`fileName.substr(dotPos)`, which always carries the dot, so a dotless row is dead on arrival. The
doc comment says "each with its leading dot (\".toy\")" but nothing enforces it, and the failure is
silent — the application sees a successful registration and no highlighting, ever. An API that has
already decided to refuse `EmptyName` and `NoHighlighter` should refuse this; a
`LanguageRegistrationError::MalformedToken` row costs one enumerator. (Uppercase fence tags are a
different story and fine as they are: `extractFenceLanguage` (`MarkdownRenderer.cpp:57-78`) does not
lowercase, so built-in tags are case-sensitive too and registered ones match the same way.)

**M3. `LanguageRegistrationError::CapacityReached` is public API with no test.**
`src/core/tui/GenericSyntaxHighlighter.hpp:191`, `.cpp:2423`.

It works — probed: refused at registration #128 with `CapacityReached`, highest id handed out 255,
`registeredCount() == 128`, no wraparound. But nothing in the suite reaches it, so the one arithmetic
in the file that could overflow a `std::uint8_t` is unproven in CI. The loop is ~10 lines and runs in
microseconds; add it.

**M4. `LanguageId::Last`'s documentation is thinner than its sibling's, and the report's "no
precedent" claim is wrong.** `src/core/tui/GenericSyntaxHighlighter.hpp:61` vs
`src/core/net/NetError.hpp:46-48`.

`NetErrorCode::Last` says "Not a code: the number of codes above it… **Never constructed, never
returned, never compared against a result**", and `toString()` carries a paragraph explaining why
its `switch` has no `default` and telling the next author not to add one. `LanguageId::Last` says
"Not a language: how many built-ins there are, which BuiltinLanguageTable uses", and
`highlightBuiltin`'s `switch` carries no such note. The shapes agree; bring the words up to the same
standard, since these two enumerators are the repository's only instances of this pattern and a
future author will read one and copy the other.

Separately: report Concern 3 says "The repository had no precedent for the `Last` sentinel". It did.
`8a88ce0` ("net: one NetError vocabulary for both lineages", 2026-09-20 23:54:22) introduced
`NetErrorCode::Last` and **is an ancestor of `f61d57b`** (23:58:25) — four minutes earlier in a
parallel lane. Not a fault, but the report states as fact something a `git grep -n "Last,"` would
have contradicted, and the controller is being asked to rule on consistency between the two.

**M5. Two small gaps in the "Consumer impact" story.**
`CHANGELOG.md:483-485`.

- *"a consumer that uses only those edits nothing: every new parameter is trailing and defaults to
  'the built-ins alone'"* is true for **calls**. It is not true for anyone who takes the address of
  `highlightLine`, `detectLanguageFromExtension`, `detectLanguageFromFenceTag` or
  `detectLanguageFromPath` — the function type changed, so a stored function pointer or an explicit
  `std::function<...>` spelling stops compiling. Unlikely, one clause to cover.
- **Forward compatibility is unstated**: once a consumer registers `.foo`, core-cpp adding `.foo` to
  `ExtensionLanguageTable` in a later release turns that registration into a runtime `TokenInUse`,
  and a consumer that ignores the `std::expected` loses its language silently. The golden tables make
  adding a built-in row deliberate, which is the mitigation; say so where the policy is described, so
  the next person to add a built-in extension knows it is a consumer break and not a free row.

---

## `tools/migrate/renames.json`: the decision was right and is now owed a follow-up

The report's reasoning — every `kind` in the table drives a mechanical rewrite to a target that must
exist, and both removals change the call *shape*, so no row — was correct **at the time it was
written**. It is also now overtaken: another lane's uncommitted working-tree changes
(`tools/migrate/check_renames_test.py`, `tools/migrate/rewrite_test.py`, both ` M`) add exactly the
`kind: "removed"` the report said would be needed, and their fixtures use
`core::tui::LanguageId::Endo` and `core::tui::registerEndoHighlighter` as the worked examples.
`renames.json` itself (committed at `23:56`, two minutes before `f61d57b`) has no row for either.

Nothing for B13a to do — but **for the controller**: once Task C0's `removed` kind lands, these two
symbols want their rows, and C0's author has evidently already decided they should have them.

---

## My own opinion on B13's gate

I tested the implementer's proposal against the tree. Its diagnosis is right and its prescription is
half right.

**Right:** `git grep -i endo src/core/tui` cannot be the gate. It matches `vendored`,
`clearToEndOfLine`, `clearToEndOfDisplay` — 16 of 30 hits are ordinary English. A gate that fails on
the word "vendored" in a comment about vendoring trains people to ignore it.

**Keep, verbatim:**

```
git grep -nP '\b(LanguageId::Endo|registerEndoHighlighter)\b' -- src/core/tui   # verified empty
```

Precise, currently empty, and it is the API half of the requirement.

**Drop or rewrite `git grep -nP '"\.?endo"'.** Verified: it misses `".endo-format"`, the exact row
that was in this module until Task A7's fix round. Widen it to `'"\.?endo\b'` or, better, replace it
with the table coverage below.

**Drop the tree-wide name grep.** It fails in three separate ways, all measured:

1. **It is not empty today.** Run on the clean tree it reports
   `src/core/tui/detail/XtVersion.hpp:80: constexpr auto Known = std::array<std::string_view, 3> {
   "kitty", "contour", "mintty" };` — the XTVERSION reply table, where `contour` is a *terminal
   emulator being detected*, not a consumer concept. A gate whose first run needs an allowlist entry
   is a gate people will disable.
2. **It does not run in the maintainer's own shell.** `grep -P` under Git Bash here fails outright:
   `grep: -P supports only unibyte and UTF-8 locales`, exit 2. It needs `LC_ALL=C.UTF-8` on both
   sides of the pipe, and a CI gate that only works under one locale is a gate that will silently
   stop working.
3. **Its exit codes are inverted.** `git grep` exits 1 when it finds nothing — the *pass* — and the
   `| grep -v` pipeline exits 0 when it emits a finding and 1 when it filters everything, so a gate
   wired on `$?` reads pass as fail and vice versa. `build-and-toolchain.md`'s "a gate that does not
   report reads as passed" is the same hazard from the other side.

**What I would make the gate instead** — assert the tables, in the test binary, where it runs on
every build on every platform rather than in one script:

1. The `LanguageId::Endo` / `registerEndoHighlighter` grep above. One line, precise, already clean.
2. **All three** built-in tables under a golden copy, not two. `FilenameLanguageTable` is the one
   that actually carried a consumer's row and is the one currently unguarded (Finding I1); moving it
   to the header next to the other two and giving it an `ExpectedFilenameTable` closes the hole that
   `.endo-format` walks straight through today.
3. `builtin_language_list_is_the_shipped_set`, unchanged — it pins the names as well as the tokens.

That is a gate that fails for the right reason, reports in every CI job, needs no allowlist, and does
not depend on PCRE, a locale, or a shell's exit-code conventions. If B13 also wants a name sweep,
scope it to the module's `FILE_SET HEADERS` — the public API is where a consumer concept actually
costs something — and accept that comments below that line are provenance the repository has decided
to keep.

---

## Verdict

**Changes requested.**

Required before this is done:

- **C1** — the cross-registry claim, in the header, the CHANGELOG and `docs/modules/tui.md`, plus a
  two-registry case in `a_registered_id_without_its_registry_is_plain_text`.
- **I1** — a golden copy for `FilenameLanguageTable`.
- **I2** — a `StyledText::fromMarkdown` registry case and a streaming-path registry case.

M1-M5 are the author's call; M1 and M3 are each under ten lines and I would take them in the same
round.

Nothing here questions the architecture. The identity choice, the decision to leave the built-ins
`constexpr` beside the seam, refusing rather than replacing, constructor injection over a setter,
and the three decisions being written down at all are all right, and the test suite is far above the
median for this repository. The Critical finding is that one sentence describing that architecture
is not true, and it is free to fix for exactly as long as v0.1.0 is untagged.

Also noted, and not this task's: `f61d57b`'s tree has 13 unformatted files in `src/core/net/`, and
`core-cpp.async`, `core-cpp.async-fallback`, `core-cpp.net`, `core-cpp.cmake-hygiene` and
`gcc-release` fail for other lanes' reasons, exactly as the report says.
