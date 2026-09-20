# SPDX-License-Identifier: Apache-2.0
#
# Proves cmake/CoreCppVendor.cmake: it exports the file set of Part I §5 from a git repository
# byte for byte, and it refuses each way a copy can stop being verbatim -- by the phrase that names
# that refusal and no other. A tool that refused everything would pass the refusing cases alone, so
# the accepting cases are half of the proof.
#
# Every refusal the script implements has a case here, and `refusalPhrases` below is the full list
# of them, so a case that fires a refusal it did not deserve is a failure too.
#
# Each case builds a git repository of its own in WORK_DIR: five files of the vendored set and two
# outside it (the brief's three files plus a dot-file, the module table, a module directory and the
# two that must NOT be copied), and whatever the case adds -- a blob with a CR byte, a symbolic
# link, a submodule. The repository is made through git's index rather than a checkout, so a
# symbolic link is recorded on a Windows host too, where creating one needs a privilege the test
# does not have. Its commit is tagged, because the tool takes a tag or a full SHA and nothing else.
#
# Usage: cmake -DTOOL=<path to cmake/CoreCppVendor.cmake> -DWORK_DIR=<scratch directory>
#              [-DSKIP_EXIT_CODE=<code>] -P tests/cmake/check-vendor-selftest.cmake

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED TOOL OR NOT EXISTS "${TOOL}")
    message(FATAL_ERROR "check-vendor-selftest: TOOL ('${TOOL}') is not set or does not exist.")
endif()
if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
    message(FATAL_ERROR "check-vendor-selftest: WORK_DIR is not set.")
endif()
if(NOT DEFINED SKIP_EXIT_CODE OR SKIP_EXIT_CODE STREQUAL "")
    set(SKIP_EXIT_CODE 77)
endif()
find_program(CORE_CPP_SELFTEST_GIT NAMES git)
if(NOT CORE_CPP_SELFTEST_GIT)
    # The tool's own MODE=check needs no git, and a build from a release tarball on a machine
    # without one is a legitimate build; this test of the tool is the only thing in the tree that
    # needs an external program. A case that could not run is a SKIP, not a failure of the tree
    # (.agent/rules/testing.md), so the script leaves with the skip code ctest is told about in
    # tests/CMakeLists.txt. cmake_language(EXIT) is CMake 3.29; where it is missing the message is
    # what ctest matches instead, which is why both properties are set on the test.
    message(STATUS
        "check-vendor-selftest: SKIPPED -- git is not on PATH, and every case builds a repository "
        "with it. (The tool's MODE=check needs no git; this test of it does.)")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
        cmake_language(EXIT ${SKIP_EXIT_CODE})
    endif()
    return()
endif()

# The tag every fixture repository carries, and a SHA that is well formed and resolves nowhere.
set(fixtureTag "v0.0.1")
set(absentCommit "0123456789abcdef0123456789abcdef01234567")

# The staging directory the tool assembles a new copy in, which no refusal may leave behind.
set(stagingName ".core-cpp-vendor-staging")

# Every phrase the tool refuses with, so a case can assert that no other refusal fired. A refusal
# missing from this list is one a case could fire by accident without anyone noticing, so the list
# is the tool's refusals read off the script, not the ones the cases happen to want.
#
# Two of the script's refusals are deliberately absent, because no case can produce them portably:
# "is in no git repository" (WORK_DIR is inside a build tree, which is inside core-cpp's own
# repository, so git ascends to it and the repository-root refusal fires instead) and "needs git"
# (find_program() searches the platform's default directories, not only PATH).
set(refusalPhrases
    "is a symbolic link"
    "is a submodule"
    "contains a CR byte"
    "differs from the manifest"
    "is missing"
    "is not in the manifest"
    "is neither a header nor"
    "header line"
    "never empty"
    "which is not a count"
    "but lists"
    "does not exist, so"
    "delete someone's work"
    "has no module for"
    "builds unconditionally"
    "is not a core-cpp tree"
    "not a repository root"
    "nor a tag of"
    "resolving the commit"
    "MODE must be sync or check"
    "DEST is not set"
    "REF is not set")

# The repository every case starts from: "<mode>|<path>|<content>". The five files of the vendored
# set are a named file, a dot-file, the module table, the base module (src/core/ itself) and a
# module directory; the two others are outside the set and must never be copied. A row is one
# element of a CMake list, so no content here carries a semicolon.
#
# The module table is what makes the tree core-cpp's as far as the tool is concerned, and what it
# reads to know which modules a MODULES list may not leave out. `log` carries a WHEN here so that
# selecting `base` alone stays legal; `base` carries none, so leaving it out is refused.
#
# .gitattributes is what makes the rows portable. file(WRITE) writes a line break in the host's own
# form -- CRLF on Windows -- so `* text=auto eol=lf` normalises every blob to LF, exactly as
# core-cpp's own .gitattributes does. The two paths a case deliberately gives a CR byte are marked
# -text, so git stores their bytes as they are, which is what a repository does for a file it
# treats as binary. Without this the host's line endings would decide what the cases prove.
set(baseRepository
    "100644|.gitattributes|* text=auto eol=lf\nsrc/core/Crlf.hpp -text\ndocs/crlf.md -text\n"
    "100644|CMakeLists.txt|project(core-cpp VERSION 0.1.0 LANGUAGES CXX)\n"
    "100644|.clang-format|ColumnLimit: 110\n"
    "100644|cmake/CoreCppModules.cmake|core_cpp_module(NAME base DIR . KIND STATIC PLATFORMS any)\ncore_cpp_module(NAME log KIND STATIC DEPS base PLATFORMS any WHEN CORE_CPP_WITH_LOG)\n"
    "100644|src/core/Utils.hpp|#pragma once\n// namespace core\n"
    "100644|src/core/log/LogStore.hpp|#pragma once\n// namespace core::log\n"
    "100644|docs/index.md|# Not part of the vendored set\n"
    "100644|tests/Foo_test.cpp|// Not part of the vendored set\n")
set(baseRepositoryFiles
    ".clang-format" "CMakeLists.txt" "cmake/CoreCppModules.cmake"
    "src/core/Utils.hpp" "src/core/log/LogStore.hpp")

set(caseCount 0)
set(failures "")

## @brief Runs git in @p dir, failing the whole self-test when git does.
function(core_cpp_selftest_git dir what)
    execute_process(
        COMMAND "${CORE_CPP_SELFTEST_GIT}" -c core.autocrlf=false -c core.eol=lf
                -c user.name=selftest -c user.email=selftest@core-cpp.invalid -c commit.gpgsign=false
                -C "${dir}" ${ARGN}
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "check-vendor-selftest: ${what} failed (git exited ${rc}): ${out}${err}")
    endif()
endfunction()

## @brief Builds a git repository in @p dir from "<mode>|<path>|<content>" rows.
##
## mode 100644 writes a regular file and adds it; 120000 records a symbolic link whose target is
## the content; 160000 records a submodule at the commit the content names. The last two go
## straight into the index, which is the only portable way to put either into a tree.
##
## The commit is tagged `${fixtureTag}` and its SHA is left in `fixtureCommit` in the caller's
## scope: the tool takes a tag or a full 40-character SHA, and both are exercised.
function(core_cpp_selftest_repository dir rows)
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}")
    core_cpp_selftest_git("${dir}" "git init in ${dir}" init --quiet)
    foreach(row IN LISTS rows)
        string(FIND "${row}" "|" bar)
        string(SUBSTRING "${row}" 0 ${bar} mode)
        math(EXPR rest "${bar} + 1")
        string(SUBSTRING "${row}" ${rest} -1 row)
        string(FIND "${row}" "|" bar)
        string(SUBSTRING "${row}" 0 ${bar} path)
        math(EXPR rest "${bar} + 1")
        string(SUBSTRING "${row}" ${rest} -1 content)

        if(mode STREQUAL "100644")
            file(WRITE "${dir}/${path}" "${content}")
            core_cpp_selftest_git("${dir}" "git add ${path}" add -- "${path}")
        elseif(mode STREQUAL "copy")
            # For a file whose bytes cannot be a row: the content names a file to copy in instead.
            get_filename_component(intoDir "${dir}/${path}" DIRECTORY)
            get_filename_component(fromName "${content}" NAME)
            file(COPY "${content}" DESTINATION "${intoDir}")
            get_filename_component(wantedName "${path}" NAME)
            if(NOT fromName STREQUAL wantedName)
                file(RENAME "${intoDir}/${fromName}" "${dir}/${path}")
            endif()
            core_cpp_selftest_git("${dir}" "git add ${path}" add -- "${path}")
        elseif(mode STREQUAL "120000")
            file(WRITE "${dir}/.selftest-link-target" "${content}")
            execute_process(
                COMMAND "${CORE_CPP_SELFTEST_GIT}" -c core.autocrlf=false -c core.eol=lf -C "${dir}"
                        hash-object -w .selftest-link-target
                RESULT_VARIABLE rc OUTPUT_VARIABLE blob ERROR_VARIABLE err)
            if(NOT rc EQUAL 0)
                message(FATAL_ERROR "check-vendor-selftest: hashing the link target failed: ${err}")
            endif()
            string(STRIP "${blob}" blob)
            file(REMOVE "${dir}/.selftest-link-target")
            core_cpp_selftest_git("${dir}" "recording the symbolic link ${path}"
                                  update-index --add --cacheinfo "120000,${blob},${path}")
        elseif(mode STREQUAL "160000")
            core_cpp_selftest_git("${dir}" "recording the submodule ${path}"
                                  update-index --add --cacheinfo "160000,${content},${path}")
        else()
            message(FATAL_ERROR "check-vendor-selftest: a repository row has mode '${mode}'.")
        endif()
    endforeach()
    core_cpp_selftest_git("${dir}" "git commit in ${dir}" commit --quiet -m "self-test tree")
    core_cpp_selftest_git("${dir}" "tagging ${fixtureTag} in ${dir}" tag "${fixtureTag}")
    execute_process(
        COMMAND "${CORE_CPP_SELFTEST_GIT}" -C "${dir}" rev-parse HEAD
        RESULT_VARIABLE rc OUTPUT_VARIABLE commit ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "check-vendor-selftest: reading HEAD of ${dir} failed: ${err}")
    endif()
    string(STRIP "${commit}" commit)
    set(fixtureCommit "${commit}" PARENT_SCOPE)
endfunction()

## @brief Sets @p outVar to whether @p path holds a CR byte.
##
## Through the hex form: a plain file(READ) opens the file in text mode on Windows and hands back a
## string a CRLF has already been taken out of.
function(core_cpp_selftest_has_cr path outVar)
    file(READ "${path}" hexContent HEX)
    string(REGEX REPLACE "(..)" "\\1;" hexBytes "${hexContent}")
    set(found OFF)
    if("0d" IN_LIST hexBytes)
        set(found ON)
    endif()
    set(${outVar} ${found} PARENT_SCOPE)
endfunction()

## @brief Removes everything in @p dir except its MANIFEST.
function(core_cpp_selftest_empty dir)
    file(GLOB entries LIST_DIRECTORIES true "${dir}/*")
    foreach(entry IN LISTS entries)
        get_filename_component(name "${entry}" NAME)
        if(NOT name STREQUAL "MANIFEST")
            file(REMOVE_RECURSE "${entry}")
        endif()
    endforeach()
endfunction()

## @brief Sets @p outVar to what a run of @p tool said, with its line breaks flattened.
##
## CMake wraps a FATAL_ERROR message at about 76 columns, so a phrase can straddle a line break.
##
## The arguments come as the NAME of a list variable rather than through ARGN, because one of them
## is `-DMODULES=base;tui`: an element holding a semicolon survives being read out of a variable,
## and does not survive being passed as a function argument and expanded again (`${ARGN}` splits it
## into `-DMODULES=base` and `tui`, and the tool then vendors `base` and says nothing).
function(core_cpp_selftest_run_tool tool argsVar outVar rcVar)
    execute_process(COMMAND "${CMAKE_COMMAND}" ${${argsVar}} -P "${tool}"
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    string(REGEX REPLACE "[ \t\r\n]+" " " said "${out} ${err}")
    set(${outVar} "${said}" PARENT_SCOPE)
    set(${rcVar} "${rc}" PARENT_SCOPE)
endfunction()

## @brief core_cpp_selftest_run_tool() with the tool under test.
function(core_cpp_selftest_run argsVar outVar rcVar)
    core_cpp_selftest_run_tool("${TOOL}" "${argsVar}" said rc)
    set(${outVar} "${said}" PARENT_SCOPE)
    set(${rcVar} "${rc}" PARENT_SCOPE)
endfunction()

## @brief Requires that @p said is the outcome @p want describes, and no other refusal.
##
## @param stage Which run this was ("sync" or "check"), for the report.
## @param want ACCEPT, or the refusal phrase that must appear.
## @param rc The tool's exit status.
## @param said Its flattened output.
## @param outVar Receives the case's problems, appended to what it already holds.
function(core_cpp_selftest_expect stage want rc said outVar)
    set(problems "${${outVar}}")
    if(want STREQUAL "ACCEPT")
        if(NOT rc EQUAL 0)
            string(APPEND problems " ${stage} refused (exit ${rc}) what it must accept:${said}")
        endif()
    elseif(rc EQUAL 0)
        string(APPEND problems " ${stage} accepted what it must refuse:${said}")
    else()
        string(FIND "${said}" "${want}" at)
        if(at EQUAL -1)
            string(APPEND problems " ${stage} refused, but not with '${want}':${said}")
        endif()
    endif()
    if(NOT want STREQUAL "ACCEPT" OR rc EQUAL 0)
        foreach(other IN LISTS refusalPhrases)
            if(other STREQUAL want)
                continue()
            endif()
            string(FIND "${said}" "${other}" at)
            if(NOT at EQUAL -1)
                string(APPEND problems " ${stage} also said '${other}', which this case does not deserve")
            endif()
        endforeach()
    endif()
    set(${outVar} "${problems}" PARENT_SCOPE)
endfunction()

## @brief Runs one case: build a repository, sync it, mutate the copy, check it.
##
##   EXTRA <row>...       rows added to the base repository
##   REF <ref>            the REF argument of the sync (default: the fixture's tag)
##   MODULES <list>       the MODULES argument of the sync (default: all of them)
##   OCCUPY <name>        a file of someone else's, placed in DEST before the sync
##   MUTATE <what>        what happens to the copy between the sync and the check
##   WANT_SYNC  <ACCEPT or phrase>
##   WANT_CHECK <ACCEPT or phrase>   (omitted: the check is not run)
##   EXPECT_FILES <path>...   the exact set of paths the manifest must list
function(core_cpp_vendor_case name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "MUTATE;OCCUPY;REF;WANT_SYNC;WANT_CHECK"
                          "EXTRA;MODULES;EXPECT_FILES")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "check-vendor-selftest: case ${name} has unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    # cmake_parse_arguments() UNSETS a one-value keyword that was not given, and an undefined
    # variable compares as its own name -- `arg_MUTATE STREQUAL ""` would be false for every case
    # that mutates nothing. Giving it a value is what makes the chain below read as written.
    if(NOT DEFINED arg_MUTATE)
        set(arg_MUTATE "")
    endif()
    set(root "${WORK_DIR}/${name}")
    set(repository "${root}/repo")
    set(copy "${root}/vendor/core-cpp")
    file(REMOVE_RECURSE "${root}")
    set(rows ${baseRepository})
    list(APPEND rows ${arg_EXTRA})
    core_cpp_selftest_repository("${repository}" "${rows}")

    set(problems "")
    if(arg_OCCUPY)
        file(WRITE "${copy}/${arg_OCCUPY}" "someone else's work\n")
    endif()

    set(ref "${fixtureTag}")
    if(arg_REF)
        set(ref "${arg_REF}")
        if(ref STREQUAL "THE-COMMIT")
            set(ref "${fixtureCommit}")
        endif()
    endif()
    set(syncArguments -DMODE=sync "-DREF=${ref}" "-DREPO=${repository}" "-DDEST=${copy}")
    if(arg_MODULES)
        # The tool takes a CMake list, so the semicolons have to reach it as one argument.
        string(REPLACE ";" "\\;" modulesList "${arg_MODULES}")
        list(APPEND syncArguments "-DMODULES=${modulesList}")
    endif()
    core_cpp_selftest_run(syncArguments said rc)
    core_cpp_selftest_expect(sync "${arg_WANT_SYNC}" "${rc}" "${said}" problems)

    if(rc EQUAL 0 AND arg_EXPECT_FILES)
        set(listed "")
        file(STRINGS "${copy}/MANIFEST" manifestLines REGEX "^[0-9a-f]+  ")
        foreach(line IN LISTS manifestLines)
            string(REGEX REPLACE "^[0-9a-f]+  " "" line "${line}")
            list(APPEND listed "${line}")
        endforeach()
        list(SORT listed)
        set(wanted ${arg_EXPECT_FILES})
        list(SORT wanted)
        if(NOT listed STREQUAL wanted)
            string(APPEND problems " the manifest lists '${listed}' where it must list '${wanted}'")
        endif()
        # The manifest's own bytes are LF whatever the host wrote them, because the consumer
        # commits the file and two correct syncs of the same tag must not differ.
        core_cpp_selftest_has_cr("${copy}/MANIFEST" manifestHasCr)
        if(manifestHasCr)
            string(APPEND problems " the MANIFEST was written with a CR byte")
        endif()
        # The text is the repository's, and the bytes carry no CR: together that is "verbatim" for
        # a tree whose blobs are LF. The text comparison is deliberately the platform's own
        # file(READ), which takes CRLF out on Windows, so it compares what was written with what
        # was copied rather than the host's line endings; the CR scan is what pins the bytes.
        foreach(path IN LISTS wanted)
            file(READ "${repository}/${path}" original)
            file(READ "${copy}/${path}" copied)
            if(NOT original STREQUAL copied)
                string(APPEND problems " ${path} was not copied with the same text")
            endif()
            core_cpp_selftest_has_cr("${copy}/${path}" hasCr)
            if(hasCr)
                string(APPEND problems " ${path} was copied with a CR byte the repository's blob has not")
            endif()
        endforeach()
    endif()

    if(arg_MUTATE STREQUAL "FLIP")
        file(APPEND "${copy}/src/core/Utils.hpp" "// a local change\n")
    elseif(arg_MUTATE STREQUAL "ADD")
        file(WRITE "${copy}/src/core/Extra.hpp" "#pragma once\n")
    elseif(arg_MUTATE STREQUAL "REMOVE")
        file(REMOVE "${copy}/src/core/Utils.hpp")
    elseif(arg_MUTATE STREQUAL "MANIFEST_GONE")
        file(REMOVE "${copy}/MANIFEST")
    elseif(arg_MUTATE STREQUAL "MANIFEST_JUNK")
        file(APPEND "${copy}/MANIFEST" "a line that is neither\n")
    elseif(arg_MUTATE STREQUAL "MANIFEST_COUNT")
        file(READ "${copy}/MANIFEST" text)
        string(REGEX REPLACE "# files [0-9]+" "# files 99" text "${text}")
        file(WRITE "${copy}/MANIFEST" "${text}")
    elseif(arg_MUTATE STREQUAL "MANIFEST_NOT_A_COUNT")
        file(READ "${copy}/MANIFEST" text)
        string(REGEX REPLACE "# files [0-9]+" "# files several" text "${text}")
        file(WRITE "${copy}/MANIFEST" "${text}")
    elseif(arg_MUTATE STREQUAL "EMPTY_ALL")
        # A copy that is gone beside a manifest that is gone: nothing is listed, so no hash
        # disagrees, nothing is missing and nothing is unlisted.
        core_cpp_selftest_empty("${copy}")
        file(WRITE "${copy}/MANIFEST" "")
    elseif(arg_MUTATE STREQUAL "EMPTY_HEADERS")
        # The same, with a well-formed header that admits to listing nothing.
        core_cpp_selftest_empty("${copy}")
        file(WRITE "${copy}/MANIFEST"
             "# repository ${repository}\n# ref ${fixtureTag}\n# commit ${absentCommit}\n"
             "# modules base\n# files 0\n")
    elseif(NOT arg_MUTATE STREQUAL "")
        message(FATAL_ERROR "check-vendor-selftest: case ${name} has MUTATE '${arg_MUTATE}'.")
    endif()

    if(DEFINED arg_WANT_CHECK)
        set(checkArguments -DMODE=check "-DDEST=${copy}")
        core_cpp_selftest_run(checkArguments said rc)
        core_cpp_selftest_expect(check "${arg_WANT_CHECK}" "${rc}" "${said}" problems)
    endif()

    math(EXPR count "${caseCount} + 1")
    set(caseCount ${count} PARENT_SCOPE)
    if(problems)
        set(failures "${failures}\n  ${name}:${problems}" PARENT_SCOPE)
    endif()
endfunction()

# --- the cases -----------------------------------------------------------------

# A sync copies the file set and nothing else, byte for byte, and the check accepts what it wrote.
core_cpp_vendor_case(syncs-a-tag-then-checks
    WANT_SYNC ACCEPT WANT_CHECK ACCEPT
    EXPECT_FILES ${baseRepositoryFiles})
core_cpp_vendor_case(syncs-a-full-commit-sha-then-checks
    REF THE-COMMIT
    WANT_SYNC ACCEPT WANT_CHECK ACCEPT
    EXPECT_FILES ${baseRepositoryFiles})

# Only a tag or a full SHA: a branch or HEAD names a different tree from one day to the next, and
# the recovery the check's message recommends would then restore something else.
core_cpp_vendor_case(refuses-a-branch-as-the-ref
    REF "master" WANT_SYNC "nor a tag of")
core_cpp_vendor_case(refuses-head-as-the-ref
    REF "HEAD" WANT_SYNC "nor a tag of")
core_cpp_vendor_case(refuses-a-sha-the-repository-has-not
    REF "${absentCommit}" WANT_SYNC "resolving the commit")

# MODULES selects module directories; src/core/ itself is `base`.
core_cpp_vendor_case(syncs-the-modules-it-is-given
    MODULES "base"
    WANT_SYNC ACCEPT WANT_CHECK ACCEPT
    EXPECT_FILES ".clang-format" "CMakeLists.txt" "cmake/CoreCppModules.cmake" "src/core/Utils.hpp")
core_cpp_vendor_case(refuses-a-module-the-ref-has-not
    MODULES "base;tui"
    WANT_SYNC "has no module for")
# `base` has no WHEN in the fixture's module table, so the build would enter its directory and the
# copy would not have one. CMake's own message for that names a path, not the argument that
# dropped it.
core_cpp_vendor_case(refuses-modules-that-omit-an-unconditional-module
    MODULES "log"
    WANT_SYNC "builds unconditionally")

# The ways a copy stops being what its manifest says.
core_cpp_vendor_case(refuses-a-changed-file
    WANT_SYNC ACCEPT MUTATE FLIP WANT_CHECK "differs from the manifest")
core_cpp_vendor_case(refuses-an-unlisted-file
    WANT_SYNC ACCEPT MUTATE ADD WANT_CHECK "is not in the manifest")
core_cpp_vendor_case(refuses-a-missing-file
    WANT_SYNC ACCEPT MUTATE REMOVE WANT_CHECK "is missing")

# The ways the manifest itself stops being a manifest. The last two are the shape that used to
# pass: a copy that is gone and a manifest that is gone have nothing left to disagree about.
core_cpp_vendor_case(refuses-a-copy-without-a-manifest
    WANT_SYNC ACCEPT MUTATE MANIFEST_GONE WANT_CHECK "does not exist, so")
core_cpp_vendor_case(refuses-an-unparsable-manifest-line
    WANT_SYNC ACCEPT MUTATE MANIFEST_JUNK WANT_CHECK "is neither a header nor")
core_cpp_vendor_case(refuses-a-manifest-whose-count-disagrees
    WANT_SYNC ACCEPT MUTATE MANIFEST_COUNT WANT_CHECK "but lists")
core_cpp_vendor_case(refuses-a-manifest-whose-count-is-not-a-number
    WANT_SYNC ACCEPT MUTATE MANIFEST_NOT_A_COUNT WANT_CHECK "which is not a count")
core_cpp_vendor_case(refuses-an-emptied-copy-beside-an-emptied-manifest
    WANT_SYNC ACCEPT MUTATE EMPTY_ALL WANT_CHECK "header line")
core_cpp_vendor_case(refuses-a-manifest-that-lists-no-file
    WANT_SYNC ACCEPT MUTATE EMPTY_HEADERS WANT_CHECK "never empty")

# What a commit may record that a verbatim copy cannot carry.
core_cpp_vendor_case(refuses-a-blob-with-a-cr-byte
    EXTRA "100644|src/core/Crlf.hpp|#pragma once\r\n"
    WANT_SYNC "contains a CR byte")
core_cpp_vendor_case(refuses-a-symbolic-link
    EXTRA "120000|src/core/Linked.hpp|Utils.hpp"
    WANT_SYNC "is a symbolic link")
core_cpp_vendor_case(refuses-a-submodule
    EXTRA "160000|src/core/net|0123456789abcdef0123456789abcdef01234567"
    WANT_SYNC "is a submodule")

# A CR byte, a link or a submodule outside the file set is no business of the tool's.
core_cpp_vendor_case(ignores-what-is-outside-the-file-set
    EXTRA "100644|docs/crlf.md|line\r\n" "120000|docs/link.md|index.md"
    WANT_SYNC ACCEPT WANT_CHECK ACCEPT
    EXPECT_FILES ${baseRepositoryFiles})

# A directory that is not a copy of ours is never emptied.
core_cpp_vendor_case(refuses-a-destination-that-is-not-a-copy
    OCCUPY "important.txt"
    WANT_SYNC "delete someone's work")

# --- re-syncing over an existing copy -------------------------------------------
#
# The second sync must leave exactly what the new selection names: a file the first copy had and
# the second does not is gone, not left behind for the check to call unlisted.
set(root "${WORK_DIR}/resyncs-over-an-existing-copy")
set(repository "${root}/repo")
set(copy "${root}/vendor/core-cpp")
file(REMOVE_RECURSE "${root}")
core_cpp_selftest_repository("${repository}" "${baseRepository}")
set(problems "")
set(arguments -DMODE=sync "-DREF=${fixtureTag}" "-DREPO=${repository}" "-DDEST=${copy}")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect("first sync" ACCEPT "${rc}" "${said}" problems)
set(arguments -DMODE=sync "-DREF=${fixtureTag}" "-DREPO=${repository}" "-DDEST=${copy}" -DMODULES=base)
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect("second sync" ACCEPT "${rc}" "${said}" problems)
if(EXISTS "${copy}/src/core/log/LogStore.hpp")
    string(APPEND problems " the second sync left src/core/log/LogStore.hpp, which its MODULES do not name")
endif()
set(arguments -DMODE=check "-DDEST=${copy}")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect(check ACCEPT "${rc}" "${said}" problems)
math(EXPR caseCount "${caseCount} + 1")
if(problems)
    string(APPEND failures "\n  resyncs-over-an-existing-copy:${problems}")
endif()

# --- a failed sync leaves the copy it found ------------------------------------
#
# docs/vendoring.md promises that a refusal leaves the previous copy exactly as it was, and the
# copy's own MODE=check is what a consumer registers as a test. A staging directory left inside
# DEST by a refusal would fail that test on a copy nothing touched, so both a refusal before the
# tree is read (an unresolvable ref) and one after every blob has been staged (a CR byte) are run
# over a good copy here, and the check must still accept it.
set(root "${WORK_DIR}/a-failed-sync-leaves-the-copy-alone")
set(repository "${root}/repo")
set(crRepository "${root}/repo-with-a-cr-byte")
set(copy "${root}/vendor/core-cpp")
file(REMOVE_RECURSE "${root}")
core_cpp_selftest_repository("${repository}" "${baseRepository}")
set(crRows ${baseRepository})
list(APPEND crRows "100644|src/core/Crlf.hpp|#pragma once\r\n")
core_cpp_selftest_repository("${crRepository}" "${crRows}")
set(problems "")
set(arguments -DMODE=sync "-DREF=${fixtureTag}" "-DREPO=${repository}" "-DDEST=${copy}")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect("the first sync" ACCEPT "${rc}" "${said}" problems)

set(arguments -DMODE=sync "-DREF=${absentCommit}" "-DREPO=${repository}" "-DDEST=${copy}")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect("a sync of a ref that does not resolve" "resolving the commit" "${rc}" "${said}" problems)
if(EXISTS "${copy}/${stagingName}")
    string(APPEND problems " the unresolvable ref left ${stagingName} inside the copy")
endif()
set(arguments -DMODE=check "-DDEST=${copy}")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect("the check after it" ACCEPT "${rc}" "${said}" problems)

set(arguments -DMODE=sync "-DREF=${fixtureTag}" "-DREPO=${crRepository}" "-DDEST=${copy}")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect("a sync of a tree with a CR byte" "contains a CR byte" "${rc}" "${said}" problems)
if(EXISTS "${copy}/${stagingName}")
    string(APPEND problems " the refused blob left ${stagingName} inside the copy")
endif()
set(arguments -DMODE=check "-DDEST=${copy}")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect("the check after it" ACCEPT "${rc}" "${said}" problems)
math(EXPR caseCount "${caseCount} + 1")
if(problems)
    string(APPEND failures "\n  a-failed-sync-leaves-the-copy-alone:${problems}")
endif()

# --- a sync run with a vendored copy's own script --------------------------------
#
# The documented check command and the MANIFEST's own header both point a consumer at
# <copy>/cmake/CoreCppVendor.cmake, so re-syncing with the script that is right there is the
# natural next thing to type. REPO then defaults to the copy's directory, and `git -C` ascends to
# the repository that CONTAINS the copy -- the consumer's own. Without a refusal the run empties
# the copy, refills it from whatever of the consumer's tree matches the file set, and writes a
# manifest that MODE=check then accepts.
#
# The fixture is the real thing rather than a sketch of one: a core-cpp-shaped repository carrying
# the tool itself, vendored into a consumer's repository with the tool's own MODE=sync, so the copy
# under test has a genuine MANIFEST and a genuine <copy>/cmake/CoreCppVendor.cmake. That matters
# twice -- the occupant check passes (the copy IS one of ours), so the run gets as far as the tree
# check it is meant to die on, and the copy's own MODE=check is available afterwards to say that
# nothing about it moved.
set(root "${WORK_DIR}/refuses-a-sync-from-inside-a-vendored-copy")
set(fixture "${root}/core-cpp")
set(enclosing "${root}/a-consumer")
set(copy "${enclosing}/vendor/core-cpp")
file(REMOVE_RECURSE "${root}")
set(fixtureRows ${baseRepository})
list(APPEND fixtureRows "copy|cmake/CoreCppVendor.cmake|${TOOL}")
core_cpp_selftest_repository("${fixture}" "${fixtureRows}")

file(MAKE_DIRECTORY "${enclosing}")
core_cpp_selftest_git("${enclosing}" "git init in ${enclosing}" init --quiet)
file(WRITE "${enclosing}/CMakeLists.txt" "project(a-consumer LANGUAGES CXX)\n")
file(WRITE "${enclosing}/src/consumer/Main.cpp" "int main() { return 0; }\n")
set(problems "")
set(arguments -DMODE=sync "-DREF=${fixtureTag}" "-DREPO=${fixture}" "-DDEST=${copy}")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect("vendoring the fixture into the consumer" ACCEPT "${rc}" "${said}" problems)
core_cpp_selftest_git("${enclosing}" "git add in ${enclosing}" add -A)
core_cpp_selftest_git("${enclosing}" "git commit in ${enclosing}" commit --quiet -m "a consumer with a vendored copy")
core_cpp_selftest_git("${enclosing}" "tagging ${fixtureTag}" tag "${fixtureTag}")
set(nestedTool "${copy}/cmake/CoreCppVendor.cmake")
if(NOT EXISTS "${nestedTool}")
    message(FATAL_ERROR "check-vendor-selftest: the vendored copy has no ${nestedTool} to run.")
endif()

set(arguments -DMODE=sync "-DREF=${fixtureTag}" "-DDEST=${copy}")
core_cpp_selftest_run_tool("${nestedTool}" arguments said rc)
core_cpp_selftest_expect("a sync with the copy's own script" "not a repository root" "${rc}" "${said}" problems)
# Pointed at the root of the enclosing repository, the ref resolves, the occupant check passes and
# the modules validate -- MODULES is optional -- so what has to stop it is the tree not being
# core-cpp's.
set(arguments -DMODE=sync "-DREF=${fixtureTag}" "-DREPO=${enclosing}" "-DDEST=${copy}")
core_cpp_selftest_run_tool("${nestedTool}" arguments said rc)
core_cpp_selftest_expect("a sync of the consumer's own repository" "is not a core-cpp tree" "${rc}" "${said}" problems)

# The copy is what it was: not merely still there, but still passing the check the consumer
# registers as a test of its own.
if(EXISTS "${copy}/${stagingName}")
    string(APPEND problems " the refused sync left ${stagingName} inside the copy")
endif()
set(arguments -DMODE=check "-DDEST=${copy}")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect("the copy's own check afterwards" ACCEPT "${rc}" "${said}" problems)
math(EXPR caseCount "${caseCount} + 1")
if(problems)
    string(APPEND failures "\n  refuses-a-sync-from-inside-a-vendored-copy:${problems}")
endif()

# --- the command line itself ----------------------------------------------------
#
# The three refusals that come before anything else happens. They need no repository, and their
# messages are the only documentation a mistyped command line gets.
set(problems "")
set(arguments -DMODE=export "-DDEST=${WORK_DIR}/nowhere")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect("a mode the tool has not" "MODE must be sync or check" "${rc}" "${said}" problems)
set(arguments -DMODE=check)
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect("a check with no DEST" "DEST is not set" "${rc}" "${said}" problems)
set(arguments -DMODE=sync "-DDEST=${WORK_DIR}/nowhere")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect("a sync with no REF" "REF is not set" "${rc}" "${said}" problems)
if(EXISTS "${WORK_DIR}/nowhere")
    string(APPEND problems " a refused command line created ${WORK_DIR}/nowhere")
endif()
math(EXPR caseCount "${caseCount} + 1")
if(problems)
    string(APPEND failures "\n  refuses-a-command-line-it-cannot-act-on:${problems}")
endif()

# --- a check with no git on PATH ------------------------------------------------
#
# A consumer runs MODE=check as a test in its own CI, where git need not be installed at all, so
# the check must never reach for it. PATH is replaced with an empty directory for the run; cmake
# itself is invoked by its absolute path and needs nothing from PATH.
set(root "${WORK_DIR}/checks-without-git-on-path")
set(repository "${root}/repo")
set(copy "${root}/vendor/core-cpp")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}/empty")
core_cpp_selftest_repository("${repository}" "${baseRepository}")
set(problems "")
set(arguments -DMODE=sync "-DREF=${fixtureTag}" "-DREPO=${repository}" "-DDEST=${copy}")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect(sync ACCEPT "${rc}" "${said}" problems)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${root}/empty"
            "${CMAKE_COMMAND}" -DMODE=check "-DDEST=${copy}" -P "${TOOL}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
string(REGEX REPLACE "[ \t\r\n]+" " " said "${out} ${err}")
core_cpp_selftest_expect("check with an empty PATH" ACCEPT "${rc}" "${said}" problems)
math(EXPR caseCount "${caseCount} + 1")
if(problems)
    string(APPEND failures "\n  checks-without-git-on-path:${problems}")
endif()

# --- a sync from a URL ----------------------------------------------------------
#
# REPO takes a remote as well as a path; a file:// URL is a remote git clones from without a
# network, which is the whole of that path apart from where the bytes come over.
set(root "${WORK_DIR}/syncs-from-a-url")
set(repository "${root}/repo")
set(copy "${root}/vendor/core-cpp")
file(REMOVE_RECURSE "${root}")
core_cpp_selftest_repository("${repository}" "${baseRepository}")
set(problems "")
# file://<absolute path>: three slashes before a Windows drive letter, two before a leading slash.
set(url "file://${repository}")
if(NOT repository MATCHES "^/")
    set(url "file:///${repository}")
endif()
set(arguments -DMODE=sync "-DREF=${fixtureTag}" "-DREPO=${url}" "-DDEST=${copy}")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect(sync ACCEPT "${rc}" "${said}" problems)
set(arguments -DMODE=check "-DDEST=${copy}")
core_cpp_selftest_run(arguments said rc)
core_cpp_selftest_expect(check ACCEPT "${rc}" "${said}" problems)
math(EXPR caseCount "${caseCount} + 1")
if(problems)
    string(APPEND failures "\n  syncs-from-a-url:${problems}")
endif()

if(failures)
    message(FATAL_ERROR "check-vendor-selftest: ${caseCount} case(s) run, these went wrong:${failures}")
endif()
message(STATUS "check-vendor-selftest: all ${caseCount} case(s) were accepted or refused as expected")
