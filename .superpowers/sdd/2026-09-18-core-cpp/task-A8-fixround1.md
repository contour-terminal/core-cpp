# Task A8: fix round 1 (Ruling R47)

Findings from the spec-and-quality review (`task-A8-review.md`, which has the full reasoning and file:line for each) and from the `/code-review` correctness pass, whose items are appended below under their own heading if it found any.

## Critical

1. **The release path this tool exists to serve is broken** — `.github/workflows/release.yml:46`.
   - The step runs `-DMODE=export`, and `cmake/CoreCppVendor.cmake:113` accepts only `sync|check`. The first tag push fails the *Vendor archive* step, so no `core-cpp-<tag>-vendor.tar.gz` and no `SHA256SUMS` is ever attached.
   - Fix: call `-DMODE=sync`. Then prove the whole step works end to end, without pushing a tag: run the same commands locally against a temporary `REF` and confirm the tarball and `SHA256SUMS` appear, and say so in the report. A workflow path nobody has executed is not verified by reading it.

## Important

2. **A mis-invocation from inside a vendored copy is silently destructive** — `cmake/CoreCppVendor.cmake:206-208`.
   - `REPO` defaults to `<script dir>/..`, and `git -C` ascends to the enclosing repository. Running `sync` with the *vendored copy's own* script — which is exactly what `docs/vendoring.md`'s check line and the MANIFEST header point a consumer at — resolves `REF` in the consumer's repository, wipes the copy, refills it from the consumer's tree, and writes a MANIFEST that `check` then happily accepts.
   - Fix: refuse unless the resolved `REPO` is the top level of its own repository (`git -C <REPO> rev-parse --show-toplevel` equals `REPO`), and refuse a ref whose tree does not look like core-cpp (for example no `cmake/CoreCppModules.cmake` or no `src/core/`). Say in the message which check failed and what the caller probably meant.
   - Add self-test cases: `sync` invoked through a copy nested inside another repository must refuse and must leave the copy untouched.
3. **An empty or headers-only MANIFEST passes `check` over an emptied copy.**
   - The header already carries a file count. Make `check` require it, require it to be greater than zero, and require the header's repository, ref and commit fields to be present and well formed. A copy whose files and manifest are both gone must fail, not pass.
   - Add a self-test case for it.

## Minor

4. Three implemented refusals have no self-test case (the review names them). Add one each, so the table covers every refusal the script implements, not only the five the brief listed.
5. `README.md:63-64`'s sync command cannot be run as written. Make every command in the README and `docs/vendoring.md` copy-pasteable, and check each one by running it.
6. The MANIFEST's own bytes are host-dependent although the consumer commits the file. Either write it with fixed `\n` endings regardless of host, or state in `docs/vendoring.md` and the `.gitattributes` guidance how a consumer keeps it stable. Prefer fixing the writer.
7. File modes are outside the contract, so a mode change escapes `check`. One sentence in `docs/vendoring.md` saying so is enough; do not widen the contract in this round.
8. Five of the CPM leg's ten watched values are empty on both sides, so those five assertions cannot currently fail. Either give them a value in the fixture so the comparison is real, or say in the test which five are structural.

## From the `/code-review` correctness pass (same round)

It verified several of these by running the tool, so treat the failure scenarios as observed, not predicted.

9. **A failed sync leaves its staging directory inside a good copy** — `cmake/CoreCppVendor.cmake:245`.
   - Verified: `-DREF=v9.9.9` against an existing copy creates `${DEST}/.core-cpp-vendor-staging/repo.git` (lines 227, 233-243) before the ref resolves, and `core_cpp_vendor_git`'s FATAL_ERROR at line 93 is the one failure path that does not remove the staging directory. The next `MODE=check` then reports 22 "is not in the manifest" refusals on a copy nobody touched.
   - `docs/vendoring.md` claims the opposite: "It writes nothing into DEST until the whole copy is legal, so a refusal leaves the previous copy exactly as it was."
   - Fix: make every failure path remove the staging directory (or stage outside DEST entirely), and add a self-test case that a failed sync leaves an existing copy passing its own check.
10. **The WebAssembly leg's pthread guard can never fire** — `tests/consumer-wasm/CMakeLists.txt:63`.
    - Verified: `find_package(Threads)` creates a directory-scoped IMPORTED target inside core-cpp's subdirectory, which the parent scope cannot see, so `if(TARGET Threads::Threads)` is always false there. If core-cpp ever did link Threads under Emscripten, the FATAL_ERROR would not trigger and the consumer would silently ship a `-pthread`/SharedArrayBuffer build.
    - Fix: inspect `LINK_LIBRARIES` of the published `CORE_CPP_TARGETS`, or keep only the CI `ninja -t commands | grep pthread` check, which does work. Prove the new check fails when a pthread link is introduced.
11. **The `-Wall`-style flag assertion skips the targets where it matters** — `tests/consumer-cpm/CMakeLists.txt:151`.
    - The loop runs over `CORE_CPP_TARGETS`, which `cmake/CoreCppTargets.cmake:266` populates only for non-INTERFACE targets. So `core::async` and `core::net_types` — the header-only targets whose every usage requirement is interface-scoped, and exactly where an `INTERFACE` flag would reach every consumer compile — are never inspected, while the CHANGELOG claims the project asserts no target carries a PUBLIC or INTERFACE flag.
    - Fix: iterate `coreCppTargetsInTree` (already collected at line 126, already including the INTERFACE libraries).
12. **A branch is accepted as a REF** — `cmake/CoreCppVendor.cmake:194`.
    - Verified: `-DREF=master` syncs and writes `# ref master` into the manifest, although the script's own message and `docs/vendoring.md` both say only a tag or a full SHA is valid. The recovery step the check's failure message recommends ("sync with the manifest's ref") would then restore a different tree.
    - Fix: enforce it — a 40-hex SHA, or a ref that resolves as a tag — or drop the claim from both the message and the docs. Enforcing is better.
13. **The smoke echo server can fail intermittently** — `tests/consumer-cpm/main.cpp:72` and `tests/consumer-vendored/main.cpp:69`.
    - The server does one `read()` and echoes that, while the client loops until it has the whole greeting. A split segment makes the server echo a prefix and close, and the leg fails with no explanation. Loopback usually delivers in one segment, so this is an intermittent red in a gating job.
    - Fix: give the server the same accumulate-until-N loop the client has.
14. **The vendor self-test fails instead of skipping without git** — `tests/CMakeLists.txt:59`, `tests/cmake/check-vendor-selftest.cmake:26-30`.
    - It is the only test needing an external tool, and `.agent/rules/testing.md` says a case that could not run is a SKIP. Building from a release tarball on a machine without git turns `ctest -L hygiene` red for a reason that has nothing to do with the tree.
    - Fix: return the skip exit code and set `SKIP_RETURN_CODE ${CORE_CPP_SKIP_EXIT_CODE}` on the test.
15. **`MODULES` can omit a module the build will still enter** — `cmake/CoreCppVendor.cmake:305`.
    - The tool checks that the named modules exist in the ref, but not that unconditional modules are all present. A module added without a `WHEN` option is not in the hand-maintained `base;log;cli;platform;async;net;testing` string that build.yml, `docs/vendoring.md` and `tests/consumer-vendored/CMakeLists.txt` each repeat, so a re-synced copy dies at configure with CMake's generic "source directory does not exist".
    - Fix: read the table's `WHEN` information and refuse a `MODULES` list that omits an unconditional module.
16. **Prefer the fix to the warning on SyncGuard** — `src/core/tui/TerminalOutput.hpp:91`, `TerminalOutput.cpp:301`.
    - The A8 fold-in documented the asymmetry instead of removing it: `syncGuard()` flushes and constructs, while the public constructor does not, so the natural RAII spelling emits previously buffered bytes inside the synchronized region — the tearing the class exists to prevent.
    - Fix: flush as the constructor's first statement so both paths are identical and the comment can go, or make the constructor private with `syncGuard()` its only friend. Add the case that fails without it.
17. **Minor, in the same round:**
    - `tests/consumer-vendored/main.cpp:43`: ~120 lines are a verbatim copy of the CPM consumer's C++, differing in one string, which is why item 13 has to be fixed twice. Share a header, or drop the duplicated halves from the vendored program, whose point is `net_tls` and the offline configure.
    - `cmake/CoreCppVendor.cmake:337`: the CR scan reads each blob a second time as hex and, on any `0d` pair, builds a one-element-per-byte list purely to print an offset. Use `string(FIND ...)` with an even-offset test, or drop the offset.
    - `src/core/tui/VtParser.cpp:653`: the size guard before `ends_with()` is redundant, and its sibling two hundred lines down was simplified in the same commit.
    - `cmake/CoreCppVendor.cmake:390` and `:281`: the two `list(JOIN ... ";" ...)` calls are no-ops; use the list variables directly, or join with ", " if a readable message was the intent.

Item 3 above and the `/code-review` finding about an empty manifest are the same defect, verified from both sides: fix it once. Item 5 and the README finding in item 17's neighbourhood are likewise one fix.

## Then

- Re-run the local set the dispatch names, including the vendor self-test and all three smoke legs.
- `python scripts/clang-format.py --check` and `mkdocs build --strict`.
- Push, watch CI to green, and dispatch and watch `portability.yml`.
- Append "Fix round 1" to `task-A8-report.md` with RED/GREEN for items 1, 2 and 3, the commands and their output.
