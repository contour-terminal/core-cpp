# Brief for Task A2

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).

### Task A2: Docs site, guidelines, CI, then create the GitHub repository

**Files:**
- Create: `README.md` (replaces the 3-line stub; outline in the build design §11: badges, modules table, CPM snippet, vendoring pointer, presets, requirements, "Used by"), `CHANGELOG.md`, `CONTRIBUTING.md`, `SECURITY.md`, `AGENT.md`, `CLAUDE.md` (`@AGENT.md`)
- Create `.agent/`:
  - `.agent/rules/`: `README.md`, `cpp-guidelines.md`, `design-principles.md`, `library-hygiene.md`, `build-and-toolchain.md`, `testing.md`, `async-and-net.md`, `platform.md`, `tui.md`
  - `.agent/guides/`: `team-run.md`, `profiling-tracy.md`, `consumer-migration.md`, `releasing.md`
  - `.agent/reference/`: `source-map.md`, `consumers.md`
- Create: `mkdocs.yml`, `docs/requirements.txt`, `docs/Doxyfile`, `docs/index.md`, `docs/getting-started/{cpm,building,options}.md`, `docs/vendoring.md`, `docs/modules/{index,base,log,cli,platform,coro,net,tui,testing}.md`, `docs/design/*.md`, `docs/contributing/{index,cpp-guidelines,releasing}.md`, `docs/changelog.md` (snippet include)
- Create in `.github/`: `workflows/{build,docs,release,downstream,portability}.yml`, `release.yml`, `dependabot.yml`, `pull_request_template.md`, `clang-tidy-matcher.json` (from `D:\endo\.github`)

**Content sources:**
- `D:\fastcached\AGENT.md` (the §C++ Coding Guidelines and §Rulebook shape)
- `D:\fastcached\.agent\rules\{README,build-and-toolchain,testing,wire-and-protocol}.md`: generic sections only, per Part I §4
- `D:\fastcached\.agent\guides\{team-run,profiling-tracy}.md`
- `D:\endo\AGENT.md` (DI, data-driven design, `std::expected`, docs tone)
- `D:\contour\AGENT.md` (enum class over bool, configuration at construction, zero-warning policy)
- `D:\Lightweight\.agent\cpp-guidelines.md`
- `D:\tuidu\AGENT.md` (coroutine parameters by value)
- `D:\fastcached\mkdocs.yml`, `D:\fastcached\docs\requirements.txt`, `D:\fastcached\.github\workflows\docs.yml` (plus a Doxygen step)
- Every carried rule is rewritten against core-cpp paths and cites its origin as a full URL.

- [ ] **Step 1: Docs build locally.** Run `python -m venv .cache/docs-venv; .cache/docs-venv/Scripts/pip install -r docs/requirements.txt; .cache/docs-venv/Scripts/mkdocs build --strict`. Expected: success, with no omitted-nav or not-found warnings.
- [ ] **Step 2: Workflows.**
  - Write `build.yml` with the Part I §4 jobs. Jobs whose modules do not exist yet still run: they build the skeleton plus the exit-code fixture.
  - `ci-ok` depends on every required job and fails unless all succeeded.
  - `docs.yml`: build on PR; on master, `mkdocs build --strict`, then `doxygen docs/Doxyfile` into `site/api`, then `upload-pages-artifact@v3`, then `deploy-pages@v4`.
- [ ] **Step 3: Commit.** Commit `docs: README, guidelines, agent rulebook, docs site and CI workflows`.
- [ ] **Step 4: Create the public repository and push.** This is an outward-facing action, authorized by the approved plan.
  ```powershell
  gh repo create contour-terminal/core-cpp --public --description "Shared C++23 foundation libraries of the Contour Terminal projects: coroutines, networking, platform, TUI" --source D:\core-cpp --remote origin --push
  gh api -X POST repos/contour-terminal/core-cpp/pages -f build_type=workflow
  gh repo edit contour-terminal/core-cpp --homepage https://contour-terminal.github.io/core-cpp/
  ```
- [ ] **Step 5: Verify.** Run `gh run watch` on the build and docs runs. Expected: `ci-ok` is green. `gh api repos/contour-terminal/core-cpp/pages --jq '.build_type, .html_url'` prints `workflow` and the URL, and the URL serves the site plus `/api/`.

