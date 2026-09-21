# Task A8 report — vendoring tool and consumer-smoke CI

Base `3f46e34`. Commits on `master`: `4be64a5` (`CORE_CPP_TARGETS`), `989954b` (the vendoring tool
and its self-test), `3bbf133` (the three consumer projects and the CI job), `8fd1f65` and `e913ffc`
(the two A7 fold-ins), `fd166c8` (the source map), `2177c41` (what the manifest's own line endings
are).

## What was built

### Step 0 — `CORE_CPP_TARGETS` (commit `4be64a5`)

`core_cpp_add_module()` appends every compiled library to the global property `CORE_CPP_TARGETS`,
by its real target name (a property cannot be set on an alias), in module-table order. INTERFACE
targets are excluded (they compile nothing of their own) and so are test binaries, which a
consumer does not build. `docs/getting-started/cpm.md` replaces its "arrives with the vendoring
task" note with the worked loop a parent uses to instrument core-cpp together with its own code.

### Step 1/2 — `cmake/CoreCppVendor.cmake` and its self-test (commit `989954b`)

`MODE=sync` resolves `REF` to a commit, enumerates it with `git ls-tree -r -z`, selects the file
set of Part I §5, reads each blob with `git -c core.autocrlf=false -c core.eol=lf cat-file blob`
into a staging directory inside `DEST`, and only then replaces what `DEST` held. It refuses, by
name and all at once:

- a submodule (gitlink) and a symbolic link in the file set;
- a blob containing a CR byte;
- a `MODULES` name the ref has no module for;
- a `DEST` that holds files and no `MANIFEST` (it is not a copy of ours to delete).

`MANIFEST` carries `# repository / # ref / # commit / # modules / # files` headers and
`<sha256>  <path>` lines sorted by path, hashed with `file(SHA256)` from the files as they lie in
`DEST`.

`MODE=check` re-hashes every listed file and refuses a hash mismatch, a missing file, an unlisted
file, an unparsable manifest line and a `# files` count that disagrees with the list. It calls no
git at all.

Deviations from the brief's literal text, both deliberate:

1. **The base module is the whole of `src/core/`, not `src/core/*.hpp|cpp`.** `src/core/CMakeLists.txt`
   is the base module's own CMakeLists and `src/core/Config.hpp.in` is what the top-level
   `configure_file()` generates `core/Config.hpp` from; without them the copy does not configure.
   Recorded in `docs/vendoring.md` and the CHANGELOG.
2. **`REPO` may be a URL**, as the spec's command line says: anything that is not a directory is
   cloned bare, once, into the staging area and removed afterwards. A `file://` self-test case
   covers that path without a network.

`tests/cmake/check-vendor-selftest.cmake` (ctest `core-cpp.vendor-selftest`, label `hygiene`; also
a step of the `style` CI job) runs **14 cases**. Each builds a git repository of its own through
git's *index*, which is the only portable way to put a symbolic link or a gitlink into a tree on a
Windows host. Every case also asserts that no *other* refusal phrase fired.

The repository is four files of the vendored set (a named file, a dot-file, `src/core/` itself and
a module directory) and two outside it, rather than the brief's three, so that a dot-file, module
selection and "what must NOT be copied" are all covered. It carries a `.gitattributes` with
`* text=auto eol=lf`, because `file(WRITE)` writes CRLF on a Windows host and the host's line
endings would otherwise decide what the CR case proves.

### Step 3 — the consumer smokes (commit `3bbf133`)

| Project | Stands for | What it asserts |
|---|---|---|
| `tests/consumer-cpm` | endo, fastcached, tuidu, dbtool, morph | its `COMPILE_OPTIONS`, `LINK_OPTIONS`, `COMPILE_DEFINITIONS`, `INCLUDE_DIRECTORIES`, `CMAKE_CXX_FLAGS*`, `CMAKE_CXX_COMPILER_LAUNCHER`, `CMAKE_CXX_STANDARD/EXTENSIONS`, `CMAKE_EXE_LINKER_FLAGS`, `CMAKE_POSITION_INDEPENDENT_CODE` and `CMAKE_INTERPROCEDURAL_OPTIMIZATION` are unchanged across `CPMAddPackage`; `CORE_CPP_TARGETS` is non-empty, names only compiled non-test libraries, and names *every* `core-cpp-*` compiled library; no core-cpp target has `INTERFACE_COMPILE_OPTIONS`, `INTERFACE_COMPILE_DEFINITIONS` or `INTERFACE_LINK_OPTIONS`; no `core-cpp-*-test` target exists. Then it builds and runs a program using `core::async`, `core::net` (loopback echo), `core::log` and `core::tui` (`MockTerminalOutput`), under its own `-Wall -Wextra -Werror` / `/W4 /WX`. |
| `tests/consumer-vendored` | contour | a copy exported from the commit under test, added with `CORE_CPP_FETCH_DEPS=OFF`, `CORE_CPP_WITH_TUI=OFF`, `CORE_CPP_WITH_TLS=ON`; CPM was never loaded (the proof that nothing was fetched); the copy has no `tests/` directory; the verbatim check is registered as one of the consumer's own tests, exactly as `docs/vendoring.md` tells a consumer to register it. Its program adds `core::net_tls`, whose OpenSSL comes from the system. |
| `tests/consumer-wasm` | morph | the subset behind one INTERFACE library, `CORE_CPP_WITH_TUI` and `CORE_CPP_FETCH_DEPS` off, `Threads::Threads` absent, no `pthread` in any build command, and the program run under node. It links `core::base core::async core::net_types`: `core::net` is `PLATFORMS native` until B3–B5, and there are no timers to await yet. Task B5 extends it to a `delay` on a host-driven loop under `-sASYNCIFY` and links `core::net`; the file says so. |

The `consumer-smoke` CI job is a 3-leg matrix (`cpm`, `vendored`, `wasm`) and `ci-ok` now needs it.
The vendored leg builds a small image (`ubuntu:24.04` + `g++-14 cmake ninja-build libssl-dev`, no
git) and runs the whole configure/build/ctest inside `docker run --network none`. It proves the
isolation instead of claiming it: the container asserts that `/sys/class/net` holds only `lo` and
that `git` is not installed.

This also closes A1's deferred minor ("the hygiene scanner is line-based; a PUBLIC or INTERFACE
flag set through a continuation line, a variable or `set_property` escapes it"): the CPM smoke reads
`INTERFACE_COMPILE_OPTIONS`, `INTERFACE_COMPILE_DEFINITIONS` and `INTERFACE_LINK_OPTIONS` off the
configured targets, whatever spelling put them there, and the mutation table below shows it firing.

The consumer projects are exempt from three CMake hygiene rules through allowlist rows with a
reason: they are *other projects*, whose directory-wide warning flags, own `CMAKE_CXX_*` variables
and own function names are the very things core-cpp must leave alone. Their C++ is core-cpp's own
and is held to every rule. `cmake/CoreCppVendor.cmake` gets one row for `source-glob`, which it
uses to enumerate a copy, not a source list.

### Folded in from A7's re-review (commits `8fd1f65`, `e913ffc`)

1. **`SyncGuard`'s class doc** credited the constructor with the flush on the way in.
   `TerminalOutput::syncGuard()` performs it and then makes the guard; a directly constructed guard
   does not flush, so anything still buffered is emitted inside the region. The doc now says that.
2. **The VtParser caps reserve the terminator's room.** A paste and a DCS payload collect their
   terminator into the same buffer and shed it again once recognised, so the cap bounded payload +
   terminator: clean behaviour ended at `MaxPasteLength - 5` and `MaxDcsLength - 1`. Each buffer's
   bound is now `cap + terminator`, so the cap means what its name says, with its magnitude
   unchanged. `MaxCsiParamLength` needs no reservation (a CSI's final byte is dispatched, never
   collected) and now says so. Two new tests pin it; both fail against the old bound. CHANGELOG
   updated as the dispatch asked.

## TDD evidence

**The self-test before the tool.** With `cmake/CoreCppVendor.cmake` moved aside, the self-test
exits 1: `check-vendor-selftest: TOOL ('D:/core-cpp/cmake/CoreCppVendor.cmake') is not set or does
not exist.` With the tool present: `all 14 case(s) were accepted or refused as expected`.

**Every vendor refusal proven able to fail.** Each run below is the self-test against a mutated
*copy* of the tool (the tool itself untouched); each names exactly the case that should break:

| Mutation | Case that failed |
|---|---|
| CR search disabled | `refuses-a-blob-with-a-cr-byte` |
| symlink branch disabled | `refuses-a-symbolic-link` |
| submodule branch disabled | `refuses-a-submodule` |
| hash comparison disabled | `refuses-a-changed-file` |
| unlisted-file scan disabled | `refuses-an-unlisted-file` |
| missing-file check disabled | `refuses-a-missing-file` |
| file-set selection disabled (copies everything) | `syncs-then-checks`, `syncs-the-modules-it-is-given`, `ignores-what-is-outside-the-file-set`, `resyncs-over-an-existing-copy` |
| occupant check disabled | `refuses-a-destination-that-is-not-a-copy` |
| MODULES validation disabled | `refuses-a-module-the-ref-has-not` |

**Every consumer-cpm assertion proven able to fail**, by mutating core-cpp (reverted after each):

| Mutation | What the consumer said |
|---|---|
| `set(CMAKE_CXX_FLAGS "... -DBOGUS" CACHE STRING "" FORCE)` in core-cpp's CMakeLists | `CMAKE_CXX_FLAGS was '' and is now ' -DBOGUS'` |
| `target_compile_options(<target> PUBLIC -DBOGUS_PUBLIC)` | `core-cpp-base has INTERFACE_COMPILE_OPTIONS='-DBOGUS_PUBLIC', which its consumers would compile with` (×9) |
| `CORE_CPP_TARGETS` never appended | `the global property CORE_CPP_TARGETS is empty` + `core-cpp-base is a compiled library that CORE_CPP_TARGETS does not name` (×9) |
| `CORE_CPP_TESTING ON` in the consumer's OPTIONS | `core-cpp-base-test exists although CORE_CPP_TESTING is off` (×n) |

One negative result worth recording: `add_compile_options()` *inside* core-cpp does **not** trip
the before/after comparison, because a subdirectory cannot change its parent's directory
properties. The comparison catches the class of leak that can actually reach a parent — a `CACHE
... FORCE` write, a global property, a `PARENT_SCOPE` assignment — and the per-target
`INTERFACE_*` assertions catch the PUBLIC-flag class, which is the one that reaches a consumer's
own compiles. The file says so where the lists are declared.

**The vendored leg's verbatim check proven able to fail:** appending one line to
`src/core/Utils.hpp` in the copy made `ctest -R core-cpp-vendored-copy` report
`src/core/Utils.hpp: differs from the manifest (expected a975cbe0…, found a0c45ede…)`.

**The VtParser change proven by regression tests:** with the buffers bounded by the bare caps
again, `VtParser.Paste.a_paste_of_exactly_the_cap_ends_at_its_terminator` and
`VtParser.Dcs.a_payload_of_exactly_the_cap_ends_at_its_terminator` both fail (`6 == 1`, and the
DCS response is never emitted).

## Local results

| What | Where | Result |
|---|---|---|
| `python scripts/clang-format.py --check` | Windows | clean, 355 files, clang-format 22.1.8 |
| `mkdocs build --strict` | Windows | clean |
| `check-cmake-hygiene` over the tree | Windows | clean, 389 files |
| hygiene self-test | Windows | clean tree passed, 23 violations refused by name |
| `check-cmake-hygiene` at **each** of the task's commits | worktree | clean at every one (bisectable for that gate) |
| `clang-debug` | WSL | build + `ctest` 19/19 passed (includes `core-cpp.vendor-selftest`) |
| `gcc-release` | WSL | build + `ctest` 19/19 passed |
| `clang-tidy` preset (clang-tidy 22.1.8) | WSL | `--clean-first`: 322/322 targets analysed, 0 findings |
| `cl-debug` | Windows (VS dev shell) | build + `ctest` 21/21 passed |
| `clangcl-debug` | Windows, `--clean-first` | build + `ctest` 21/21 passed |
| vendor self-test | Windows and WSL | 14/14 |
| `sync` + `check` of the real tree | Windows, WSL | 212 files before the tool itself was committed, 213 after; `check` passes; no CR byte anywhere in the copy |
| consumer-cpm | WSL, clang | configure assertions pass, builds, `ctest` 1/1 |
| consumer-vendored | WSL, gcc | configure, build, `ctest` 2/2 (verbatim check + program). **Not run in a container locally** — Docker is not installed on this machine; the offline leg runs in CI. |
| consumer-wasm | WSL, emsdk 3.1.56, node 24 | configure, build, `ctest` 1/1 under node |

## CI

**Build `35502129229` on `e913ffc` — success: every job green, `ci-ok` included.** The three new
legs:

- `consumer-smoke (vendored)`: `core-cpp-vendor: e913ffc0… copied into …/vendor/core-cpp: 213
  file(s), modules base;log;cli;platform;async;net;testing`; in the container,
  `network interfaces: lo`, git absent, then `1/2 core-cpp-vendored-copy … Passed`,
  `2/2 consumer-vendored … Passed`.
- `consumer-smoke (cpm)`: `core-cpp published 9 compiled target(s): core-cpp-base;core-cpp-log;
  core-cpp-cli;core-cpp-testing;core-cpp-testing_dialogs;core-cpp-platform;core-cpp-net;
  core-cpp-tui_output;core-cpp-tui`, then `100% tests passed`.
- `consumer-smoke (wasm)`: `0 command(s) mention pthread`, then `100% tests passed` under node.

**Portability `35502138081` on `e913ffc` — success** (FreeBSD, system clang), dispatched by hand as
the task asked.

**Build `35502531036` on `fd166c8`** (the source map) — success, `consumer-smoke` and `ci-ok`
included.

**Build `35502877055` on `2177c41`** (the line-endings clarification) — success, every job green.
`master` is green at `2177c41`, the tip.

Unrelated, found while checking: the nightly **Downstream** run `35499910731` (on `dc9f63f`, before
this task) **fails**: fastcached's `cmake/portable/CompileCache.cmake` has moved on, so core-cpp's
verbatim copy is stale (`_fc_auto_install_host`, `CMAKE_HOST_SYSTEM_PROCESSOR` → `_processor`).
That is the drift job doing its job; re-syncing the file is a task of its own and is not in these
commits.

## Files changed

- `cmake/CoreCppTargets.cmake`, `cmake/CoreCppVendor.cmake` (new)
- `tests/cmake/check-vendor-selftest.cmake` (new), `tests/cmake/check-cmake-hygiene.cmake`,
  `tests/CMakeLists.txt`
- `tests/consumer-cpm/{CMakeLists.txt,main.cpp}`, `tests/consumer-vendored/{CMakeLists.txt,main.cpp}`,
  `tests/consumer-wasm/{CMakeLists.txt,main.cpp}` (new)
- `.github/workflows/build.yml`
- `src/core/tui/{TerminalOutput.hpp,VtParser.hpp,VtParser.cpp,VtParser_test.cpp}`
- `docs/vendoring.md`, `docs/getting-started/cpm.md`, `README.md`, `CHANGELOG.md`,
  `.agent/reference/source-map.md`

## Self-review

- The tool writes nothing into `DEST` until the whole copy is legal, and refuses a `DEST` that is
  not a copy of ours — a first draft deleted the destination first, which would have made a
  refusal destructive.
- `file(READ)` opens a file in **text mode on Windows** and hands back a string a CRLF has already
  been taken out of. The first CR check used it and passed a CRLF file through; the tool now reads
  the hex form, uses a cheap `string(FIND "0d")` as a filter and confirms at byte boundaries
  (`0d` also occurs straddling two bytes). The self-test's own byte comparison uses the same hex
  scan.
- The commits are bisectable for the hygiene gate: the `source-glob` allowlist row sits in the
  commit that introduces the glob, not in the later one (checked at each commit, see above).
- The brief names one commit subject; it is split in two (`tool + self-test`, then
  `consumers + CI job`) because they are separable and the dispatch asks for small semantic
  commits.
- Working-tree line endings: several edits were made through Python, which writes CRLF on Windows.
  Every touched file was normalised to LF before committing and `git status` is clean; the CI job
  "No tracked file contains a CR byte" is the backstop.

## Concerns

0. **Not mine, but live:** the nightly `downstream.yml` fails because fastcached's
   `cmake/portable/CompileCache.cmake` has moved on and core-cpp's verbatim copy is stale. Under
   AGENT.md that file is re-synced verbatim whenever fastcached's changes; doing it inside this
   task would have mixed an unrelated upstream sync into it, so it needs a task of its own.
1. **The offline leg never ran offline locally** (no Docker on this machine). Its isolation, the
   image contents and the `g++-14` install are first exercised in CI (green there).
2. **`tests/consumer-*` are configured only by CI and by hand**, not by core-cpp's own ctest: each
   is a separate project with its own toolchain requirements (a container, an emsdk), so a
   `add_test` that configures them from inside core-cpp's build would drag those requirements into
   every preset. The `consumer-smoke` job is the gate.
3. **A sync spawns one `git cat-file` per file** (213 for contour's module set): about 9 s on
   Windows, ~2 s on Linux. `--batch` would be faster but its binary framing is not safely
   splittable in CMake.
4. **`CMAKE_CXX_SCAN_FOR_MODULES OFF` is needed in a WebAssembly consumer** (CMake scans C++23
   sources for module imports and wants `clang-scan-deps`, which an emsdk has not). It is the
   consumer's own setting, so it lives in `tests/consumer-wasm`; worth carrying into C8's morph
   migration notes.
5. The CPM smoke cannot see a leak that CMake's scoping already prevents (see the negative result
   above); it covers the leaks that are actually reachable.

---

# Fix round 1 (Ruling R47)

`task-A8-fixround1.md`, items 1–17. Seven commits on `master`, `2177c41..735a0c2`.

A previous session had started this round and was cut off. What it left uncommitted is described
under **What was inherited** below, with what had to be corrected — the short version is that the
tool's new refusals were sound and the self-test that was meant to prove them **had never been
run** and could not run.

## What was inherited, and what was wrong with it

Three modified, uncommitted files: `.github/workflows/release.yml`, `cmake/CoreCppVendor.cmake`
and `tests/cmake/check-vendor-selftest.cmake`.

| Inherited | Verdict |
|---|---|
| `release.yml`: `-DMODE=export` → `-DMODE=sync`, plus a `sed -n '1,7p' MANIFEST` line | Correct; unverified. Proven by running the step (below). |
| `CoreCppVendor.cmake`: the repository-root guard, the core-cpp-tree guard, the manifest-header and count requirements, `core_cpp_vendor_unstage()`, the REF-shape enforcement, the unconditional-module check, the `string(FIND)` CR scan, the `list(JOIN)` fixes, `file(CONFIGURE … NEWLINE_STYLE UNIX)` | Correct. Every guard kept, and each one now has a case proven able to fail. |
| `check-vendor-selftest.cmake`: the new cases | **Broken.** It did not run at all, and two of its cases were wrong. Rewritten in place. |

Three defects in the inherited self-test:

1. **It could not run.** `cmake_parse_arguments()` *unsets* a one-value keyword that was not given,
   and an undefined CMake variable compares as its own name, so `arg_MUTATE STREQUAL ""` was false
   for every case that mutates nothing and the very first case died on the chain meant to reject an
   unknown `MUTATE`:

   ```
   CMake Error at tests/cmake/check-vendor-selftest.cmake:350 (message):
     check-vendor-selftest: case syncs-a-tag-then-checks has MUTATE ''.
   ```

2. **A case passed by accident.** `MODULES "base;tui"` was passed down through `${ARGN}`, which
   re-splits a list element holding a semicolon, so the tool received `-DMODULES=base` and a stray
   `tui`, vendored `base` and said nothing:

   ```
   refuses-a-module-the-ref-has-not: sync accepted what it must refuse:
     -- core-cpp-vendor: v0.0.1 (…) copied into …: 4 file(s), modules base
   ```

   The run helpers now take the NAME of a list variable, which is the only way an element with a
   semicolon survives. Probed directly: through a function argument,
   `[-DMODULES=base]` `[tui]`; through a variable, `[-DMODULES=base;tui]`.

3. **The vendored-copy case never reached the check it was written for.** Its fixture copy had no
   `MANIFEST`, so the second sub-case hit the occupant refusal rather than the tree refusal:

   ```
   refuses-a-sync-from-inside-a-vendored-copy: a sync of the consumer's own repository refused,
     but not with 'is not a core-cpp tree': … holds 2 file(s) and no MANIFEST …
   ```

   The fixture is now the real thing: a core-cpp-shaped repository that carries the tool itself,
   vendored into a consumer's repository **with the tool's own `MODE=sync`**. The copy therefore
   has a genuine `MANIFEST` and a genuine `<copy>/cmake/CoreCppVendor.cmake`, the run gets as far as
   the tree check, and the copy's own `MODE=check` afterwards says that nothing about it moved.

Two more inherited claims that were not true and are now made so: the self-test's header said the
test "is registered with `SKIP_REGULAR_EXPRESSION`", which `tests/CMakeLists.txt` did not do
(item 14); and the new `file(GLOB)` in the self-test had no `source-glob` allowlist row, so
`ctest -L hygiene` was red.

## Item by item

### 1 — the release path (Critical) — RED/GREEN

`.github/workflows/release.yml:46` ran `-DMODE=export`; the tool takes `sync|check`.

RED, before: `core-cpp-vendor: MODE must be sync or check, not 'export'` — which is now a self-test
case of its own (`refuses-a-command-line-it-cannot-act-on`).

GREEN: the step's own five lines, run verbatim against a throwaway tag in a clone (nothing was
added to the working repository):

```
$ git clone --quiet /mnt/d/core-cpp $clone && git -C $clone tag v0.0.0-a8-dryrun HEAD
$ stage="$RUNNER_TEMP/core-cpp-$TAG"
$ cmake -DMODE=sync -DREF="$TAG" -DREPO="$GITHUB_WORKSPACE" -DDEST="$stage" -P cmake/CoreCppVendor.cmake
-- core-cpp-vendor: v0.0.0-a8-dryrun (2177c4199f60…) copied into …/core-cpp-v0.0.0-a8-dryrun:
   378 file(s), modules base;async;cli;log;net;platform;testing;tui
$ sed -n '1,7p' "$stage/MANIFEST"
# ref v0.0.0-a8-dryrun
# commit 2177c4199f60a6e68db8245782a85b5d56ec4b3d
# modules base;async;cli;log;net;platform;testing;tui
# files 378
$ tar -czf "core-cpp-$TAG-vendor.tar.gz" -C "$RUNNER_TEMP" "core-cpp-$TAG"
$ sha256sum "core-cpp-$TAG-vendor.tar.gz" > SHA256SUMS && cat SHA256SUMS
2c144f26e31ea6b0853a07cab2924a798f460ce231596f8f0de9a7e6b2c290c5  core-cpp-v0.0.0-a8-dryrun-vendor.tar.gz
-rw-r--r-- 741026 core-cpp-v0.0.0-a8-dryrun-vendor.tar.gz
core-cpp-v0.0.0-a8-dryrun-vendor.tar.gz: OK
```

And what a packager actually gets: the archive unpacked, then `MODE=check` with the copy's own
script — `matches its manifest: 378 file(s), commit 2177c419…`.

### 2, 3, 9, 12, 15 — the refusals, each proven able to fail

Every guard was broken in a **copy** of `cmake/CoreCppVendor.cmake` (nothing in the working tree
was touched) and the self-test run against that copy. Baseline first:

```
### baseline -> exit 0
    -- check-vendor-selftest: all 28 case(s) were accepted or refused as expected

### no-repo-root-guard -> exit 1                      (item 2, first half)
    refuses-a-sync-from-inside-a-vendored-copy: a sync with the copy's own script accepted what it
      must refuse: -- core-cpp-vendor: v0.0.1 (…) copied into …

### no-core-cpp-tree-guard -> exit 1                  (item 2, second half)
    refuses-a-sync-from-inside-a-vendored-copy: a sync of the consumer's own repository refused,
      but not with 'is not a core-cpp tree' …

### no-manifest-header-guard -> exit 1                (item 3)
    refuses-a-manifest-whose-count-is-not-a-number: check refused, but not with 'which is not a count'
    refuses-an-emptied-copy-beside-an-emptied-manifest: check refused, but not with 'header line'

### no-empty-manifest-guard -> exit 1                 (item 3)
    refuses-a-manifest-that-lists-no-file: check accepted what it must refuse:
      -- core-cpp-vendor: … matches its manifest: 0 file(s),

### no-unstage -> exit 1                              (item 9)
    a-failed-sync-leaves-the-copy-alone: the unresolvable ref left .core-cpp-vendor-staging inside
      the copy … the refused blob left .core-cpp-vendor-staging inside the copy … the check after
      it refused (exit 1) what it must accept
    refuses-a-sync-from-inside-a-vendored-copy: the refused sync left .core-cpp-vendor-staging
      inside the copy … the copy's own check afterwards refused (exit 1) what it must accept

### no-ref-shape-guard -> exit 1                      (item 12)
    refuses-a-branch-as-the-ref: sync accepted what it must refuse: -- core-cpp-vendor: master (…)
    refuses-head-as-the-ref:     sync accepted what it must refuse: -- core-cpp-vendor: HEAD (…)

### no-unconditional-modules-guard -> exit 1          (item 15)
    refuses-modules-that-omit-an-unconditional-module: sync accepted what it must refuse

### no-mode-guard / no-dest-guard / no-ref-set-guard -> exit 1   (item 4)
    refuses-a-command-line-it-cannot-act-on: … refused, but not with 'MODE must be sync or check'
    refuses-a-command-line-it-cannot-act-on: … refused, but not with 'DEST is not set'
      (without that guard DEST becomes the working directory: "…/core-cpp/MANIFEST does not exist")
    refuses-a-command-line-it-cannot-act-on: … refused, but not with 'REF is not set'
```

Eleven mutations, eleven expected cases red, no others.

Item 15 needed no design decision in the end: the ref's own `cmake/CoreCppModules.cmake` is read
and every `core_cpp_module()` row without a `WHEN` is required to be in `MODULES`. That is exactly
the hand-maintained `base;log;cli;platform;async;net;testing` that `build.yml`,
`docs/vendoring.md` and `tests/consumer-vendored` each repeat, and the real table was verified to
still accept it (213 files copied).

Item 9 also gained the strongest form of its assertion: after a refused sync, the copy's **own
`MODE=check`** must still accept it, not merely "the staging directory is gone".

### 4 — every refusal has a case

28 cases. Added this round: the count that is not a number, and the three command-line refusals
(`MODE`, `DEST`, `REF`). `refusalPhrases` is now the tool's refusals read off the script rather
than the ones the cases happened to want, so a case firing a refusal it did not deserve fails.
Two refusals are named in the file as deliberately uncovered, because no case can reach them
portably: `"is in no git repository"` (`WORK_DIR` lives inside a build tree, which is inside
core-cpp's own repository, so git ascends to it and the repository-root refusal fires first) and
`"needs git"` (`find_program()` searches the platform's default directories, not only `PATH`).

### 5 — the documented commands run as written

`README.md:63` gave `-P cmake/CoreCppVendor.cmake`, which from a consumer's root does not exist.
`docs/vendoring.md`'s `MODULES` example was unquoted, so a shell would have eaten its semicolons.
Both fixed, and every command on both pages executed:

```
=== README: sync a ref into a consumer tree with the CHECKOUT'S script ===
-- core-cpp-vendor: 2177c419… copied into …/consumer/vendor/core-cpp: 378 file(s)
=== README: verify it with the COPY'S OWN script, no git needed ===
-- core-cpp-vendor: …/consumer/vendor/core-cpp matches its manifest: 378 file(s)
=== docs/vendoring.md: the explicit REPO + quoted MODULES form ===
-- core-cpp-vendor: 2177c419… copied into …: 213 file(s), modules base;log;cli;platform;async;net;testing
-- core-cpp-vendor: … matches its manifest: 213 file(s)
--- tests/ is not in the copy: absent (right)
```

### 6 — the MANIFEST's own bytes

Fixed at the writer, which is what the item preferred: `file(CONFIGURE … NEWLINE_STYLE UNIX)`, the
one write in script mode that takes a newline style. Verified on a copy written from Windows —
`the manifest's own line endings: LF only (right)`. `docs/vendoring.md`'s sentence that they are
the writing host's is replaced by what is now true, so `2177c41`'s title is superseded.

(The `# repository <path>` line still records the exporting machine's absolute path. That is the
review's Minor 5 "consider", not part of this fix list, and it is left alone.)

### 7 — file modes

One paragraph in `docs/vendoring.md`: the copy is bytes and paths, `sync` writes every blob as an
ordinary file, `check` compares no mode, and nothing in the file set may become executable without
that line changing first. The contract is not widened.

### 8 — the five watched values that were empty on both sides

Given real values in the fixture, so the comparison works in both directions:
`add_compile_definitions(CONSUMER_CPM_OWN_DEFINITION=1)`, a `-L` / `/LIBPATH:` search path that
holds no library, `CMAKE_POSITION_INDEPENDENT_CODE ON`, `CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF`,
and a launcher of the consumer's own — `CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND}" -E env`,
which runs the compiler and nothing else, so the value is real without the build depending on a
tool being installed. All ten values survive `CPMAddPackage`; the leg builds and passes.

### 10 — the WebAssembly pthread guard — RED/GREEN

`if(TARGET Threads::Threads)` was dead: `find_package(Threads)` inside core-cpp creates a
directory-scoped imported target the parent cannot see. The leg now reads `LINK_LIBRARIES` and
`INTERFACE_LINK_LIBRARIES` off core-cpp's own targets, and refuses to run at all if it collected
none to look at.

RED — `target_link_libraries(core-cpp-base INTERFACE Threads::Threads)` injected into
`src/core/CMakeLists.txt`, with a probe for the guard it replaced left in place:

```
-- RED PROOF: the OLD guard `if(TARGET Threads::Threads)` is FALSE here
  consumer-wasm: core-cpp links threads, which a single-threaded WebAssembly build must not:
    core-cpp-base INTERFACE_LINK_LIBRARIES='Threads::Threads'
  -pthread forces SharedArrayBuffer, and with it cross-origin isolation, onto every page …
(configure exit 1)
```

That is both halves in one run: the old guard stayed quiet on the very defect it was written for,
and the new one names the target and the property. GREEN: the leg configures, builds and runs under
node locally (emsdk in WSL), `0 command(s) mention pthread`, and reports
`consumer-wasm: 8 core-cpp target(s), none of them linking threads`.

### 11 — the flag assertion skipped the targets where it matters — RED/GREEN

The loop ran over `CORE_CPP_TARGETS` (compiled libraries only). It now runs over
`coreCppTargetsInTree`: **11 targets** against the 9 published — the two extra are `core-cpp-async`
and `core-cpp-net_types`, the header-only ones where an INTERFACE flag reaches every consumer
compile.

RED — `target_compile_options(core-cpp-async INTERFACE -DINJECTED_BY_THE_RED_PROOF)`:

```
  consumer-cpm: core-cpp's targets are not what a consumer is promised:
    core-cpp-async has INTERFACE_COMPILE_OPTIONS='-DINJECTED_BY_THE_RED_PROOF',
      which its consumers would compile with
(configure exit 1)
```

`core-cpp-async` is precisely the target the old loop never looked at.

### 13 — the echo server's one read — RED/GREEN

Two sends over loopback were still delivered as **one** read here, which is exactly why the review
called the defect intermittent. So the condition the fix is about — a `read()` that returns less
than the whole request — is modelled directly: the server's read is capped at 5 bytes.

```
=== GREEN: the accumulate loop, with every read capped at 5 bytes ===
ok   the server flow echoed the whole request
ok   the client flow read its own bytes back
(exit 0)

=== RED: the one-read server this replaced, same 5-byte cap ===
ok   the server flow echoed the whole request      <-- the server reports success …
FAIL the client flow read its own bytes back       <-- … and the client got 5 of 26 bytes
(exit 1)
```

The RED line "the server flow echoed the whole request: ok" is the review's point in one line: the
server was satisfied by echoing what it happened to read, and the failure surfaced at the other end
with nothing in the output to explain it.

### 14 — the self-test skips rather than fails without git — proven

```
$ cmake -DTOOL=… -DWORK_DIR=… -DSKIP_EXIT_CODE=77 -P <copy with find_program stubbed out>
-- check-vendor-selftest: SKIPPED -- git is not on PATH, and every case builds a repository with it.
EXIT=77
```

and what ctest is told, read back from the generated `CTestTestfile.cmake`:

```
set_tests_properties([=[core-cpp.vendor-selftest]=] PROPERTIES LABELS "core-cpp;hygiene"
  SKIP_REGULAR_EXPRESSION "check-vendor-selftest: SKIPPED" SKIP_RETURN_CODE "77" …)
```

Both properties, because `cmake_language(EXIT)` is CMake 3.29 and the project supports 3.25: where
the exit code cannot be set, the message is what ctest matches. Neither path can read as a pass.

### 16 — SyncGuard — RED/GREEN

The flush on the way in is the constructor's first statement now, mirroring the destructor's;
`syncGuard()` is a one-liner; the paragraph that documented the asymmetry is gone.

RED — the constructor's flush removed and put back in `syncGuard()`, i.e. the old code, against the
new case:

```
/mnt/d/core-cpp/src/core/tui/TerminalOutput_test.cpp:79: FAILED:
  CHECK( output.captured() == "before\033[?2026h" )
with expansion:
  "" == "before"
test cases: 1 | 1 failed
```

`""` is the whole finding: with the old constructor, `before` was still in the buffer when the
region opened, so it would have been emitted *inside* the synchronized frame.

### 17 — the minors

- **The ~120 duplicated lines** are now `tests/consumer-shared/ConsumerSmoke.hpp`, included by both
  programs, which is why item 13 was one fix rather than two. Each program keeps what is its own:
  `core::tui` for the CPM consumer, `core::net_tls` for the vendored one. The container mounts
  `tests/consumer-shared` next to `tests/consumer-vendored`, so
  `#include "../consumer-shared/ConsumerSmoke.hpp"` resolves across the two mounts exactly as it
  does in a checkout — verified locally by reproducing that layout (`/consumer` and
  `/consumer-shared` as two sibling directories) rather than the checkout's.
- **The CR scan** already used `string(FIND)` with an even-offset test in the inherited work; kept.
- **`VtParser.cpp:653`**: the `size() >= PasteEnd.size()` guard before `ends_with()` is gone.
- **The two `list(JOIN … ";" …)` no-ops** were already fixed in the inherited work (they join with
  `" and no "` and `", "` now); kept.

## Verification

| Where | What | Result |
|---|---|---|
| WSL | `clang-debug`: build, `ctest` (19 tests) | green |
| WSL | `gcc-release`: build, `ctest` (19 tests) | green |
| WSL | `ctest -L hygiene` (7 tests, incl. `vendor-selftest`, 28 cases) | green |
| WSL | `clang-tidy` preset | green, no diagnostic |
| Windows | `clangcl-debug` (`--clean-first`, header edits): build, `ctest` (21 tests) | green |
| Windows | `cl-debug`: build, `ctest` (21 tests) | green |
| WSL | vendor self-test standalone, 28 cases | green |
| WSL | 11 guard mutations | each turns exactly its own case red |
| WSL | consumer-cpm: configure, build, ctest | green |
| WSL | consumer-wasm: `emcmake` configure, build, ctest under node | green |
| WSL | consumer-vendored: sync HEAD, configure, build, ctest, **in the container's directory layout** | green |
| WSL | the same, after a local edit to the copy | `src/core/Utils.hpp: differs from the manifest` — the consumer's own gate fires |
| WSL | `python3 scripts/clang-format.py --check` (clang-format 22.1.8) | 356 files clean |
| Windows | `mkdocs build --strict` | built, no warning |

**Not run locally:** the vendored leg **inside the container**. Docker Desktop is not running on
this machine (`failed to connect to the docker API at npipe:…`), so the network isolation
(`/sys/class/net` holding only `lo`), the absence of git in the image and the `g++-14` install stay
CI's to prove. Everything the container then does — the sync, the configure, the build, both tests,
and the two-mount include path — was run here outside it.

## CI

All green, on the round's final tree.

| Run | Workflow | Head | Result |
|---|---|---|---|
| [35511335156](https://github.com/contour-terminal/core-cpp/actions/runs/35511335156) | Build | `735a0c2` | **success** — 22 jobs, `ci-ok: success`, `consumer-smoke (cpm / vendored / wasm): success` |
| [35511335082](https://github.com/contour-terminal/core-cpp/actions/runs/35511335082) | Docs | `735a0c2` | success |
| [35511352144](https://github.com/contour-terminal/core-cpp/actions/runs/35511352144) | Portability (`workflow_dispatch`) | `735a0c2` | success — FreeBSD (system clang) |
| [35511575686](https://github.com/contour-terminal/core-cpp/actions/runs/35511575686) | Build | `8d1fc18` (HEAD) | **success**, `ci-ok: success` |

`consumer-smoke (vendored)` passing is what confirms the one thing that could not be tried here:
the container's two read-only mounts, `/consumer` and `/consumer-shared`, with
`#include "../consumer-shared/ConsumerSmoke.hpp"` resolving across them, inside
`docker run --network none` on an image with no git.

The second Build run is the comment-only commit `8d1fc18`; the run before it is where every
substantive change was exercised. Both are green, so HEAD is green.

## Commits

| Commit | What |
|---|---|
| `77c593a` | `build: the release job's vendor archive calls a mode the tool has` (item 1) |
| `9c78a64` | `build: the vendoring tool refuses the mis-invocations that were silent` (items 2, 3, 4, 9, 12, 14, 15, 17) |
| `0d555a0` | `docs: the vendoring commands run as written, and say what they refuse` (items 5, 6, 7) |
| `c52aa4f` | `test: the consumer smoke legs assert what they claimed to assert` (items 8, 10, 11, 13, 17) |
| `9aff7b0` | `tui: SyncGuard's constructor flushes, so both ways of making one agree` (items 16, 17) |
| `02ce17a` | `build: the self-test's copy-emptying glob has its allowlist row` |
| `735a0c2` | `docs: the changelog and source map record the round's fixes` |
| `8d1fc18` | `docs: the self-test's header says which refusals no case can reach` |

## Concerns

1. **The container leg's two mounts were the one thing only CI could confirm** — and it did:
   `consumer-smoke (vendored)` is green on both runs. Nothing outstanding.
2. **`CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND}" -E env` in `tests/consumer-cpm`** puts an
   extra process in front of every compile of that leg. It is what makes the launcher promise a
   real assertion rather than a watched empty string, and it costs a fork per translation unit in
   one smoke leg; if that ever shows in the job's time, the alternative is to document those
   values as watched-for-appearance instead.
3. **The tool's module-table reader is a regular expression over the ref's own
   `cmake/CoreCppModules.cmake`.** It matches `core_cpp_module(` and not `core_cpp_module_target(`,
   and the rows inside that file's own `message()` strings carry no `NAME <word>`, so it reads the
   table correctly today. A future row spelled across a `)` inside a string would need it
   revisited; the self-test's fixture table is what would catch a change in shape.
4. Concern 0 of the original report still stands: the verbatim
   `cmake/portable/CompileCache.cmake` is stale against fastcached, and re-syncing it is a task of
   its own rather than something to mix into this round.

---

# Fix round 2 (Ruling R48)

`task-A8-fixround2.md`, four Minor items from the re-review of round 1, plus one question answered
here rather than in code. Four commits.

## Item by item

### 1 — the self-test over-claimed, and one tool branch was unreachable

**The branch.** `cmake/CoreCppVendor.cmake`'s `${REF} has no file of the vendored set for modules …`
could not fire. The tree guard above it refuses any ref without `cmake/CoreCppModules.cmake`, and
that file is in the file set for *every* `MODULES` list (it is under `cmake/`, which is taken
whole), so `copied` is never empty by the time the branch is read. Deleted, per the fix list's
preference, with a comment where it was saying why nothing stands there.

**The header.** It said every refusal had a case; six had neither a case nor a phrase. The header
now splits the tool's stops into the two kinds they actually are, and says which is proved:

- **Judgements** — a command line it cannot act on, a `REPO` or `REF` it will not read, a tree that
  is not core-cpp's, a `MODULES` list that would not configure, a blob it cannot copy verbatim, a
  copy or a manifest that disagrees with the other. Every one has a case and a phrase, bar the two
  already named as unreachable portably (`is in no git repository`, `needs git`).
- **Reports of git failing** — `cloning … failed`, `listing the tree … failed`, `reading blob …
  failed`, an `ls-tree` line in a shape git does not emit, and the two `core_cpp_vendor_git()`
  wrappers (`finding the root of the repository at …`, `reading cmake/CoreCppModules.cmake of …`).
  No case provokes one, and the header says why: provoking one means breaking git or its output
  format rather than handing the tool a tree or a copy, and a test that broke git would be testing
  git. These are deliberately *not* in `refusalPhrases`, which the comment there now also states.

The re-review's specific objection was that the file did not apply that distinction consistently,
because `resolving the commit` comes from the same git wrapper and *does* have a case and a phrase.
The header names it as the exception that makes the split readable: its message is the wrapper's,
but what reaches it is the caller's mistake — a well-formed SHA that resolves nowhere — not git
misbehaving, so it is judged like a judgement.

### 2 — the stale attribution in `~SyncGuard`

`src/core/tui/TerminalOutput.cpp:317` still read "syncGuard() flushes on the way in, so the two ends
match" after round 1 moved that flush into the constructor. It now reads "The constructor flushes on
the way in for the same reason, so the two ends match". Comment only; no behaviour.

### 3 — the avoidable hygiene allowlist row

`core_cpp_selftest_empty()` deleted everything in a copy *except* its `MANIFEST`, which needed a
`file(GLOB)` and therefore a `source-glob` allowlist row exempting the whole file (the rule is
per-file, not per-line). Both call sites overwrite that `MANIFEST` on the very next line, so
`file(REMOVE_RECURSE "${copy}")` over the whole directory is exactly equivalent. The function, the
glob and the allowlist row are all gone; `tests/cmake/check-cmake-hygiene.cmake` is back to the
rows it had before round 1.

Proved equivalent rather than assumed: the two cases that use it (`EMPTY_ALL`, `EMPTY_HEADERS`)
still pass, and both still go red under the mutations that cover them, identically to round 1.

### 4 — the two documents

`docs/vendoring.md` said `sync` "writes nothing into `DEST` until the whole copy is legal". It does
write into `DEST`, into `DEST/.core-cpp-vendor-staging/`, from the moment it starts staging; what
is true is the consequent, and it is true *because* every refusal deletes that directory on its way
out. The bullet is now its own, and says where the staging directory is, when it goes, and that a
copy a refused sync found still passes its own `check`.

The `check` refusal list on both `docs/vendoring.md` and `CHANGELOG.md` named only the `# files`
family. Four more are implemented and reachable by hand-editing a manifest, and both now list them:
a line that is neither a `#` header nor `<sha256>  <path>`, a missing `# repository`, a missing
`# ref`, and a `# commit` that is not 40 lowercase hex digits.

## Answered, not built: pinning the module table's shape

**The residual.** `cmake/CoreCppVendor.cmake` reads the ref's own `cmake/CoreCppModules.cmake` with
a regular expression to find the rows that carry no `WHEN`. The self-test's fixture table is two
clean single-line rows, so it proves the mechanism and pins none of the real file's shape — no
comments, no `core_cpp_module_target(` rows, no multi-line row, none of the six
`message(FATAL_ERROR "core_cpp_module(${arg_NAME}): …")` fragments that survive the scan only
because `${arg_NAME}` leaves no space after `NAME`. The two directions are not symmetric: a phantom
row causes a false refusal, which is loud; a *missed* row silently restores the defect item 15 was
about.

**Not here, and not as a fixture.** A fixture that imitates the real file's shape pins the
imitation. The day the real table grows a shape the copy does not have — a row generated by a loop,
a value carrying a `)`, a renamed table function — the fixture is still green, which is the same
silent direction one level out. Copying today's table into the fixture buys a check that decays the
first time the real one changes and nobody updates the copy.

**The assertion that closes it is over the real file, and is one line of idea.** Read
`cmake/CoreCppModules.cmake` twice and compare: once by `include()`ing it, which leaves
`CORE_CPP_MODULES` and a `WHEN` field per module because that is what `core_cpp_module()` sets; and
once with the vendoring tool's own regular expression. Require the two name sets to be equal, and
require them to agree on which names have an empty `WHEN`. A row the regex misses — for any reason,
including ones nobody has thought of — fails immediately and by name. Nothing to keep in step, and
no fixture.

**Where it belongs: `tests/cmake/check-layering.cmake`.** That test's subject is already the real
module table, and it already configures projects from it. The vendor self-test's subject is the
*tool*, exercised against repositories it builds for the purpose; every case there deliberately
avoids depending on core-cpp's own tree, and giving it a second job of policing the real table would
blur that and couple it to a file it otherwise never reads.

**Why not in this round.** It is a new assertion in another test's file with failure modes of its
own, and A8 is closing. The exposure meanwhile is bounded and visible: the only consumer-facing
symptom is a re-synced copy that does not configure, which the consumer hits at its next configure,
and `consumer-smoke (vendored)` syncs the real repository with the documented `MODULES` list on
every push, so the loud direction is covered today. Recommended as a follow-up ticket against
`check-layering.cmake`, not as a blocker on A8.

## Verification

Nothing in this round changes a header, so the clang-cl tree needed no `--clean-first`.

| Where | What | Result |
|---|---|---|
| WSL | vendor self-test standalone | `all 28 case(s) were accepted or refused as expected` |
| WSL | the two mutations covering the reworked cases | unchanged from round 1 (below) |
| WSL | `clang-debug`: build, `ctest` | green, 19 tests, `Total Test time (real) = 110.15 sec` |
| WSL | `gcc-release`: build, `ctest` | green, 19 tests, `Total Test time (real) = 107.76 sec` |
| WSL | `ctest -L hygiene` (7 tests, `cmake-hygiene` included) | green — the allowlist row is gone and the rule is satisfied, not exempted |
| Windows | `clangcl-debug`: build, `ctest` | green, 21 tests, 42.64 s |
| Windows | `cl-debug`: build, `ctest` | green, 21 tests, 42.39 s |
| WSL | `python3 scripts/clang-format.py --check` | `356 file(s) are formatted with clang-format 22.1.8` |
| Windows | `mkdocs build --strict` | built, no warning |

The glob removal is proved equivalent rather than assumed — the two cases that used
`core_cpp_selftest_empty()` still go red under exactly the mutations that made them go red in
round 1, and under nothing else:

```
### baseline -> exit 0
    -- check-vendor-selftest: all 28 case(s) were accepted or refused as expected

### no-manifest-header-guard -> exit 1
    refuses-a-manifest-whose-count-is-not-a-number: check refused, but not with 'which is not a count'
    refuses-an-emptied-copy-beside-an-emptied-manifest: check refused, but not with 'header line'

### no-empty-manifest-guard -> exit 1
    refuses-a-manifest-that-lists-no-file: check accepted what it must refuse:
      -- core-cpp-vendor: … matches its manifest: 0 file(s),
```

## CI

| Run | Workflow | Head | Result |
|---|---|---|---|
| [35513381325](https://github.com/contour-terminal/core-cpp/actions/runs/35513381325) | Build | `8581313` | **success** — `ci-ok: success`, `consumer-smoke (cpm / vendored / wasm): success` |
| [35513381346](https://github.com/contour-terminal/core-cpp/actions/runs/35513381346) | Docs | `8581313` | success |
| [35513384532](https://github.com/contour-terminal/core-cpp/actions/runs/35513384532) | Portability (`workflow_dispatch`) | `8581313` | success — FreeBSD (system clang) |

## Commits

| Commit | What |
|---|---|
| `18e273e` | `build: the vendoring tool has no branch nothing can reach` (item 1, and item 3's glob) |
| `eb50914` | `build: the source-glob allowlist loses the row it no longer needs` (item 3) |
| `0599842` | `tui: ~SyncGuard's comment credits the flush to what performs it` (item 2) |
| `8581313` | `docs: the vendoring page says what sync writes, and lists every check refusal` (item 4) |

## Concerns

None new. The three carried from round 1 stand as written: the CPM leg's `cmake -E env` launcher
costs a fork per translation unit in that job; the module table's shape is pinned nowhere, with the
follow-up recommended above; and `cmake/portable/CompileCache.cmake` is stale against fastcached and
needs a task of its own.

One observation from the re-review is **not** covered by R48 and is left open deliberately: the
replacement phase of `sync` (`CoreCppVendor.cmake`: empty `DEST`, then rename the staged entries in)
is the one window that is not transactional — a `file(RENAME)` failure there, such as a locked file
on Windows, leaves `DEST` half-replaced. It is the only remaining path on which a `sync` can damage
a copy, and it was not on this fix list.

---

# Fix round 3 (Ruling R49)

One item, carried from fix round 2's own concern: `sync`'s replacement phase was not
transactional. Two commits.

## What was wrong

The replacement emptied `DEST` and then moved the staged entries in one by one:

```cmake
file(GLOB existing LIST_DIRECTORIES true "${DEST}/*")   # …and remove each
…
file(GLOB staged LIST_DIRECTORIES true "${stagingDir}/*")
foreach(entry IN LISTS staged)
    file(RENAME "${entry}" "${DEST}/${name}")           # …one at a time
endforeach()
```

A `file(RENAME)` that failed part-way through that loop left `DEST` holding some of the new copy,
none of the old one and — since the manifest is written afterwards — no manifest at all. It was the
last path on which a sync could damage a copy that was already there, and contour's build depends
on that copy being the old one or the new one.

## The fix

Two directory renames, through a **sibling** of `DEST` rather than a child of it, because `DEST`
itself is what gets renamed now:

```
  DEST                      ->  DEST.core-cpp-vendor-old     (skipped when DEST does not exist)
  DEST.core-cpp-vendor-new  ->  DEST
  remove DEST.core-cpp-vendor-old
```

- A failure at the first leaves `DEST` exactly as it was, with nothing of the new copy written into
  it.
- A failure at the second puts the previous copy back and refuses, naming what failed and saying
  the copy is the one it was.
- Only if that restore *also* fails is there nothing left to do but name both directories and
  delete neither, so a human can finish by hand.
- A `DEST.core-cpp-vendor-old` found on disk is a previous run that failed between the two renames,
  so it holds the only copy of what was there. A sync refuses rather than delete it to make room,
  and says how to finish. That is new behaviour, and it is also what makes the *first* rename fail
  in a way a test can arrange.
- An empty `DEST` and a `DEST` whose parent does not exist both still work: the parent is created,
  and an existing `DEST` of any shape is simply renamed aside.

The new copy is assembled in `DEST.core-cpp-vendor-new` from the start, so the earlier promise is
unchanged and slightly stronger: a refusal leaves the previous copy exactly as it was **and nothing
beside it**, which the self-test now asserts for both siblings rather than for one child directory.

## Why it is a module

`cmake/CoreCppVendorReplace.cmake` holds one function, `core_cpp_vendor_replace()`, and nothing
else. The reason is the proof, not tidiness.

**There is no portable way to force the failure through the script**, and the reason is structural
rather than a limitation of the test: the two renames are the same operation on the same kind of
object in the same directory, so once the first has succeeded, nothing a test can set up will make
the second fail. `file(RENAME)` fails portably on a non-empty target or a missing source, and after
step 1 the target does not exist and the source does. The Windows-only ways (an open handle, a lock,
a scanner) are not arrangeable from a `cmake -P` child on Linux, and the Linux-only ones (a
read-only parent) fail *both* renames, not the second alone.

So, as the ruling allowed, the restore path is **tested directly**: the self-test `include()`s the
module and calls the real function with a new copy that does not exist, which is the portable way to
make exactly that rename fail. It is the production code that runs, not a transcription of it.

The function's last outcome — both renames failing — stays unreached for the same reason one level
down: it needs the restore itself to fail. The file header and the self-test header both say so, in
the same place they name the git-failure reports no case provokes.

## Proof

30 cases, up from 28. The two new ones:

- `refuses-a-leftover-backup-beside-the-copy` — a real vendored copy with a
  `…core-cpp-vendor-old` beside it. The sync must refuse, must not delete the leftover, must leave
  no `…-new`, and the copy must still pass its own `check`.
- `restores-the-copy-when-the-replacement-fails` — calls `core_cpp_vendor_replace()` over a real
  copy with a `newCopy` that does not exist, and asserts the state, the message, that both siblings
  are gone, that the manifest's SHA-256 is the one it was before, and that `MODE=check` still
  accepts the copy.

Each guarantee broken in a **copy** of the two files, the self-test run against that copy
(the working tree was not touched):

```
### baseline -> exit 0
    -- check-vendor-selftest: all 30 case(s) were accepted or refused as expected

### deletes-a-leftover-backup -> exit 1
    refuses-a-leftover-backup-beside-the-copy: a sync beside a leftover backup accepted what it
      must refuse: -- core-cpp-vendor: v0.0.1 (…) copied into …

### no-restore -> exit 1
    restores-the-copy-when-the-replacement-fails: the replacement reported 'FAILED-KEEP-BOTH'
      where the rename could not have worked … the message does not say 'put back' …

### never-moved-aside -> exit 1
    restores-the-copy-when-the-replacement-fails: the message does not say 'put back' …

### a-message-that-is-a-list -> exit 1
    restores-the-copy-when-the-replacement-fails: the message is a list, so it prints with stray
      semicolons: could not move the new copy from …
```

### A defect the end-to-end run found before the self-test did

The first draft built each message as several arguments to `set()`. That makes a **list**, and the
caller prints a list with a semicolon at every join, so the refusal came out as:

```
  already exists.  That is where a run moves the previous copy aside, so a
  ;previous run failed and never put it back.  Nothing has been changed now.
  Move it back ;to /tmp/…/vendor/core-cpp, or delete it once you are sure
```

The self-test's phrase assertions did not catch it — each phrase happened to sit inside one
fragment. It was found by reading the output of the end-to-end run against the real repository.
Every message is `string(CONCAT)` now, the module says why in a comment, and the case asserts that
the message carries no semicolon at all, which is the mutation `a-message-that-is-a-list` above.

### End to end, against the real repository

```
=== 1. a fresh DEST whose parent does not exist ===
-- core-cpp-vendor: 8581313f… copied into …/deep/nested/vendor/core-cpp: 378 file(s)
--- the copy carries the replacement module too (cmake/** is taken whole) ---
…/vendor/core-cpp/cmake/CoreCppVendorReplace.cmake
-- core-cpp-vendor: … matches its manifest: 378 file(s)
--- what is beside the copy ---
core-cpp

=== 2. a re-sync over that copy, dropping tui (the documented MODULES list) ===
-- core-cpp-vendor: 8581313f… copied into …: 213 file(s), modules base;log;cli;platform;async;net;testing
src/core/tui: gone (right)
-- core-cpp-vendor: … matches its manifest: 213 file(s)
--- what is beside the copy ---
core-cpp

=== 3. a leftover backup beside a real copy ===
  already exists.  That is where a run moves the previous copy aside, so a
  previous run failed and never put it back.  Nothing has been changed now.
  Move it back to …/vendor/core-cpp, or delete it once you are sure …
--- the leftover survived: ---
the previous copy
--- and the copy still checks out: ---
-- core-cpp-vendor: … matches its manifest: 213 file(s)

=== 4. the release workflow's Vendor archive step, on a throwaway tag ===
-- core-cpp-vendor: v0.0.0-a8r3-dryrun (8581313f…) copied into …: 378 file(s)
d7617a346c8d8f4512df6dac8bab7637e219b02c3cbd7a0a363462a5039f4ff5  core-cpp-v0.0.0-a8r3-dryrun-vendor.tar.gz
--- what RUNNER_TEMP holds (a sibling here would land in the tarball's directory) ---
core-cpp-v0.0.0-a8r3-dryrun
-- core-cpp-vendor: …/unpacked/core-cpp-v0.0.0-a8r3-dryrun matches its manifest: 378 file(s)
ALL FOUR OK
```

Step 2 is the one that used to depend on the file-by-file loop to remove what the new ref no longer
has: swapping whole directories does it by construction, and `src/core/tui` is gone rather than
left behind for `check` to call unlisted. Step 4 confirms the release tarball's directory has no
sibling in it — the siblings are removed on success, so neither reaches the archive.

## Verification

No C++ changed this round, so the clang-cl tree needed no `--clean-first` and both Windows builds
were `ninja: no work to do` before their test runs.

| Where | What | Result |
|---|---|---|
| WSL | vendor self-test standalone | `all 30 case(s) were accepted or refused as expected` |
| WSL | four guard mutations | each turns exactly its own case red (above) |
| WSL | end to end against the real repository, four scenarios | all four OK (above) |
| WSL | `clang-debug`: build, `ctest` | green, 19 tests, `Total Test time (real) = 110.71 sec` |
| WSL | `gcc-release`: build, `ctest` | green, 19 tests, `Total Test time (real) = 108.24 sec` |
| WSL | `ctest -L hygiene` (7 tests) | green, `100% tests passed, 0 tests failed out of 7` |
| Windows | `clangcl-debug`: build, `ctest` | green, 21 tests, 43.49 s |
| Windows | `cl-debug`: build, `ctest` | green, 21 tests, 43.77 s |
| WSL | `python3 scripts/clang-format.py --check` | `356 file(s) are formatted with clang-format 22.1.8` |
| Windows | `mkdocs build --strict` | built, no warning |

## CI

| Run | Workflow | Head | Result |
|---|---|---|---|
| [35514743433](https://github.com/contour-terminal/core-cpp/actions/runs/35514743433) | Build | `b1db039` | **success** — `ci-ok: success`, `consumer-smoke (cpm / vendored / wasm): success` |
| [35514743431](https://github.com/contour-terminal/core-cpp/actions/runs/35514743431) | Docs | `b1db039` | success |
| [35514743303](https://github.com/contour-terminal/core-cpp/actions/runs/35514743303) | Portability (`workflow_dispatch`) | `b1db039` | success — FreeBSD (system clang) |

`consumer-smoke (vendored)` is the new replacement exercised for real: the job syncs this commit
into `$RUNNER_TEMP/vendor/core-cpp`, then configures, builds and `check`s that copy inside
`docker run --network none` on an image with no git.

## Commits

| Commit | What |
|---|---|
| `08dcb40` | `build: a sync leaves DEST the old copy or the new one, never half of each` |
| `b1db039` | `docs: the vendoring page says what the replacement guarantees` |

## Concerns

The out-of-scope observation that opened this round is closed: the replacement is recoverable, and
`sync` now has no path on which it can leave a copy in a state that is neither the old one nor the
new one.

What remains is what round 2 left, unchanged:

1. **`CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND}" -E env` in `tests/consumer-cpm`** puts a fork
   in front of every translation unit of that job. It is what makes the launcher assertion real.
2. **The module table's shape is pinned nowhere.** The follow-up is agreed and carried to
   `check-layering.cmake`: read `cmake/CoreCppModules.cmake` twice — once by `include()`ing it, once
   with the vendoring tool's regular expression — and require the two to agree on the names and on
   which of them have an empty `WHEN`.
3. **`cmake/portable/CompileCache.cmake` is stale against fastcached**, and re-syncing a verbatim
   file is a task of its own.

Two things are reachable in production and unreached by any test, both named where a reader will
meet them rather than left implicit:

- `core_cpp_vendor_replace()`'s `FAILED-KEEP-BOTH`, which needs the restore itself to fail
  (`cmake/CoreCppVendorReplace.cmake`'s header, and the self-test's).
- The tool's reports of git itself failing (the self-test's header, from round 2).

---

# Fix round 4 (Ruling R50) — the last one

Five Minor items from the re-review of round 3, plus two of its out-of-scope observations. Two
commits.

## Item by item

### 1 — a `DEST` that is an existing regular file — RED/GREEN

Round 3 moved the staging directory out of `DEST` and lost a protection that had been incidental:
`file(MAKE_DIRECTORY "${DEST}/.core-cpp-vendor-staging")` used to stop the run on a file. The
occupant guard never covered it, because a file holds no files, so the replacement renamed it aside
like any previous copy and deleted it after a successful swap — reporting success over someone's
destroyed file, while `docs/vendoring.md` and `CHANGELOG.md` both promised a `DEST` that is not
ours is refused.

`if(EXISTS "${DEST}" AND NOT IS_DIRECTORY "${DEST}")` now refuses by name, before anything is read.
The case `refuses-a-destination-that-is-a-file` asserts the refusal *and* that what is there is
still a file with its contents.

RED, with the guard removed in a copy of the tool:

```
    refuses-a-destination-that-is-a-file: sync accepted what it must refuse:
      -- core-cpp-vendor: v0.0.1 (…) copied into …/vendor/core-cpp: 5 file(s)
```

and against the real repository, so the damage is not hypothetical:

```
$ echo "someone else's file" > …/vendor/core-cpp
$ cmake -DMODE=sync … -P <the guard-less copy>
-- core-cpp-vendor: b1db0391… copied into …/vendor/core-cpp: 214 file(s), modules base;log;…
  -> DEST is now a DIRECTORY; the file is gone

$ echo "someone else's file" > …/vendor/core-cpp
$ cmake -DMODE=sync … -P cmake/CoreCppVendor.cmake
  would destroy it.  Delete it, or point DEST at a directory.
  -> the file is still a file: someone else's file
```

### 2 and 3 — the manifest moves into the staged tree — RED/GREEN

These are one fix. The `MANIFEST` was written into `DEST` *after* the swap, which left two things
wrong:

- A kill between the two stranded a manifest-less copy with the previous one already deleted: a
  copy that fails its own `check`, and that the next `sync` then refuses to overwrite.
- `FAILED-KEEP-BOTH` named both trees and told a human to adopt either, but the new one had no
  manifest yet, so adopting it produced exactly that stuck state.

The manifest is written into `<DEST>.core-cpp-vendor-new` before the swap now, hashed from the
files as they lie there. The staged tree is a complete vendored copy before anything moves, so the
swap is the last thing that happens and whichever of the two directories exists when a sync stops
is one that passes `check`. The bytes are unchanged: nothing in the manifest names `DEST`, and
`MANIFEST` is not among the files it lists, so it never hashes itself. `FAILED-KEEP-BOTH` now says
both are complete copies, that the one you keep passes `check` as it stands, and that renaming the
previous copy back is the state the sync had promised.

The window is only observable by stopping the process at that instant, so that is what the proof
does: a copy of the tool that returns the moment the replacement reports OK, run over an existing
good copy.

```
--- GREEN: the manifest is written into the staged tree, before the swap ---
-- core-cpp-vendor: [the process is killed here, right after the swap]
  MANIFEST present? yes
  the copy's own check: PASSES
  and the next sync over it: runs

--- RED: the old ordering, where the manifest was written after the swap ---
-- core-cpp-vendor: [the process is killed here, right after the swap]
  MANIFEST present? NO
  the copy's own check: FAILS
  and the next sync over it: REFUSES
```

The RED leg is the whole finding in three lines: the copy is there, it is wrong, and the tool will
not repair it.

**Does any ordering remain where a crash loses the old copy before the new one is complete?**
No. The order is now: guards (nothing written) → assemble `<DEST>.core-cpp-vendor-new` → write its
`MANIFEST` → `DEST` to `<DEST>.core-cpp-vendor-old` → `<DEST>.core-cpp-vendor-new` to `DEST` →
delete the backup. The new copy is complete before the first rename, so:

| Killed | On disk | Next sync |
|---|---|---|
| before the first rename | `DEST` is the old copy; a stale `…-new` beside it | removes the stale `…-new` and runs |
| between the two renames | `DEST` gone; `…-old` and `…-new` are both complete copies | refuses, names `…-old`, nothing lost |
| between the second rename and the delete | `DEST` is the new copy; `…-old` still there | refuses, names `…-old`, nothing lost |
| after the delete | `DEST` is the new copy | runs |

The worst outcome is a leftover the next sync refuses over, by name, with what to do. There is no
point at which the previous copy is gone and what stands in its place is unfinished.

### 4 — the leftover-backup refusal moves early

It lived in `core_cpp_vendor_replace()` and therefore fired after the clone, the tree walk and every
blob had been written — minutes, for a condition true in a second. It is beside the other two
guards now, same refusal and same message, and the function's own copy is gone rather than left as
a branch no caller can reach (round 2's rule about dead code in this tool). The case
`refuses-a-leftover-backup-beside-the-copy` is unchanged and still passes, now exercising the early
guard.

### 5 and 6 — the two stale sentences

`docs/vendoring.md` said the clone lands "into a temporary directory under `DEST`"; it is a bare
clone inside `<dir>.core-cpp-vendor-new`, beside `<dir>`, removed before that copy is put in place.
It also said "a `DEST` holding a manifest is emptied first", which was the file-by-file move round 3
replaced — the effect is the same, the mechanism is a whole-directory swap. Both are rewritten, and
the page and the CHANGELOG also carry what round 4 adds.

### 7 — the self-test's taxonomy

It said the script stops for exactly two kinds of reason. The replacement adds a third, and its
three messages are named now with which has a case:

- `could not move the new copy … into <DEST>` — has one, reached by calling the function
  (`restores-the-copy-when-the-replacement-fails`).
- `could not move <DEST> aside to <backup>` — no case: it needs a `DEST` that will not move
  although the caller has just refused every reason it would not.
- the pair of paths that says the restore failed too — no case: it needs a rename to be unable to
  undo itself in the directory it has just emptied.

## Verification

No C++ changed, so the clang-cl tree needed no `--clean-first`.

| Where | What | Result |
|---|---|---|
| WSL | vendor self-test standalone | `all 31 case(s) were accepted or refused as expected` |
| WSL | item 1's mutation, and the same against the real repository | RED/GREEN above |
| WSL | item 3's killed-sync pair | RED/GREEN above |
| WSL | `clang-debug`: build, `ctest` | `100% tests passed, 0 tests failed out of 19` |
| WSL | `gcc-release`: build, `ctest` | `100% tests passed, 0 tests failed out of 19` |
| WSL | `ctest -L hygiene` (7 tests) | `100% tests passed, 0 tests failed out of 7` |
| Windows | `clangcl-debug`: `ctest` | `100% tests passed, 0 tests failed out of 21` |
| Windows | `cl-debug`: `ctest` | `100% tests passed, 0 tests failed out of 21` |
| WSL | `python3 scripts/clang-format.py --check` | `356 file(s) are formatted with clang-format 22.1.8` |
| Windows | `mkdocs build --strict` | built, no warning |

## Commits

| Commit | What |
|---|---|
| `6990dcf` | `build: a sync refuses a DEST it cannot own, and finishes the copy before the swap` (items 1, 2, 3, 4, 7) |
| `f7a89f3` | `docs: the vendoring page and the changelog describe round 3's mechanism` (items 5, 6) |

## CI

| Run | Workflow | Head | Result |
|---|---|---|---|
| [35516528273](https://github.com/contour-terminal/core-cpp/actions/runs/35516528273) | Build | `f7a89f3` | **success** — `ci-ok: success`, `consumer-smoke (cpm / vendored / wasm): success` |
| [35516528296](https://github.com/contour-terminal/core-cpp/actions/runs/35516528296) | Docs | `f7a89f3` | success |
| [35516528221](https://github.com/contour-terminal/core-cpp/actions/runs/35516528221) | Portability (`workflow_dispatch`) | `f7a89f3` | success — FreeBSD (system clang) |

## Concerns

None from this round. What A8 closes with, unchanged from round 3:

1. **`CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND}" -E env` in `tests/consumer-cpm`** puts a fork
   in front of every translation unit of that job. It is what makes the launcher assertion real,
   and the trade-off is recorded rather than hidden.
2. **The module table's shape is pinned nowhere.** Agreed as a follow-up on `check-layering.cmake`:
   read `cmake/CoreCppModules.cmake` twice — once by `include()`ing it, once with the vendoring
   tool's regular expression — and require the two to agree on the names and on which have an empty
   `WHEN`. Carried by the lead.
3. **`cmake/portable/CompileCache.cmake` is stale against fastcached.** Re-syncing a verbatim file
   is a task of its own.

And the states that are reachable in production but not from a test, each named where a reader
meets them rather than left implicit:

- `core_cpp_vendor_replace()`'s `could not move <DEST> aside` and `FAILED-KEEP-BOTH`
  (`cmake/CoreCppVendorReplace.cmake`'s header, and the self-test's).
- The tool's reports of git itself failing, and the two judgements no case can reach portably
  (the self-test's header).

## A8, at the end of four fix rounds

`cmake/CoreCppVendor.cmake` and `cmake/CoreCppVendorReplace.cmake` refuse, by name, every way the
tool can be handed something it cannot copy correctly or a destination it must not own; the
self-test has 31 cases and each of them has been watched to fail; and a `sync` that stops for any
reason — a refusal, a failed rename, or being killed — leaves `DEST` holding a whole vendored copy,
the old one or the new one, that passes its own `check`. That is the property contour's build
depends on.
