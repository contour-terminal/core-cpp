# SPDX-License-Identifier: Apache-2.0
#
# Proves which of a module's source lists core_cpp_add_module() and core_cpp_add_test() compile on
# each platform (cmake/CoreCppTargets.cmake). Emscripten sets UNIX, but it is not a native POSIX
# system: it compiles SOURCES_EMSCRIPTEN and never SOURCES_POSIX, and a wasm-subset module compiles
# nothing but its SOURCES_EMSCRIPTEN there.
#
# And which table rows build at all (core_cpp_row_builds): a native row builds nowhere under
# Emscripten, which is what lets a module's header-only target build there while the rest of the
# module does not, and a row whose WHEN option is OFF builds nowhere.
#
# Usage: cmake -DTARGETS=<cmake/CoreCppTargets.cmake> -P tests/cmake/check-platform-sources.cmake
#
# CoreCppTargets.cmake reads the platform variables when it is included, so each scenario runs in a
# cmake -P of its own, which this script starts with SCENARIO set.

cmake_minimum_required(VERSION 3.25)

# "<scenario>|<platform variables that are true>|<PLATFORMS>|<sources expected, in order>"
set(scenarios
    "linux|UNIX,LINUX|any|common.cpp,posix.cpp,linux.cpp"
    "macos|UNIX,APPLE|any|common.cpp,posix.cpp,bsd.cpp"
    "freebsd|UNIX,BSD|any|common.cpp,posix.cpp,bsd.cpp"
    "windows|WIN32|any|common.cpp,windows.cpp"
    "emscripten|UNIX,EMSCRIPTEN|any|common.cpp,emscripten.cpp"
    "emscripten-subset|UNIX,EMSCRIPTEN|wasm-subset|emscripten.cpp"
    "linux-subset|UNIX,LINUX|wasm-subset|common.cpp,posix.cpp,linux.cpp"
    "windows-subset|WIN32|wasm-subset|common.cpp,windows.cpp"
)

# "<scenario>|<platform variables that are true>|<PLATFORMS>|<its WHEN option: ON, OFF or none>|<builds>"
set(rowScenarios
    "row-linux-native|UNIX,LINUX|native|none|ON"
    "row-windows-native|WIN32|native|none|ON"
    "row-emscripten-native|UNIX,EMSCRIPTEN|native|none|OFF"
    "row-emscripten-any|UNIX,EMSCRIPTEN|any|none|ON"
    "row-emscripten-subset|UNIX,EMSCRIPTEN|wasm-subset|none|ON"
    "row-linux-when-on|UNIX,LINUX|native|ON|ON"
    "row-linux-when-off|UNIX,LINUX|native|OFF|OFF"
    "row-emscripten-any-when-off|UNIX,EMSCRIPTEN|any|OFF|OFF"
)
set(platformVariables UNIX LINUX APPLE BSD WIN32 EMSCRIPTEN)

if(DEFINED SCENARIO)
    # A script run defines WIN32 or UNIX for its host; only the scenario's variables are true here.
    foreach(variable IN LISTS platformVariables)
        set(${variable} "")
    endforeach()
    string(REPLACE "," ";" trueVariables "${TRUE_VARIABLES}")
    foreach(variable IN LISTS trueVariables)
        set(${variable} 1)
    endforeach()

    include("${TARGETS}")

    if(DEFINED WHEN_VALUE)
        set(when "")
        if(NOT WHEN_VALUE STREQUAL "none")
            set(CORE_CPP_SCENARIO_OPTION ${WHEN_VALUE})
            set(when CORE_CPP_SCENARIO_OPTION)
        endif()
        core_cpp_row_builds("${PLATFORMS}" "${when}" builds)
        if(NOT builds STREQUAL EXPECTED)
            message(FATAL_ERROR "${SCENARIO}: builds '${builds}', expected '${EXPECTED}'")
        endif()
        message(STATUS "${SCENARIO}: builds '${builds}'")
        return()
    endif()

    # As cmake_parse_arguments(PARSE_ARGV ... arg ...) leaves a call that names one file per list.
    set(arg_SOURCES common.cpp)
    set(arg_SOURCES_POSIX posix.cpp)
    set(arg_SOURCES_LINUX linux.cpp)
    set(arg_SOURCES_BSD bsd.cpp)
    set(arg_SOURCES_WINDOWS windows.cpp)
    set(arg_SOURCES_EMSCRIPTEN emscripten.cpp)
    core_cpp_selected_sources(arg "${PLATFORMS}" selected)

    string(REPLACE "," ";" expected "${EXPECTED}")
    if(NOT selected STREQUAL expected)
        message(FATAL_ERROR "${SCENARIO}: selected '${selected}', expected '${expected}'")
    endif()
    message(STATUS "${SCENARIO}: '${selected}'")
    return()
endif()

if(NOT DEFINED TARGETS OR NOT EXISTS "${TARGETS}")
    message(FATAL_ERROR "check-platform-sources: TARGETS ('${TARGETS}') does not name cmake/CoreCppTargets.cmake.")
endif()

set(failures "")
# Each row of either table runs as its own cmake -P; a row scenario passes its WHEN value too.
set(runs "")
foreach(row IN LISTS scenarios)
    string(REPLACE "|" ";" fields "${row}")
    list(GET fields 0 scenario)
    list(GET fields 1 trueVariables)
    list(GET fields 2 platforms)
    list(GET fields 3 expected)
    list(APPEND runs "${scenario}|${trueVariables}|${platforms}|${expected}|")
endforeach()
foreach(row IN LISTS rowScenarios)
    string(REPLACE "|" ";" fields "${row}")
    list(GET fields 0 scenario)
    list(GET fields 1 trueVariables)
    list(GET fields 2 platforms)
    list(GET fields 3 when)
    list(GET fields 4 expected)
    list(APPEND runs "${scenario}|${trueVariables}|${platforms}|${expected}|-DWHEN_VALUE=${when}")
endforeach()

foreach(run IN LISTS runs)
    string(REPLACE "|" ";" fields "${run}")
    list(GET fields 0 scenario)
    list(GET fields 1 trueVariables)
    list(GET fields 2 platforms)
    list(GET fields 3 expected)
    list(GET fields 4 whenArgument)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DSCENARIO=${scenario}" "-DTRUE_VARIABLES=${trueVariables}"
                "-DPLATFORMS=${platforms}" "-DEXPECTED=${expected}" "-DTARGETS=${TARGETS}"
                ${whenArgument} -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    string(STRIP "${output}" output)
    string(REGEX REPLACE "[\r\n]+[ \t]*" " " output "${output}")
    if(result EQUAL 0)
        message(STATUS "ok    ${output}")
    else()
        message(STATUS "FAIL  ${output}")
        list(APPEND failures ${scenario})
    endif()
endforeach()

if(failures)
    message(FATAL_ERROR "check-platform-sources: failed: ${failures}")
endif()
list(LENGTH scenarios count)
list(LENGTH rowScenarios rowCount)
message(STATUS
    "check-platform-sources: ${count} scenario(s) select what they should, and ${rowCount} row(s) build where they should")
