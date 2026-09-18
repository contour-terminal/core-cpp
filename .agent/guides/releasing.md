# Releasing core-cpp

How a core-cpp release is cut, and what a release promises its consumers. The policy is the
design spec's,
[Part I §4](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md);
the rules it rests on are in [`../rules/library-hygiene.md`](../rules/library-hygiene.md).

## Versioning

- **SemVer, with tags spelled `vX.Y.Z`.** Nothing else is a release tag.
- **While the major version is 0, a minor release may break the API.** Every break is listed
  under **Breaking** in `CHANGELOG.md`, with a migration note that says what a caller changes.
  A patch release never breaks.
- **The version literal in `project(core-cpp VERSION X.Y.Z ...)` is the source of truth.** The
  tag must equal it, and the release workflow refuses a tag that does not.
- **Consumers pin a tag**, or temporarily a full commit SHA; never a branch.

## What a release carries

`.github/workflows/release.yml` runs on a pushed `v*` tag and:

1. runs `cmake -DTAG=<tag> -DROOT=<checkout> -P tests/cmake/check-release.cmake`, which refuses
   unless the tag equals `project(VERSION)` and `CHANGELOG.md` has a `## [X.Y.Z]` section;
2. exports the vendoring file set with `cmake/CoreCppVendor.cmake` (Task A8) and packs it as
   `core-cpp-vX.Y.Z-vendor.tar.gz`, the archive contour's packagers can vendor from without git;
3. writes `SHA256SUMS` for it;
4. opens a **draft** GitHub release with generated notes, grouped by the pull requests' `type/`
   labels (`.github/release.yml`, which keeps an "Other changes" catch-all so no unlabelled pull
   request disappears from the notes).

A human publishes the draft. Nothing is published by CI.

## Cutting a release

1. **Move the `[Unreleased]` entries under a new `## [X.Y.Z] - YYYY-MM-DD` heading** in
   `CHANGELOG.md`, and record every import's commit where the release carries a new one.
2. **Set `project(core-cpp VERSION X.Y.Z)`** in `CMakeLists.txt` to the same number.
3. **Check it locally before tagging:**
   ```sh
   cmake -DTAG=vX.Y.Z -DROOT=. -P tests/cmake/check-release.cmake
   ```
4. **Commit, tag and push**, or let the `/draft-release` skill do steps 1 to 4: it bumps the
   version where the project keeps it, stamps the changelog, commits and pushes the tag, and
   stops before anything is published.
5. **Verify and publish** with `/publish-release`, which refuses until CI is green on the tag and
   every expected asset (the vendor archive and `SHA256SUMS`) is attached, then publishes, marks
   the release latest and opens the next `[Unreleased]` section.
6. **Tell the consumers.** Each consumer's pull request moves its pin; contour re-vendors the
   tag and its manifest check verifies the copy
   ([`consumer-migration.md`](consumer-migration.md)).

## What makes a release safe to cut

- `ci-ok` is green on the commit being tagged: every OS leg, the sanitizers, clang-tidy, the
  Emscripten subset on both emsdk versions, and the compiler-cache job.
- The documentation site builds with `mkdocs build --strict`.
- Every public header change since the last release has a CHANGELOG entry, and every break a
  migration note.
