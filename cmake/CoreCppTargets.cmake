# SPDX-License-Identifier: Apache-2.0
#
# How a module directory declares its targets and its tests.
#
#   core_cpp_add_module(<name> KIND STATIC|INTERFACE|OBJECT
#                       [HEADERS <public header>...]
#                       [SOURCES <source or private header>...]
#                       [SOURCES_POSIX ...] [SOURCES_LINUX ...] [SOURCES_BSD ...] [SOURCES_WINDOWS ...]
#                       [PUBLIC_LIBS <lib>...] [PRIVATE_LIBS <lib>...])
#
#   core_cpp_add_test(<module> [SOURCES ...] [SOURCES_POSIX ...] [SOURCES_LINUX ...]
#                     [SOURCES_BSD ...] [SOURCES_WINDOWS ...] [LIBS <lib>...] [LABELS <label>...])
#
# core_cpp_add_module() creates the real target core-cpp-<name> and its alias
# core::<name>. HEADERS are the public headers; they form the target's HEADERS file
# set, based at src/, so the target is install-ready. Private headers (detail/,
# posix/, linux/, darwin/, windows/, backend/) go in a SOURCES list and are in no
# file set.
#
# It must be called from a directory that core_cpp_add_modules() entered for a row
# of the module table, and a target it links as core::<x> must belong to that same
# module or to one the row lists in DEPS. That is how the table's layering is
# enforced rather than merely documented.
#
# core_cpp_add_test() builds core-cpp-<module>-test from the module's *_test.cpp
# files, links it with core::<module> (when that target exists) and
# core::testing_main, and registers it with ctest as core-cpp.<module>.

include_guard(GLOBAL)

# The exit status of a test binary whose every test case was skipped. ctest reports
# such a run as skipped (SKIP_RETURN_CODE), core::testing_main returns it
# (core/Config.hpp), and core::testing_main carries it as the target property
# CORE_CPP_SKIP_EXIT_CODE for consumers that register their own tests.
set(CORE_CPP_SKIP_EXIT_CODE 77)

# "<keyword>|<variable>": the platform-specific source list <keyword> is compiled
# when <variable> is true. BSD includes macOS, which shares its kqueue.
set(CORE_CPP_PLATFORM_BSD OFF)
if(APPLE OR BSD)
    set(CORE_CPP_PLATFORM_BSD ON)
endif()
set(CORE_CPP_PLATFORM_SOURCE_TABLE
    "SOURCES_POSIX|UNIX"
    "SOURCES_LINUX|LINUX"
    "SOURCES_BSD|CORE_CPP_PLATFORM_BSD"
    "SOURCES_WINDOWS|WIN32"
)
set(CORE_CPP_SOURCE_KEYWORDS SOURCES)
foreach(_coreCppRow IN LISTS CORE_CPP_PLATFORM_SOURCE_TABLE)
    string(REGEX REPLACE "\\|.*$" "" _coreCppKeyword "${_coreCppRow}")
    list(APPEND CORE_CPP_SOURCE_KEYWORDS ${_coreCppKeyword})
endforeach()

## @brief Sets @p outVar to the sources the parsed arguments with prefix @p prefix
## select for this platform: SOURCES plus every platform list that applies.
function(core_cpp_selected_sources prefix outVar)
    set(sources ${${prefix}_SOURCES})
    foreach(row IN LISTS CORE_CPP_PLATFORM_SOURCE_TABLE)
        string(REPLACE "|" ";" fields "${row}")
        list(GET fields 0 keyword)
        list(GET fields 1 platform)
        if(${platform})
            list(APPEND sources ${${prefix}_${keyword}})
        endif()
    endforeach()
    set(${outVar} "${sources}" PARENT_SCOPE)
endfunction()

## @brief Refuses a core::<x> in @p libs that belongs neither to @p module nor to a
## module its table row lists in DEPS.
function(core_cpp_check_layering target module libs)
    foreach(lib IN LISTS libs)
        if(NOT lib MATCHES "^core::(.+)$")
            continue()
        endif()
        set(real "core-cpp-${CMAKE_MATCH_1}")
        if(NOT TARGET ${real})
            message(FATAL_ERROR
                "${target} links ${lib}, which does not exist (yet). A module may only link modules "
                "declared above it in cmake/CoreCppModules.cmake.")
        endif()
        get_target_property(owner ${real} CORE_CPP_MODULE)
        if(NOT owner STREQUAL module AND NOT owner IN_LIST CORE_CPP_MODULE_${module}_DEPS)
            message(FATAL_ERROR
                "${target} links ${lib} from module '${owner}', which the '${module}' row of "
                "cmake/CoreCppModules.cmake does not list in DEPS (it lists: "
                "'${CORE_CPP_MODULE_${module}_DEPS}').")
        endif()
    endforeach()
endfunction()

function(core_cpp_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "KIND"
                          "HEADERS;${CORE_CPP_SOURCE_KEYWORDS};PUBLIC_LIBS;PRIVATE_LIBS")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "core_cpp_add_module(${name}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT DEFINED CORE_CPP_CURRENT_MODULE)
        message(FATAL_ERROR
            "core_cpp_add_module(${name}) is called outside a module directory. Add a core_cpp_module() "
            "row to cmake/CoreCppModules.cmake; core_cpp_add_modules() enters its directory.")
    endif()
    set(module "${CORE_CPP_CURRENT_MODULE}")
    if(name STREQUAL module AND NOT arg_KIND STREQUAL CORE_CPP_MODULE_${module}_KIND)
        message(FATAL_ERROR
            "core_cpp_add_module(${name}) says KIND ${arg_KIND}; its row in cmake/CoreCppModules.cmake "
            "says ${CORE_CPP_MODULE_${module}_KIND}.")
    endif()
    core_cpp_selected_sources(arg sources)

    set(target core-cpp-${name})
    if(arg_KIND STREQUAL "STATIC")
        if(NOT sources)
            message(FATAL_ERROR "core_cpp_add_module(${name}): a STATIC library needs sources; a header-only one is KIND INTERFACE.")
        endif()
        add_library(${target} STATIC)
        set(usage PUBLIC)
    elseif(arg_KIND STREQUAL "OBJECT")
        add_library(${target} OBJECT)
        set(usage PUBLIC)
    elseif(arg_KIND STREQUAL "INTERFACE")
        if(sources OR arg_PRIVATE_LIBS)
            message(FATAL_ERROR "core_cpp_add_module(${name}): an INTERFACE library has no sources and no private libraries.")
        endif()
        add_library(${target} INTERFACE)
        set(usage INTERFACE)
    else()
        message(FATAL_ERROR "core_cpp_add_module(${name}): KIND must be STATIC, INTERFACE or OBJECT, not '${arg_KIND}'.")
    endif()
    add_library(core::${name} ALIAS ${target})
    set_target_properties(${target} PROPERTIES CORE_CPP_MODULE "${module}" EXPORT_NAME "${name}")

    if(sources)
        target_sources(${target} PRIVATE ${sources})
    endif()
    if(arg_HEADERS)
        target_sources(${target} ${usage}
            FILE_SET HEADERS BASE_DIRS "${CORE_CPP_SOURCE_DIR}/src" FILES ${arg_HEADERS})
    endif()
    target_include_directories(${target} ${usage}
        "$<BUILD_INTERFACE:${CORE_CPP_SOURCE_DIR}/src>"
        "$<BUILD_INTERFACE:${CORE_CPP_GENERATED_INCLUDE_DIR}>")

    core_cpp_check_layering(${target} ${module} "${arg_PUBLIC_LIBS};${arg_PRIVATE_LIBS}")
    if(arg_PUBLIC_LIBS)
        target_link_libraries(${target} ${usage} ${arg_PUBLIC_LIBS})
    endif()
    if(arg_PRIVATE_LIBS)
        target_link_libraries(${target} PRIVATE ${arg_PRIVATE_LIBS})
    endif()

    if(arg_KIND STREQUAL "INTERFACE")
        target_compile_features(${target} INTERFACE cxx_std_23)
    else()
        core_cpp_apply_toolchain(${target})
    endif()
endfunction()

function(core_cpp_add_test module)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "${CORE_CPP_SOURCE_KEYWORDS};LIBS;LABELS")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "core_cpp_add_test(${module}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT CORE_CPP_TESTING)
        return()
    endif()
    core_cpp_selected_sources(arg sources)
    if(NOT sources)
        message(FATAL_ERROR "core_cpp_add_test(${module}): no test sources for this platform.")
    endif()

    set(target core-cpp-${module}-test)
    add_executable(${target} ${sources})
    set(libs ${arg_LIBS} core::testing_main)
    if(TARGET core::${module})
        list(PREPEND libs core::${module})
    endif()
    target_link_libraries(${target} PRIVATE ${libs})
    core_cpp_apply_toolchain(${target})

    # The variable rather than core::testing_main's property: a module declared before testing in
    # the table registers its test before that target exists.
    set(labels core-cpp ${module} ${arg_LABELS})
    add_test(NAME core-cpp.${module} COMMAND ${target})
    set_tests_properties(core-cpp.${module} PROPERTIES
        SKIP_RETURN_CODE ${CORE_CPP_SKIP_EXIT_CODE}
        LABELS "${labels}")
endfunction()
