# SPDX-License-Identifier: Apache-2.0

# Test helpers that need no test framework, built wherever core-cpp is, the WebAssembly subset
# included: the Windows dialog suppression, a no-op everywhere else, and the test doubles of
# core::base's interfaces (Environment.hpp).
core_cpp_add_module(testing KIND STATIC
    HEADERS Environment.hpp SuppressWindowsDialogs.hpp
    SOURCES SuppressWindowsDialogs.cpp
    PUBLIC_LIBS core::base)

# An OBJECT library, because its one translation unit is referenced by nothing: in a static library
# the linker would drop it. Linking this adds its object to the executable itself.
core_cpp_add_module(testing_dialogs KIND OBJECT
    SOURCES SuppressWindowsDialogsAtStartup.cpp
    PUBLIC_LIBS core::testing)

# main() for every test binary: Catch2 with core-cpp's exit-code contract. The only part of this
# module that needs Catch2, so the only part CORE_CPP_CATCH2_MAIN gates.
if(CORE_CPP_CATCH2_MAIN)
    core_cpp_add_module(testing_main KIND STATIC
        HEADERS ExitCode.hpp
        SOURCES CatchMain.cpp ExitCode.cpp
        PUBLIC_LIBS core::testing Catch2::Catch2)
    set_target_properties(core-cpp-testing_main PROPERTIES CORE_CPP_SKIP_EXIT_CODE ${CORE_CPP_SKIP_EXIT_CODE})

    # The dialog suppression's object goes onto the link line of every executable that links
    # core::testing_main, as an interface link item. Inside this archive nothing would pull it in.
    target_link_libraries(core-cpp-testing_main INTERFACE
        core::testing_dialogs "$<TARGET_OBJECTS:core-cpp-testing_dialogs>")
endif()

core_cpp_add_test(testing SOURCES ExitCode_test.cpp)
