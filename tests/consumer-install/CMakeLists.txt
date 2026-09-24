# SPDX-License-Identifier: Apache-2.0
#
# A consumer of an INSTALLED core-cpp: the way a distribution that unbundles it, or a vcpkg port,
# hands it to a project (core-cpp#5). Nothing here knows where core-cpp's sources are; it finds the
# package through CMAKE_PREFIX_PATH, as any consumer of an installed library does.
#
#   cmake -S tests/consumer-install -B <dir> -DCMAKE_PREFIX_PATH=<prefix> -DEXPECTED_VERSION=<x.y.z>
#   cmake --build <dir> && ctest --test-dir <dir> --output-on-failure
#
# tests/cmake/check-install.cmake installs the build under test into a fresh prefix and drives this.
cmake_minimum_required(VERSION 3.25...3.31)
project(core-cpp-consumer-install VERSION 1.0.0 LANGUAGES CXX)

if(NOT EXPECTED_VERSION MATCHES "^([0-9]+)\.([0-9]+)\.[0-9]+$")
    message(FATAL_ERROR "consumer-install: -DEXPECTED_VERSION=<major.minor.patch> is required, not '${EXPECTED_VERSION}'")
endif()
set(expectedMajor "${CMAKE_MATCH_1}")
set(expectedMinor "${CMAKE_MATCH_2}")

find_package(core-cpp "${expectedMajor}.${expectedMinor}" CONFIG REQUIRED)
if(NOT core-cpp_VERSION STREQUAL EXPECTED_VERSION)
    message(FATAL_ERROR "consumer-install: found core-cpp ${core-cpp_VERSION}, expected ${EXPECTED_VERSION}")
endif()

# While core-cpp is 0.x a minor release may break the API, so the package accepts a request for its
# own major.minor and refuses the next one: SameMinorVersion, asked from outside. After the real
# lookup, not before: a refusal resets core-cpp_DIR, and under a cross toolchain that variable is the
# only way to the prefix (tests/cmake/check-install.cmake). A refused version loads no config, so
# the targets found above stay as they are.
math(EXPR nextMinor "${expectedMinor} + 1")
find_package(core-cpp "${expectedMajor}.${nextMinor}" CONFIG QUIET)
if(core-cpp_FOUND)
    message(FATAL_ERROR
        "consumer-install: core-cpp ${core-cpp_VERSION} accepted a request for ${expectedMajor}.${nextMinor}; "
        "a 0.x package is compatible within its own minor version only")
endif()

# The installed names are the aliases a source build has, so a consumer's link line does not
# change with how it gets core-cpp.
foreach(target IN ITEMS core::base core::log core::platform core::async core::net_types core::net)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "consumer-install: the installed core-cpp package defines no ${target}")
    endif()
endforeach()

enable_testing()
add_executable(consumer-install main.cpp)
target_compile_features(consumer-install PRIVATE cxx_std_23)
# No C++ modules here, and a C++20-or-later target is scanned for them by default: em++ has no
# clang-scan-deps beside it, so the scan fails before a line compiles (tests/consumer-wasm says so too).
set_target_properties(consumer-install PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_link_libraries(consumer-install PRIVATE core::net)
target_compile_definitions(consumer-install PRIVATE "CONSUMER_EXPECTED_VERSION=\"${EXPECTED_VERSION}\"")
if(EMSCRIPTEN)
    # core-cpp throws, and emcc disables exceptions by default (tests/consumer-wasm says why).
    target_compile_options(consumer-install PRIVATE -fexceptions)
    target_link_options(consumer-install PRIVATE -fexceptions)
endif()
# An instrumented core-cpp -- a sanitizer or coverage build -- needs its runtime on this link too.
# Passed '|'-separated, because a ';' would split the argument on its way here.
if(INSTRUMENT_COMPILE_FLAGS)
    string(REPLACE "|" ";" instrumentFlags "${INSTRUMENT_COMPILE_FLAGS}")
    target_compile_options(consumer-install PRIVATE ${instrumentFlags})
endif()
if(INSTRUMENT_LINK_FLAGS)
    string(REPLACE "|" ";" instrumentFlags "${INSTRUMENT_LINK_FLAGS}")
    target_link_options(consumer-install PRIVATE ${instrumentFlags})
endif()
add_test(NAME consumer-install COMMAND consumer-install)
set_tests_properties(consumer-install PROPERTIES TIMEOUT 60)

# core::testing_main is installed only by a build that FOUND Catch2 (cmake/CoreCppInstall.cmake), and
# the check says which kind of build it installed. Its main() links an object library's object
# through the package, and its exit-code contract reaches a consumer's ctest as a target property.
if(EXPECT_TESTING_MAIN)
    if(NOT TARGET core::testing_main)
        message(FATAL_ERROR "consumer-install: the build installed core::testing_main, and the package defines no such target")
    endif()
    get_target_property(skipExitCode core::testing_main CORE_CPP_SKIP_EXIT_CODE)
    if(NOT skipExitCode)
        message(FATAL_ERROR "consumer-install: core::testing_main carries no CORE_CPP_SKIP_EXIT_CODE")
    endif()
    add_executable(consumer-install-testing testing.cpp)
    target_compile_features(consumer-install-testing PRIVATE cxx_std_23)
    set_target_properties(consumer-install-testing PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    target_link_libraries(consumer-install-testing PRIVATE core::testing_main)
    if(INSTRUMENT_COMPILE_FLAGS)
        string(REPLACE "|" ";" instrumentFlags "${INSTRUMENT_COMPILE_FLAGS}")
        target_compile_options(consumer-install-testing PRIVATE ${instrumentFlags})
    endif()
    if(INSTRUMENT_LINK_FLAGS)
        string(REPLACE "|" ";" instrumentFlags "${INSTRUMENT_LINK_FLAGS}")
        target_link_options(consumer-install-testing PRIVATE ${instrumentFlags})
    endif()
    add_test(NAME consumer-install-testing COMMAND consumer-install-testing)
    set_tests_properties(consumer-install-testing PROPERTIES SKIP_RETURN_CODE ${skipExitCode} TIMEOUT 60)
endif()
