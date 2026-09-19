# SPDX-License-Identifier: Apache-2.0

# core::tui_output: the leaf of endo's terminal UI (endo f774a210), the part that composes escape
# sequences and writes them. Its row in cmake/CoreCppModules.cmake says DEPS base, so the layering
# check refuses a link to libunicode's users, to the coroutines or to core::platform here. That is
# the point of the split: a program that only prints styled text and progress (Lightweight's
# dbtool) links this and nothing else.
#
# The module's root holds only platform-independent code. What goes through the operating system --
# the write, the terminal's size, the XTVERSION probe, the terminal test -- is in posix/ and
# windows/, and the source lists below choose it, so no file there guards itself with an #ifdef of
# its platform. In endo those two files each carried a full copy of the byte composition as well
# (`platform/TerminalOutput.cpp`, `platform/TerminalOutputWin32.cpp`); here it is shared, and what
# is left is only what actually differs.
core_cpp_add_module(tui_output KIND STATIC
    HEADERS
        CursorShape.hpp
        Error.hpp
        SgrBuilder.hpp
        TerminalOutput.hpp
        TerminalProtocols.hpp
    SOURCES
        SgrBuilder.cpp
        TerminalOutput.cpp
        detail/XtVersion.hpp
    SOURCES_POSIX
        posix/PosixIO.hpp
        posix/TerminalOutput.cpp
    SOURCES_WINDOWS
        windows/TerminalOutput.cpp
        windows/Win32Utf.hpp
    PRIVATE_LIBS core::base)

# Image decoding is the one part with an option of its own: stb is fetched only with
# CORE_CPP_WITH_IMAGES, so the files that decode or scale an image are built only there. What stays
# either way is the ImageProvider interface, which MarkdownRenderer takes by reference; what goes
# is its filesystem implementation and the loader under it.
set(_coreCppTuiImageHeaders "")
set(_coreCppTuiImageSources "")
set(_coreCppTuiImagePosix "")
set(_coreCppTuiImageWindows "")
set(_coreCppTuiImageTests "")
set(_coreCppTuiImageLibs "")
if(CORE_CPP_WITH_IMAGES)
    set(_coreCppTuiImageHeaders FilesystemImageProvider.hpp ImageLoader.hpp)
    set(_coreCppTuiImageSources FilesystemImageProvider.cpp ImageLoader.cpp StbImageImpl.cpp)
    set(_coreCppTuiImagePosix posix/ImageLoader.cpp)
    set(_coreCppTuiImageWindows windows/ImageLoader.cpp)
    set(_coreCppTuiImageTests ImageLoader_test.cpp MarkdownImage_test.cpp)
    set(_coreCppTuiImageLibs stb_image)

    # StbImageImpl.cpp is the one translation unit that compiles stb's implementations. It contains
    # no code of core-cpp's, so core-cpp's warning set says nothing about it and its findings are
    # not core-cpp's to fix: the file is compiled with warnings off rather than annotated with
    # pragmas, which .agent/rules/cpp-guidelines.md does not allow and which would belong in stb's
    # sources anyway. -fno-sanitize=undefined goes with it, because stb_image_resize2's SIMD paths
    # trip UBSan's alignment and integer checks (endo carries the same option at f774a210). Both are
    # PRIVATE, per-source options: neither reaches another file, let alone a consumer.
    #
    # Only for GCC, Clang and clang-cl. cl has nothing to say about stb (endo compiles it at /W4
    # /WX without an exemption), and adding /w there would only earn a D9025 for overriding /W4.
    set(_coreCppStbOptions "")
    if(CORE_CPP_GCC_OR_CLANG)
        list(APPEND _coreCppStbOptions -w -fno-sanitize=undefined)
    endif()
    if(_coreCppStbOptions)
        set_source_files_properties(StbImageImpl.cpp PROPERTIES COMPILE_OPTIONS "${_coreCppStbOptions}")
    endif()
endif()

# core::tui: the rest of endo's terminal UI. Native only -- there is no terminal under Emscripten,
# where CORE_CPP_WITH_TUI is forced off -- and, as above, its root holds nothing platform-specific:
# posix/ has the termios raw mode, the SIGWINCH self-pipe and the clipboard tools, windows/ the
# console modes, the input records and the resize event.
#
# runtime/ keeps its own EventSource and PollEventSource for now. Task B12 moves the runtime onto
# core::net::EventLoop and deletes them, so they are imported as they stand rather than split, and
# runtime/PollEventSource.cpp is the one file here that still chooses its platform with an #ifdef.
core_cpp_add_module(tui KIND STATIC
    HEADERS
        ${_coreCppTuiImageHeaders}
        Box.hpp
        Buffer.hpp
        Canvas.hpp
        Cell.hpp
        CommandPalettePopup.hpp
        CommandRegistry.hpp
        CompletionPopup.hpp
        Component.hpp
        Dialog.hpp
        EditAction.hpp
        Element.hpp
        FuzzyPickerPopup.hpp
        GenericSyntaxHighlighter.hpp
        GhostTextHelper.hpp
        HoverState.hpp
        HyperlinkEmitter.hpp
        ImageProvider.hpp
        InputEvent.hpp
        InputField.hpp
        KeyBindings.hpp
        KeyCode.hpp
        List.hpp
        LogPanel.hpp
        MarkdownHtml.hpp
        MarkdownInline.hpp
        MarkdownRenderer.hpp
        MarkdownTable.hpp
        MockTerminalOutput.hpp
        Modifier.hpp
        PopupKeyDispatch.hpp
        QuestionComponent.hpp
        Rect.hpp
        Screen.hpp
        ScrollableSelection.hpp
        SemanticBlockClient.hpp
        Sixel.hpp
        Spinner.hpp
        StatusBar.hpp
        StyledText.hpp
        Terminal.hpp
        TerminalInput.hpp
        TestHelpers.hpp
        Text.hpp
        TextDecorator.hpp
        Theme.hpp
        TimerUtils.hpp
        Tooltip.hpp
        TreeTableView.hpp
        Unicode.hpp
        VtParser.hpp
        completer/Completer.hpp
        completer/CompletionItem.hpp
        completer/CompletionProvider.hpp
        completer/FuzzyMatch.hpp
        completer/SmartCaseMatch.hpp
        runtime/EventSource.hpp
        runtime/Modal.hpp
        runtime/ModalComponent.hpp
        runtime/PollEventSource.hpp
        runtime/TerminalEventSource.hpp
        runtime/TuiRuntime.hpp
        runtime/WithTimeout.hpp
        runtime/testing/MockEventSource.hpp
    SOURCES
        ${_coreCppTuiImageSources}
        Box.cpp
        Buffer.cpp
        Canvas.cpp
        Cell.cpp
        CommandPalettePopup.cpp
        CommandRegistry.cpp
        CompletionPopup.cpp
        Component.cpp
        Dialog.cpp
        FuzzyPickerPopup.cpp
        GenericSyntaxHighlighter.cpp
        HoverState.cpp
        HyperlinkEmitter.cpp
        InputField.cpp
        KeyBindings.cpp
        List.cpp
        LogPanel.cpp
        MarkdownHtml.cpp
        MarkdownRenderer.cpp
        MarkdownTable.cpp
        MockTerminalOutput.cpp
        QuestionComponent.cpp
        Screen.cpp
        SemanticBlockClient.cpp
        Sixel.cpp
        Spinner.cpp
        StatusBar.cpp
        StyledText.cpp
        Terminal.cpp
        TerminalInput.cpp
        Text.cpp
        Theme.cpp
        Tooltip.cpp
        TreeTableView.cpp
        Unicode.cpp
        VtParser.cpp
        completer/Completer.cpp
        completer/FuzzyMatch.cpp
        completer/SmartCaseMatch.cpp
        runtime/PollEventSource.cpp
        runtime/TuiRuntime.cpp
    SOURCES_POSIX
        ${_coreCppTuiImagePosix}
        posix/Terminal.cpp
        posix/TerminalInput.cpp
        runtime/posix/PollHelpers.hpp
        runtime/posix/TerminalEventSource.cpp
    SOURCES_WINDOWS
        ${_coreCppTuiImageWindows}
        runtime/windows/TerminalEventSource.cpp
        windows/Terminal.cpp
        windows/TerminalInput.cpp
    PUBLIC_LIBS core::base core::tui_output core::platform core::async unicode::unicode
    PRIVATE_LIBS ${_coreCppTuiImageLibs})

core_cpp_add_test(tui NAME tui_output
    SOURCES
        TerminalOutput_test.cpp
        TerminalProtocols_test.cpp)

# One binary for the module. windows/TerminalInput_test.cpp is Windows-only: it drives the console
# API directly, and serialises its cases on a named mutex because they share the one console the
# test process has.
core_cpp_add_test(tui
    SOURCES
        ${_coreCppTuiImageTests}
        CommandPalettePopup_test.cpp
        CommandRegistry_test.cpp
        Completer_test.cpp
        CompletionPopup_test.cpp
        FuzzyMatch_test.cpp
        FuzzyPickerPopup_test.cpp
        GenericSyntaxHighlighter_test.cpp
        InputField_test.cpp
        KeyBindings_test.cpp
        MarkdownHtml_test.cpp
        MarkdownRenderer_test.cpp
        MarkdownTable_test.cpp
        PopupKeyDispatch_test.cpp
        QuestionComponent_test.cpp
        Renderer_test.cpp
        Screen_test.cpp
        ScrollableSelection_test.cpp
        SemanticBlockClient_test.cpp
        Sixel_test.cpp
        SmartCaseMatch_test.cpp
        StyledText_test.cpp
        TerminalQuery_test.cpp
        TreeTableView_test.cpp
        Unicode_test.cpp
        VtParser_test.cpp
        runtime/Modal_test.cpp
        runtime/TuiRuntime_test.cpp
    SOURCES_WINDOWS
        windows/TerminalInput_test.cpp)
