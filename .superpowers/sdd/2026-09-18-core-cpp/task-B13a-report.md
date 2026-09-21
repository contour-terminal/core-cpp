# Task B13a report: `core::tui` stops shipping endo's language (core-cpp#24)

**Status:** DONE_WITH_CONCERNS (one concern, about what B13's gate can grep for; nothing unresolved
in the code).

**Commits:** `f61d57b..aa49c53` on `master`.

| Commit | What |
|---|---|
| `f61d57b` | `feat(tui)!: an application registers its language, core::tui stops shipping one` — the seam, its tests, the CHANGELOG entry, `docs/modules/tui.md`. `Closes #24`. |
| `d0345d0` | `test(tui): the fixtures name no application either` — `FuzzyMatch_test.cpp` and `MarkdownImage_test.cpp` fixture data. |
| `aa49c53` | `test(tui): the detection tables stay answerable at compile time` — six `static_assert`s making every CI toolchain prove the two `constexpr` lookups are still constant-evaluable now that each takes a registry pointer whose non-null branch calls a non-`constexpr` member. |

---

## What was removed, and what replaces it

Gone from the public API: `LanguageId::Endo`, `registerEndoHighlighter(HighlightFunction)`, the
`.endo` row of `ExtensionLanguageTable`, the `endo` row of `FenceTagLanguageTable`, and the
`case LanguageId::Endo:` dispatch — together with the file-scope `static HighlightFunction` that
`registerEndoHighlighter()` wrote to.

New, all in `src/core/tui/GenericSyntaxHighlighter.{hpp,cpp}`:

```cpp
struct LanguageDefinition { std::string name; std::vector<std::string> extensions;
                            std::vector<std::string> fenceTags; HighlightFunction highlight; };
enum class LanguageRegistrationError : std::uint8_t
    { EmptyName, NoHighlighter, NameInUse, TokenInUse, CapacityReached };
struct LanguageRegistrationFailure { LanguageRegistrationError error; std::string token; };

class SyntaxHighlighterRegistry
{
  public:
    auto registerLanguage(LanguageDefinition) -> std::expected<LanguageId, LanguageRegistrationFailure>;
    auto find(std::string_view name) const noexcept -> LanguageId;
    auto name(LanguageId) const noexcept -> std::string_view;
    auto registeredCount() const noexcept -> std::size_t;
    auto detectFromExtension(std::string_view ext) const noexcept -> LanguageId;
    auto detectFromFenceTag(std::string_view tag) const noexcept -> LanguageId;
    auto detectFromPath(std::string_view filePath) const -> LanguageId;
    auto highlightLine(std::string_view, LanguageId, HighlightState = HighlightState::Normal) const
        -> std::pair<HighlightMap, HighlightState>;
};

inline constexpr auto FirstRegisteredLanguageId = std::uint8_t { 128 };
constexpr auto isRegisteredLanguage(LanguageId) noexcept -> bool;
inline constexpr auto BuiltinLanguageTable = std::to_array<LanguageName>({ … });  // id → canonical name
```

Every existing entry point gained one trailing `SyntaxHighlighterRegistry const* = nullptr`:
`detectLanguageFromExtension`, `detectLanguageFromFenceTag`, `detectLanguageFromPath`,
`highlightLine`, `MarkdownRenderer`'s constructor and `StyledText::fromMarkdown`. `nullptr` means
"the built-in languages alone", which is exactly today's behaviour, so **no existing caller in
core-cpp or in any consumer changes a line** unless it registered a language.

---

## The three decisions the dispatch asked to be recorded

### 1. Identity: a reserved id range handed out by the registry

`registerLanguage()` returns `LanguageId` values from `FirstRegisteredLanguageId` (128) upward, in
registration order. `LanguageId`'s own enumerators stay at 0…13.

Chosen over the other two options because it keeps `LanguageId` as the single currency:
`MarkdownRenderer::_codeLanguage`, `highlightLine`'s parameter and every consumer's variable keep
their type. A `Custom` discriminator plus an index would have made the currency a struct
(`{ LanguageId, index }`) and changed every signature that carries a language; a `std::string_view`
name at the seam would have replaced the enum with a string whose lifetime the caller owns and
whose comparison is a string compare on every fence tag and every extension.

**Cost, stated plainly:**

- An id means something only to the registry that handed it out. Passed to a different registry, or
  to a call with no registry, it highlights the line as **plain text** — it degrades to unstyled,
  never to a different language. There is a test for exactly this
  (`SyntaxHighlighterRegistry.a_registered_id_without_its_registry_is_plain_text`).
- A registry holds at most `256 − 128 = 128` registered languages; past that,
  `registerLanguage()` returns `CapacityReached`.
- `LanguageId` is no longer a closed set at runtime, so a `switch` over it can see a value no `case`
  matches. The built-in dispatch (`highlightBuiltin`) still switches over the closed set and has no
  `default`, so `-Wswitch` still names a forgotten built-in; a registered id simply falls out of the
  switch into the plain-text return, which is the documented behaviour.
- `enum class LanguageId : std::uint8_t` has a fixed underlying type, so the values 128…255 are
  valid enumeration values and the cast is well defined — no UB.

### 2. The built-in languages: unchanged, beside the seam

The built-ins keep their two `constexpr` tables and their free functions; a registry consults the
built-in table **first**, then its own rows, so a registry answers for everything the highlighter
knows and registering one costs an application nothing it already had.

Rationale: `ExtensionLanguageTable` and `FenceTagLanguageTable` are `constexpr` data answered at
compile time, and `detectLanguageFromExtension`/`FenceTag` remain usable in a constant expression.
Routing thirteen languages through the registry would have traded that for a heap-allocating
runtime construction in every process that links `core::tui`, for data that cannot change, and
would have made "built-in language works" depend on somebody having run a registration.

One built-in-side addition: `BuiltinLanguageTable`, one row per `LanguageId` in enumerator order,
giving each built-in a canonical lowercase name (`"cpp"`, `"python"`, …). It exists so that (a) a
registration that would shadow a built-in is refused **by name** as well as by token, (b)
`registry.name(id)` answers for built-ins too, and (c) the test suite has a built-in list to assert
against. Its extent is anchored on the new `LanguageId::Last` (the enumerator count, not a
language), with a `consteval` check that each row sits at its own enumerator's index — so appending
an enumerator without its row is a compile error, not a silent read past the end
(`.agent/rules/design-principles.md`, "A table indexed by an enumerator proves its order").

### 3. Re-registration: refused, and so is any claimed token

`registerLanguage()` refuses, with `NameInUse` or `TokenInUse`, a name / extension / fence tag that
a built-in or an already-registered language claims. It does not replace and does not shadow.

Reason: replacing would repoint a `LanguageId` already handed out, so whoever held it would get a
*wrong answer that looks right* — the failure mode `.agent/rules/design-principles.md` says is not
acceptable. Refusing is a value the caller can act on, and
`LanguageRegistrationFailure::token` names which of the several tokens in one definition was at
fault, because the code alone cannot say. Every check runs before anything is stored, so a refused
definition leaves the registry byte-for-byte as it was (asserted).

This is the tight direction: relaxing "refuse" to "allow, built-ins win" later is source-compatible;
tightening the other way would not be.

---

## Injected or global: **injected**, and nothing stood in the way

`registerEndoHighlighter()` was process-wide mutable state. The seam adds none: a
`SyntaxHighlighterRegistry` is an ordinary copyable value that a composition root constructs, fills
and passes on. Two parts of one program can hold different ones, and a test never has to undo a
registration — the registry-refusal tests each construct their own.

Every call site inside core-cpp reaches one without a wider change:

| Call site | How it reaches the registry |
|---|---|
| `MarkdownRenderer` (2 highlight calls, 2 fence lookups) | constructor parameter → `_highlighters` member |
| `StyledText::fromMarkdown` (1 highlight call, 1 fence lookup) | static-factory parameter, threaded to the lambda |
| free `detectLanguageFrom*` / `highlightLine` | trailing parameter |

The dispatch named `Buffer.cpp` and `Terminal.cpp` as call sites. **They are not**: in core-cpp,
`git grep 'highlightLine\|detectLanguageFrom\|LanguageId'` over `src/` finds only
`GenericSyntaxHighlighter.*`, `MarkdownRenderer.*` and `StyledText.cpp`. `Buffer.cpp` and
`Terminal.cpp` are endo's call sites, i.e. Task C1's, and they reach the registry the same way —
endo's shell owns one and passes it where it already passes a `MarkdownTheme`.

`MarkdownRenderer` takes the registry **in its constructor**, not through a setter, even though the
class has a row of `setX()` configuration setters inherited from the import: the rule is
configuration at construction, and there was no reason to add to that debt.

---

## Tests: RED then GREEN, verbatim

Written first, in two stages so the RED is behavioural where it can be.

### Stage 1 — the golden tables, which compile against the *old* header

`GenericSyntaxHighlighter_test.cpp`, cases `extension_table_is_the_shipped_set` and
`fence_tag_table_is_the_shipped_set`: a row-for-row golden copy of both shipped tables, without the
`.endo` and `endo` rows.

RED (`out/build/clang-debug`, clang 22):

```
GenericSyntaxHighlighter.extension_table_is_the_shipped_set
/mnt/d/core-cpp/src/core/tui/GenericSyntaxHighlighter_test.cpp:955: FAILED:
  REQUIRE( shipped.size() == golden.size() )
with expansion:
  60 == 59

GenericSyntaxHighlighter.fence_tag_table_is_the_shipped_set
/mnt/d/core-cpp/src/core/tui/GenericSyntaxHighlighter_test.cpp:955: FAILED:
  REQUIRE( shipped.size() == golden.size() )
with expansion:
  70 == 69

test cases:  70 |  68 passed | 2 failed
assertions: 269 | 267 passed | 2 failed
```

That is the `.endo` row and the `endo` row, named by the count.

### Stage 2 — the registry cases, which cannot compile against the old header

RED:

```
GenericSyntaxHighlighter_test.cpp:1010:43: error: use of undeclared identifier 'BuiltinLanguageTable'
GenericSyntaxHighlighter_test.cpp:1053:21: error: use of undeclared identifier 'SyntaxHighlighterRegistry'
GenericSyntaxHighlighter_test.cpp:1141:34: error: use of undeclared identifier 'LanguageRegistrationError'
…
20 errors generated.
```

### Stage 3 — the Markdown fence path, RED behaviourally

With the registry implemented and `MarkdownRenderer` holding it but *not yet consulting it* for the
fence tag (the two `detectLanguageFromFenceTag` calls left un-threaded on purpose):

```
MarkdownRenderer.a_registered_fence_tag_highlights_its_code_block
/mnt/d/core-cpp/src/core/tui/MarkdownRenderer_test.cpp:1527: FAILED:
  CHECK( output.spans == std::vector<std::string> { "l", "et x" } )
with expansion:
  { "let x" } == { "l", "et x" }

test cases: 4 | 3 passed | 1 failed
```

### GREEN

```
$ ./out/build/clang-debug/src/core/tui/core-cpp-tui-test
All tests passed (3540 assertions in 1023 test cases)
```

### The cases, against the dispatch's list

| Dispatch requirement | Case | Where |
|---|---|---|
| a registered language highlights — path, fence, direct call | `SyntaxHighlighterRegistry.a_registered_language_is_selected_and_highlights` (`detectLanguageFromPath("/src/main.toy", &r)`, `detectLanguageFromFenceTag("toy", &r)`, `highlightLine(…, &r)`) plus `MarkdownRenderer.a_registered_fence_tag_highlights_its_code_block` for the real renderer | `GenericSyntaxHighlighter_test.cpp`, `MarkdownRenderer_test.cpp` |
| a second registration does not clobber the first | `…a_second_registration_does_not_clobber_the_first` — distinct ids, both detect, each highlighter runs | `GenericSyntaxHighlighter_test.cpp` |
| re-registering a name is *defined* | `…re_registering_a_name_is_refused` — `NameInUse`, `token == "toy"`, `registeredCount() == 1`, the refused definition's tokens still `None`, the first highlighter still runs. Plus `…a_claimed_token_is_refused` over four SECTIONs (registered extension, built-in extension, built-in fence tag, built-in name) and `…a_nameless_or_mute_definition_is_refused` | `GenericSyntaxHighlighter_test.cpp` |
| unregistered extension and fence tag fall through to `None` | `…an_unregistered_token_falls_through_to_none`, all nine spellings (member, free-with-registry, free-without) | `GenericSyntaxHighlighter_test.cpp` |
| every built-in still detects, table-driven, new built-in needs an answer | `…every_builtin_detects_from_extension_and_fence_tag` over `BuiltinProbes`, with `static_assert(BuiltinProbes.size() + 1 == BuiltinLanguageTable.size())` | `GenericSyntaxHighlighter_test.cpp` |
| a consumer-specific name coming back is a red test | the two golden-table cases above, plus `…builtin_language_list_is_the_shipped_set` (fixed names, each at its enumerator's index) | `GenericSyntaxHighlighter_test.cpp` |
| — (added) | a registered id used without its registry, or with another one, is plain text | `GenericSyntaxHighlighter_test.cpp` |
| — (added) | an unregistered fence tag, and a registered one without a registry, stay plain text in the renderer; a built-in fence still highlights beside a registered one | `MarkdownRenderer_test.cpp` |

---

## The migration text for Task C1

This is the CHANGELOG's **Breaking** entry verbatim (`CHANGELOG.md`, `[Unreleased]` → `Breaking`),
which is what endo pastes:

```cpp
// was: a process-wide callback, and a closed enumerator naming one application's language.
core::tui::registerEndoHighlighter(highlightEndoLine);
auto const language = core::tui::LanguageId::Endo;

// is: a registry the application owns, filled once at startup and injected.
auto highlighters = core::tui::SyntaxHighlighterRegistry {};
auto const registered = highlighters.registerLanguage({
    .name = "endo",
    .extensions = { ".endo" },
    .fenceTags = { "endo" },
    .highlight = highlightEndoLine,
});
// std::expected<LanguageId, LanguageRegistrationFailure>; *registered replaces LanguageId::Endo.

// and each entry point takes the registry, as a trailing argument defaulting to nullptr:
auto renderer = core::tui::MarkdownRenderer { output, theme, &highlighters };
auto const styled = core::tui::StyledText::fromMarkdown(text, width, &theme, &highlighters);
auto const detected = core::tui::detectLanguageFromPath(path, &highlighters);
auto const [map, next] = core::tui::highlightLine(line, detected, state, &highlighters);
```

endo also gets back what it lost in Task A7's fix round: `.endo` files and ` ```endo ` fences, now
because endo asked for them rather than because core-cpp shipped them. endo's own
`.endo-format` dotfile is still **not** selectable by file name — registered languages claim
extensions and fence tags, not well-known file names — so endo passes the language to
`highlightLine()` itself for that one, exactly as Task A7's fix round left it.

## `tools/migrate/renames.json`: no row, deliberately

The table existed by the time this landed (`ddddc67`, `748142f`). **No row was added**, and
`ctest -L hygiene -R core-cpp.migrate-renames` passes without one.

Reason: every `kind` in that table (`symbol`, `include`, `macro`, `namespace`, `member`) drives a
*mechanical rewrite* to a `target` that must exist in this tree, and both removals here change the
**call shape**, not the spelling. `registerEndoHighlighter(fn)` becomes a construction, a
`registerLanguage({…})` call and an `std::expected` check; `LanguageId::Endo` becomes a value
returned at runtime. A row that rewrote either into `SyntaxHighlighterRegistry` would produce code
that either does not compile or compiles into the wrong thing — and a compile error at
`LanguageId::Endo` is the better signal for a two-call-site change in one consumer. The reason is
recorded in the CHANGELOG entry itself, so C1 does not have to rediscover it.

**If Task C0's owner wants this covered by the table**, it needs a new kind (a `removed` row with a
migration note and no rewrite target) — that is their call, not one I made on their file, which is
still untracked in this working tree.

## What B13's gate should check (Ruling R81, as amended in fix round 1)

The dispatch's acceptance line was `git grep -i endo src/core/tui` finding nothing. Written that
way the gate cannot pass, and should not: `-i endo` is a substring match that hits ordinary
English. In the current tree it matches 30 lines, 16 of them `clearToEndOfLine`,
`clearToEndOfDisplay`, `isEndOfStream` and — the good one — **`vendored`**, which contains `endo`.
The remaining 14 word-boundary matches are all provenance comments, which the Global Constraints'
upstream-sync discipline is the reason for, and which the controller has recorded as correct.

**R81 settles it.** The gate is the symbol grep, which the reviewer verified empty:

```
git grep -nP '\b(LanguageId::Endo|registerEndoHighlighter)\b' -- src/core/tui   # must be empty
git grep -nP '"\.?endo"' -- src/core/tui                                         # must be empty
```

I had also proposed a tree-wide consumer-name scan. **That proposal is withdrawn**, and the
controller was right to strike it: it is not empty today — it fires on
`src/core/tui/detail/XtVersion.hpp:80`, where `"contour"` is *a terminal being detected*, a true
negative — `grep -P` fails outright in Git Bash on this machine, and its exit codes are inverted
because `git grep` exits 1 on the pass case. A gate that fires on a true negative, cannot run on
one of our two shells and reports backwards is worse than no gate.

**The durable half is the golden tables, and after fix round 1 they cover all three.** These fail
red in every build, on every preset, the moment a consumer's token comes back:

| Case | Pins |
|---|---|
| `extension_table_is_the_shipped_set` | `ExtensionLanguageTable`, row for row |
| `fence_tag_table_is_the_shipped_set` | `FenceTagLanguageTable`, row for row |
| `filename_table_is_the_shipped_set` | `FilenameLanguageTable`, row for row, **plus** each row resolving through `detectLanguageFromPath()` |
| `builtin_language_list_is_the_shipped_set` | the built-in languages and their names, each at its enumerator's index |

The third row is what fix round 1 added, and it is the one that matters most: `.endo-format` lived
in `FilenameLanguageTable`, and until this round nothing guarded it — the symbol grep above misses
it too, because the token has no `endo` in a position that `"\.?endo"` matches.

---

## Verification

| Check | Result |
|---|---|
| `python scripts/clang-format.py --check` (10 touched files, clang-format 22.1.8) | clean |
| `clang-tidy` preset, `src/core/tui/` | clean (one finding fixed: a `constexpr` test constant in camelBack) |
| WSL `clang-debug` build + `ctest --preset clang-debug` | `core-cpp.tui` and `core-cpp.tui_output` pass; 4 failures, all elsewhere — see Concerns |
| WSL `gcc-release` build + `ctest -L tui` | tui targets build; 2/2 tui tests pass |
| Windows `cl-debug` build + `ctest -L tui` | 2/2 pass |
| Windows `clangcl-release`, `--clean-first` | EXIT=0, 2/2 pass. `--clean-first` because the installed `fastcache-cc 0.2.0-739-gd4451c3b` is **not** a descendant of fastcached `ca8dfc32` (`git merge-base --is-ancestor` says no), and headers changed |
| `ctest -L hygiene` | 9/10 pass, incl. `core-cpp.migrate-renames`; `core-cpp.cmake-hygiene` fails — see Concerns |
| `mkdocs build --strict` | clean |
| `git grep -nP '\b(LanguageId::Endo\|registerEndoHighlighter)\b' -- src/core/tui` | empty |

**CI: green.** `Build` run **`35540278214`** — head `98937f7`, which contains `f61d57b` and
`d0345d0`, this task's two substantive commits — completed **`success`, 24 of 24 jobs, none
non-success**: `style`, `clang-tidy`, `coverage`, `compile-cache`,
`linux (clang-22, clang-22-arm64, clang-22-cxx26, clang-22-tracy, gcc-14, gcc-15)`,
`macos (appleclang, llvm-22)`, `sanitizers (clang-asan-ubsan, clang-tsan)`,
`windows (cl-debug, cl-release, cl-release-tls, clangcl-release)`,
`emscripten (emsdk 3.1.56, emsdk latest)` and `consumer-smoke (cpm, vendored, wasm)`.

The third commit, `aa49c53`, is covered by `Build` run **`35540789314`** (head `ac9e2e2`), which
also completed **`success`, 24 of 24, none non-success** — so every compiler CI runs (AppleClang,
LLVM 22, GCC 14 and 15, clang 22 in four configurations, MSVC `cl`, `clang-cl`, Emscripten 3.1.56
and latest) accepted the six `constexpr` `static_assert`s, which is what they were added to prove.

This checkout is shared and every session's push cancels the run in flight through the workflow's
concurrency group — three of mine were killed that way (`35540269220` at `d0345d0`,
`35540400481` at `f90aacb`, `35540507109` at `aa49c53`) — so the two green runs above are the ones
that survived long enough to finish. `Docs` for `d0345d0` is run `35540269215`.

---

## Concerns

1. **Four ctest failures in this shared working tree are not mine.** `core-cpp.async`,
   `core-cpp.async-fallback` (Subprocess aborted), `core-cpp.net` (Failed) and
   `core-cpp.cmake-hygiene` (Failed). `cmake-hygiene`'s output names only another session's
   untracked files — `src/core/async/{IExecutor,ParkedWork,ResumeOn,SyncRun,ThreadPoolExecutor,
   AsyncQueue,DetachedTask}.hpp` and their tests — as having "no row in
   `.agent/reference/provenance.md`". `gcc-release` additionally fails to compile
   `src/core/async/IExecutor.hpp:56` with `‘virtual void core::async::IExecutor::submit(…)’ was
   hidden [-Werror=overloaded-virtual=]`, which GCC diagnoses and clang does not. Reported, not
   touched. My change adds no file, so it owes no provenance row.
2. **Concurrency cancels Build runs constantly in this shared checkout** — three of mine were
   killed by other sessions' pushes within seconds of starting, which is why the green evidence is
   two runs whose heads *contain* my commits rather than two runs whose heads *are* my commits.
   Both are fully green, so nothing about this task is unverified; it is a note about how to read
   CI in this sprint, not an open question.
3. **`LanguageId::Last` is public API.** It is the count, not a language; `highlightBuiltin`'s
   `switch` names it beside `None` in the same `break`. The repository had no precedent for the
   `Last` sentinel, but `.agent/rules/design-principles.md` prescribes it for a table indexed by an
   enumerator, which `BuiltinLanguageTable` is. Reviewable in one place if the reviewer disagrees.
4. **The golden tables duplicate 128 rows of shipped data into the test.** That duplication is the
   point — it is what makes re-adding `.endo` a red test rather than a code review — but it means
   an intentional new extension is a two-place edit. Documented at the tables.
5. **`FuzzyMatch_test.cpp` was not in the dispatch's list** of test files that name endo (it lists
   `GenericSyntaxHighlighter_test.cpp`, `MarkdownRenderer_test.cpp`, `MarkdownImage_test.cpp`). I
   changed it anyway, in its own commit, because its fixture is `endo.exe` in a path — the same
   rule. Every property the regression was written for is preserved: `demo.exe` is eight graphemes
   as `endo.exe` was, the haystack length is unchanged, and the pattern's leading `d` still occurs
   earlier in the haystack (in `./build` and `clangcl-debug`), so the greedy matcher still has a
   stray character to bind to. Reverting that one commit costs nothing if the controller prefers it
   left alone.

---

# Fix round 1 (Rulings R80–R82)

All eight findings addressed. The Critical and both Importants were reproduced first, RED captured,
then fixed.

## Critical — the cross-registry claim was false (R80)

**Reproduced.** I wrote the reviewer's scenario as a test before changing anything: two registries,
`toys` = toy-first and `others` = doll-first, and `toys`' id handed to `others`.

RED, against the claim as documented:

```
/mnt/d/core-cpp/src/core/tui/GenericSyntaxHighlighter_test.cpp:1298: FAILED:
  CHECK( highlightLine("let x", *toy, HighlightState::Normal, &others).first == HighlightMap(5, Cat::Default) )
with expansion:
  { 3, 3, 3, 3, 3 } == { 0, 0, 0, 0, 0 }
```

`3` is `HighlightCategory::String` — doll's category, from toy's id. The claim *"degrades to
unstyled, never to a different language"* is false, exactly as the review said, and it was the
recorded justification for the identity choice.

**Retracted, per R80.** The identity choice stands; its advertised cost was wrong and is now
written as an ordinary C++ precondition, the one `std::vector::iterator` has with its container:
**a registered `LanguageId` belongs to the registry that issued it.** Built-in ids — everything
below `FirstRegisteredLanguageId` — are stated as portable, because that half is true and
consumers will rely on it. I did not consider a process-wide counter worth arguing for: it is the
ambient mutable state this task exists to remove.

I grepped for the claim rather than for the files I had edited, and it lived in **four** places,
not the three the ruling named:

| Where | Now says |
|---|---|
| `GenericSyntaxHighlighter.hpp`, `isRegisteredLanguage()` | a `@warning` with the precondition, what it costs when violated, and the portable built-in half |
| `GenericSyntaxHighlighter.hpp`, `SyntaxHighlighterRegistry` class `@note` | points at `isRegisteredLanguage()` |
| `GenericSyntaxHighlighter.hpp`, `SyntaxHighlighterRegistry::highlightLine()` | the fourth site — it promised "an id this registry did not hand out … yields plain text", the same falsehood, and the ruling did not list it |
| `CHANGELOG.md` | a paragraph of its own in the Breaking entry |
| `docs/modules/tui.md` | an `!!! warning` admonition |
| the migration snippet | "hold exactly one per program unless you keep each id with the registry that issued it" |

GREEN: the test now pins **both** halves — `*doll == *toy` (nothing distinguishes them),
`others` resolving toy's id to `Cat::String`, and built-in ids answering identically in two
registries, in an empty one and with no registry at all. It is renamed
`a_registered_id_belongs_to_the_registry_that_issued_it`, because the old name described the
guarantee that did not hold.

## Important — `FilenameLanguageTable` was the third table, and unguarded

**How I found the third table this time, since the method that found two missed it.** The first
pass took the dispatch's list of four places as the inventory and grepped the header — so it found
the two tables *in the header* and stopped. That method cannot find a table the dispatch did not
name. What finds all of them is to ask the code, not the ticket: enumerate every table of the
shipped type, and every table the entry point reads.

```
$ git grep -n "to_array<LanguageToken>" -- src/core/tui
src/core/tui/GenericSyntaxHighlighter.cpp:2316:    constexpr auto FilenameLanguageTable = ...
src/core/tui/GenericSyntaxHighlighter.hpp:344:inline constexpr auto ExtensionLanguageTable = ...
src/core/tui/GenericSyntaxHighlighter.hpp:415:inline constexpr auto FenceTagLanguageTable = ...
```

Three, and the odd one out is in the `.cpp`, in an anonymous namespace — which is precisely why a
header grep missed it and why no test could reach it. Reading `detectLanguageFromPath()`'s body
would have found it too: it consults the file-name table *first*.

**Fixed by making it public**, beside the other two, with the same golden treatment. Public is the
right answer and not merely the convenient one: it is the same kind of data as its two siblings, it
is part of what `detectLanguageFromPath()` promises, and being in the header is what puts it in
reach of both a golden test and a grep.

RED — the review's own probe, `{ ".endo-format", LanguageId::Yaml }` injected into the table, which
previously left the suite green at 1023/3540:

```
GenericSyntaxHighlighter.filename_table_is_the_shipped_set
/mnt/d/core-cpp/src/core/tui/GenericSyntaxHighlighter_test.cpp:968: FAILED:
  REQUIRE( shipped.size() == golden.size() )
with expansion:
  9 == 8
```

GREEN with the probe removed. The case pins the rows *and* checks each one resolving through
`detectLanguageFromPath()`, bare and with a leading directory, so it holds behaviour and not just
data.

## Important — three changed call sites had no coverage

`StyledText.cpp`'s fence lookup and `MarkdownRenderer::processStreamBuffer()`'s pair (a second copy
of the fence handling that `render()` never reaches — my first-round tests all went through
`render()`).

RED, with all three un-threaded as the review did:

```
/mnt/d/core-cpp/src/core/tui/MarkdownRenderer_test.cpp:1592: FAILED:
with expansion:  { "let x" } == { "l", "et x" }
/mnt/d/core-cpp/src/core/tui/StyledText_test.cpp:191: FAILED:
with expansion:  { "let x" } == { "l", "et x" }
```

Seven new cases: four in `StyledText_test.cpp` (registered tag highlights, unregistered tag plain,
no registry plain, built-in still highlights) and three in `MarkdownRenderer_test.cpp` over
`beginStream()`/`feedToken()`/`endStream()`. GREEN with the threading restored.

Un-threading *both* `StyledText` sites does not compile — `-Werror=unused-parameter` — so the RED
above is the fence lookup alone, which is the site the review named.

## Minor

| # | Finding | What I decided |
|---|---|---|
| 4 | `.editorconfig` registers as an extension and is then shadowed by the file-name table | **Refused**, `TokenInUse`, same as every other collision. Now that `FilenameLanguageTable` is public, `registerLanguage()` consults it. |
| 5 | A dotless extension registers and is dead | **Refused**, new `LanguageRegistrationError::MalformedToken`: `detectLanguageFromPath()` only ever looks up from a dot onwards, so `"toy"` could never match. An extension that is only `"."`, and an empty fence tag, are refused by the same rule — an empty tag is what a bare fence yields, so a registered one would capture every unlabelled code block in every document. |
| 6 | `CapacityReached` untested | `the_reserved_id_range_runs_out` registers all 128, asserts each id is `128 + i` (so the last is 255 and nothing wraps onto a built-in), then asserts the 129th is refused and that a full registry still answers for its own languages and the built-ins. |
| 7 | `LanguageId::Last` documented more thinly than `NetErrorCode::Last` | Rewritten in the sibling's shape: what it is, that it is never detected/highlighted/returned, that a new language goes **above** it and below `FirstRegisteredLanguageId`, and what refuses one appended below — `builtinLanguageTableIsIndexedByLanguageId()`, at compile time. |
| 8 | "a consumer edits nothing" omits two breaks | Both stated in the CHANGELOG: taking the **address** of any of the four functions sees a changed function type, because a default argument is not part of one; and a consumer that registers a token core-cpp later ships as a built-in gets `TokenInUse` on upgrade — a forward-compatibility risk the refusal makes loud instead of silent. |

## R81 and R82

**R81** — accepted; the gate section above is rewritten. My tree-wide consumer-name scan is
withdrawn, and the golden tables now cover all three tables.

**R82** — **the rows were already there.** `a8e5212` landed the `kind: "removed"` machinery using
my two symbols as its fixtures, so `core::tui::LanguageId::Endo` and
`core::tui::registerEndoHighlighter` are in `renames.json` at HEAD, and `check-renames.py` reports
`2 removed` with no failure against either. I did not add duplicates. I did correct one note that
was inaccurate about the API it describes — it said `registerLanguage()` "returns the LanguageId",
where it returns `std::expected<LanguageId, LanguageRegistrationFailure>` — and added the
one-registry precondition to it, since that note is text endo pastes.

## Verification, fix round 1

| Check | Result |
|---|---|
| `core-cpp.tui`, clang-debug | 1033 cases / 3870 assertions pass (was 1023 / 3540) |
| WSL `gcc-release`, tui targets | `EXIT=0`, 2/2 tests pass |
| Windows `cl-debug`, tui targets | `EXIT=0`, 2/2 tests pass |
| Windows `clangcl-release`, `--clean-first` | `EXIT=0`, 2/2 tests pass |
| `clang-tidy` preset over `src/core/tui/` | clean |
| `python scripts/clang-format.py` on the 7 touched files | formatted, 22.1.8 |
| `python -m mkdocs build --strict` | clean — run, and the `!!! warning` admonition renders |
| `python -m ruff format --check` | `62 files already formatted` |
| `ctest -L hygiene` | 11/12; `core-cpp.upstream-drift` fails on the async lane's `ThreadPoolExecutor.hpp` provenance row, not mine. `core-cpp.cmake-hygiene`, which failed last round, now passes |
| `tools/migrate/check-renames.py` | 10 failures, all net-lane rows; `git grep -iE "tui\|endo\|Language"` over its output is empty |
| both R81 greps over `src/core/tui` | empty (exit 1) |

## One mistake worth recording

Mid-round I ran `git checkout -- src/core/tui/GenericSyntaxHighlighter.hpp` to remove the injected
probe row, and it discarded every fix-round edit in that header — the R80 warning, the public
`FilenameLanguageTable`, the `MalformedToken` enumerator — because the file had no staged copy to
fall back to. I noticed immediately (`grep -c` for four markers, all zero), redid the four edits and
re-verified. Nothing shipped wrong, but `git checkout --` is not an undo for one hunk, and on a
shared tree the right tool was the inverse edit I had just made.

## Two things the controller should know (fix round 1)

1. **Another lane's regeneration of `renames.json` silently dropped my note correction.** I edited
   the note in the working tree, then the net lane rewrote the file (424 added lines) without it.
   My correction is safe in the commit, because that was staged from `HEAD` plus my edit alone —
   but their working-tree copy would have reverted it on their next commit, so I re-applied it to
   the working tree too. Worth knowing that `renames.json` is being regenerated rather than hand
   edited: a hand edit to it is not durable unless it also lands in whatever generates it.
2. **`scripts/clang-format.py` is syntactically broken in the working tree right now** (a lane is
   mid-edit: an f-string with a literal newline at line ~112, `SyntaxError: unterminated f-string
   literal`). HEAD's copy is fine, so CI's `style` job is unaffected. I verified my five files
   against `HEAD`'s copy of the script, then restored their in-progress copy byte for byte. The
   change they are making is a good one — it adds `--all` and refuses a bare tree-wide run, which
   is exactly the hazard this shared checkout has — it just does not parse yet.

## CI for fix round 1: red, and none of it mine (R73)

**Stated plainly, as R73 requires: I am closing fix round 1 with no completed green run covering
`002af92`.** Five consecutive `Build` runs were superseded within seconds by other sessions
(`35544091105` → `35544236731` → `35544311790` → `35544327857` → `35544422449`); the first four
report `cancelled` with **zero jobs**, which is the signature R73 names. The run that got to
execute, `35544422449` at head `18ee3607`, is **red** — and every failure traces to one of two
other lanes:

| Failing job(s) | Failing step | Cause |
|---|---|---|
| `style`, and the `Test` step of `linux (gcc-14, gcc-15, clang-22-arm64, clang-22-cxx26)`, `macos (appleclang, llvm-22)`, `windows (cl-debug)`, `sanitizers (clang-tsan)` | CMake and C++ hygiene / Test | `core-cpp.cmake-hygiene`: `.agent/reference/provenance.md` has a row naming a **pattern** (`src/FastCache/Async/ThreadPoolExecutor.{hpp,cpp}`) where the rule wants one file and a full SHA. The async lane's row; it is the same failure I saw locally as `core-cpp.upstream-drift`. |
| `consumer-smoke (cpm)`, `consumer-smoke (vendored)` | the smoke builds | `fatal error: 'core/net/DefaultEventSource.hpp' file not found`, from `tests/consumer-shared/ConsumerSmoke.hpp`. The net lane's `e7963de` ("IoBackend replaces EventSource") deleted the header without updating the shared consumer-smoke fixture. |

**`core-cpp.tui` passes in that run**, on every platform that got to run it — from the gcc-14 log:

```
10/24 Test #12: core-cpp.tui_output ................   Passed    0.00 sec
23/24 Test #13: core-cpp.tui .......................   Passed    8.78 sec
96% tests passed, 1 tests failed out of 24
```

The one failure in 24 is `core-cpp.cmake-hygiene`. Nothing in `src/core/tui/`, `docs/` or
`CHANGELOG.md` is implicated, and both R81 greps are empty at HEAD. Both breakages are reported
here rather than fixed, per the dispatch's standing instruction about other lanes' work.

---

# Fix round 2 (the re-review's new Important, plus two minors)

**Commit:** `aae25fc` — `fix(tui): LanguageId::Last's doc named a guard that does not fire`.

## The Important: the same shape as the Critical, in the edit that fixed a Minor

`LanguageId::Last`'s new documentation — written in fix round 1 to bring it up to
`NetErrorCode::Last`'s standard — claimed a guard that does not exist. I measured all three cases
rather than reasoning about them:

| Probe | Result |
|---|---|
| `Rust` appended after `Last`, nothing else | **build fails**: `error: enumeration value 'Rust' not handled in switch [-Werror,-Wswitch]`. So "still satisfies the switch" is backwards — the switch is the one thing that notices. |
| `Rust` plus the case the compiler just demanded, no table row | **clean build, green suite, 3870 assertions in 1033 cases.** `builtinLanguageTableIsIndexedByLanguageId()` compares the table's size against `Last`; appending after `Last` moves neither, so it fires only once the table row is added — once the mistake is half corrected. |
| `Rust` plus a case dispatching to `highlightCpp`, no table row | **clean build, still green.** This is the harmful version: a language that *works*, reachable by id, invisible to `BuiltinLanguageTable`, to `name()` and to all three golden tables. |

The doc now states what was measured, including what remains uncaught. RED for the new guard,
against the third probe:

```
/mnt/d/core-cpp/src/core/tui/GenericSyntaxHighlighter_test.cpp:1131: FAILED:
  CHECK( highlightLine(probe, language).first == HighlightMap(probe.size(), HighlightCategory::Default) )
with expansion:
  { 1, 1, 1, 0, 0, '\t', 0, 7, 7, 7, 7, 7, 7, 7 }
  ==
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
with message:
  LanguageId value 15
```

`no_language_hides_above_Last` walks `[Last, FirstRegisteredLanguageId)` and asserts, for each
value, plain text from `highlightLine()`, an empty `name()`, and no row in any of the three
built-in tables — the tui half of `NetError_test.cpp`'s "No code hides above Last". `src/core/tui`
had no test that mentioned `LanguageId::Last` at all before this. Its comment records honestly
what it cannot see: an enumerator above `Last` whose case only breaks and which no table names.
C++ cannot enumerate enumerators, so nothing can see that one — and it is unreachable, unnamed and
inert, which is to say not yet a language. GREEN with both probes removed.

## Minor: one case, two call sites

`processStreamBuffer()`'s two registry sites are **in series** — a registered id can only come from
the lookup — so only the first can be isolated outright, and the split says so rather than
pretending otherwise. `the_fence_lookup_consults_the_registry` registers a highlighter that
classifies nothing, so its only trace is that `renderHighlightedLine()` ran at all: the span count
is identical either way, and the recorded foreground separates a resolved fence (a syntax-palette
colour) from an unresolved one (the theme's code-block style, whose `fg` is default-constructed).
`SpanRecordingOutput` now records `Color` alongside the text.

Measured, breaking one site at a time:

| Broken site | Red cases |
|---|---|
| the fence lookup (`detectLanguageFromFenceTag`) | **both** — `the_fence_lookup_consults_the_registry` *and* `the_highlight_call_consults_the_registry` |
| the highlight call (`highlightLine`) | **only** `the_highlight_call_consults_the_registry` |

So the pair names which site broke: both red means the lookup, the second alone means the dispatch.
That is written into the comment above the two cases.

## Minor: both removal notes carry pasteable code

`registerEndoHighlighter`'s note described the shape in prose where its sibling gave the lines.
Both are what endo pastes, so both now carry the code. The edit is escaped through `json.dumps`
and validated by `json.loads` before anything is written — the first attempt broke the file with
raw quotes and the guard caught it.

## Verification, fix round 2

| Check | Result |
|---|---|
| `core-cpp.tui`, clang-debug | 1035 cases / 4445 assertions pass (was 1033 / 3870) |
| WSL `gcc-release`, tui targets | 2/2 tests pass |
| Windows `cl-debug`, tui targets | 2/2 tests pass |
| Windows `clangcl-release`, `--clean-first` | `EXIT=0`, 2/2 tests pass |
| `clang-tidy` preset over `src/core/tui/` | clean |
| `scripts/clang-format.py` on the 4 touched files | formatted, 22.1.8 |
| `python -m mkdocs build --strict` | clean |

Committed through a private `GIT_INDEX_FILE` again: the shared index held another lane's staged
work, and was additionally **locked** (`Unable to create '.git/index.lock'`) by a live git process
mid-round. I did not remove the lock — it was not mine — and routed around it instead. Afterwards
I reset my four paths in the shared index so its stale pre-commit copies cannot revert this commit.

## CI for fix round 2

`Build` run **`35546843248`**, head **`aae25fc`** — my own commit, so the run is pinned to this
work and was not superseded before finishing. It completed `failure`, **21 of 24 jobs green**:

| Job | Result |
|---|---|
| `style`, `clang-tidy`, `coverage`, `compile-cache` | success |
| `linux` (clang-22, clang-22-arm64, clang-22-cxx26, clang-22-tracy, gcc-14, gcc-15) | success |
| `windows` (cl-debug, cl-release, cl-release-tls, clangcl-release) | success |
| `sanitizers` (clang-asan-ubsan, clang-tsan) | success |
| `emscripten` (3.1.56, latest), `consumer-smoke` (cpm, vendored, wasm) | success |
| `macos (appleclang)`, `macos (llvm-22)` | **failure**, and `ci-ok` with them |

Both macOS failures are the same test and are not mine: `core-cpp.net`, at
`src/core/net/BackendParity_test.cpp:1026`, macOS only. `core-cpp.tui` **passes on both of them** —
`23/25 Test #14: core-cpp.tui … Passed 10.56 sec` (appleclang) and `24/25 … Passed 9.48 sec`
(llvm-22).

Everything that was red for other lanes last round is green now: the async lane fixed the
provenance row that failed `cmake-hygiene` everywhere, and the net lane fixed the consumer-smoke
fixture that had lost `core/net/DefaultEventSource.hpp`. What remains is one macOS-only net test.

## CI: green, and the last open item is closed

`Build` run **`35546989595`**, head **`1709a3c`** — which contains this task's `aae25fc`, verified
with `git merge-base --is-ancestor` — completed **`success`, 24 of 24 jobs, none non-success.**

That closes the one item this report was still carrying. The `core-cpp.net` failure on
`macos (appleclang)` and `macos (llvm-22)` in my own pinned run `35546843248` was the IoBackend
lane's kqueue re-arm, and their fix (`1709a3c`, "a muted registration's un-muting is asked where
the answer is portable") landed minutes after my run started. Both macOS jobs are green in the run
above, so the attribution in the previous section is confirmed rather than merely argued: nothing
in `src/core/tui/` was implicated, and the failure cleared without a line of mine changing.

**Final state of Task B13a:** commits `f61d57b..aae25fc` on `master`, all pushed, all under a
completed green Build.

---

# Closed

Re-review over `aae25fc`: all three ADDRESSED, no new Critical or Important.

Two external confirmations worth keeping next to the code they concern:

- The re-review **built the inert shape** that `no_language_hides_above_Last`'s comment declares
  invisible — an enumerator after `Last` with a case that only `break`s, no table row, no name —
  and the suite stayed green at 4445 assertions in 1035 cases. The comment's stated limit is
  therefore measured from the outside as well as from the inside, which is what a limit written
  into a test comment is for.
- Both `renames.json` notes' pasted code was checked against the **real signatures** of
  `MarkdownRenderer`, `StyledText::fromMarkdown`, `detectLanguageFromPath`, `highlightLine` and
  `LanguageDefinition`'s fields, not merely confirmed to exist.

**Ruling on the enumerator-set gate (my Concern 3): no source-level scan for Task B13.** The
behavioural guard closes the harmful case — a hidden language that *works* — and the residual is
inert by construction: unreachable, unnamed, and observably identical to `None` or `Last`. No
runtime probe can close it and no user can trigger it. It stays the documented limitation it
already is, with no follow-up ticket, on the grounds that an issue nobody will act on reads as
tracked when it is not.

**Final state:** commits `f61d57b..aae25fc` on `master`, all pushed, all under `Build` run
`35546989595` (head `1709a3c`), which completed `success` 24/24.
