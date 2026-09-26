# Installing core-cpp

core-cpp installs as the CMake package `core-cpp`. A consumer of the installed package links the
same names a source build's aliases have:

```cmake
find_package(core-cpp 0.4 CONFIG REQUIRED)
target_link_libraries(app PRIVATE core::net)
```

```sh
cmake --preset gcc-release
cmake --build --preset gcc-release
cmake --install out/build/gcc-release --prefix /usr/local --component core-cpp
```

## What is installed

- Every module target this build created, as `core::<name>` (`EXPORT_NAME`; the targets are
  `core-cpp-<name>` inside the build), with its `HEADERS` file set under
  `${CMAKE_INSTALL_INCLUDEDIR}`. The generated `core/Config.hpp` is part of `core::base`'s file set.
- `core-cppConfig.cmake`, `core-cppConfigVersion.cmake` and `core-cppTargets.cmake` under
  `${CMAKE_INSTALL_LIBDIR}/cmake/core-cpp`.
- `core::testing_dialogs`' object file under `${CMAKE_INSTALL_LIBDIR}/core-cpp-objects`, which
  `core::testing_main` puts on its consumers' link line.

Every rule is in the install component `core-cpp`. `--component core-cpp` installs core-cpp and
none of the install rules of a dependency CPM fetched beside it.

**A target that links a dependency this build fetched is not installed**, because no installed
package could re-find it: that dependency is a target of this build, not one that `find_package()`
imported. Configure says so, one line per target:

```text
-- [core-cpp] install: core-cpp-tui is not installed: it links unicode, which this build fetched rather than found, ...
```

So `core::tui` is installed only by a build that found libunicode, `core::testing_main` only by one
that found Catch2, and nothing that links Tracy unless Tracy was found. A distribution packaging
core-cpp provides these through `find_package()` (`CMAKE_PREFIX_PATH`), and `CORE_CPP_FETCH_DEPS=OFF`
makes a missing one a configure error rather than a smaller install.

## What the package re-finds

`core-cppConfig.cmake` calls `find_dependency()` for each row of
[`cmake/CoreCppDependencies.cmake`](https://github.com/contour-terminal/core-cpp/blob/master/cmake/CoreCppDependencies.cmake)
whose targets an installed target links, with that row's `find_package()` arguments: Threads
wherever core-cpp links it, OpenSSL for a build with `CORE_CPP_WITH_TLS`, libunicode for an
installed `core::tui`, Catch2 for an installed `core::testing_main`.

## Versions

The version file is `SameMinorVersion` while core-cpp is 0.x, because a minor release may break
the API: `find_package(core-cpp 0.4)` accepts 0.4.z and refuses 0.5.0.

## As a subproject: CORE_CPP_INSTALL

`CORE_CPP_INSTALL` defaults to on when core-cpp is the top-level project and off otherwise, so a
consumer that vendors core-cpp or adds it through CPM installs nothing of core-cpp's.

A parent that installs an export of its own whose targets link core-cpp's turns it on. CMake
refuses such an export otherwise:

```text
install(EXPORT "morphTargets" ...) includes target "morph" which requires target "core-cpp-base"
that is not in any export set.
```

```cmake
CPMAddPackage(NAME core-cpp GITHUB_REPOSITORY contour-terminal/core-cpp GIT_TAG v0.4.3
              OPTIONS "CORE_CPP_INSTALL ON")
```

With it on, core-cpp's targets are in the export set `core-cppTargets`, the parent's export
refers to them as `core::<name>`, and the parent's package config calls
`find_dependency(core-cpp)`.

`core-cpp.install` (`tests/cmake/check-install.cmake`) holds all of this: it installs the build
under test into an empty prefix, builds and runs `tests/consumer-install` against it, and
configures `tests/consumer-install-nested` with `CORE_CPP_INSTALL` on, which must generate, and
off, which must fail as above.
