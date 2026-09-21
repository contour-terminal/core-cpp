# Task B13a: fix round 1 (Rulings R80–R82)

The review verdicts spec compliance **met** and task quality **Changes requested**: 1 Critical,
2 Important, 5 Minor. Your departure on the 14 provenance comments is recorded, argued and
**correct** — it is not a finding.

## Critical

1. **The cross-registry safety claim is false, and the test named for it cannot fail for it.**
   `GenericSyntaxHighlighter.hpp:71-78`, `CHANGELOG.md:486-489`, `docs/modules/tui.md`, test at
   `GenericSyntaxHighlighter_test.cpp:1269`.

   Ids are dense from 128 in registration order with no discriminator, so with registry `a` =
   toy-then-doll and `b` = doll-then-toy, `b.highlightLine(line, a.find("toy"))` runs **doll's**
   highlighter — a different language, not unstyled. The reviewer ran this. Your documented
   guarantee — *"degrades to unstyled, never to a different language"* — is the recorded
   justification for the identity choice, and it fails in exactly the two-registry scenario the
   design advertises. The test asserts against an **empty** registry, the one case where the claim
   holds.

   **Ruling R80: retract the claim; keep the identity choice; document the precondition.**
   The reviewer's recommendation, and I agree with its reasoning: the identity choice is still
   right, only its advertised cost is wrong. I considered making the claim true with a
   process-wide id counter and rejected it — that is ambient mutable state, which
   `.agent/rules/design-principles.md` refuses, in exchange for a guarantee nobody has asked for.

   What to write instead is an ordinary C++ contract, the same one `std::vector::iterator` has:
   **a registered `LanguageId` belongs to the registry that issued it, and passing it to another
   registry is a precondition violation.** Built-in ids are below 128 and *are* portable across
   registries — say that too, because it is the half that is true and consumers will rely on it.

   Then make the test pin both halves: built-in ids identical across two registries, and
   registered ids **not** portable — asserting the actual observed behaviour, not a guarantee.
   A test that asserts against an empty registry is not testing a cross-registry property.

   **The claim lives in three places** — the header, the CHANGELOG and `docs/modules/tui.md`.
   All three change. From the ledger: *a correction is not finished until every document that
   carried the wrong version carries the right one; grep for the claim, not for the file you were
   editing.*

   The migration note needs it as well. endo holds one registry and will never notice; a consumer
   with two would, silently and in the wrong language.

## Important

2. **`FilenameLanguageTable` (`GenericSyntaxHighlighter.cpp:2316`) is a third built-in table and
   nothing guards it** — and it is the one that actually held `.endo-format`. The reviewer added
   `{".endo-format", Yaml}` and the suite stayed green at 3540/1023, and your own
   `git grep -nP '"\.?endo"'` gate misses it. Your golden tables cover two of three tables; the
   one that carried the consumer's concept is the uncovered one.
   - Bring it under the same golden treatment, and say in the report how you found the third
     table this time, since the method that found two missed it.
3. **Three changed call sites have zero coverage** — `StyledText.cpp:473`,
   `MarkdownRenderer.cpp:831` and `:855`. The reviewer un-threaded all three and the suite stayed
   green at 3540/1023. `StyledText::fromMarkdown` is **the second line of the migration snippet
   endo will paste**, so this is the path a consumer hits first.

## Minor

4. `registerLanguage()` ignores `FilenameLanguageTable`, so `.editorconfig` registers and is then
   shadowed (reviewer probed it). Decide: refuse the registration, or let it win, or document the
   shadowing — but decide, do not leave it discovered.
5. A dotless extension registers and is dead (probed). Same treatment.
6. `CapacityReached` works but is untested (probed reachable at #128, id 255, no wraparound).
7. `LanguageId::Last`'s documentation is thinner than `NetErrorCode::Last`'s. The sibling task
   answered the same question; consistency between the two is worth having.
8. The "a consumer edits nothing" claim omits function-pointer takers, and omits the
   forward-compatibility break when core-cpp later adds a built-in extension a consumer had
   registered. Say both.

## Two rulings that are not findings

**Ruling R81 — the gate B13 will use.** Keep your symbol grep
(`\b(LanguageId::Endo|registerEndoHighlighter)\b` and `"\.?endo"`), which the reviewer verified
empty. **Drop the tree-wide consumer-name scan**: it is not empty today — it fires on
`src/core/tui/detail/XtVersion.hpp:80`, where `"contour"` is *a terminal being detected*, not a
consumer concept — `grep -P` fails outright in Git Bash here (`supports only unibyte and UTF-8
locales`, exit 2), and its exit codes are inverted because `git grep` exits 1 on the pass case.
A gate that fires on a true negative, cannot run on one of our two shells, and reports backwards
is three ways worse than no gate. The durable half is the golden tables — and after finding 2,
make it cover **all three**.

**Ruling R82 — you now owe two `renames.json` rows.** Your "no row" was right when you wrote it,
and you were right about why: every kind drove a mechanical rewrite, and a removal that changes
the call shape would generate code that compiles into the wrong thing. That gap is closed —
`a8e5212` landed a `kind: "removed"` whose rows assert the symbol stays **absent**, an inverse
gate, and it uses your two symbols as its fixtures. Add rows for `LanguageId::Endo` and
`registerEndoHighlighter`, with a `note` carrying the migration text endo will paste.

`d0345d0`'s FuzzyMatch rename is verified and stays.

## Then

- `python scripts/clang-format.py --check` on what you touched (13 current violations are the net
  lane's, not yours), `ruff format --check`, `ctest -L hygiene`, and **`mkdocs build --strict`**,
  which the review did not run and which your `docs/modules/tui.md` edit needs.
- WSL `clang-debug`, `gcc-release`; Windows `cl-debug`, `clangcl-release`.
- Under R73, watch the newest head rather than your own SHA, and say plainly if you close with no
  completed green run covering your commits. A superseded run reports `cancelled` with **zero
  jobs**: `gh run view <id> --json jobs --jq '.jobs | length'`.
- Never a bare `git commit`, `git commit -a` or `git add` — `git commit --only -- <pathspecs>`,
  or a private `GIT_INDEX_FILE` for hunks of a shared file. `git show --stat` after each.
- Append "Fix round 1" to `task-B13a-report.md` with RED/GREEN for the Critical and both
  Importants.
