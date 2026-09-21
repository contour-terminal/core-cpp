# Task A6 re-review: fix round 1 (Ruling R42)

Base def7325, head c4a083a. Diff read in full (`review-A6-fix1.diff`, 2436 lines); CI (Build
35413031946, FreeBSD 35413048140) taken as green per the controller.

### Finding Verdicts

1. **Important 1 (file-wide platform guards): ADDRESSED.**
   - All 22 guarded files in `src/core/net/{posix,windows,linux,bsd}/` lost their file-wide guard:
     11 in `posix/` (`AcceptLoop.{cpp,hpp}`, `FdPassing_test.cpp`, `PosixListener.{cpp,hpp}`,
     `PosixSocket.{cpp,hpp}`, `SocketsPosix.cpp`, `UnixListener.{cpp,hpp}`, `UnixSocket_test.cpp` —
     `review-A6-fix1.diff:1024-1731`), 7 in `windows/` (`SocketsWin32.cpp:1893-1957`,
     `WindowsListener.{cpp,hpp}:1958-2087`, `WindowsLoopback.{cpp,hpp}:2088-2170`,
     `WindowsSocket.{cpp,hpp}:2171-2261`, four of them also losing a guard around their leading
     `<winsock2.h>` block), 2 in `linux/` (`EpollEventSource.{cpp,hpp}:926-1023`), 2 in `bsd/`
     (`KqueueEventSource.{cpp,hpp}:818-925`).
   - I diffed each hunk by eye against its pre-image: every hunk is a pure de-indent, with include
     groups (blank-line-separated) preserved in the same order — clang-format's own regrouping is
     absent because the includes were already correctly grouped, just indented. No line content
     changed beyond the leading whitespace, and no include was added, dropped or reordered, except
     that `testing/windows/InMemoryTransport.cpp` now includes `<array>` itself where contour's
     branch got it transitively from `WindowsLoopback.hpp` (documented, see Item 3 below).
   - Within-family `#if`s survive exactly where the fix round's report says: `AcceptLoop.cpp`'s
     `#ifdef __linux__`/`#ifndef __linux__` (`:1068-1087`), `FdPassing_test.cpp`'s
     `#ifndef __APPLE__` (`:1219-1223`), `PosixSocket.cpp`'s `#ifndef MSG_CMSG_CLOEXEC` and
     `#ifdef SO_NOSIGPIPE` (`:1386-1425`), `WindowsListener.cpp`'s
     `#ifndef IO_REPARSE_TAG_AF_UNIX` (`:2000-2008`). `FdUtils.hpp` never had a file-wide guard and
     is untouched by this diff (absent from the file list), consistent with its provenance note.
   - Every one of the 22 files' provenance rows was updated to say its guard was dropped and name
     the source list that now selects it (`.agent/reference/provenance.md`, diff lines 120-178),
     and each kept `#if` is named in its own row. `.agent/rules/platform.md:304-309` and
     `src/core/net/CMakeLists.txt:757-763`(current file) state the rule and its two remaining
     exceptions (`DefaultEventSource.cpp`, `EventSourceParity_test.cpp`'s POSIX-only cases).

2. **Important 2 (target rows carry authoritative DEPS): ADDRESSED**, and it is the strongest part
   of this fix round.
   - `core_cpp_module_target()` (`cmake/CoreCppModules.cmake:441-481`) now takes `DEPS`, validated
     against the parent row before being stored: each entry must be the module's own name, a
     target of the same module declared in an earlier row (`CORE_CPP_TARGET_${dep}_MODULE`, which
     is only set once that row has executed — this makes self- and forward-references fail
     naturally, without extra bookkeeping), or a module the parent row lists in its own `DEPS`.
     Anything else is `FATAL_ERROR`'d by name at declaration time.
   - `core_cpp_check_layering()` (`cmake/CoreCppTargets.cmake:150-181`) branches on `row_OWN`
     (whether the target has a `core_cpp_module_target()` row of its own). For an owning row, the
     whole check is the row's `DEPS`: a same-module link resolves to the linked target's own name,
     a cross-module link to the owning module's name, and either must appear in `row_DEPS` or it
     is refused by name, with the empty case worded as "it has none, so the target links no
     core-cpp target" (confirmed in the file directly, not just the diff). A target without its
     own row keeps the pre-existing module-level check (`elseif` branch, unchanged in substance
     from before the fix).
   - Table: `net_types` (`cmake/CoreCppModules.cmake:505`) has no `DEPS` and its `CMakeLists.txt`
     confirms it links nothing (`src/core/net/CMakeLists.txt:12-15`, no `PUBLIC_LIBS`/
     `PRIVATE_LIBS`). `net_tls` (`:507`) has `DEPS net`, and its `CMakeLists.txt:99-105` links
     exactly `PUBLIC_LIBS core::net` (plus `PRIVATE_LIBS OpenSSL::*`, outside the `core::` check) —
     the DEPS row and the real link list agree exactly, so the bound is not just declared but
     tight.
   - **The 12 scenarios are meaningful.** I traced four by hand against the mechanism:
     `net_types-links-net` (a same-module link to a *different* target of the same module, refused
     because `net_types`'s row lists nothing — this is exactly the hole Important 2 reported),
     `row-links-undeclared-sibling` (a rowed target may not silently reach a same-module sibling
     its own DEPS didn't name, even though the *old* per-module rule would have allowed it — this
     is the concrete tightening), `row-deps-itself` and `row-deps-an-undeclared-sibling` (both rely
     on CMake's linear execution leaving `CORE_CPP_TARGET_<dep>_MODULE` undefined for a
     self/forward reference, so the refusal isn't a special case, it falls out of the validation
     order). All 12 would fail if `DEPS` were reverted to undeclared/unchecked: the implementer's
     reported RED run (`task-A6-report.md:384-399`) shows 9 of 12 failing for exactly that reason,
     and the remaining `net_types-links-nothing`/`module-links-its-targets`/
     `module-links-undeclared-module` scenarios pin pre-existing behaviour that must keep working
     (they'd fail too if that behaviour regressed).
   - **Can it be fooled?** A `core::`-prefixed lib hidden behind a generator expression (e.g.
     `$<$<CONFIG:Debug>:core::x>`) would not match `^core::(.+)$` and would slip through
     unchecked — but this is the pre-existing regex, unchanged by this fix, not a new hole.
     Likewise, a `target_link_libraries()` call made directly outside `core_cpp_add_module()` (and
     so outside `core_cpp_check_layering()`) bypasses the check entirely, and a target created via
     `core_cpp_add_module()` under a name that was never given a `core_cpp_module_target()` row
     (so `row_KIND` is empty and the KIND check at `CoreCppTargets.cmake:199` never fires) falls
     back to the loose module-level rule — I verified this by reading the unmodified surrounding
     function (`CoreCppTargets.cmake:186-209`, `:198-203`); it is exactly the same fallback that
     existed before this fix, not a new one it introduces. Neither is in scope for R42; noted under
     Out-of-Scope below.
   - **The "target without a row" judgment call is sound and correctly scoped.** `core::net`,
     `testing_main` and `testing_dialogs` have no `core_cpp_module_target()` row and keep the old
     module-level rule (same-module links always allowed, module's `DEPS` otherwise) — which is
     what lets `net` keep linking `core::net_types` (`src/core/net/CMakeLists.txt:96`) without any
     table change. R42's own dispatch text only asks that `net_types` get no DEPS and `net_tls` get
     `DEPS net`, and says nothing changes `net`'s row; the fix-round report's concern 1
     (`task-A6-report.md:471-479`) states this reading and its consequence explicitly, so a later
     reader (A7) is not surprised by it. This is the correct minimal fix: it closes exactly the
     hole Important 2 named (an *existing* row that carries no bound) without taking on the larger,
     unrequested redesign of making every target — rowed or not — bounded.
   - Proof is test-first: RED before c7712a3 (9/12 failing because `DEPS` was an unexpected
     argument), GREEN after (12/12), reproduced in CI's `style` job and on FreeBSD.

3. **Minor 1 (`InMemoryTransport.cpp` split): ADDRESSED.**
   - `testing/InMemoryTransport.cpp` (72 lines, both `#ifdef` branches) is deleted
     (`review-A6-fix1.diff:1732-1809`) and replaced by `testing/posix/InMemoryTransport.cpp` (38
     lines, `:1810-1853`) and `testing/windows/InMemoryTransport.cpp` (33 lines, `:1854-1892`),
     each holding exactly one of the two original bodies verbatim (the Windows half additionally
     includes `<array>` directly, noted in its provenance row). The declaration stays untouched in
     the public `testing/InMemoryTransport.hpp` (not present in the diff at all — confirms it truly
     wasn't touched). No logic is duplicated between the two files (they never shared any), and the
     old combined file is gone, not left dead.
   - `docs/modules/net.md:723-730` and `src/core/net/CMakeLists.txt:758-763` (current file) both
     now name the two remaining exceptions (`DefaultEventSource.cpp`, `EventSourceParity_test.cpp`'s
     two POSIX-only cases), closing the doc gap the review reported.

4. **Minor 2 (stale/incomplete provenance notes): ADDRESSED.**
   - `posix/AcceptLoop.cpp` and `windows/WindowsListener.cpp` rows now say "`PeerAddress.hpp` from
     `detail/`" (`review-A6-fix1.diff:136,173`).
   - `Tls.cpp`'s row adds "no `NOLINT`", naming the three `NOLINTNEXTLINE` comments it dropped and
     why (`:113`), matching the fix instruction and going a bit further (useful for the eventual
     delta check).

5. **Minor 3 (consumer-migration TLS row): ADDRESSED.**
   - `.agent/guides/consumer-migration.md` gets exactly the requested API-delta row
     (`review-A6-fix1.diff:85-86`) plus an updated contour-row note pointing at the daemon
     specifically (`:63-64`).

6. **Minor 4 (report sentence correction): ADDRESSED.**
   - `task-A6-report.md:51-52` now correctly states that `UnixSocket_test.cpp`'s local `TempDir` was
     always there upstream and is unchanged, matching what the review found.

### New Breakage in the Fix Diff

None. Every hunk I inspected either removes a guard with no other content change, adds the two new
test/target files with logic lifted verbatim from the deleted file, or is a documentation/provenance
edit consistent with the code change next to it. I did not find any behavioural change beyond the
guard removal and the DEPS mechanism, and the local test counts the implementer reports (net 120/626,
net_tls 6/36 etc.) are unchanged from before the fix round, consistent with a non-functional change to
production code.

### Out-of-Scope Observations

- The layering check still cannot see a `core::` lib hidden behind a CMake generator expression, and
  still trusts that every target is declared through `core_cpp_add_module()` rather than a raw
  `target_link_libraries()`; an un-rowed secondary target of a module also still falls back to the
  loose module-level rule. All three are pre-existing properties of the mechanism, not changed by
  this fix round, and none was asked for by R42 — but the second and third are worth a line in
  `library-hygiene.md`'s Layering section before A7 leans on this further (e.g. a note that `DEPS`
  only bounds a target that is *given* a row).
- `docs/modules/net.md`'s new sentence names `EventSourceParity_test.cpp`'s POSIX-only cases as "a
  closed descriptor's registration, descriptor exhaustion" — matches the review's Minor 1 wording,
  so it's consistent, but I did not independently re-verify against the test file's current case
  names (out of the focus items; low risk since this is prose, not code).

### Verdict

**Fix round:** All findings addressed, no new Critical/Important breakage.

- Important 1 (file-wide platform guards): ADDRESSED.
- Important 2 (target rows carry authoritative DEPS): ADDRESSED.
- Minor 1 (`InMemoryTransport.cpp` split): ADDRESSED.
- Minor 2 (provenance notes): ADDRESSED.
- Minor 3 (consumer-migration TLS row): ADDRESSED.
- Minor 4 (report sentence): ADDRESSED.
