# SPDX-License-Identifier: Apache-2.0
#
# The module table: every directory under src/core that builds a library, in
# dependency order (Part I §1).
#
#   core_cpp_module(NAME <name> [DIR <directory under src/core>] KIND STATIC|INTERFACE
#                   [DEPS <module>...] PLATFORMS any|native|wasm-subset [WHEN <option>])
#
# DIR defaults to NAME. DEPS lists the modules this one may link, and every one of
# them must appear in an earlier row. WHEN names the option that has to be ON for
# the module to build. PLATFORMS says what of the module an Emscripten build has
# (Part I §1):
#
#   any          all of it: SOURCES, plus SOURCES_EMSCRIPTEN;
#   wasm-subset  only its SOURCES_EMSCRIPTEN, which lists the subset;
#   native       none of it: the module is skipped.
#
# core_cpp_add_module() and core_cpp_add_test() apply it (cmake/CoreCppTargets.cmake).
#
# core_cpp_add_modules() walks the rows in order and enters each enabled module's
# directory. That directory declares its targets with core_cpp_add_module(), which
# holds them to the row: the KIND of the target named after the module, and the
# DEPS every core::<x> it links must come from.

include_guard(GLOBAL)

function(core_cpp_module)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;DIR;KIND;PLATFORMS;WHEN" "DEPS")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "core_cpp_module(${arg_NAME}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_NAME)
        message(FATAL_ERROR "core_cpp_module(): NAME is required.")
    endif()
    if(NOT arg_KIND MATCHES "^(STATIC|INTERFACE)$")
        message(FATAL_ERROR "core_cpp_module(${arg_NAME}): KIND must be STATIC or INTERFACE, not '${arg_KIND}'.")
    endif()
    if(NOT arg_PLATFORMS MATCHES "^(any|native|wasm-subset)$")
        message(FATAL_ERROR
            "core_cpp_module(${arg_NAME}): PLATFORMS must be any, native or wasm-subset, not '${arg_PLATFORMS}'.")
    endif()
    if(arg_NAME IN_LIST CORE_CPP_MODULES)
        message(FATAL_ERROR "core_cpp_module(${arg_NAME}): declared twice.")
    endif()
    foreach(dep IN LISTS arg_DEPS)
        if(NOT dep IN_LIST CORE_CPP_MODULES)
            message(FATAL_ERROR
                "core_cpp_module(${arg_NAME}): DEPS names '${dep}', which is not declared in an earlier row.")
        endif()
    endforeach()
    if(NOT arg_DIR)
        set(arg_DIR "${arg_NAME}")
    endif()

    set(CORE_CPP_MODULES ${CORE_CPP_MODULES} ${arg_NAME} PARENT_SCOPE)
    foreach(field IN ITEMS DIR KIND DEPS PLATFORMS WHEN)
        set(CORE_CPP_MODULE_${arg_NAME}_${field} "${arg_${field}}" PARENT_SCOPE)
    endforeach()
endfunction()

## @brief Enters the directory of every module the configuration enables, in table order.
##
## A module whose WHEN option is OFF, or which is native-only in an Emscripten build,
## is skipped. An enabled module whose DEPS include a skipped one stops the configure.
function(core_cpp_add_modules)
    set(enabled "")
    foreach(module IN LISTS CORE_CPP_MODULES)
        set(when "${CORE_CPP_MODULE_${module}_WHEN}")
        if(when AND NOT ${when})
            message(STATUS "[core-cpp] module ${module}: off (${when}=OFF)")
            continue()
        endif()
        if(EMSCRIPTEN AND CORE_CPP_MODULE_${module}_PLATFORMS STREQUAL "native")
            message(STATUS "[core-cpp] module ${module}: off (native only, and this is Emscripten)")
            continue()
        endif()
        foreach(dep IN LISTS CORE_CPP_MODULE_${module}_DEPS)
            if(NOT dep IN_LIST enabled)
                message(FATAL_ERROR
                    "[core-cpp] module ${module} is enabled but depends on module ${dep}, which is not. "
                    "Enable ${dep} (see its row in cmake/CoreCppModules.cmake) or turn ${module} off.")
            endif()
        endforeach()

        set(CORE_CPP_CURRENT_MODULE "${module}")
        add_subdirectory("${CORE_CPP_SOURCE_DIR}/src/core/${CORE_CPP_MODULE_${module}_DIR}"
                         "${CORE_CPP_BINARY_DIR}/src/core/${CORE_CPP_MODULE_${module}_DIR}")
        list(APPEND enabled ${module})
        message(STATUS "[core-cpp] module ${module}: on")
    endforeach()
endfunction()

# --- the table -----------------------------------------------------------------

# The headers directly in src/core/ are core::base, namespace core.
core_cpp_module(NAME base DIR . KIND STATIC PLATFORMS any)
core_cpp_module(NAME log KIND STATIC DEPS base PLATFORMS any)
core_cpp_module(NAME cli KIND STATIC DEPS base log PLATFORMS any)

# No WHEN: core::testing needs no test framework and consumers use it with CORE_CPP_TESTING off.
# Only core::testing_main needs Catch2, and CORE_CPP_CATCH2_MAIN gates that one target; it links
# log to apply the LOG filter.
core_cpp_module(NAME testing KIND STATIC DEPS base log PLATFORMS any)

# Header-only, and needing nothing but the standard library.
core_cpp_module(NAME coro KIND INTERFACE PLATFORMS any)

# Under single-threaded Emscripten only Types, PlatformError, Clock, StringUtils, PathUtils,
# GlobMatch and FileUri build: see its SOURCES_EMSCRIPTEN.
core_cpp_module(NAME platform KIND STATIC DEPS base log coro PLATFORMS wasm-subset)
