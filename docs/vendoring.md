# Vendoring

A project that must build without fetching anything, as a distribution packager requires, can
carry a verbatim copy of core-cpp in its own tree. contour consumes core-cpp this way, so that
packaging contour adds no new dependency. This page is the contract between core-cpp and such a
consumer; it is the design spec's
[Part I §5](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md).

!!! note "Status"
    The vendoring tool, `cmake/CoreCppVendor.cmake`, arrives with Task A8 of the
    implementation plan. This page states the contract it implements.

## The copy is verbatim

- **Byte-identical to a tag, or to a full commit SHA.** Nothing else is a valid source.
- **No local changes, ever.** A fix goes to core-cpp, is released as a patch version, and the
  consumer re-vendors that tag. The check below refuses a copy that differs from its manifest,
  so a local edit fails the consumer's own test suite.

## The commands

```sh
# Copy a tag's files into <dir> and write <dir>/MANIFEST.
cmake -DMODE=sync -DREF=<tag> -DDEST=<dir> [-DREPO=<url|path>] \
      [-DMODULES=base;log;cli;platform;coro;net;testing] \
      -P <copy>/cmake/CoreCppVendor.cmake

# Verify <dir> against its MANIFEST. Needs no git.
cmake -DMODE=check -DDEST=<dir> -P <dir>/cmake/CoreCppVendor.cmake
```

- **`sync` reads git blobs** with `git -c core.autocrlf=false -c core.eol=lf`, so no working
  tree's line-ending settings reach the copy, and it refuses a file containing a CR byte, a
  symbolic link and a submodule.
- **`MANIFEST`** starts with header lines naming the repository, the ref, the commit and the file
  count (`# repository ...`, `# ref ...`, `# commit ...`, `# files ...`), followed by one
  `<sha256>  <path>` line per file, sorted by path.
- **`check` refuses** a hash mismatch, a file the manifest lists that is missing, and a file the
  manifest does not list.

## What is copied

- `CMakeLists.txt` and `cmake/**`;
- `src/core/*.hpp` and `src/core/*.cpp` (the `base` module);
- `src/core/<module>/**` for each module in `MODULES`, tests included;
- `LICENSE`, `NOTICE`, `README.md`, `CHANGELOG.md`, `.clang-format` and `.clang-tidy`.

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

## Release archives

Each release also carries `core-cpp-vX.Y.Z-vendor.tar.gz`, the exported file set of that tag,
and a `SHA256SUMS` file, for a packager who vendors without git.
