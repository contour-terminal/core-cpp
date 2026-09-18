# SPDX-License-Identifier: Apache-2.0

# An OBJECT library, because its one translation unit is referenced by nothing: in a static library
# the linker would drop it. Linking this adds its object to the executable itself.
core_cpp_add_module(testing_dialogs KIND OBJECT
    SOURCES SuppressWindowsDialogsAtStartup.cpp)

core_cpp_add_module(testing KIND STATIC
    HEADERS ExitCode.hpp SuppressWindowsDialogs.hpp
    SOURCES ExitCode.cpp
    PUBLIC_LIBS Catch2::Catch2)

# main() for every test binary: Catch2 with core-cpp's exit-code contract.
core_cpp_add_module(testing_main KIND STATIC
    SOURCES CatchMain.cpp
    PUBLIC_LIBS core::testing)
set_target_properties(core-cpp-testing_main PROPERTIES CORE_CPP_SKIP_EXIT_CODE ${CORE_CPP_SKIP_EXIT_CODE})

# The dialog suppression's object goes onto the link line of every executable that links
# core::testing_main, as an interface link item. Inside this archive nothing would pull it in.
target_link_libraries(core-cpp-testing_main INTERFACE
    core::testing_dialogs "$<TARGET_OBJECTS:core-cpp-testing_dialogs>")

core_cpp_add_test(testing SOURCES ExitCode_test.cpp)
