# SPDX-License-Identifier: Apache-2.0
#
# The dependency table of Part I §3, and how each row is resolved.
#
#   core_cpp_dependency(<name> WHEN <condition>... TARGETS <target>...
#                       [FIND_PACKAGE <find_package() arguments>...]
#                       [CPM <CPMAddPackage() arguments>... | NO_FETCH]
#                       [WRAP <function>])
#
# WHEN is an if() condition over options, e.g. `CORE_CPP_TESTING OR CORE_CPP_CATCH2_MAIN`.
# core_cpp_resolve_dependencies() resolves every row whose condition holds, stopping at
# the first step that provides all of TARGETS:
#
#   1. the parent project already defines them;
#   2. find_package(<FIND_PACKAGE arguments> QUIET);
#   3. CPMAddPackage(<CPM arguments>), only with CORE_CPP_FETCH_DEPS=ON and never for
#      a NO_FETCH row;
#   4. otherwise a FATAL_ERROR that names the condition that needed the dependency.
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
    cmake_parse_arguments(PARSE_ARGV 1 arg "NO_FETCH" "WRAP" "WHEN;TARGETS;FIND_PACKAGE;CPM")
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
    list(JOIN CORE_CPP_DEPENDENCY_${name}_WHEN " " when)
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

    set(needs "${name} is needed because `${when}` holds")
    if(CORE_CPP_DEPENDENCY_${name}_NO_FETCH)
        message(FATAL_ERROR
            "${needs}, but ${findCall} did not provide ${targets}, and it is never fetched. "
            "Install it, or make `${when}` false.")
    endif()
    if(NOT CORE_CPP_FETCH_DEPS)
        message(FATAL_ERROR
            "${needs}, but neither the parent project nor ${findCall} provides ${targets}, and "
            "CORE_CPP_FETCH_DEPS is OFF. Provide it, set CORE_CPP_FETCH_DEPS=ON, or make `${when}` false.")
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

## @brief Resolves every row of the table whose WHEN condition holds, in table order.
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

## @brief WRAP of the Catch2 row: a Catch2 that core-cpp built from source is compiled as C++23.
##
## Catch2 compiles parts of itself only when the standard has them: the
## StringMaker<std::string_view> specialization needs C++17. Built at the compiler's default,
## which is C++14 on MSVC, it lacks them, and a C++23 test that CHECKs a string_view fails to
## link (LNK2019). A top-level build sets CMAKE_CXX_STANDARD, but core-cpp may not set that for a
## parent, so the standard goes on the targets core-cpp created. It sets properties, not CPM
## OPTIONS, which would write a cache entry into the parent's cache. An IMPORTED Catch2 (from
## find_package) was built by someone else and is left alone; one the parent provided never
## reaches a WRAP.
function(core_cpp_catch2_standard name)
    foreach(alias IN ITEMS Catch2::Catch2 Catch2::Catch2WithMain)
        if(NOT TARGET ${alias})
            continue()
        endif()
        get_target_property(imported ${alias} IMPORTED)
        if(imported)
            continue()
        endif()
        get_target_property(real ${alias} ALIASED_TARGET)
        if(NOT real)
            set(real ${alias})
        endif()
        set_target_properties(${real} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    endforeach()
endfunction()

# Catch2 for core-cpp's own tests and for core::testing_main, which consumers link to their tests.
core_cpp_dependency(Catch2
    WHEN CORE_CPP_TESTING OR CORE_CPP_CATCH2_MAIN
    TARGETS Catch2::Catch2
    FIND_PACKAGE Catch2 3.8
    CPM NAME Catch2 VERSION 3.8.0 GITHUB_REPOSITORY catchorg/Catch2 EXCLUDE_FROM_ALL YES SYSTEM YES
    WRAP core_cpp_catch2_standard)

# OpenSSL, for core::net_tls: taken from the system, never fetched (Part I §3). It is linked PRIVATE
# and no OpenSSL type appears in a core-cpp header, so what a consumer's own code sees of OpenSSL is
# whatever that consumer includes itself.
core_cpp_dependency(OpenSSL
    WHEN CORE_CPP_WITH_TLS
    TARGETS OpenSSL::SSL OpenSSL::Crypto
    FIND_PACKAGE OpenSSL
    NO_FETCH)

# Tracy, when core-cpp is instrumented for it: core::base links the client PUBLIC and
# <core/Profiling.hpp> includes its header. The version is the one contour's cmake/Tracy.cmake
# pins (6777ff05), because a client and the profiler that reads its captures must match. A fetched
# client is built as contour builds it: TRACY_ENABLE, which is off upstream and would compile the
# client away to nothing, and TRACY_ONLY_LOCALHOST, which keeps the listening socket and the
# client's announcement on this machine. Those are Tracy's options, set only when core-cpp fetches it.
core_cpp_dependency(Tracy
    WHEN CORE_CPP_WITH_TRACY
    TARGETS Tracy::TracyClient
    FIND_PACKAGE Tracy 0.14.1
    CPM NAME tracy VERSION 0.14.1 GITHUB_REPOSITORY wolfpld/tracy GIT_TAG v0.14.1
        EXCLUDE_FROM_ALL YES SYSTEM YES
        OPTIONS "TRACY_ENABLE ON" "TRACY_STATIC ON" "TRACY_ONLY_LOCALHOST ON")
