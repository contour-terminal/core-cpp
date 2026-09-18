# SPDX-License-Identifier: Apache-2.0

# core::log: categorised logging, its sinks, and the contract checks that report through it.
core_cpp_add_module(log KIND STATIC
    HEADERS Assert.hpp LogSink.hpp LogStore.hpp
    SOURCES LogSink.cpp LogStore.cpp
    PUBLIC_LIBS core::base)

core_cpp_add_test(log SOURCES LogSink_test.cpp)
