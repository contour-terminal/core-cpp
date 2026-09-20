# SPDX-License-Identifier: Apache-2.0
#
# A consumer's build with a VERBATIM COPY of core-cpp in its own tree, as contour carries one, and
# as docs/vendoring.md tells a consumer to do it: nothing is fetched, the copy is added as a
# subdirectory, and the verbatim check is registered as a test so a local edit to the copy fails
# this project's own suite.
#
#   cmake -DMODE=sync -DREF=<tag> -DDEST=<somewhere>/vendor/core-cpp \
#         "-DMODULES=base;log;cli;platform;async;net;testing" -P <core-cpp>/cmake/CoreCppVendor.cmake
#   cmake -S tests/consumer-vendored -B <build> -DCORE_CPP_VENDOR_DIR=<somewhere>/vendor/core-cpp
#   cmake --build <build> && ctest --test-dir <build> --output-on-failure
#
# The CI job runs all of that inside `docker run --network none`, on an image with no git either:
# the copy must configure, build and verify with no network and no version control, which is the
# whole reason a packager vendors.
#
# It deliberately shares no code with tests/consumer-cpm: each is what one kind of consumer writes,
# and a helper shared between them would be neither.
cmake_minimum_required(VERSION 3.25...3.31)
project(core-cpp-consumer-vendored VERSION 1.0.0 LANGUAGES CXX)

if(NOT DEFINED CORE_CPP_VENDOR_DIR)
    set(CORE_CPP_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/core-cpp")
endif()
get_filename_component(CORE_CPP_VENDOR_DIR "${CORE_CPP_VENDOR_DIR}" ABSOLUTE)
if(NOT EXISTS "${CORE_CPP_VENDOR_DIR}/MANIFEST")
    message(FATAL_ERROR
        "consumer-vendored: '${CORE_CPP_VENDOR_DIR}' holds no MANIFEST, so it is not a vendored "
        "copy of core-cpp. Create one with MODE=sync (see the header of this file).")
endif()

enable_testing()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options(/W4 /WX /utf-8)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

# The consumer's obligations, from docs/vendoring.md: fetch nothing, build only what is linked, and
# keep core-cpp's headers out of this project's warning set.
set(CORE_CPP_FETCH_DEPS OFF)
set(CORE_CPP_WITH_TUI OFF)
set(CORE_CPP_WITH_TLS ON)
add_subdirectory("${CORE_CPP_VENDOR_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/vendor/core-cpp"
                 SYSTEM EXCLUDE_FROM_ALL)

# With CORE_CPP_FETCH_DEPS OFF nothing may reach the network, and the one thing that could is CPM:
# core-cpp includes cmake/CPM.cmake only when a dependency has to be fetched, and that include is
# what downloads the bootstrap. Its absence is the proof that this configure fetched nothing.
if(COMMAND CPMAddPackage)
    message(FATAL_ERROR
        "consumer-vendored: core-cpp loaded CPM although CORE_CPP_FETCH_DEPS is OFF; a vendored "
        "build must resolve every dependency from the system or from this project.")
endif()

# The vendored copy carries the modules' own *_test.cpp files but no tests/ directory of its own,
# because the file set of Part I §5 does not include one. CORE_CPP_TESTING is off here (it defaults
# to off for a subproject), so nothing looks for it.
if(EXISTS "${CORE_CPP_VENDOR_DIR}/tests")
    message(FATAL_ERROR
        "consumer-vendored: the vendored copy has a tests/ directory, which the file set does not "
        "include; the manifest and docs/vendoring.md disagree about what is copied.")
endif()

# The obligation docs/vendoring.md puts on a consumer: the copy is verified by the consumer's own
# suite, so an edit to it is a failing test rather than a surprise at the next release. It needs
# neither git nor a network.
add_test(NAME core-cpp-vendored-copy
         COMMAND "${CMAKE_COMMAND}" -DMODE=check "-DDEST=${CORE_CPP_VENDOR_DIR}"
                 -P "${CORE_CPP_VENDOR_DIR}/cmake/CoreCppVendor.cmake")

add_executable(consumer-vendored main.cpp)
target_link_libraries(consumer-vendored PRIVATE core::async core::log core::net core::net_tls)
add_test(NAME consumer-vendored COMMAND consumer-vendored)
set_tests_properties(consumer-vendored PROPERTIES TIMEOUT 120)
