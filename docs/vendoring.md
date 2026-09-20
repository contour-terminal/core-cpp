# Vendoring

A project that must build without fetching anything, as a distribution packager requires, can
carry a verbatim copy of core-cpp in its own tree. contour consumes core-cpp this way, so that
packaging contour adds no new dependency. This page is the contract between core-cpp and such a
consumer; it is the design spec's
[Part I §5](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md).

The tool is [`cmake/CoreCppVendor.cmake`](https://github.com/contour-terminal/core-cpp/blob/master/cmake/CoreCppVendor.cmake),
a CMake script run with `cmake -P`. It needs nothing but CMake and, for `sync`, git.

## The copy is verbatim

- **Byte-identical to a tag, or to a full commit SHA.** Nothing else is a valid source: a branch
  changes under your CI without a commit in your repository.
- **No local changes, ever.** A fix goes to core-cpp, is released as a patch version, and the
  consumer re-vendors that tag. The check below refuses a copy that differs from its manifest,
  so a local edit fails the consumer's own test suite.

## The commands

```sh
# Copy a ref's files into <dir> and write <dir>/MANIFEST.
cmake -DMODE=sync -DREF=<tag or full SHA> -DDEST=<dir> [-DREPO=<url or path>] \
      [-DMODULES=base;log;cli;platform;async;net;testing] \
      -P <core-cpp>/cmake/CoreCppVendor.cmake

# Verify <dir> against its MANIFEST. Needs no git.
cmake -DMODE=check -DDEST=<dir> -P <dir>/cmake/CoreCppVendor.cmake
```

- **`REPO`** is a local checkout, a bare repository, or a URL, which is cloned once into a
  temporary directory under `DEST`. It defaults to the repository the script itself is part of.
- **`MODULES`** selects which module directories are copied; it defaults to every module the ref
  has. A module that no option can switch off must be in the list, or the copy will not configure:
  `CORE_CPP_WITH_TUI=OFF` is what lets contour leave `tui` out.
- **`sync` reads git blobs** with `git -c core.autocrlf=false -c core.eol=lf cat-file blob`, so no
  working tree's line-ending settings reach the copy, and it refuses a file containing a CR byte,
  a symbolic link and a submodule. It writes nothing into `DEST` until the whole copy is legal, so
  a refusal leaves the previous copy exactly as it was.
- **`sync` replaces the copy it finds.** A `DEST` holding a manifest is emptied first, so a file
  the new ref no longer has is gone rather than left behind; a `DEST` with files and no manifest
  is refused, because it is not a copy of ours to delete.
- **`MANIFEST`** starts with header lines naming the repository, the ref, the commit, the modules
  and the file count (`# repository ...`, `# ref ...`, `# commit ...`, `# modules ...`,
  `# files ...`), followed by one `<sha256>  <path>` line per file, sorted by path.
- **`check` refuses** a hash mismatch, a file the manifest lists that is missing, and a file the
  manifest does not list. It reports every one of them, not only the first.

## What is copied

- `CMakeLists.txt` and `cmake/**`;
- everything directly in `src/core/` (the `base` module: its headers, its sources, its own
  `CMakeLists.txt` and `Config.hpp.in`, without which the copy does not configure);
- `src/core/<module>/**` for each module in `MODULES`, the modules' own `*_test.cpp` files
  included;
- `LICENSE`, `NOTICE`, `README.md`, `CHANGELOG.md`, `.clang-format` and `.clang-tidy`.

The top-level `tests/` directory is not part of the set, so a vendored copy cannot build
core-cpp's own test suite: leave `CORE_CPP_TESTING` off, which is its default for a subproject.

## What the consumer does

1. **Mark the copy as binary for git and skip it in formatting:** `.gitattributes` gets
   `vendor/core-cpp/** -text`, and `.clang-format-ignore` gets `vendor/**`. Otherwise a
   checkout's line-ending conversion or a formatting run changes the bytes the manifest pins.
2. **Add it without fetching and without building what you do not link:**

   ```cmake
   set(CORE_CPP_FETCH_DEPS OFF)
   add_subdirectory(vendor/core-cpp SYSTEM EXCLUDE_FROM_ALL)
   ```

   With `CORE_CPP_FETCH_DEPS OFF`, a dependency that neither your project nor `find_package`
   provides stops the configure with the option that needed it, instead of reaching the network.
3. **Register the verbatim check as a test**, so a local edit fails your own suite:

   ```cmake
   add_test(NAME core-cpp-vendored-copy
            COMMAND ${CMAKE_COMMAND} -DMODE=check
                    -DDEST=${CMAKE_CURRENT_SOURCE_DIR}/vendor/core-cpp
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/vendor/core-cpp/cmake/CoreCppVendor.cmake)
   ```

To move to a new release, run `sync` with the new tag and commit the result as one change.

`tests/consumer-vendored/` in core-cpp is exactly this, as a project of its own: core-cpp's own CI
exports a copy of each commit, then configures, builds and verifies it inside a container with no
network and no git, which is what a packager's build has.

## Release archives

Each release also carries `core-cpp-vX.Y.Z-vendor.tar.gz`, the exported file set of that tag,
and a `SHA256SUMS` file, for a packager who vendors without git.
