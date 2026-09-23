# SPDX-License-Identifier: Apache-2.0
#
# Proves that the module table bounds what every target links, at configure time
# (cmake/CoreCppModules.cmake, cmake/CoreCppTargets.cmake):
#
#   - a target with a core_cpp_module_target() row links exactly what that row's DEPS name, and
#     nothing at all when it names none, not even another target of its own module;
#   - a module's own target links what the module's row lists in DEPS, and its module's targets;
#   - a row whose DEPS name something its module may not link is refused where it is declared.
#
# Each scenario configures a project of its own, with no language enabled: the real table, the
# rows the scenario adds to it, stand-ins for the targets it links, and one core_cpp_add_module()
# call. A scenario either configures, or fails naming what it refused.
#
# And it proves the same table bounds what every module INCLUDES, which the link check cannot see:
# every module's headers sit under one include root, so an `#include <core/tui/...>` from a file of
# `core::log` compiles whether or not `log` may reach `tui`, and a layering violation builds clean.
# The table is read from the real cmake/CoreCppModules.cmake -- by configuring it, not by a regex
# that imitates it -- and a module may include itself and whatever its DEPS reach, transitively,
# because that is exactly what its target links. Tests (`*_test.cpp`) and canaries (`*Canary.cpp`)
# are separate executables with links of their own, and are not scanned. The scan is watched
# refusing a planted violation, and accepting a planted compliant tree, before it is run over this
# one -- a scan that found nothing in a tree it could not read would read exactly like a clean one.
#
# What the include scan does not see: a module's SUB-target (`tui_output`, `net_types`) is held to
# its module's row rather than its own, because the files a sub-target owns are named in its
# directory's CMakeLists.txt, not in the table; the link check above covers those rows. An include
# spelled through a macro, or on a line carrying a `;`, is not read.
#
# Usage: cmake -DROOT=<source root> -DWORK_DIR=<scratch directory>
#              [-DGENERATOR=<generator>] [-DMAKE_PROGRAM=<its build tool>]
#              -P tests/cmake/check-layering.cmake

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ROOT OR NOT EXISTS "${ROOT}/cmake/CoreCppModules.cmake")
    message(FATAL_ERROR "check-layering: ROOT ('${ROOT}') does not name core-cpp's source root.")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "check-layering: WORK_DIR is not set.")
endif()

# What every scenario's project starts with. stand_in() makes an empty core::<name> that belongs to
# the module the table says it does; add() declares <target> of <module> as its directory would.
set(preamble [=[
cmake_minimum_required(VERSION 3.25)
project(core-cpp-layering-scenario LANGUAGES NONE)
set(CORE_CPP_SOURCE_DIR "${ROOT}")
set(CORE_CPP_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(CORE_CPP_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
# In the order the real build includes them. Options first is not cosmetic: the table's rows carry
# WHEN conditions naming those options, and a scenario that reads the table without them declares
# rows whose condition cannot be evaluated. That was invisible until core_cpp_check_when() started
# refusing an undefined WHEN, at which point every scenario here failed on the real net_tls row.
include("${ROOT}/cmake/CoreCppOptions.cmake")
include("${ROOT}/cmake/CoreCppTargets.cmake")
include("${ROOT}/cmake/CoreCppModules.cmake")

function(core_cpp_layering_stand_in)
    foreach(name IN LISTS ARGN)
        set(module "${name}")
        if(DEFINED CORE_CPP_TARGET_${name}_MODULE)
            set(module "${CORE_CPP_TARGET_${name}_MODULE}")
        endif()
        add_library(core-cpp-${name} INTERFACE)
        add_library(core::${name} ALIAS core-cpp-${name})
        set_target_properties(core-cpp-${name} PROPERTIES CORE_CPP_MODULE "${module}")
    endforeach()
endfunction()

function(core_cpp_layering_add module name)
    set(CORE_CPP_CURRENT_MODULE "${module}")
    core_cpp_add_module(${name} KIND INTERFACE PUBLIC_LIBS ${ARGN})
endfunction()
]=])

# "<scenario>|<expected: 'configures', or a regex the refusal must match>|<the scenario's CMake>".
# The real net row lists DEPS async platform; net_x is a row a scenario adds to the net module.
set(scenarios
    # The real rows of core::net_types: it links nothing, not even core::net.
    "net_types-links-nothing|configures|core_cpp_layering_add(net net_types)"
    "net_types-links-async|core-cpp-net_types links core::async|core_cpp_layering_stand_in(async)\ncore_cpp_layering_add(net net_types core::async)"
    "net_types-links-net|core-cpp-net_types links core::net[^_]|core_cpp_layering_stand_in(net)\ncore_cpp_layering_add(net net_types core::net)"

    # A row with DEPS links what they name: its module's own target, a module its module's row lists.
    "row-links-its-deps|configures|core_cpp_module_target(NAME net_x MODULE net KIND INTERFACE PLATFORMS any DEPS net async)\ncore_cpp_layering_stand_in(net async)\ncore_cpp_layering_add(net net_x core::net core::async)"
    "row-links-undeclared-module|core-cpp-net_x links core::platform|core_cpp_module_target(NAME net_x MODULE net KIND INTERFACE PLATFORMS any DEPS async)\ncore_cpp_layering_stand_in(async platform)\ncore_cpp_layering_add(net net_x core::async core::platform)"
    "row-links-undeclared-sibling|core-cpp-net_x links core::net_types|core_cpp_module_target(NAME net_x MODULE net KIND INTERFACE PLATFORMS any DEPS net)\ncore_cpp_layering_stand_in(net net_types)\ncore_cpp_layering_add(net net_x core::net core::net_types)"
    "row-without-deps-links-a-module-dep|core-cpp-net_x links core::async|core_cpp_module_target(NAME net_x MODULE net KIND INTERFACE PLATFORMS any)\ncore_cpp_layering_stand_in(async)\ncore_cpp_layering_add(net net_x core::async)"

    # A row's DEPS are refused where the row is declared unless its module may link each of them.
    "row-deps-outside-its-module|core_cpp_module_target\\(net_x\\): DEPS names 'base'|core_cpp_module_target(NAME net_x MODULE net KIND INTERFACE PLATFORMS any DEPS async base)"
    "row-deps-an-undeclared-sibling|core_cpp_module_target\\(net_x\\): DEPS names 'net_y'|core_cpp_module_target(NAME net_x MODULE net KIND INTERFACE PLATFORMS any DEPS net_y)\ncore_cpp_module_target(NAME net_y MODULE net KIND INTERFACE PLATFORMS any)"
    "row-deps-itself|core_cpp_module_target\\(net_x\\): DEPS names 'net_x'|core_cpp_module_target(NAME net_x MODULE net KIND INTERFACE PLATFORMS any DEPS net_x)"

    # A row's WHEN names a variable that exists. A misspelled one expands, in core_cpp_row_builds(),
    # to `NOT <undefined>` -- which is true -- so the target and every test registered against it
    # disappear from the build with no diagnostic at all: it configures clean, it builds clean, and
    # a module is simply not there. That is a guard whose misspelling passes, which is the failure
    # this repository has removed three times tonight in other files.
    #
    # The test is DEFINED, not "is an option()", and the middle two scenarios are what makes that a
    # decision rather than a preference: CORE_CPP_USE_THREADS is a plain set() in
    # CoreCppDependencies.cmake and is already used as a WHEN there, so an option()-only rule would
    # refuse a condition this codebase uses today -- born needing the workaround that stops a rule
    # being read. A typo is undefined by construction, which is exactly and only what is refused.
    "when-names-an-undeclared-option|core_cpp_module_target\\(net_x\\): WHEN names 'CORE_CPP_WITH_TSL'|core_cpp_module_target(NAME net_x MODULE net KIND INTERFACE PLATFORMS any WHEN CORE_CPP_WITH_TSL)"
    "when-names-a-declared-option|configures|option(CORE_CPP_SCENARIO_OPT \"\" OFF)\ncore_cpp_module_target(NAME net_x MODULE net KIND INTERFACE PLATFORMS any WHEN CORE_CPP_SCENARIO_OPT)"
    "when-names-a-plain-variable|configures|set(CORE_CPP_SCENARIO_FLAG ON)\ncore_cpp_module_target(NAME net_x MODULE net KIND INTERFACE PLATFORMS any WHEN CORE_CPP_SCENARIO_FLAG)"
    "when-names-a-variable-set-off|configures|set(CORE_CPP_SCENARIO_FLAG OFF)\ncore_cpp_module_target(NAME net_x MODULE net KIND INTERFACE PLATFORMS any WHEN CORE_CPP_SCENARIO_FLAG)"
    "module-when-names-an-undeclared-option|core_cpp_module\\(demo\\): WHEN names 'CORE_CPP_WITH_DEMO'|core_cpp_module(NAME demo KIND INTERFACE DEPS base PLATFORMS any WHEN CORE_CPP_WITH_DEMO)"

    # A module's own target follows the module's row: its DEPS, and the module's targets.
    "module-links-its-targets|configures|core_cpp_module(NAME demo KIND INTERFACE DEPS base PLATFORMS any)\ncore_cpp_module_target(NAME demo_types MODULE demo KIND INTERFACE PLATFORMS any)\ncore_cpp_layering_stand_in(base demo_types)\ncore_cpp_layering_add(demo demo core::base core::demo_types)"
    "module-links-undeclared-module|core-cpp-demo links core::log|core_cpp_module(NAME demo KIND INTERFACE DEPS base PLATFORMS any)\ncore_cpp_layering_stand_in(log)\ncore_cpp_layering_add(demo demo core::log)"
)

set(generatorArguments "")
if(GENERATOR)
    list(APPEND generatorArguments -G "${GENERATOR}")
endif()
if(MAKE_PROGRAM)
    list(APPEND generatorArguments "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
set(failures "")
foreach(row IN LISTS scenarios)
    string(FIND "${row}" "|" bar)
    string(SUBSTRING "${row}" 0 ${bar} scenario)
    math(EXPR rest "${bar} + 1")
    string(SUBSTRING "${row}" ${rest} -1 row)
    string(FIND "${row}" "|" bar)
    string(SUBSTRING "${row}" 0 ${bar} expected)
    math(EXPR rest "${bar} + 1")
    string(SUBSTRING "${row}" ${rest} -1 body)

    set(dir "${WORK_DIR}/${scenario}")
    file(WRITE "${dir}/CMakeLists.txt" "${preamble}\n${body}\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${generatorArguments} "-DROOT=${ROOT}" -S "${dir}" -B "${dir}/build"
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    string(REGEX REPLACE "[\r\n]+[ \t]*" " " output "${output}")

    if(expected STREQUAL "configures")
        if(rc EQUAL 0)
            message(STATUS "ok    ${scenario}: configures")
        else()
            message(STATUS "FAIL  ${scenario}: refused (${rc}): ${output}")
            list(APPEND failures ${scenario})
        endif()
    elseif(rc EQUAL 0)
        message(STATUS "FAIL  ${scenario}: was not refused")
        list(APPEND failures ${scenario})
    elseif(NOT output MATCHES "${expected}")
        message(STATUS "FAIL  ${scenario}: refused, but not by name ('${expected}'): ${output}")
        list(APPEND failures ${scenario})
    else()
        message(STATUS "ok    ${scenario}: refused, naming '${expected}'")
    endif()
endforeach()

# ---- include edges ------------------------------------------------------------------------------

# The table, as the build reads it: a project that includes the real module files and writes each
# row's directory and DEPS out. `|` separates the fields; DEPS stays a `;` list inside its field.
set(tableProject "${WORK_DIR}/include-edges-table")
file(WRITE "${tableProject}/CMakeLists.txt" "${preamble}\n" [=[
set(rows "")
foreach(module IN LISTS CORE_CPP_MODULES)
    string(APPEND rows "${module}|${CORE_CPP_MODULE_${module}_DIR}|${CORE_CPP_MODULE_${module}_WHEN}|${CORE_CPP_MODULE_${module}_DEPS}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/table.txt" "${rows}")
]=])
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${generatorArguments} "-DROOT=${ROOT}" -S "${tableProject}" -B "${tableProject}/build"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
if(NOT rc EQUAL 0 OR NOT EXISTS "${tableProject}/build/table.txt")
    message(FATAL_ERROR "check-layering: could not read the module table by configuring it (${rc}): ${output}")
endif()
file(STRINGS "${tableProject}/build/table.txt" tableRows)

set(modules "")
foreach(row IN LISTS tableRows)
    string(REPLACE "|" ";" fields "${row}")
    list(GET fields 0 module)
    list(GET fields 1 dir)
    list(GET fields 2 when)
    list(LENGTH fields fieldCount)
    set(deps "")
    if(fieldCount GREATER 3)
        list(SUBLIST fields 3 -1 deps)
    endif()
    list(APPEND modules "${module}")
    if(when STREQUAL "")
        list(APPEND unconditionalByConfigure "${module}")
    endif()
    set(moduleDir_${module} "${dir}")
    set(moduleDeps_${module} ${deps})
    set(dirModule_${dir} "${module}")
endforeach()
list(LENGTH modules moduleCount)
if(moduleCount LESS 2)
    message(FATAL_ERROR "check-layering: the module table read back ${moduleCount} row(s); the scan would have nothing to hold")
endif()

# The same file read the OTHER way it is read: cmake/CoreCppVendor.cmake has no build to configure,
# so it finds the rows with a regex and decides from the text which modules a copy must carry
# (a row with no WHEN). The two readings must agree on the names and on which rows are
# unconditional, or the vendoring tool refuses a copy the build would accept, or accepts one the
# build cannot configure. The regex below is the tool's, copied deliberately (its lines 513-521):
# change one and this check fails until the other matches. No fixture -- a fixture would pin the
# imitation and go stale silently; this reads the real table.
file(READ "${ROOT}/cmake/CoreCppModules.cmake" moduleTableText)
string(REGEX REPLACE "(^|\n)[ \t]*#[^\n]*" "\\1" moduleTableText "${moduleTableText}")
string(REGEX MATCHALL "core_cpp_module\\([^)]*\\)" moduleRowTexts "${moduleTableText}")
set(namesByRegex "")
set(unconditionalByRegex "")
foreach(row IN LISTS moduleRowTexts)
    if(NOT row MATCHES "NAME[ \t\r\n]+([A-Za-z0-9_]+)")
        continue()
    endif()
    set(rowName "${CMAKE_MATCH_1}")
    list(APPEND namesByRegex "${rowName}")
    if(NOT row MATCHES "[ \t\r\n]WHEN[ \t\r\n]")
        list(APPEND unconditionalByRegex "${rowName}")
    endif()
endforeach()
foreach(pair IN ITEMS "modules|namesByRegex" "unconditionalByConfigure|unconditionalByRegex")
    string(REPLACE "|" ";" pair "${pair}")
    list(GET pair 0 left)
    list(GET pair 1 right)
    set(leftSorted ${${left}})
    set(rightSorted ${${right}})
    list(SORT leftSorted)
    list(SORT rightSorted)
    if(NOT "${leftSorted}" STREQUAL "${rightSorted}")
        message(STATUS "FAIL  table-readings: ${left} is '${leftSorted}' but ${right} is '${rightSorted}'")
        list(APPEND failures table-readings)
    endif()
endforeach()
if(NOT "table-readings" IN_LIST failures)
    list(JOIN unconditionalByConfigure ", " shownUnconditional)
    message(STATUS "ok    table-readings: configure and the vendoring tool's regex agree on ${moduleCount} module(s), unconditional: ${shownUnconditional}")
endif()

# What each module may include: itself, and everything its DEPS reach.
foreach(module IN LISTS modules)
    set(reach "")
    set(pending ${moduleDeps_${module}})
    while(pending)
        list(POP_FRONT pending next)
        if(NOT next IN_LIST reach)
            list(APPEND reach "${next}")
            list(APPEND pending ${moduleDeps_${next}})
        endif()
    endwhile()
    set(moduleReach_${module} "${module};${reach}")
endforeach()

## @brief Scans @p root's src/core/ for includes of a module its own module's row does not reach.
## @param root        A tree laid out as this one is.
## @param outViolations The violations, one per offending include.
## @param outFiles    How many files were read.
## @param outEdges    How many cross-module includes were seen, allowed or not.
function(core_cpp_layering_scan_includes root outViolations outFiles outEdges)
    set(violations "")
    set(files 0)
    set(edges 0)
    foreach(module IN LISTS modules)
        set(dir "${moduleDir_${module}}")
        if(dir STREQUAL ".")
            file(GLOB sources LIST_DIRECTORIES false "${root}/src/core/*.hpp" "${root}/src/core/*.cpp")
        else()
            file(GLOB_RECURSE sources LIST_DIRECTORIES false "${root}/src/core/${dir}/*.hpp" "${root}/src/core/${dir}/*.cpp")
        endif()
        list(FILTER sources EXCLUDE REGEX "(_test|Canary)\\.cpp$")
        foreach(source IN LISTS sources)
            math(EXPR files "${files} + 1")
            file(STRINGS "${source}" includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]core/")
            foreach(line IN LISTS includes)
                if(NOT line MATCHES "core/([^>\"]+)[>\"]")
                    continue()
                endif()
                set(included "${CMAKE_MATCH_1}")
                set(targetDir ".")
                if(included MATCHES "^([^/]+)/")
                    set(targetDir "${CMAKE_MATCH_1}")
                endif()
                if(NOT DEFINED dirModule_${targetDir})
                    continue() # not a module directory -- nothing the table speaks for
                endif()
                set(target "${dirModule_${targetDir}}")
                if(target STREQUAL module)
                    continue()
                endif()
                math(EXPR edges "${edges} + 1")
                if(NOT target IN_LIST moduleReach_${module})
                    file(RELATIVE_PATH shown "${root}" "${source}")
                    list(APPEND violations
                         "${shown} includes <core/${included}>, and ${module}'s row does not reach ${target}")
                endif()
            endforeach()
        endforeach()
    endforeach()
    set(${outViolations} "${violations}" PARENT_SCOPE)
    set(${outFiles} ${files} PARENT_SCOPE)
    set(${outEdges} ${edges} PARENT_SCOPE)
endfunction()

# Watched refusing first, and accepting, on a planted tree: `log` reaching `net`, which its row does
# not; `net` reaching `platform` and -- through it -- `base`, which its row does; and a test file
# reaching `tui`, which is not scanned.
set(fixture "${WORK_DIR}/include-edges-fixture")
file(REMOVE_RECURSE "${fixture}")
file(WRITE "${fixture}/src/core/log/Bad.hpp" "#pragma once\n#include <core/net/EventLoop.hpp>\n")
file(WRITE "${fixture}/src/core/net/Good.hpp"
     "#pragma once\n#include <core/platform/Types.hpp>\n#include <core/Utils.hpp>\n#include <core/async/Task.hpp>\n")
file(WRITE "${fixture}/src/core/net/Good_test.cpp" "#include <core/tui/Screen.hpp>\n")
core_cpp_layering_scan_includes("${fixture}" planted plantedFiles plantedEdges)
list(LENGTH planted plantedCount)
if(NOT plantedCount EQUAL 1 OR NOT planted MATCHES "src/core/log/Bad.hpp includes <core/net/EventLoop.hpp>, and log's row does not reach net")
    message(STATUS "FAIL  include-edges-fixture: wanted exactly the planted violation, got ${plantedCount}: ${planted}")
    list(APPEND failures include-edges-fixture)
elseif(NOT plantedEdges EQUAL 4)
    message(STATUS "FAIL  include-edges-fixture: counted ${plantedEdges} cross-module include(s), not 4 -- the test file was scanned, or an allowed edge was missed")
    list(APPEND failures include-edges-fixture)
else()
    message(STATUS "ok    include-edges-fixture: refuses log -> net by name, allows net -> platform -> base, skips tests")
endif()

core_cpp_layering_scan_includes("${ROOT}" violations scannedFiles scannedEdges)
if(scannedFiles EQUAL 0 OR scannedEdges EQUAL 0)
    message(STATUS "FAIL  include-edges: read ${scannedFiles} file(s) and ${scannedEdges} cross-module include(s) under ${ROOT}/src/core -- the scan is broken, not the tree clean")
    list(APPEND failures include-edges)
elseif(violations)
    foreach(violation IN LISTS violations)
        message(STATUS "FAIL  include-edges: ${violation}")
    endforeach()
    list(APPEND failures include-edges)
else()
    message(STATUS "ok    include-edges: ${scannedEdges} cross-module include(s) in ${scannedFiles} file(s), each an edge the table allows")
endif()

if(failures)
    message(FATAL_ERROR "check-layering: failed: ${failures}")
endif()
list(LENGTH scenarios count)
message(STATUS "check-layering: all ${count} scenario(s) configure or are refused as they should, and every include is an edge of the table")
