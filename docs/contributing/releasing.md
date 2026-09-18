# Releasing

core-cpp follows [Semantic Versioning](https://semver.org/) with `vX.Y.Z` tags. While the major
version is 0, a minor release may break the API; every break is listed under **Breaking** in the
[changelog](../changelog.md) with a migration note. A patch release never breaks.

## What a release is

- **The tag equals the version in `project(core-cpp VERSION X.Y.Z)`**, and `CHANGELOG.md` has a
  `## [X.Y.Z]` section. The release workflow runs `tests/cmake/check-release.cmake` on the tag and
  refuses otherwise.
- **A release carries** `core-cpp-vX.Y.Z-vendor.tar.gz`, the [vendoring](../vendoring.md) file
  set of the tag, and `SHA256SUMS`.
- **CI opens a draft release** with notes grouped by the pull requests' `type/` labels. A person
  publishes it.

## Cutting one

```sh
# 1. Move the [Unreleased] entries under "## [X.Y.Z] - YYYY-MM-DD" in CHANGELOG.md.
# 2. Set project(core-cpp VERSION X.Y.Z) in CMakeLists.txt.
# 3. Check before tagging:
cmake -DTAG=vX.Y.Z -DROOT=. -P tests/cmake/check-release.cmake
# 4. Commit, tag vX.Y.Z and push the tag; then review and publish the draft release.
```

The maintainers use the `/draft-release` and `/publish-release` skills for steps 1 to 4 and for
publishing; the checklist is in
[`releasing.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/guides/releasing.md).

## Pinning a release

Consumers pin a tag, or temporarily a full commit SHA, never a branch. See
[Using core-cpp with CPM](../getting-started/cpm.md) and [Vendoring](../vendoring.md).
