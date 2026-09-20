# SPDX-License-Identifier: Apache-2.0
#
# Refuses what the design spec (Part I §3) and the Global Constraints forbid in core-cpp's CMake and
# C++ sources: global state outside cmake/CoreCppTopLevel.cmake, unprefixed options, cache variables
# and functions, untyped libraries, PUBLIC flags, globbed sources, modules included by name, NOLINT,
# diagnostic-muting pragmas, C-style index loops, a missing SPDX header, and a source under src/core/
# whose first namespace is not the one its directory names.
#
# The rules are a data table: what is refused, in which kind of file, and the reason printed with each
# refusal. The allowlist is a second table of {rule, file, reason}, and it is the only way past a
# rule. An allowlist row whose file no longer contains what it allows is refused as stale, so an
# exemption cannot outlive its reason. check-cmake-hygiene-selftest.cmake proves each rule.
#
# Usage: cmake -DROOT=<source tree> -P tests/cmake/check-cmake-hygiene.cmake
#        cmake -DLIST_RULES=ON -P tests/cmake/check-cmake-hygiene.cmake

cmake_minimum_required(VERSION 3.25)

set(CORE_CPP_HYGIENE_RULES "")

## @brief Declares a rule: a line of a file of one of the KIND list (cmake, cpp) that matches REGEX and
## not EXCEPT is refused with REASON. FIRST_LINE applies the rule to a file's first line only.
function(core_cpp_hygiene_rule name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "FIRST_LINE" "REGEX;EXCEPT;REASON" "KIND")
    set(CORE_CPP_HYGIENE_RULES ${CORE_CPP_HYGIENE_RULES} ${name} PARENT_SCOPE)
    foreach(field IN ITEMS FIRST_LINE KIND REGEX EXCEPT REASON)
        set(CORE_CPP_HYGIENE_${name}_${field} "${arg_${field}}" PARENT_SCOPE)
    endforeach()
endfunction()

set(CORE_CPP_HYGIENE_ALLOWLIST "")

## @brief Allows rule @p rule in @p file (relative to ROOT), for @p reason.
function(core_cpp_hygiene_allow rule file reason)
    list(LENGTH CORE_CPP_HYGIENE_ALLOWLIST index)
    set(CORE_CPP_HYGIENE_ALLOWLIST ${CORE_CPP_HYGIENE_ALLOWLIST} ${index} PARENT_SCOPE)
    set(CORE_CPP_HYGIENE_ALLOW_${index}_RULE "${rule}" PARENT_SCOPE)
    set(CORE_CPP_HYGIENE_ALLOW_${index}_FILE "${file}" PARENT_SCOPE)
    set(CORE_CPP_HYGIENE_ALLOW_${index}_REASON "${reason}" PARENT_SCOPE)
endfunction()

# --- the rules ---------------------------------------------------------------------------------------

core_cpp_hygiene_rule(missing-spdx KIND cmake cpp FIRST_LINE
    REGEX "^"
    EXCEPT "^(#|//) SPDX-License-Identifier: Apache-2\\.0$"
    REASON "every source file starts with its SPDX license identifier")
core_cpp_hygiene_rule(unprefixed-option KIND cmake
    REGEX "^[ \t]*(cmake_dependent_)?option[ \t]*\\("
    EXCEPT "^[ \t]*(cmake_dependent_)?option[ \t]*\\([ \t]*CORE_CPP_"
    REASON "an option is CORE_CPP_-prefixed (Part I §3)")
core_cpp_hygiene_rule(unprefixed-cache-variable KIND cmake
    REGEX "^[ \t]*set[ \t]*\\([ \t]*[A-Za-z0-9_]+[ \t].*[ \t]CACHE([ \t]|$)"
    EXCEPT "^[ \t]*set[ \t]*\\([ \t]*CORE_CPP_"
    REASON "a cache variable is CORE_CPP_-prefixed (Part I §3)")
core_cpp_hygiene_rule(unprefixed-function KIND cmake
    REGEX "^[ \t]*(function|macro)[ \t]*\\("
    EXCEPT "^[ \t]*(function|macro)[ \t]*\\([ \t]*core_cpp_"
    REASON "a function or macro is core_cpp_-prefixed (Part I §3)")
core_cpp_hygiene_rule(global-compile-options KIND cmake
    REGEX "^[ \t]*(add_compile_options|add_compile_definitions|add_definitions|remove_definitions|add_link_options|include_directories|link_directories|link_libraries)[ \t]*\\("
    REASON "directory-wide flags reach every target of a consumer; set them per target, PRIVATE")
core_cpp_hygiene_rule(global-cmake-variable KIND cmake
    REGEX "^[ \t]*((set|unset)[ \t]*\\([ \t]*CMAKE_|(string|list)[ \t]*\\([ \t]*[A-Z_]+[ \t]+CMAKE_)"
    REASON "CMAKE_* variables are global state, which only cmake/CoreCppTopLevel.cmake may touch")
core_cpp_hygiene_rule(process-environment KIND cmake
    REGEX "^[ \t]*(set|unset)[ \t]*\\([ \t]*ENV\\{"
    REASON "the environment is process-wide state, which only cmake/CoreCppTopLevel.cmake may touch")
core_cpp_hygiene_rule(top-level-only-include KIND cmake
    REGEX "^[ \t]*include[ \t]*\\(.*(CompileCache|FetchTransferBound)\\.cmake"
    REASON "the compiler cache and the fetch bound change the whole configure, so only cmake/CoreCppTopLevel.cmake includes them")
core_cpp_hygiene_rule(untyped-library KIND cmake
    REGEX "^[ \t]*add_library[ \t]*\\("
    EXCEPT "^[ \t]*add_library[ \t]*\\([ \t]*[^ \t)]+[ \t]+(STATIC|SHARED|MODULE|OBJECT|INTERFACE|ALIAS|IMPORTED|UNKNOWN)([ \t)]|$)"
    REASON "a library's type is always explicit (Part I §1)")
core_cpp_hygiene_rule(public-flags KIND cmake
    REGEX "^[ \t]*target_(compile_options|compile_definitions|link_options)[ \t]*\\([ \t]*[^ \t)]+[ \t]+(PUBLIC|INTERFACE)"
    REASON "no PUBLIC or INTERFACE compile or link flags: they would change every consumer's flags")
core_cpp_hygiene_rule(source-glob KIND cmake
    REGEX "^[ \t]*file[ \t]*\\([ \t]*GLOB"
    REASON "source lists are explicit; a glob misses a new file until the next configure")
core_cpp_hygiene_rule(include-by-name KIND cmake
    REGEX "^[ \t]*include[ \t]*\\([ \t]*[A-Za-z]"
    EXCEPT "^[ \t]*include[ \t]*\\([ \t]*(CheckCXXCompilerFlag|CheckCXXSourceCompiles|CheckCompilerFlag|CheckLinkerFlag|CMakeDependentOption|CMakePackageConfigHelpers|FetchContent|GNUInstallDirs)[ \t]*\\)"
    REASON "core-cpp's own modules are included by absolute path, never through CMAKE_MODULE_PATH")
core_cpp_hygiene_rule(nolint KIND cpp
    REGEX "NOLINT"
    REASON "no NOLINT: fix the finding, or give the check a documented exception in .clang-tidy")
core_cpp_hygiene_rule(diagnostic-pragma KIND cpp
    REGEX "(^[ \t]*#[ \t]*pragma[ \t]+(warning|clang[ \t]+diagnostic|GCC[ \t]+diagnostic|diag_suppress))|_Pragma[ \t]*\\([ \t]*\"(warning|clang[ \t]+diagnostic|GCC[ \t]+diagnostic)"
    REASON "no diagnostic-muting pragmas outside an allowlist row with a reason")
core_cpp_hygiene_rule(c-style-for KIND cpp
    REGEX "(^|[^A-Za-z0-9_])for[ \t]*\\([^;]*;[^;]*;"
    REASON "no C-style for(;;) loops: use a range-for over a range, e.g. std::views::iota")

# A rule over a file rather than a line: under src/core/, the first named namespace a C++ source
# declares is the one its directory PATH names, every segment of it. src/core/<dir>/... declares
# core::<dir>, src/core/<dir>/testing/... declares core::<dir>::testing, and each declares it that
# way -- or a namespace nested in it (core::<dir>::detail) -- spelled in one piece, not as
# `namespace core { namespace <dir>`. A file directly in src/core/ declares core, or a namespace
# nested in it (core::base64).
#
# The platform and private-detail directories are layout rather than namespace, and are skipped
# when the expected namespace is assembled: they are exactly the directories core_cpp_add_module()
# holds private headers in (cmake/CoreCppTargets.cmake), and nothing else -- an entry for a
# directory that does not exist reads later as permission to create one. src/core/net/posix/ is therefore
# core::net, src/core/tui/runtime/posix/ is core::tui::runtime, and src/core/net/detail/ is
# core::net or core::net::detail. Every other segment is a namespace of its own: testing/ is
# ::testing, runtime/ is ::runtime. Taking only the FIRST segment, as this rule did, is how a file
# in src/core/platform/testing/ declaring core::platform passed clean.
#
# Helper namespaces inside the first are free in their name, but not in their case: EVERY named
# namespace the file declares, not only the first, has every segment lowercase
# (readability-identifier-naming.NamespaceCase in .clang-tidy says the same). A file that declares
# no named namespace (a main(), a file of TU-local helpers, a header of macros) is not checked, and
# neither is a namespace alias.
#
# Known limitation (core-cpp#23): "first" is taken literally, so a leading forward-declaration
# block for another module's type -- `namespace core::platform { class Wakeup; }` at the top of a
# `core::tui` header -- is refused, and the header carries a full include instead
# (`src/core/tui/Terminal.hpp`, `src/core/tui/TerminalInput.hpp`). A forward declaration defines
# no namespace, so the rule should skip such a block.
set(CORE_CPP_HYGIENE_RULES ${CORE_CPP_HYGIENE_RULES} namespace-directory)
set(CORE_CPP_HYGIENE_namespace-directory_REASON
    "a source's first namespace is the one its whole directory path names, in lowercase: src/core/<dir>/ is core::<dir> and src/core/<dir>/<sub>/ is core::<dir>::<sub>, bar the platform and detail directories, which are layout; src/core/ is core (Part I §1)")
set(CORE_CPP_HYGIENE_NAMESPACE_REGEX "^[ \t]*(inline[ \t]+)?namespace[ \t]+([A-Za-z_][A-Za-z0-9_:]*)([ \t{/].*)?$")
set(CORE_CPP_HYGIENE_NAMESPACE_ALIAS_REGEX "^[ \t]*namespace[ \t]+[A-Za-z0-9_:]+[ \t]*=")
set(CORE_CPP_HYGIENE_PRIVATE_DIRECTORIES bsd darwin detail emscripten linux posix windows)

# The rule the allowlist itself answers to.
set(CORE_CPP_HYGIENE_RULES ${CORE_CPP_HYGIENE_RULES} stale-allowlist)

# A rule over the tree against a table, not a line: every file under src/core/, cmake/portable/ and
# cmake/FetchTransferBound.cmake has a row in .agent/reference/provenance.md, naming the upstream
# file and SHA it was imported from, or "origin: core-cpp" for new code. A row naming a file that
# does not exist is refused too, so a rename or deletion cannot leave a stale row behind.
set(CORE_CPP_HYGIENE_RULES ${CORE_CPP_HYGIENE_RULES} provenance)
set(CORE_CPP_HYGIENE_provenance_REASON
    "every file under src/core/, cmake/portable/ and cmake/FetchTransferBound.cmake has one row in .agent/reference/provenance.md naming its upstream file and SHA, or 'origin: core-cpp' for new code (Global Constraints, \"Upstream sync discipline\"); a row that names an upstream names ONE file, not a pattern, and a full 40-character SHA")
set(CORE_CPP_HYGIENE_PROVENANCE_TABLE ".agent/reference/provenance.md")

# --- the allowlist -----------------------------------------------------------------------------------

set(_compileCacheReason
    "a verbatim copy of fastcached's module (cmake/portable/README.md), whose unprefixed options and launcher are shared across the organisation's projects, and which only CoreCppTopLevel.cmake includes")
core_cpp_hygiene_allow(unprefixed-option cmake/portable/CompileCache.cmake "${_compileCacheReason}")
core_cpp_hygiene_allow(unprefixed-cache-variable cmake/portable/CompileCache.cmake "${_compileCacheReason}")
core_cpp_hygiene_allow(unprefixed-function cmake/portable/CompileCache.cmake "${_compileCacheReason}")
core_cpp_hygiene_allow(global-cmake-variable cmake/portable/CompileCache.cmake "${_compileCacheReason}")
core_cpp_hygiene_allow(unprefixed-cache-variable cmake/FetchTransferBound.cmake
    "a verbatim copy of fastcached's module, whose FASTCACHED_FETCH_* bounds the organisation's projects share")
core_cpp_hygiene_allow(process-environment cmake/FetchTransferBound.cmake
    "a verbatim copy of fastcached's module, which only CoreCppTopLevel.cmake includes (rule top-level-only-include)")
core_cpp_hygiene_allow(global-cmake-variable cmake/CoreCppTopLevel.cmake
    "the one file that may touch global state, included only when core-cpp is the top-level project")
core_cpp_hygiene_allow(top-level-only-include cmake/CoreCppTopLevel.cmake
    "the one file that may touch global state, included only when core-cpp is the top-level project")
core_cpp_hygiene_allow(source-glob tests/cmake/check-cmake-hygiene.cmake
    "enumerates the tree it scans; it is not a source list")
core_cpp_hygiene_allow(source-glob cmake/CoreCppVendor.cmake
    "enumerates a vendored copy to find the files its manifest does not list; it is not a source list")

# The consumer smoke projects under tests/ are not core-cpp's build: each is a project of its own,
# written the way the consumer it stands for writes one. These rules say what core-cpp may do
# INSIDE such a build, and a consumer setting its own warning flags, its own CMAKE_CXX_* variables
# and its own functions is exactly what they must be able to do -- it is what the smoke tests then
# prove core-cpp leaves alone. Their C++ is core-cpp's own and is held to every rule.
set(_consumerProjectReason
    "a consumer's own project, not core-cpp's build: these are the flags and variables core-cpp must leave untouched, which is what the project asserts")
foreach(_consumerProject IN ITEMS consumer-cpm consumer-vendored consumer-wasm)
    core_cpp_hygiene_allow(global-compile-options "tests/${_consumerProject}/CMakeLists.txt" "${_consumerProjectReason}")
    core_cpp_hygiene_allow(global-cmake-variable "tests/${_consumerProject}/CMakeLists.txt" "${_consumerProjectReason}")
endforeach()
core_cpp_hygiene_allow(unprefixed-function tests/consumer-cpm/CMakeLists.txt "${_consumerProjectReason}")
core_cpp_hygiene_allow(unprefixed-function tests/consumer-wasm/CMakeLists.txt "${_consumerProjectReason}")
core_cpp_hygiene_allow(diagnostic-pragma src/core/testing/SuppressWindowsDialogsAtStartup.cpp
    "#pragma init_seg(lib) raises C4073 by design, to say that it was used; the file exists to run before ordinary static initializers")

if(LIST_RULES)
    foreach(rule IN LISTS CORE_CPP_HYGIENE_RULES)
        message("rule: ${rule}")
    endforeach()
    return()
endif()

if(NOT DEFINED ROOT OR NOT IS_DIRECTORY "${ROOT}")
    message(FATAL_ERROR "check-cmake-hygiene: ROOT ('${ROOT}') is not set or not a directory.")
endif()

# --- the files ---------------------------------------------------------------------------------------

# Where core-cpp's sources are. A build tree (out/, build/) is never under any of these.
set(scanned "")
foreach(top IN ITEMS CMakeLists.txt cmake src tests examples)
    if(IS_DIRECTORY "${ROOT}/${top}")
        file(GLOB_RECURSE found LIST_DIRECTORIES false RELATIVE "${ROOT}" "${ROOT}/${top}/*")
        list(APPEND scanned ${found})
    elseif(EXISTS "${ROOT}/${top}")
        list(APPEND scanned "${top}")
    endif()
endforeach()
list(SORT scanned)

# "<kind>|<file name regex>"
set(kinds
    "cmake|(^|/)CMakeLists\\.txt$"
    "cmake|\\.cmake$"
    "cpp|\\.(cpp|hpp|h|ipp|inl)(\\.in)?$")

# The characters CMake's list syntax gives a meaning to, each replaced by a control character while a
# file is split into lines: ';' separates elements, '[' and ']' group them, and '\' escapes a ';'.
string(ASCII 1 semicolonCode)
string(ASCII 2 openBracketCode)
string(ASCII 3 closeBracketCode)
string(ASCII 4 backslashCode)

# A string rather than a list: a reason or a source line may contain ';'.
set(violations "")
set(violationCount 0)
set(allowUsed "")

## @brief Refuses @p line of @p path under @p rule, unless an allowlist row allows the rule in that
## file. @p shown is the text printed under the reason.
function(core_cpp_hygiene_refuse rule path lineNumber shown)
    set(allowed OFF)
    foreach(index IN LISTS CORE_CPP_HYGIENE_ALLOWLIST)
        if(CORE_CPP_HYGIENE_ALLOW_${index}_RULE STREQUAL rule
           AND CORE_CPP_HYGIENE_ALLOW_${index}_FILE STREQUAL path)
            set(allowed ON)
            list(APPEND allowUsed ${index})
        endif()
    endforeach()
    if(NOT allowed)
        string(STRIP "${shown}" shown)
        string(APPEND violations
            "\n  ${path}:${lineNumber}: [${rule}] ${CORE_CPP_HYGIENE_${rule}_REASON}\n      ${shown}")
        math(EXPR violationCount "${violationCount} + 1")
    endif()
    set(violations "${violations}" PARENT_SCOPE)
    set(violationCount ${violationCount} PARENT_SCOPE)
    set(allowUsed "${allowUsed}" PARENT_SCOPE)
endfunction()

foreach(path IN LISTS scanned)
    set(kind "")
    foreach(row IN LISTS kinds)
        string(FIND "${row}" "|" bar)
        math(EXPR patternAt "${bar} + 1")
        string(SUBSTRING "${row}" ${patternAt} -1 pattern)
        if(path MATCHES "${pattern}")
            string(SUBSTRING "${row}" 0 ${bar} kind)
            break()
        endif()
    endforeach()
    if(NOT kind)
        continue()
    endif()

    file(READ "${ROOT}/${path}" content)
    string(REPLACE "\\" "${backslashCode}" content "${content}")
    string(REPLACE ";" "${semicolonCode}" content "${content}")
    string(REPLACE "[" "${openBracketCode}" content "${content}")
    string(REPLACE "]" "${closeBracketCode}" content "${content}")
    string(REPLACE "\n" ";" lines "${content}")

    # The namespace this file's first named namespace must be, or be nested in (namespace-directory).
    # Empty where the rule does not reach this file at all, which is also what stops the case check
    # below from running; `firstNamespaceSeen` is what limits the DIRECTORY half to the first
    # declaration, while every later one is still held to lowercase.
    set(expectedNamespace "")
    if(kind STREQUAL "cpp" AND path MATCHES "^src/core/(.+)/[^/]+$")
        set(expectedNamespace "core")
        string(REPLACE "/" ";" namespaceSegments "${CMAKE_MATCH_1}")
        foreach(namespaceSegment IN LISTS namespaceSegments)
            if(NOT namespaceSegment IN_LIST CORE_CPP_HYGIENE_PRIVATE_DIRECTORIES)
                string(APPEND expectedNamespace "::${namespaceSegment}")
            endif()
        endforeach()
    elseif(kind STREQUAL "cpp" AND path MATCHES "^src/core/[^/]+$")
        set(expectedNamespace "core")
    endif()
    set(firstNamespaceSeen OFF)

    set(lineNumber 0)
    foreach(encoded IN LISTS lines)
        math(EXPR lineNumber "${lineNumber} + 1")
        string(REPLACE "${semicolonCode}" ";" line "${encoded}")
        string(REPLACE "${openBracketCode}" "[" line "${line}")
        string(REPLACE "${closeBracketCode}" "]" line "${line}")
        string(REPLACE "${backslashCode}" "\\" line "${line}")

        if(expectedNamespace AND NOT line MATCHES "${CORE_CPP_HYGIENE_NAMESPACE_ALIAS_REGEX}")
            string(REGEX MATCH "${CORE_CPP_HYGIENE_NAMESPACE_REGEX}" declaration "${line}")
            if(declaration)
                set(declared "${CMAKE_MATCH_2}")
                if(NOT firstNamespaceSeen AND NOT declared STREQUAL expectedNamespace
                   AND NOT declared MATCHES "^${expectedNamespace}::")
                    core_cpp_hygiene_refuse(namespace-directory "${path}" ${lineNumber}
                        "${line}    (expected ${expectedNamespace})")
                elseif(declared MATCHES "[A-Z]")
                    core_cpp_hygiene_refuse(namespace-directory "${path}" ${lineNumber}
                        "${line}    (namespaces are lowercase)")
                endif()
                set(firstNamespaceSeen ON)
            endif()
        endif()

        foreach(rule IN LISTS CORE_CPP_HYGIENE_RULES)
            if(NOT kind IN_LIST CORE_CPP_HYGIENE_${rule}_KIND)
                continue()
            endif()
            if(CORE_CPP_HYGIENE_${rule}_FIRST_LINE AND NOT lineNumber EQUAL 1)
                continue()
            endif()
            if(NOT line MATCHES "${CORE_CPP_HYGIENE_${rule}_REGEX}")
                continue()
            endif()
            if(CORE_CPP_HYGIENE_${rule}_EXCEPT AND line MATCHES "${CORE_CPP_HYGIENE_${rule}_EXCEPT}")
                continue()
            endif()
            core_cpp_hygiene_refuse(${rule} "${path}" ${lineNumber} "${line}")
        endforeach()
    endforeach()
endforeach()

# --- the provenance table -----------------------------------------------------------------------------

# The files this rule covers: everything under src/core/ and cmake/portable/, plus the one file
# FetchTransferBound.cmake. "scanned" already lists every file under cmake/ and src/, not just the
# cmake/cpp kinds the line-based rules read.
set(provenanceScope "")
foreach(path IN LISTS scanned)
    if(path MATCHES "^src/core/" OR path MATCHES "^cmake/portable/" OR path STREQUAL "cmake/FetchTransferBound.cmake")
        list(APPEND provenanceScope "${path}")
    endif()
endforeach()

# The "core-cpp path" column of every data row of the provenance table: a line "| <path> | ... |",
# skipping the header row and the "|---|---|...|" separator row. Backticks around a path are markup,
# not part of it.
set(provenanceNamed "")
if(EXISTS "${ROOT}/${CORE_CPP_HYGIENE_PROVENANCE_TABLE}")
    file(STRINGS "${ROOT}/${CORE_CPP_HYGIENE_PROVENANCE_TABLE}" provenanceLines)
    foreach(provenanceLine IN LISTS provenanceLines)
        if(NOT provenanceLine MATCHES "^\\|([^|]*)\\|")
            continue()
        endif()
        set(provenanceCell "${CMAKE_MATCH_1}")
        string(REPLACE "`" "" provenanceCell "${provenanceCell}")
        string(STRIP "${provenanceCell}" provenanceCell)
        if(provenanceCell STREQUAL "" OR provenanceCell STREQUAL "core-cpp path" OR provenanceCell MATCHES "^[:-]+$")
            continue()
        endif()
        list(APPEND provenanceNamed "${provenanceCell}")

        # The upstream half of the row: which file it came from, and the commit it was taken at.
        # scripts/check-upstream-drift.py refuses both of the defects below as well, but it can say
        # nothing at all without the upstream checkouts and exits 77 -- a skip -- when they are
        # absent, which is every CI runner. Neither defect needs a checkout or a network to see, so
        # they are refused here instead, where the gate runs everywhere. The checker keeps the half
        # that genuinely needs the checkouts (has upstream MOVED since this SHA), and this keeps the
        # half that is a defect in the table itself. One defect, one gate.
        if(NOT provenanceLine MATCHES "^\\|[^|]*\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|")
            core_cpp_hygiene_refuse(provenance "${CORE_CPP_HYGIENE_PROVENANCE_TABLE}" "-"
                "the row for '${provenanceCell}' has fewer than the five columns the table's header names")
            continue()
        endif()
        set(provenanceRepo "${CMAKE_MATCH_1}")
        set(provenanceUpstream "${CMAKE_MATCH_2}")
        set(provenanceSha "${CMAKE_MATCH_3}")
        foreach(cellVar provenanceRepo provenanceUpstream provenanceSha)
            string(REPLACE "`" "" ${cellVar} "${${cellVar}}")
            string(STRIP "${${cellVar}}" ${cellVar})
        endforeach()

        # A row written here rather than imported has no upstream to name, and says so in both cells.
        if(provenanceRepo STREQUAL "origin: core-cpp")
            continue()
        endif()

        # One cell, one upstream file. The preamble settles it: a file adapted from more than one
        # upstream file names its primary upstream here and the others in notes. A brace or a glob
        # makes the row unreadable by `git log -- <path>`, so the row silently stops being checkable
        # instead of reporting anything -- which is how it passed unnoticed until the checker ran.
        if(provenanceUpstream MATCHES "[{}*?]")
            core_cpp_hygiene_refuse(provenance "${CORE_CPP_HYGIENE_PROVENANCE_TABLE}" "-"
                "'${provenanceCell}': upstream path '${provenanceUpstream}' is a pattern, not a file; a merged file names its primary upstream here and the others in notes")
        endif()

        # An abbreviated SHA resolves only in a checkout that has the object, so a row carrying one
        # is a pin that cannot be verified from the table alone. CMake's regex has no {n} repetition,
        # hence the explicit length.
        string(LENGTH "${provenanceSha}" provenanceShaLength)
        if(NOT provenanceSha MATCHES "^[0-9a-f]+$" OR NOT provenanceShaLength EQUAL 40)
            core_cpp_hygiene_refuse(provenance "${CORE_CPP_HYGIENE_PROVENANCE_TABLE}" "-"
                "'${provenanceCell}': synced SHA '${provenanceSha}' is not a full 40-character hash")
        else()
            # Keep the pin so NOTICE's copy of it can be held against this one below.
            string(MAKE_C_IDENTIFIER "${provenanceCell}" provenanceKey)
            set(provenancePin_${provenanceKey} "${provenanceSha}")
        endif()
    endforeach()
endif()

# Every file in scope needs a row, named by the file.
foreach(path IN LISTS provenanceScope)
    if(NOT path IN_LIST provenanceNamed)
        core_cpp_hygiene_refuse(provenance "${path}" "-" "no row in ${CORE_CPP_HYGIENE_PROVENANCE_TABLE}")
    endif()
endforeach()

# A row naming a file that does not exist is refused, named by the table it appears in: a rename or
# deletion cannot leave a stale row behind.
foreach(named IN LISTS provenanceNamed)
    if(NOT EXISTS "${ROOT}/${named}")
        core_cpp_hygiene_refuse(provenance "${CORE_CPP_HYGIENE_PROVENANCE_TABLE}" "-"
            "names '${named}', which does not exist")
    endif()
endforeach()

# NOTICE records a pin too, and until now nothing read it: the re-sync of the two verbatim cmake
# files to 5a9dca04 updated cmake/portable/README.md and the provenance table and left NOTICE
# naming eb9c9c68, where it sat until someone read it by hand. Three documents carry pins and one
# of them was checked. A correction is not finished until every document that carried the wrong
# version carries the right one, so NOTICE's pins are held against the table's here.
#
# Only the bullets NOTICE itself marks "(verbatim)" are compared. A merged file is deliberately
# listed under the commit of EACH upstream it took something from, while the table names its
# primary upstream only -- so for those two the documents disagree by design, and comparing them
# would be a false positive. "(verbatim)" means one upstream file and one commit, which is exactly
# the case that can be checked and exactly the one that rotted.
if(EXISTS "${ROOT}/NOTICE")
    set(noticePin "")
    file(STRINGS "${ROOT}/NOTICE" noticeLines)
    foreach(noticeLine IN LISTS noticeLines)
        # "Imported at <sha>", and the "<project>, at <sha>" form the documentation section uses.
        if(noticeLine MATCHES "at ([0-9a-f]+)")
            string(LENGTH "${CMAKE_MATCH_1}" noticeShaLength)
            if(noticeShaLength EQUAL 40)
                set(noticePin "${CMAKE_MATCH_1}")
            endif()
        endif()
        if(NOT noticeLine MATCHES "^[ \t]*-[ \t]+([^ \t(]+)[ \t]+\\(verbatim\\)")
            continue()
        endif()
        set(noticePath "${CMAKE_MATCH_1}")
        if(NOT EXISTS "${ROOT}/${noticePath}")
            core_cpp_hygiene_refuse(provenance "NOTICE" "-"
                "names '${noticePath}' as a verbatim copy, and no such file exists")
            continue()
        endif()
        # A verbatim file outside the provenance table's scope (.github/, say) has no row to be held
        # against; that it exists is all this rule can say about it.
        string(MAKE_C_IDENTIFIER "${noticePath}" noticeKey)
        if(NOT DEFINED provenancePin_${noticeKey})
            continue()
        endif()
        if(NOT noticePin STREQUAL "${provenancePin_${noticeKey}}")
            core_cpp_hygiene_refuse(provenance "NOTICE" "-"
                "records '${noticePath}' at ${noticePin}, but ${CORE_CPP_HYGIENE_PROVENANCE_TABLE} pins it at ${provenancePin_${noticeKey}}")
        endif()
    endforeach()
endif()

# An allowlist row that allows nothing in a file that exists has outlived its reason. A row whose file
# is gone is left alone: whatever the file was renamed to is not allowlisted, so it is refused anyway.
foreach(index IN LISTS CORE_CPP_HYGIENE_ALLOWLIST)
    set(file "${CORE_CPP_HYGIENE_ALLOW_${index}_FILE}")
    if(EXISTS "${ROOT}/${file}" AND NOT index IN_LIST allowUsed)
        string(APPEND violations
            "
  ${file}: [stale-allowlist] the allowlist row for ${CORE_CPP_HYGIENE_ALLOW_${index}_RULE} allows nothing in this file any more, so remove the row")
        math(EXPR violationCount "${violationCount} + 1")
    endif()
endforeach()

if(violationCount GREATER 0)
    message(FATAL_ERROR "check-cmake-hygiene: ${violationCount} violation(s):${violations}")
endif()
list(LENGTH scanned fileCount)
message(STATUS "check-cmake-hygiene: ${fileCount} file(s) under ${ROOT} are clean")
