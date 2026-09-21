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

if(failures)
    message(FATAL_ERROR "check-layering: failed: ${failures}")
endif()
list(LENGTH scenarios count)
message(STATUS "check-layering: all ${count} scenario(s) configure or are refused as they should")
