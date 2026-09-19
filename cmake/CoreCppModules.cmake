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
#   core_cpp_module_target(NAME <target> MODULE <module> KIND STATIC|INTERFACE|OBJECT
#                          [DEPS <target or module>...]
#                          PLATFORMS any|native|wasm-subset [WHEN <option>])
#
# A module builds the target named after it, and may build more in the same directory
# and namespace (core::net_types and core::net_tls beside core::net). Such a target
# follows its module's row unless it has a row of its own, which it needs where that
# row does not describe it: a part that builds under Emscripten while the rest of the
# module does not, a part that needs an option the module does not, or a part that
# links less than the module does. Its PLATFORMS and WHEN then apply to it alone, and
# its KIND is checked like the module's.
#
# A row's DEPS are all that its target may link, and a row without DEPS links no
# core-cpp target at all. Each entry is another target of the same module, by its
# name (the module's own, or one with an earlier row), or a module the module's row
# lists in DEPS, whose targets it may then link; anything else is refused here. A
# target that follows its module's row links what that row lists and the module's
# other targets.
#
# core_cpp_add_modules() walks the rows in order and enters each enabled module's
# directory. That directory declares its targets with core_cpp_add_module(), which
# holds them to the row: the KIND of the target named after the module or with a row
# of its own, the DEPS every core::<x> it links must come from, and whether the
# target builds here at all.

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
    set(CORE_CPP_MODULE_${arg_NAME}_TARGETS "" PARENT_SCOPE)
endfunction()

function(core_cpp_module_target)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;MODULE;KIND;PLATFORMS;WHEN" "DEPS")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "core_cpp_module_target(${arg_NAME}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_NAME OR NOT arg_MODULE)
        message(FATAL_ERROR "core_cpp_module_target(): NAME and MODULE are required.")
    endif()
    if(NOT arg_MODULE IN_LIST CORE_CPP_MODULES)
        message(FATAL_ERROR
            "core_cpp_module_target(${arg_NAME}): MODULE names '${arg_MODULE}', which is not declared in an earlier row.")
    endif()
    if(arg_NAME IN_LIST CORE_CPP_MODULES OR DEFINED CORE_CPP_TARGET_${arg_NAME}_MODULE)
        message(FATAL_ERROR "core_cpp_module_target(${arg_NAME}): declared twice.")
    endif()
    if(NOT arg_KIND MATCHES "^(STATIC|INTERFACE|OBJECT)$")
        message(FATAL_ERROR
            "core_cpp_module_target(${arg_NAME}): KIND must be STATIC, INTERFACE or OBJECT, not '${arg_KIND}'.")
    endif()
    if(NOT arg_PLATFORMS MATCHES "^(any|native|wasm-subset)$")
        message(FATAL_ERROR
            "core_cpp_module_target(${arg_NAME}): PLATFORMS must be any, native or wasm-subset, not '${arg_PLATFORMS}'.")
    endif()
    foreach(dep IN LISTS arg_DEPS)
        if(dep STREQUAL arg_MODULE OR CORE_CPP_TARGET_${dep}_MODULE STREQUAL arg_MODULE
           OR dep IN_LIST CORE_CPP_MODULE_${arg_MODULE}_DEPS)
            continue()
        endif()
        message(FATAL_ERROR
            "core_cpp_module_target(${arg_NAME}): DEPS names '${dep}', which is neither a target of module "
            "'${arg_MODULE}' declared in an earlier row nor a module the '${arg_MODULE}' row lists in DEPS "
            "(it lists: '${CORE_CPP_MODULE_${arg_MODULE}_DEPS}').")
    endforeach()

    set(CORE_CPP_MODULE_${arg_MODULE}_TARGETS ${CORE_CPP_MODULE_${arg_MODULE}_TARGETS} ${arg_NAME} PARENT_SCOPE)
    foreach(field IN ITEMS MODULE KIND DEPS PLATFORMS WHEN)
        set(CORE_CPP_TARGET_${arg_NAME}_${field} "${arg_${field}}" PARENT_SCOPE)
    endforeach()
endfunction()

## @brief Enters the directory of every module the configuration enables, in table order.
##
## A module whose WHEN option is OFF is skipped. So is a module that builds nothing on
## this platform: one that is native-only in an Emscripten build, unless a target of it
## has a row of its own that builds there, in which case only that target is created.
## An enabled module whose DEPS include a skipped one stops the configure.
function(core_cpp_add_modules)
    set(enabled "")
    foreach(module IN LISTS CORE_CPP_MODULES)
        set(when "${CORE_CPP_MODULE_${module}_WHEN}")
        if(when AND NOT ${when})
            message(STATUS "[core-cpp] module ${module}: off (${when}=OFF)")
            continue()
        endif()
        set(only "")
        if(EMSCRIPTEN AND CORE_CPP_MODULE_${module}_PLATFORMS STREQUAL "native")
            foreach(target IN LISTS CORE_CPP_MODULE_${module}_TARGETS)
                core_cpp_row_builds("${CORE_CPP_TARGET_${target}_PLATFORMS}" "${CORE_CPP_TARGET_${target}_WHEN}" builds)
                if(builds)
                    list(APPEND only core::${target})
                endif()
            endforeach()
            if(NOT only)
                message(STATUS "[core-cpp] module ${module}: off (native only, and this is Emscripten)")
                continue()
            endif()
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
        if(only)
            list(JOIN only ", " only)
            message(STATUS "[core-cpp] module ${module}: ${only} only (the rest is native only, and this is Emscripten)")
        else()
            message(STATUS "[core-cpp] module ${module}: on")
        endif()
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
core_cpp_module(NAME async KIND INTERFACE PLATFORMS any)

# Under single-threaded Emscripten only Types, PlatformError, Clock, StringUtils, PathUtils,
# GlobMatch, FileUri and the POSIX providers build: see its SOURCES_EMSCRIPTEN.
core_cpp_module(NAME platform KIND STATIC DEPS base log PLATFORMS wasm-subset)

# contour's event loop, sockets and HTTP server, native only until Tasks B3 to B5 bring its
# WebAssembly subset. Its error vocabulary, core::net_types, is header-only, links nothing and
# builds everywhere; core::net_tls is the part that needs OpenSSL, on top of core::net.
core_cpp_module(NAME net KIND STATIC DEPS async platform PLATFORMS native)
core_cpp_module_target(NAME net_types MODULE net KIND INTERFACE PLATFORMS any)
core_cpp_module_target(NAME net_tls MODULE net KIND STATIC DEPS net PLATFORMS native WHEN CORE_CPP_WITH_TLS)
