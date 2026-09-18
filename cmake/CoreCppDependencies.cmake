# SPDX-License-Identifier: Apache-2.0
#
# The dependency table of Part I §3, and how each row is resolved.
#
#   core_cpp_dependency(<name> WHEN <variable> TARGETS <target>...
#                       [FIND_PACKAGE <find_package() arguments>...]
#                       [CPM <CPMAddPackage() arguments>... | NO_FETCH]
#                       [WRAP <function>])
#
# core_cpp_resolve_dependencies() resolves every row whose WHEN variable is true,
# stopping at the first step that provides all of TARGETS:
#
#   1. the parent project already defines them;
#   2. find_package(<FIND_PACKAGE arguments> QUIET);
#   3. CPMAddPackage(<CPM arguments>), only with CORE_CPP_FETCH_DEPS=ON and never for
#      a NO_FETCH row;
#   4. otherwise a FATAL_ERROR that names the option that needed the dependency.
#
# WRAP names a function that runs after find_package() or CPM has provided the
# dependency and turns what it provides into TARGETS, for example an INTERFACE
# target over a DOWNLOAD_ONLY source tree. It is called with the dependency's name
# and sees <name>_SOURCE_DIR.
#
# The table is the whole list: adding a dependency takes an option, a row here and a
# CHANGELOG entry. Rows are added by the task that first needs them, so a configure
# never fetches what nothing links.

include_guard(GLOBAL)

function(core_cpp_dependency name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "NO_FETCH" "WHEN;WRAP" "TARGETS;FIND_PACKAGE;CPM")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "core_cpp_dependency(${name}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_WHEN OR NOT arg_TARGETS)
        message(FATAL_ERROR "core_cpp_dependency(${name}): WHEN and TARGETS are required.")
    endif()
    if(arg_NO_FETCH AND arg_CPM)
        message(FATAL_ERROR "core_cpp_dependency(${name}): NO_FETCH and CPM contradict each other.")
    endif()
    if(NOT arg_NO_FETCH AND NOT arg_CPM)
        message(FATAL_ERROR "core_cpp_dependency(${name}): say how to fetch it (CPM ...) or that it never is (NO_FETCH).")
    endif()
    if(name IN_LIST CORE_CPP_DEPENDENCIES)
        message(FATAL_ERROR "core_cpp_dependency(${name}): declared twice.")
    endif()
    set(CORE_CPP_DEPENDENCIES ${CORE_CPP_DEPENDENCIES} ${name} PARENT_SCOPE)
    foreach(field IN ITEMS WHEN WRAP TARGETS FIND_PACKAGE CPM NO_FETCH)
        set(CORE_CPP_DEPENDENCY_${name}_${field} "${arg_${field}}" PARENT_SCOPE)
    endforeach()
endfunction()

## @brief TRUE in @p outVar when every target of dependency @p name exists.
function(core_cpp_dependency_present name outVar)
    set(present TRUE)
    foreach(target IN LISTS CORE_CPP_DEPENDENCY_${name}_TARGETS)
        if(NOT TARGET ${target})
            set(present FALSE)
        endif()
    endforeach()
    set(${outVar} ${present} PARENT_SCOPE)
endfunction()

## @brief Resolves dependency @p name, or stops the configure saying why it cannot.
##
## A function rather than inline code, so that what find_package() and CPM leave
## behind stays local to it. Imported targets are directory-scoped, and fetched
## ones are global, so both outlive the call.
function(core_cpp_resolve_dependency name)
    set(when "${CORE_CPP_DEPENDENCY_${name}_WHEN}")
    set(wrap "${CORE_CPP_DEPENDENCY_${name}_WRAP}")
    set(targets "${CORE_CPP_DEPENDENCY_${name}_TARGETS}")

    core_cpp_dependency_present(${name} present)
    if(present)
        message(STATUS "[core-cpp] ${name}: from the parent project (${targets})")
        return()
    endif()

    set(findArgs ${CORE_CPP_DEPENDENCY_${name}_FIND_PACKAGE})
    list(JOIN findArgs " " findCall)
    set(findCall "find_package(${findCall})")
    if(findArgs)
        list(GET findArgs 0 package)
        find_package(${findArgs} QUIET)
        if(${package}_FOUND)
            if(wrap)
                cmake_language(CALL ${wrap} ${name})
            endif()
            core_cpp_dependency_present(${name} present)
            if(present)
                set(version "")
                if(${package}_VERSION)
                    set(version ", version ${${package}_VERSION}")
                endif()
                message(STATUS "[core-cpp] ${name}: found by ${findCall}${version}")
                return()
            endif()
        endif()
    endif()

    set(needs "${name} is needed because ${when} is ON")
    if(CORE_CPP_DEPENDENCY_${name}_NO_FETCH)
        message(FATAL_ERROR
            "${needs}, but ${findCall} did not provide ${targets}, and it is never fetched. "
            "Install it, or turn ${when} off.")
    endif()
    if(NOT CORE_CPP_FETCH_DEPS)
        message(FATAL_ERROR
            "${needs}, but neither the parent project nor ${findCall} provides ${targets}, and "
            "CORE_CPP_FETCH_DEPS is OFF. Provide it, set CORE_CPP_FETCH_DEPS=ON, or turn ${when} off.")
    endif()

    if(NOT COMMAND CPMAddPackage)
        include("${CORE_CPP_SOURCE_DIR}/cmake/CPM.cmake")
    endif()
    CPMAddPackage(${CORE_CPP_DEPENDENCY_${name}_CPM})
    if(wrap)
        cmake_language(CALL ${wrap} ${name})
    endif()
    core_cpp_dependency_present(${name} present)
    if(NOT present)
        message(FATAL_ERROR "[core-cpp] ${name}: CPM fetched it, but it did not provide ${targets}.")
    endif()
    list(JOIN CORE_CPP_DEPENDENCY_${name}_CPM " " printable)
    message(STATUS "[core-cpp] ${name}: fetched by CPMAddPackage(${printable})")
endfunction()

## @brief Resolves every row of the table whose WHEN variable is true, in table order.
function(core_cpp_resolve_dependencies)
    foreach(name IN LISTS CORE_CPP_DEPENDENCIES)
        if(${CORE_CPP_DEPENDENCY_${name}_WHEN})
            core_cpp_resolve_dependency(${name})
        endif()
    endforeach()
endfunction()

# --- the table ---------------------------------------------------------------

# Threads, except under single-threaded Emscripten: linking it there would force
# -pthread, and with it SharedArrayBuffer, onto every consumer.
set(CORE_CPP_USE_THREADS ON)
if(CORE_CPP_SINGLE_THREADED_WASM)
    set(CORE_CPP_USE_THREADS OFF)
endif()
set(THREADS_PREFER_PTHREAD_FLAG ON)
core_cpp_dependency(Threads
    WHEN CORE_CPP_USE_THREADS
    TARGETS Threads::Threads
    FIND_PACKAGE Threads
    NO_FETCH)

core_cpp_dependency(Catch2
    WHEN CORE_CPP_TESTING
    TARGETS Catch2::Catch2
    FIND_PACKAGE Catch2 3.8
    CPM NAME Catch2 VERSION 3.8.0 GITHUB_REPOSITORY catchorg/Catch2 EXCLUDE_FROM_ALL YES SYSTEM YES)
