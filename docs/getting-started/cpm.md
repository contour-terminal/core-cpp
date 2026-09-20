# Using core-cpp with CPM

[CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) fetches core-cpp at configure time and adds it
as a subproject. This is how endo, fastcached, tuidu, Lightweight's `dbtool` and morph consume it.

## Adding it

```cmake
CPMAddPackage(
    NAME core-cpp
    GITHUB_REPOSITORY contour-terminal/core-cpp
    GIT_TAG v0.1.0
    SYSTEM YES              # core-cpp headers never trip your -Werror
    EXCLUDE_FROM_ALL YES    # build only what you link
    OPTIONS "CORE_CPP_WITH_TUI ON" "CORE_CPP_WITH_TLS OFF")
target_link_libraries(myapp PRIVATE core::async core::net core::tui)
# local development against a checkout: -DCPM_core-cpp_SOURCE=/path/to/core-cpp
```

- **Pin a tag**, or temporarily a full commit SHA; never a branch, which changes under your CI
  without a commit in your repository.
- **`SYSTEM YES`** makes core-cpp's include directories system includes in your targets, so its
  headers are not held to your warning flags.
- **`EXCLUDE_FROM_ALL YES`** builds only the targets you link.
- **Link the aliases** (`core::<module>`), never the real target names.
- **Develop against a checkout** with `-DCPM_core-cpp_SOURCE=/path/to/core-cpp`: CPM then uses
  that directory instead of fetching.

## What core-cpp does to your build

Nothing outside its own targets:

- **No compiler launcher, no `CMAKE_CXX_STANDARD`, no environment.** Those are set only when
  core-cpp is the top-level project. Your targets keep whatever you chose, and core-cpp's targets
  use your launcher.
- **No directory-wide flag and no PUBLIC compile or link flag.** Every warning, sanitizer and
  definition core-cpp compiles with is PRIVATE to its own targets. The C++23 requirement is the
  one usage requirement it passes on.
- **No tests, no examples and no fetch of Catch2**: `CORE_CPP_TESTING` and
  `CORE_CPP_BUILD_EXAMPLES` default to off when core-cpp is not top-level.
- **Every option is `CORE_CPP_`-prefixed**, so none collides with yours. Set one before
  `CPMAddPackage`, or in its `OPTIONS`; see [the options](options.md).

## Dependencies

Each dependency core-cpp needs is resolved in this order:

1. a target your project already defines (for example your own `Catch2::Catch2`);
2. `find_package(<name> QUIET)`;
3. a CPM fetch, only when `CORE_CPP_FETCH_DEPS` is on (the default);
4. otherwise the configure stops, naming the option that needed the dependency.

OpenSSL (for `CORE_CPP_WITH_TLS`) and Threads are never fetched. To build without network
access, provide the dependencies and set `CORE_CPP_FETCH_DEPS OFF`.

## Using `core::testing_main` in your own tests

`core::testing_main` is a Catch2 `main()` whose exit status says what happened: 0 when everything
passed, 1 when anything failed, 77 when every test case skipped, 2 when nothing ran. ctest scores
77 as skipped when the test is registered with `SKIP_RETURN_CODE 77`:

```cmake
set(CORE_CPP_CATCH2_MAIN ON)     # builds core::testing_main; needs Catch2
CPMAddPackage(NAME core-cpp ...)

add_executable(mytests Foo_test.cpp)
target_link_libraries(mytests PRIVATE core::testing_main)
add_test(NAME mytests COMMAND mytests)
get_target_property(skipCode core::testing_main CORE_CPP_SKIP_EXIT_CODE)
set_tests_properties(mytests PROPERTIES SKIP_RETURN_CODE ${skipCode})
```

Linking `core::testing_main` also installs the Windows dialog suppression in the executable, so a
failed `assert()` in a Debug build ends the test instead of waiting for a click. See the
[`testing` module](../modules/testing.md).

## Sanitizers and coverage in a parent build

`CORE_CPP_SANITIZERS` is for core-cpp's own top-level build and stops the configure when core-cpp
is a subproject: instrumenting only core-cpp's targets inside your build would mix instrumented
and uninstrumented code, which is what makes ThreadSanitizer report races that are not there.
Instrument core-cpp's targets together with yours instead. The global property `CORE_CPP_TARGETS`
lists every compiled library core-cpp built, by its real target name (a property cannot be set on
an alias), in the order the module table declares them:

```cmake
get_property(coreCppTargets GLOBAL PROPERTY CORE_CPP_TARGETS)
foreach(target IN LISTS coreCppTargets)
    target_compile_options(${target} PRIVATE -fsanitize=thread)
    target_link_options(${target} PRIVATE -fsanitize=thread)
endforeach()
```

Header-only targets (`core::async`, `core::net_types`) are not in the list: they compile nothing
of their own, so your flags reach their code through your targets. Neither are core-cpp's test
binaries, which a consumer does not build.
