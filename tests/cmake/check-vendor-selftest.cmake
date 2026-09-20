# SPDX-License-Identifier: Apache-2.0
#
# Proves cmake/CoreCppVendor.cmake: it exports the file set of Part I §5 from a git repository
# byte for byte, and it refuses each way a copy can stop being verbatim -- by the phrase that names
# that refusal and no other. A tool that refused everything would pass the refusing cases alone, so
# the accepting cases are half of the proof.
#
# Each case builds a git repository of its own in WORK_DIR: four files of the vendored set and two
# outside it (the brief's three files plus a dot-file, a module directory and the two that must NOT
# be copied), and whatever the case adds -- a blob with a CR byte, a symbolic link, a submodule.
# The repository is made through git's index rather than a checkout, so a symbolic link is recorded
# on a Windows host too, where creating one needs a privilege the test does not have.
#
# Usage: cmake -DTOOL=<path to cmake/CoreCppVendor.cmake> -DWORK_DIR=<scratch directory>
#              -P tests/cmake/check-vendor-selftest.cmake

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED TOOL OR NOT EXISTS "${TOOL}")
    message(FATAL_ERROR "check-vendor-selftest: TOOL ('${TOOL}') is not set or does not exist.")
endif()
if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
    message(FATAL_ERROR "check-vendor-selftest: WORK_DIR is not set.")
endif()
find_program(CORE_CPP_SELFTEST_GIT NAMES git)
if(NOT CORE_CPP_SELFTEST_GIT)
    message(FATAL_ERROR
        "check-vendor-selftest: git is not on PATH, and every case builds a repository with it. "
        "(The tool's MODE=check needs no git; this test of it does.)")
endif()

# Every phrase the tool refuses with, so a case can assert that no other refusal fired.
set(refusalPhrases
    "is a symbolic link"
    "is a submodule"
    "contains a CR byte"
    "differs from the manifest"
    "is missing"
    "is not in the manifest"
    "delete someone's work"
    "has no module for")

# The repository every case starts from: "<mode>|<path>|<content>". The four files of the vendored
# set are a named file, a dot-file, the base module (src/core/ itself) and a module directory; the
# two others are outside the set and must never be copied. A row is one element of a CMake list, so
# no content here carries a semicolon.
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
    "100644|src/core/Utils.hpp|#pragma once\n// namespace core\n"
    "100644|src/core/log/LogStore.hpp|#pragma once\n// namespace core::log\n"
    "100644|docs/index.md|# Not part of the vendored set\n"
    "100644|tests/Foo_test.cpp|// Not part of the vendored set\n")
set(baseRepositoryFiles ".clang-format" "CMakeLists.txt" "src/core/Utils.hpp" "src/core/log/LogStore.hpp")

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

## @brief Sets @p outVar to what a tool run said, with its line breaks flattened.
##
## CMake wraps a FATAL_ERROR message at about 76 columns, so a phrase can straddle a line break.
function(core_cpp_selftest_run outVar rcVar)
    execute_process(COMMAND "${CMAKE_COMMAND}" ${ARGN} -P "${TOOL}"
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    string(REGEX REPLACE "[ \t\r\n]+" " " said "${out} ${err}")
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
##   MODULES <list>       the MODULES argument of the sync (default: all of them)
##   OCCUPY <name>        a file of someone else's, placed in DEST before the sync
##   MUTATE FLIP|ADD|REMOVE   what happens to the copy between the sync and the check
##   WANT_SYNC  <ACCEPT or phrase>
##   WANT_CHECK <ACCEPT or phrase>   (omitted: the check is not run)
##   EXPECT_FILES <path>...   the exact set of paths the manifest must list
function(core_cpp_vendor_case name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "MUTATE;OCCUPY;WANT_SYNC;WANT_CHECK"
                          "EXTRA;MODULES;EXPECT_FILES")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "check-vendor-selftest: case ${name} has unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
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

    set(modulesArgument "")
    if(arg_MODULES)
        string(REPLACE ";" "\\;" modulesList "${arg_MODULES}")
        set(modulesArgument "-DMODULES=${modulesList}")
    endif()
    core_cpp_selftest_run(said rc -DMODE=sync "-DREF=HEAD" "-DREPO=${repository}" "-DDEST=${copy}"
                          ${modulesArgument})
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
    endif()

    if(DEFINED arg_WANT_CHECK)
        core_cpp_selftest_run(said rc -DMODE=check "-DDEST=${copy}")
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
core_cpp_vendor_case(syncs-then-checks
    WANT_SYNC ACCEPT WANT_CHECK ACCEPT
    EXPECT_FILES ${baseRepositoryFiles})

# MODULES selects module directories; src/core/ itself is `base`.
core_cpp_vendor_case(syncs-the-modules-it-is-given
    MODULES "base"
    WANT_SYNC ACCEPT WANT_CHECK ACCEPT
    EXPECT_FILES ".clang-format" "CMakeLists.txt" "src/core/Utils.hpp")
core_cpp_vendor_case(refuses-a-module-the-ref-has-not
    MODULES "base;tui"
    WANT_SYNC "has no module for")

# The three ways a copy stops being what its manifest says.
core_cpp_vendor_case(refuses-a-changed-file
    WANT_SYNC ACCEPT MUTATE FLIP WANT_CHECK "differs from the manifest")
core_cpp_vendor_case(refuses-an-unlisted-file
    WANT_SYNC ACCEPT MUTATE ADD WANT_CHECK "is not in the manifest")
core_cpp_vendor_case(refuses-a-missing-file
    WANT_SYNC ACCEPT MUTATE REMOVE WANT_CHECK "is missing")

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
core_cpp_selftest_run(said rc -DMODE=sync -DREF=HEAD "-DREPO=${repository}" "-DDEST=${copy}")
core_cpp_selftest_expect("first sync" ACCEPT "${rc}" "${said}" problems)
core_cpp_selftest_run(said rc -DMODE=sync -DREF=HEAD "-DREPO=${repository}" "-DDEST=${copy}" -DMODULES=base)
core_cpp_selftest_expect("second sync" ACCEPT "${rc}" "${said}" problems)
if(EXISTS "${copy}/src/core/log/LogStore.hpp")
    string(APPEND problems " the second sync left src/core/log/LogStore.hpp, which its MODULES do not name")
endif()
core_cpp_selftest_run(said rc -DMODE=check "-DDEST=${copy}")
core_cpp_selftest_expect(check ACCEPT "${rc}" "${said}" problems)
math(EXPR caseCount "${caseCount} + 1")
if(problems)
    string(APPEND failures "\n  resyncs-over-an-existing-copy:${problems}")
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
core_cpp_selftest_run(said rc -DMODE=sync -DREF=HEAD "-DREPO=${repository}" "-DDEST=${copy}")
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
core_cpp_selftest_run(said rc -DMODE=sync -DREF=HEAD "-DREPO=${url}" "-DDEST=${copy}")
core_cpp_selftest_expect(sync ACCEPT "${rc}" "${said}" problems)
core_cpp_selftest_run(said rc -DMODE=check "-DDEST=${copy}")
core_cpp_selftest_expect(check ACCEPT "${rc}" "${said}" problems)
math(EXPR caseCount "${caseCount} + 1")
if(problems)
    string(APPEND failures "\n  syncs-from-a-url:${problems}")
endif()

if(failures)
    message(FATAL_ERROR "check-vendor-selftest: ${caseCount} case(s) run, these went wrong:${failures}")
endif()
message(STATUS "check-vendor-selftest: all ${caseCount} case(s) were accepted or refused as expected")
