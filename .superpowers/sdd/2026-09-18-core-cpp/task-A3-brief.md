# Brief for Task A3

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).

### Task A3: `core::base`, `core::log`, `core::cli`

**Files:**
- Create: `src/core/{Assert,Defines,Environment,Escape,FNV,Flags,Times,UserInfo,Utils,Overloaded,Deferred,Base64,Profiling,Ranges}.hpp`, `src/core/{Environment,UserInfo,Utils}.cpp` (as they exist upstream), `src/core/CMakeLists.txt`, `src/core/testing/Environment.hpp` (crispy `testing/Environment.hpp`)
- Create: `src/core/log/{LogStore,LogSink}.{hpp,cpp}`, `src/core/log/CMakeLists.txt`
- Create: `src/core/cli/{CLI,App}.{hpp,cpp}`, `src/core/cli/CMakeLists.txt`
- Tests: bring each file's upstream `*_test.cpp` from `D:\contour\src\crispy` at `6777ff05`, plus fastcached `Core/Profiling_test.cpp`

**Namespace map:**

| From | To |
|---|---|
| `crispy::` | `core::` |
| `crispy::cli::` | `core::cli::` |
| `crispy::base64::` | `core::base64::` |
| `logstore::` | `core::log::` |
| `FC_ZONE_*` / `FC_TRACY_ENABLED` | `CORE_ZONE_*` / `CORE_CPP_WITH_TRACY` |

Remove `gsl::not_null` (use a reference or an asserted pointer).

- [ ] **Step 1: Import the tests first.**
  ```bash
  git -C D:\contour -c core.autocrlf=false -c core.eol=lf show 6777ff05:src/crispy/<File>_test.cpp > src/core/<File>_test.cpp
  ```
  Rewrite the namespaces and includes (`<crispy/X.hpp>` → `<core/X.hpp>`). Register them with `core_cpp_add_test(base …)`, `(log …)` and `(cli …)`. Build: expected FAIL (headers missing).
- [ ] **Step 2: Import the headers and sources** the same way (blobs, refuse CR). Apply the namespace map. Add the module-table rows `base`, `log DEPS base`, `cli DEPS base log`.
- [ ] **Step 3: Build and test** on Windows `clangcl-debug` + `cl-debug` and WSL `clang-debug` + `gcc-debug`. Fix `-Wconversion`/`-Wsign-conversion` findings in code; never suppress. If a finding cannot be fixed without an API change, file a core-cpp issue and add a temporary `-Wno-<x>` row to the toolchain table that cites the issue.
- [ ] **Step 4: Emscripten.** In the CI `emscripten` job (a matrix of emsdk 3.1.56 and latest):
  - Configure with `emcmake cmake --preset emscripten -DCMAKE_CROSSCOMPILING_EMULATOR=node`, where the `emscripten` preset is Release, has no `-pthread`, and sets `CORE_CPP_WITH_TUI=OFF`.
  - Build base, log, cli and testing, and run their tests under node.
  - Expected: `core-cpp.base`, `core-cpp.log` and `core-cpp.cli` pass, and `Threads::Threads` does not appear in the link line (`ninja -t commands | grep -c pthread` equals 0).
- [ ] **Step 4b: `LOG` filter in the test main.** Test first: in `src/core/testing/CatchMain_test.cpp`, with `LOG=net` set, the `core::log` category `net` is enabled and `tui` is not. Then add to `CatchMain.cpp`, before `session.run()`, a call to `core::log::configure(std::getenv("LOG"))` that handles `nullptr`. This is endo's `test_main` convention. `core::testing_main` now links `core::log`.
- [ ] **Step 5: Commit.** Commit `base: import crispy's generic utilities as core, core::log and core::cli`. Update the CHANGELOG import row.

