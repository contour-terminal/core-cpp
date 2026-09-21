# Brief for Task A5b

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).

### Task A5b: Rename `core::coro` to `core::async`; `Generator` moves to base

User decision (2026-09-18). After B1 the module holds executors, a thread pool, `AsyncQueue`, detached tasks and cancellation, and coroutines are only the mechanism. `core::async` also matches fastcached's `FastCache::Async`. Namespaces and directories stay lowercase, and CMake targets stay `core::<module>`. `Generator` leaves the module, because `core::async::Generator` would read as an *asynchronous* generator (a `co_await`-able stream). Ours is the synchronous `std::generator` stand-in, and it needs only std, so it belongs in base.

**Files:**
- `git mv src/core/coro src/core/async`, then `git mv src/core/async/Generator{,_test}.* src/core/`.
- Modify:
  - `cmake/CoreCppModules.cmake`: the row `coro` → `async`; `platform DEPS base log` (Generator was its only use of the coroutine module).
  - `src/core/CMakeLists.txt` (base gains `Generator.hpp` + `Generator_test.cpp`).
  - `src/core/async/CMakeLists.txt`.
  - `src/core/platform/FileSystem.hpp` (`<core/Generator.hpp>`, `core::Generator`).
  - The `.clang-tidy` naming block gains `readability-identifier-naming.NamespaceCase: lower_case`.
  - Anything that tests or checks the module name: hygiene rules, the emscripten test list, the provenance hygiene rule.
- Docs:
  - `git mv docs/modules/coro.md docs/modules/async.md`; update `mkdocs.yml` nav.
  - `docs/modules/{index,base,platform}.md`, README, AGENT.md (module table and text), `.agent/rules/*` and `.agent/reference/{source-map,consumers,provenance}.md`.
  - CHANGELOG `[Unreleased]`: no Breaking entry, because no release ever shipped `core::coro`. The existing `[Unreleased]` entries are rewritten to the new names, and the import rows keep naming contour's upstream `src/coro`.

**Renames** (every occurrence in core-cpp except upstream paths in provenance's upstream columns):

| From | To |
|---|---|
| `src/core/coro/` | `src/core/async/` |
| `namespace core::coro` | `namespace core::async` |
| `<core/coro/X.hpp>` | `<core/async/X.hpp>` |
| targets `core-cpp-coro`, `core::coro`, `core-cpp-coro-test`, `core-cpp-coro-fallback-test` | `core-cpp-async`, `core::async`, `core-cpp-async-test`, `core-cpp-async-fallback-test` |
| ctest `core-cpp.coro`, `core-cpp.coro-fallback`; label `coro` | `core-cpp.async`, `core-cpp.async-fallback`; label `async` |
| `CORE_CORO_FORCE_STOP_TOKEN_FALLBACK` | `CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK` |
| `core::coro::Generator`, `src/core/coro/Generator.hpp` | `core::Generator`, `src/core/Generator.hpp` (`CORE_GENERATOR_FORCE_FALLBACK` unchanged) |

- [ ] **Step 1: The check first.** Add `NamespaceCase: lower_case` to `.clang-tidy`. Add a hygiene self-test row that a `namespace core::Async` in a temporary tree is refused by the namespace-directory rule, which now also demands lowercase. Run `ctest -L hygiene`: expected FAIL on the new self-test row until the rule is extended. Extend the rule, then expect PASS.
- [ ] **Step 2: Move Generator to base.** Use `git mv` so history follows. Change the namespace to `core`. `FileSystem.hpp` includes `<core/Generator.hpp>`. The platform row drops the coroutine dependency. Build: expected PASS on `clang-debug`.
- [ ] **Step 3: Rename the module** per the table, with `git mv`. `rg -n "core::coro|core/coro|core-cpp-coro|CORE_CORO_|core-cpp\.coro" --glob '!docs/superpowers/**' --glob '!.superpowers/**'` must print nothing. Upstream references stay: contour's `src/coro` paths in provenance's upstream columns, and contour's `coro::` in migration notes.
- [ ] **Step 4: Verify.**
  - Windows `clangcl-debug` (`--clean-first`) and `cl-debug`; WSL `clang-debug`, `gcc-debug`, `clang-tsan` and `clang-tidy` (zero findings with the new NamespaceCase rule).
  - `python scripts/clang-format.py --check` and `mkdocs build --strict`.
  - Push, and `ci-ok` must be green, including both emscripten legs.
- [ ] **Step 5: Commit.**
  - `base: Generator is core::Generator, and platform no longer needs the coroutine module`
  - `async: the coroutine module is core::async`
  - `tidy: namespaces are lowercase`

