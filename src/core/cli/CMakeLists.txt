# SPDX-License-Identifier: Apache-2.0

# core::cli: the command-line parser and the application scaffold built on it.
core_cpp_add_module(cli KIND STATIC
    HEADERS App.hpp CLI.hpp
    SOURCES App.cpp CLI.cpp
    PUBLIC_LIBS core::base core::log)

core_cpp_add_test(cli SOURCES App_test.cpp CLI_test.cpp)
