# SPDX-License-Identifier: Apache-2.0
#
# Proves that check-cmake-hygiene.cmake refuses what it exists to refuse. For every rule it writes a
# small tree that is clean except for one violating file, and requires the scanner to fail naming that
# rule and that file. A clean tree must pass. A rule without a case here is refused too, so a rule
# added to the scanner cannot go unproven.
#
# Usage: cmake -DSCANNER=<check-cmake-hygiene.cmake> -DWORK_DIR=<scratch directory>
#              -P tests/cmake/check-cmake-hygiene-selftest.cmake

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED SCANNER OR NOT EXISTS "${SCANNER}")
    message(FATAL_ERROR "hygiene-selftest: SCANNER ('${SCANNER}') is not set or does not exist.")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "hygiene-selftest: WORK_DIR is not set.")
endif()

# A tree with nothing to refuse: "<file>|<content>".
set(clean
    "CMakeLists.txt|# SPDX-License-Identifier: Apache-2.0\noption(CORE_CPP_FOO \"x\" ON)\ninclude(CMakeDependentOption)\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/cmake/Foo.cmake\")\n"
    "cmake/Foo.cmake|# SPDX-License-Identifier: Apache-2.0\nfunction(core_cpp_foo)\nendfunction()\nset(CORE_CPP_FOO_DIR \"\" CACHE PATH \"x\")\n"
    "src/core/foo/CMakeLists.txt|# SPDX-License-Identifier: Apache-2.0\nadd_library(core-cpp-foo STATIC Foo.cpp)\ntarget_compile_options(core-cpp-foo PRIVATE -Wall)\n"
    "src/core/foo/Foo.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core::foo {}\n"
    "src/core/foo/detail/Bar.hpp|// SPDX-License-Identifier: Apache-2.0\nnamespace fs = std::filesystem<semicolon>\nnamespace core::foo::detail\n{\n}\n"
    "src/core/foo/Main.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace\n{\n}\nint main() { return 0<semicolon> }\n"
    "src/core/Top.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace core\n{\nnamespace views\n{\n}\n} // namespace core\n"
    "src/core/Base64.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace core::base64\n{\n}\n"
    ".agent/reference/provenance.md|# Provenance\n\n| core-cpp path | upstream repo | upstream path | synced SHA | notes |\n|---|---|---|---|---|\n| `src/core/foo/CMakeLists.txt` | origin: core-cpp | - | - | - |\n| `src/core/foo/Foo.cpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/detail/Bar.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/Main.cpp` | origin: core-cpp | - | - | - |\n| `src/core/Top.hpp` | origin: core-cpp | - | - | - |\n| `src/core/Base64.hpp` | origin: core-cpp | - | - | - |\n"
)

# At least one violating file per rule: "<rule>|<file>|<content>". The file replaces its clean
# namesake.
# Content spells ';' as <semicolon>, which would otherwise split the row: rows are list elements.
set(cases
    "missing-spdx|src/core/foo/Foo.cpp|namespace core::foo {}\n"
    "unprefixed-option|CMakeLists.txt|# SPDX-License-Identifier: Apache-2.0\noption(FOO \"x\" ON)\n"
    "unprefixed-cache-variable|cmake/Foo.cmake|# SPDX-License-Identifier: Apache-2.0\nset(FOO_DIR \"\" CACHE PATH \"x\")\n"
    "unprefixed-function|cmake/Foo.cmake|# SPDX-License-Identifier: Apache-2.0\nfunction(foo)\nendfunction()\n"
    "global-compile-options|CMakeLists.txt|# SPDX-License-Identifier: Apache-2.0\nadd_compile_options(-Wall)\n"
    "global-cmake-variable|CMakeLists.txt|# SPDX-License-Identifier: Apache-2.0\nset(CMAKE_CXX_FLAGS \"-O2\")\n"
    "process-environment|cmake/Foo.cmake|# SPDX-License-Identifier: Apache-2.0\nset(ENV{GIT_HTTP_LOW_SPEED_TIME} 120)\n"
    "top-level-only-include|cmake/Foo.cmake|# SPDX-License-Identifier: Apache-2.0\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/FetchTransferBound.cmake\")\n"
    "untyped-library|src/core/foo/CMakeLists.txt|# SPDX-License-Identifier: Apache-2.0\nadd_library(x a.cpp)\n"
    "public-flags|src/core/foo/CMakeLists.txt|# SPDX-License-Identifier: Apache-2.0\nadd_library(x STATIC a.cpp)\ntarget_compile_options(x PUBLIC -Wall)\n"
    "source-glob|src/core/foo/CMakeLists.txt|# SPDX-License-Identifier: Apache-2.0\nfile(GLOB sources *.cpp)\n"
    "include-by-name|CMakeLists.txt|# SPDX-License-Identifier: Apache-2.0\ninclude(CoreCppOptions)\n"
    "nolint|src/core/foo/Foo.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core::foo {} // NOLINT\n"
    "diagnostic-pragma|src/core/foo/Foo.cpp|// SPDX-License-Identifier: Apache-2.0\n#pragma clang diagnostic ignored \"-Wshadow\"\n"
    "c-style-for|src/core/foo/Foo.cpp|// SPDX-License-Identifier: Apache-2.0\nvoid count() { for (int i = 0<semicolon> i < 3<semicolon> ++i) {} }\n"
    "stale-allowlist|src/core/testing/SuppressWindowsDialogsAtStartup.cpp|// SPDX-License-Identifier: Apache-2.0\n"
    "namespace-directory|src/core/foo/Foo.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace bar {}\n"
    "namespace-directory|src/core/foo/Foo.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core::foobar {}\n"
    "namespace-directory|src/core/foo/detail/Bar.hpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core\n{\nnamespace foo\n{\n}\n}\n"
    "namespace-directory|src/core/Top.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace crispy\n{\n}\n"
    "namespace-directory|src/core/Base64.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace core::Async\n{\n}\n"
    "provenance|src/core/foo/Extra.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core::foo {}\n"
    "provenance|.agent/reference/provenance.md|# Provenance\n\n| core-cpp path | upstream repo | upstream path | synced SHA | notes |\n|---|---|---|---|---|\n| `src/core/foo/CMakeLists.txt` | origin: core-cpp | - | - | - |\n| `src/core/foo/Foo.cpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/detail/Bar.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/Main.cpp` | origin: core-cpp | - | - | - |\n| `src/core/Top.hpp` | origin: core-cpp | - | - | - |\n| `src/core/Base64.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/DoesNotExist.cpp` | origin: core-cpp | - | - | - |\n"
)

## @brief Writes the "<file>|<content>" rows of the list named @p rowsVar under @p dir.
function(core_cpp_selftest_write dir rowsVar)
    foreach(row IN LISTS ${rowsVar})
        string(FIND "${row}" "|" bar)
        string(SUBSTRING "${row}" 0 ${bar} path)
        math(EXPR contentAt "${bar} + 1")
        string(SUBSTRING "${row}" ${contentAt} -1 content)
        string(REPLACE "<semicolon>" ";" content "${content}")
        file(WRITE "${dir}/${path}" "${content}")
    endforeach()
endfunction()

## @brief Runs the scanner over @p dir, setting @p rcVar and @p outputVar.
function(core_cpp_selftest_scan dir rcVar outputVar)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DROOT=${dir}" -P "${SCANNER}"
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    set(${rcVar} "${rc}" PARENT_SCOPE)
    set(${outputVar} "${out}${err}" PARENT_SCOPE)
endfunction()

set(failures "")

# Every rule the scanner has needs a case here, and every case must name a rule the scanner has.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -DLIST_RULES=ON -P "${SCANNER}"
    RESULT_VARIABLE listed
    OUTPUT_VARIABLE listing
    ERROR_VARIABLE listing)
if(NOT listed EQUAL 0)
    message(FATAL_ERROR "hygiene-selftest: the scanner did not list its rules (${listed}): ${listing}")
endif()
string(REGEX MATCHALL "rule: [a-z-]+" scannerRules "${listing}")
list(TRANSFORM scannerRules REPLACE "^rule: " "")
set(caseRules "")
foreach(row IN LISTS cases)
    string(REGEX REPLACE "\\|.*$" "" rule "${row}")
    list(APPEND caseRules "${rule}")
endforeach()
foreach(rule IN LISTS scannerRules)
    if(NOT rule IN_LIST caseRules)
        list(APPEND failures "rule '${rule}' has no case in this self-test")
    endif()
endforeach()
foreach(rule IN LISTS caseRules)
    if(NOT rule IN_LIST scannerRules)
        list(APPEND failures "case '${rule}' names no rule of the scanner")
    endif()
endforeach()

# A clean tree passes.
file(REMOVE_RECURSE "${WORK_DIR}")
core_cpp_selftest_write("${WORK_DIR}/clean" clean)
core_cpp_selftest_scan("${WORK_DIR}/clean" rc output)
if(NOT rc EQUAL 0)
    list(APPEND failures "the clean tree was refused (${rc}): ${output}")
endif()

# Each violation is refused by name, in the file that has it.
set(caseIndex 0)
foreach(row IN LISTS cases)
    string(FIND "${row}" "|" bar)
    string(SUBSTRING "${row}" 0 ${bar} rule)
    math(EXPR fileAt "${bar} + 1")
    string(SUBSTRING "${row}" ${fileAt} -1 fileRow)
    string(REGEX REPLACE "\\|.*$" "" path "${fileRow}")

    math(EXPR caseIndex "${caseIndex} + 1")
    set(dir "${WORK_DIR}/${caseIndex}-${rule}")
    core_cpp_selftest_write("${dir}" clean)
    set(violation "${fileRow}")
    core_cpp_selftest_write("${dir}" violation)
    core_cpp_selftest_scan("${dir}" rc output)
    if(rc EQUAL 0)
        list(APPEND failures "${rule}: ${path} was not refused")
    elseif(NOT output MATCHES "\\[${rule}\\]")
        list(APPEND failures "${rule}: refused, but not by name: ${output}")
    elseif(NOT output MATCHES "${path}")
        list(APPEND failures "${rule}: refused, but without naming ${path}: ${output}")
    endif()
endforeach()

if(failures)
    string(REPLACE ";" "\n  " printable "${failures}")
    message(FATAL_ERROR "hygiene-selftest:\n  ${printable}")
endif()
list(LENGTH cases caseCount)
message(STATUS "hygiene-selftest: the clean tree passed and all ${caseCount} violations were refused by name")
