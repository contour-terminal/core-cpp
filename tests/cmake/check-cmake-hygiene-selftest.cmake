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
    # A private directory is layout, not a namespace: this declares the namespace of the directory
    # above it, and that is what the rule expects of it.
    "src/core/foo/posix/Impl.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core::foo\n{\n}\n"
    # A public one is a namespace: every segment of the path is.
    "src/core/foo/testing/Fake.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace core::foo::testing\n{\n}\n"
    "src/core/foo/Main.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace\n{\n}\nint main() { return 0<semicolon> }\n"
    "src/core/Top.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace core\n{\nnamespace views\n{\n}\n} // namespace core\n"
    "src/core/Base64.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace core::base64\n{\n}\n"
    # A leading block that only forward-declares another module's types defines nothing, so the
    # namespace the directory names is the first one AFTER it (core-cpp#23): on one line, and
    # spread over several, with a comment and an enum's underlying type.
    "src/core/foo/Fwd.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace core::bar { class Wakeup<semicolon> }\nnamespace core::bar::detail\n{\nstruct Slot<semicolon> // private\nenum class Kind : std::uint8_t<semicolon>\n}\nnamespace core::foo\n{\n}\n"
    "CHANGELOG.md|# Changelog

### Imported

| From | Commit | What |
|---|---|---|
| [contour](https://github.com/contour-terminal/contour) | `1111111111111111111111111111111111111111` | `src/core/Base64.hpp`, verbatim |
"
    "NOTICE|core-cpp

contour-terminal/contour
  Imported at 1111111111111111111111111111111111111111
  - src/core/Base64.hpp (verbatim)
"
    ".agent/reference/provenance.md|# Provenance\n\n| core-cpp path | upstream repo | upstream path | synced SHA | notes |\n|---|---|---|---|---|\n| `src/core/foo/CMakeLists.txt` | origin: core-cpp | - | - | - |\n| `src/core/foo/Foo.cpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/detail/Bar.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/posix/Impl.cpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/testing/Fake.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/Main.cpp` | origin: core-cpp | - | - | - |\n| `src/core/Top.hpp` | origin: core-cpp | - | - | - |\n| `src/core/Base64.hpp` | contour-terminal/contour | `src/crispy/Base64.hpp` | 1111111111111111111111111111111111111111 | verbatim |\n| `src/core/foo/Fwd.hpp` | origin: core-cpp | - | - | - |\n"
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
    "hand-spelled-stop-token-probe|src/core/foo/Foo.hpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core::foo { template <typename P> bool has(P awaiting) { return requires { awaiting.promise().stopToken()<semicolon> }<semicolon> } }\n"
    "stale-allowlist|src/core/testing/SuppressWindowsDialogsAtStartup.cpp|// SPDX-License-Identifier: Apache-2.0\n"
    "namespace-directory|src/core/foo/Foo.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace bar {}\n"
    "namespace-directory|src/core/foo/Foo.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core::foobar {}\n"
    "namespace-directory|src/core/foo/detail/Bar.hpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core\n{\nnamespace foo\n{\n}\n}\n"
    "namespace-directory|src/core/Top.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace crispy\n{\n}\n"
    "namespace-directory|src/core/Base64.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace core::Async\n{\n}\n"
    "namespace-directory|src/core/foo/Foo.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core::foo\n{\nnamespace Detail\n{\n}\n}\n"
    # A leading block is skipped only if it forward-declares and does nothing else.
    "namespace-directory|src/core/foo/Fwd.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace core::bar { class Wakeup<semicolon> void wake()<semicolon> }\nnamespace core::foo\n{\n}\n"
    "namespace-directory|src/core/foo/Fwd.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace core::bar\n{\nclass Wakeup {}<semicolon>\n}\nnamespace core::foo\n{\n}\n"
    # And its name is still held to lowercase.
    "namespace-directory|src/core/foo/Fwd.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace core::Bar { class Wakeup<semicolon> }\nnamespace core::foo\n{\n}\n"
    "namespace-directory|src/core/foo/testing/Fake.hpp|// SPDX-License-Identifier: Apache-2.0\n#pragma once\nnamespace core::foo\n{\n}\n"
    "provenance|src/core/foo/Extra.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core::foo {}\n"
    "provenance|.agent/reference/provenance.md|# Provenance\n\n| core-cpp path | upstream repo | upstream path | synced SHA | notes |\n|---|---|---|---|---|\n| `src/core/foo/CMakeLists.txt` | origin: core-cpp | - | - | - |\n| `src/core/foo/Foo.cpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/detail/Bar.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/posix/Impl.cpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/testing/Fake.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/Main.cpp` | origin: core-cpp | - | - | - |\n| `src/core/Top.hpp` | origin: core-cpp | - | - | - |\n| `src/core/Base64.hpp` | contour-terminal/contour | `src/crispy/Base64.hpp` | 1111111111111111111111111111111111111111 | verbatim |\n| `src/core/foo/DoesNotExist.cpp` | origin: core-cpp | - | - | - |\n"
    # The upstream half of a row. scripts/check-upstream-drift.py refuses both of these too, but it
    # needs the upstream checkouts to say anything at all and SKIPs (77) without them, which is every
    # CI runner -- so on its own it catches these two only on a developer's machine. Neither needs a
    # checkout or a network, so they are refused here, where the gate runs everywhere.
    "provenance|.agent/reference/provenance.md|# Provenance\n\n| core-cpp path | upstream repo | upstream path | synced SHA | notes |\n|---|---|---|---|---|\n| `src/core/foo/CMakeLists.txt` | origin: core-cpp | - | - | - |\n| `src/core/foo/Foo.cpp` | contour-terminal/contour | `src/foo/Foo.{cpp,hpp}` | 6777ff05014f8ff163b071e8b0e942830119db80 | - |\n| `src/core/foo/detail/Bar.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/posix/Impl.cpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/testing/Fake.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/Main.cpp` | origin: core-cpp | - | - | - |\n| `src/core/Top.hpp` | origin: core-cpp | - | - | - |\n| `src/core/Base64.hpp` | contour-terminal/contour | `src/crispy/Base64.hpp` | 1111111111111111111111111111111111111111 | verbatim |\n"
    "provenance|.agent/reference/provenance.md|# Provenance\n\n| core-cpp path | upstream repo | upstream path | synced SHA | notes |\n|---|---|---|---|---|\n| `src/core/foo/CMakeLists.txt` | origin: core-cpp | - | - | - |\n| `src/core/foo/Foo.cpp` | contour-terminal/contour | `src/foo/Foo.cpp` | 6777ff05 | - |\n| `src/core/foo/detail/Bar.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/posix/Impl.cpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/testing/Fake.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/Main.cpp` | origin: core-cpp | - | - | - |\n| `src/core/Top.hpp` | origin: core-cpp | - | - | - |\n| `src/core/Base64.hpp` | contour-terminal/contour | `src/crispy/Base64.hpp` | 1111111111111111111111111111111111111111 | verbatim |\n"
    # A row too short to hold the two cells above would otherwise skip both checks silently.
    "provenance|.agent/reference/provenance.md|# Provenance\n\n| core-cpp path | upstream repo | upstream path | synced SHA | notes |\n|---|---|---|---|---|\n| `src/core/foo/CMakeLists.txt` | origin: core-cpp | - | - | - |\n| `src/core/foo/Foo.cpp` | origin: core-cpp | - |\n| `src/core/foo/detail/Bar.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/posix/Impl.cpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/testing/Fake.hpp` | origin: core-cpp | - | - | - |\n| `src/core/foo/Main.cpp` | origin: core-cpp | - | - | - |\n| `src/core/Top.hpp` | origin: core-cpp | - | - | - |\n| `src/core/Base64.hpp` | contour-terminal/contour | `src/crispy/Base64.hpp` | 1111111111111111111111111111111111111111 | verbatim |\n"
    # NOTICE carries pins too, and nothing read them until now: the re-sync of the two verbatim
    # cmake files updated the README and the table and left NOTICE two commits behind, where it
    # sat until someone read it by hand.
    "provenance|NOTICE|core-cpp

contour-terminal/contour
  Imported at 2222222222222222222222222222222222222222
  - src/core/Base64.hpp (verbatim)
"
    "provenance|NOTICE|core-cpp

contour-terminal/contour
  Imported at 1111111111111111111111111111111111111111
  - src/core/Gone.hpp (verbatim)
"
    # CHANGELOG.md's Imported table is the THIRD statement of a pin, and it was the last one
    # still stale after the two verbatim cmake files were re-synced: one file gave two answers.
    "provenance|CHANGELOG.md|# Changelog

### Imported

| From | Commit | What |
|---|---|---|
| [contour](https://github.com/contour-terminal/contour) | `2222222222222222222222222222222222222222` | `src/core/Base64.hpp`, verbatim |
"
    # A row claiming verbatim must name ONLY verbatim files. The stale pin above survived
    # because its row was right about three subjects and wrong about two, so a row that mixes
    # them is refused rather than half-checked.
    "provenance|CHANGELOG.md|# Changelog

### Imported

| From | Commit | What |
|---|---|---|
| [contour](https://github.com/contour-terminal/contour) | `1111111111111111111111111111111111111111` | `src/core/Base64.hpp`, verbatim<semicolon> and the bootstrap download in `cmake/CPM.cmake` |
"
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

# Several violations of different rules at once are each reported, and counted (core-cpp#45).
# Every case above plants ONE, so none of them could see one rule's report mask another's: with
# five provenance rows missing, the scan once appeared to report a single, unrelated
# stale-allowlist. The reproduction at 29cddd6 reported all five, but nothing pinned it. This tree
# has one violation of each of five rules, in five files, plus the provenance violation the
# stale-allowlist file brings with it (it is under src/core/ and has no row): six in all, and the
# scanner must name every one and count exactly six.
set(several
    "CMakeLists.txt|# SPDX-License-Identifier: Apache-2.0\noption(FOO \"x\" ON)\n"
    "src/core/foo/Foo.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core::foo {} // NOLINT\n"
    "src/core/foo/Extra.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace core::foo {}\n"
    "src/core/foo/Main.cpp|// SPDX-License-Identifier: Apache-2.0\nnamespace bar {}\n"
    "src/core/testing/SuppressWindowsDialogsAtStartup.cpp|// SPDX-License-Identifier: Apache-2.0\n")
# "<rule>|<path>": what the scanner must name, each on a line of its own.
set(severalExpected
    "unprefixed-option|CMakeLists.txt"
    "nolint|src/core/foo/Foo.cpp"
    "provenance|src/core/foo/Extra.cpp"
    "namespace-directory|src/core/foo/Main.cpp"
    "stale-allowlist|src/core/testing/SuppressWindowsDialogsAtStartup.cpp"
    "provenance|src/core/testing/SuppressWindowsDialogsAtStartup.cpp")
list(LENGTH severalExpected severalCount)
set(severalDir "${WORK_DIR}/several")
core_cpp_selftest_write("${severalDir}" clean)
core_cpp_selftest_write("${severalDir}" several)
core_cpp_selftest_scan("${severalDir}" severalRc severalOutput)
if(severalRc EQUAL 0)
    list(APPEND failures "several at once: a tree with ${severalCount} violations was not refused")
else()
    foreach(expected IN LISTS severalExpected)
        string(FIND "${expected}" "|" bar)
        string(SUBSTRING "${expected}" 0 ${bar} rule)
        math(EXPR pathAt "${bar} + 1")
        string(SUBSTRING "${expected}" ${pathAt} -1 path)
        # One report line: "<path>:<line>: [<rule>]" or, for a file-level rule, "<path>: [<rule>]".
        string(REPLACE "." "\\." pathPattern "${path}")
        if(NOT severalOutput MATCHES "${pathPattern}(:[0-9-]+)?: \\[${rule}\\]")
            list(APPEND failures "several at once: [${rule}] in ${path} was not reported: ${severalOutput}")
        endif()
    endforeach()
    if(NOT severalOutput MATCHES "${severalCount} violation\\(s\\)")
        list(APPEND failures "several at once: the count is not ${severalCount}: ${severalOutput}")
    endif()
endif()

# The walk that feeds every rule must find the tree it was given (core-cpp#45). A relative ROOT
# used to glob nothing, and the scan then reported every allowlist row as stale and no real
# violation at all -- so the same tree, named relatively, must give the answer it gives absolutely.
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DROOT=several" -P "${SCANNER}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE relativeRc
    OUTPUT_VARIABLE relativeOut
    ERROR_VARIABLE relativeErr)
if(relativeRc EQUAL 0 OR NOT "${relativeOut}${relativeErr}" MATCHES "${severalCount} violation\\(s\\)")
    list(APPEND failures
         "relative ROOT: the several-at-once tree named relatively did not report its ${severalCount} violations: ${relativeOut}${relativeErr}")
endif()
# And a directory that is not a source tree is refused rather than scanned clean.
file(MAKE_DIRECTORY "${WORK_DIR}/empty")
core_cpp_selftest_scan("${WORK_DIR}/empty" emptyRc emptyOutput)
# CMake wraps a FATAL_ERROR message at about 76 columns, so the phrase can straddle a line break --
# measured on CI, where the longer WORK_DIR moved "CMakeLists.txt" onto the next line.
string(REGEX REPLACE "[ \t\r\n]+" " " emptyOutput "${emptyOutput}")
if(emptyRc EQUAL 0 OR NOT emptyOutput MATCHES "no CMakeLists\\.txt")
    list(APPEND failures "empty ROOT: a directory with nothing in it was not refused as no source tree: ${emptyOutput}")
endif()

# A file the dispatch drops is a file no rule runs over, and the scanner still reports success: the
# kind walk skips silently (`if(NOT kind)`), and the count it prints comes from the list it globbed
# rather than from the files it checked, so the number does not move when the checking stops. That
# is not hypothetical -- scripts/check-upstream-drift.py's row total slid from 351 to 350 because a
# substring match swallowed a data row, and nothing said a word.
#
# So this case plants a DEFECT rather than a violation: a copy of the scanner that skips every .hpp
# after its kind is assigned, which is what a misplaced `continue` looks like. The clean tree has
# five of them and eleven files the kind table matches, so the two counts must disagree by five and the
# scanner must die naming them. A mutation that changed both counts together -- deleting a row from
# the `kinds` table, say -- would prove nothing, which is why it is the dispatch that is broken here
# and not the table.
#
# The clean tree above already proves the UNMUTATED scanner passes; that is the half that keeps this
# from being a control that refuses everything.
file(READ "${SCANNER}" scannerText)
string(REPLACE "if(NOT kind)" "if(NOT kind OR path MATCHES \"\\\\.hpp$\")" mutatedText "${scannerText}")
if(mutatedText STREQUAL scannerText)
    list(APPEND failures
         "dropped-file control: the anchor 'if(NOT kind)' is gone from the scanner, so this case planted nothing -- re-anchor it rather than deleting it")
endif()
set(mutatedScanner "${WORK_DIR}/dropped-file-scanner.cmake")
file(WRITE "${mutatedScanner}" "${mutatedText}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${WORK_DIR}/clean" -P "${mutatedScanner}"
    RESULT_VARIABLE droppedRc
    OUTPUT_VARIABLE droppedOut
    ERROR_VARIABLE droppedErr)
if(droppedRc EQUAL 0)
    list(APPEND failures
         "dropped-file control: a scanner that silently skips every .hpp still reported success")
elseif(NOT "${droppedOut}${droppedErr}" MATCHES "checked 6 file")
    list(APPEND failures
         "dropped-file control: refused, but not over the count it checked: ${droppedOut}${droppedErr}")
endif()

if(failures)
    string(REPLACE ";" "\n  " printable "${failures}")
    message(FATAL_ERROR "hygiene-selftest:\n  ${printable}")
endif()
list(LENGTH cases caseCount)
message(STATUS "hygiene-selftest: the clean tree passed and all ${caseCount} violations were refused by name")
