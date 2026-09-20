# SPDX-License-Identifier: Apache-2.0
#
# The vendoring tool of Part I §5 of the design spec: it exports a verbatim copy of core-cpp into
# a consumer's tree, and it verifies that such a copy is still verbatim.
#
#   cmake -DMODE=sync -DREF=<tag or full SHA> -DDEST=<dir> [-DREPO=<url or path>]
#         [-DMODULES=base;log;cli;platform;async;net;testing]
#         -P <core-cpp>/cmake/CoreCppVendor.cmake
#
#   cmake -DMODE=check -DDEST=<dir> -P <dir>/cmake/CoreCppVendor.cmake
#
# sync reads git BLOBS, never a working tree: `git -c core.autocrlf=false -c core.eol=lf cat-file
# blob` hands over the bytes the commit records, whatever the machine's line-ending configuration
# is. That is the whole point of the exercise -- a copy that a checkout's `core.autocrlf` rewrote
# is not the tree that was reviewed, tested and tagged, and its hashes would differ per machine.
# For the same reason a CR byte, a symbolic link and a submodule are refused: the first means a
# file git treated as binary (a text import never is), and neither of the other two survives being
# copied into another repository as the bytes it names.
#
# check needs no git at all, because a consumer runs it as a test in its own CI, where core-cpp is
# a directory of files and nothing else. It re-hashes every file the MANIFEST lists and refuses a
# hash mismatch, a file that is missing and a file the manifest does not list -- so a local edit,
# the one thing a vendored copy may never carry, fails the consumer's own test suite.
#
# Every refusal that applies is reported, not only the first.
# tests/cmake/check-vendor-selftest.cmake proves each of them by name.

cmake_minimum_required(VERSION 3.25)

# --- the file set (Part I §5) --------------------------------------------------
#
# The files taken by name. Everything else is selected by directory: cmake/**, the base module
# (every file directly in src/core/) and src/core/<module>/** for each module asked for.
#
# The spec says "src/core/*.hpp|cpp" for base; the whole directory is taken instead, because
# src/core/CMakeLists.txt is the base module's own CMakeLists and src/core/Config.hpp.in is what
# the top-level configure_file() generates core/Config.hpp from. Without those two the copy does
# not configure, so the narrower reading would ship a tree that cannot be built.
set(CORE_CPP_VENDOR_NAMED_FILES
    CMakeLists.txt
    LICENSE
    NOTICE
    README.md
    CHANGELOG.md
    .clang-format
    .clang-tidy)

# The manifest's name inside the copy, and the staging directory a sync assembles the new copy in
# before it replaces the old one (so a refusal leaves the previous copy intact).
set(CORE_CPP_VENDOR_MANIFEST "MANIFEST")
set(CORE_CPP_VENDOR_STAGING ".core-cpp-vendor-staging")

## @brief Sets @p outVar to ON when @p path belongs to the file set for @p modules.
##
## @param path A repository-relative path, as git ls-tree reports it.
## @param modules The module names the copy carries; `base` is src/core/ itself.
## @param outVar Receives ON or OFF.
function(core_cpp_vendor_selects path modules outVar)
    set(selected OFF)
    if(path IN_LIST CORE_CPP_VENDOR_NAMED_FILES)
        set(selected ON)
    elseif(path MATCHES "^cmake/")
        set(selected ON)
    elseif(path MATCHES "^src/core/[^/]+$")
        if("base" IN_LIST modules)
            set(selected ON)
        endif()
    elseif(path MATCHES "^src/core/([^/]+)/")
        if("${CMAKE_MATCH_1}" IN_LIST modules)
            set(selected ON)
        endif()
    endif()
    set(${outVar} ${selected} PARENT_SCOPE)
endfunction()

## @brief Runs git in the repository @p repo and sets @p outVar to its standard output.
##
## Stops the script when git fails, saying what it was doing. The two -c options are on every
## invocation, including the ones that only read metadata: one place to state them is one place
## for them to be wrong.
##
## @param outVar Receives the output, with trailing newlines removed.
## @param repo The repository to run in.
## @param what What the call was for, for the message when it fails.
## @param ARGN The git arguments.
function(core_cpp_vendor_git outVar repo what)
    execute_process(
        COMMAND "${CORE_CPP_VENDOR_GIT}" -c core.autocrlf=false -c core.eol=lf -C "${repo}" ${ARGN}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "core-cpp-vendor: ${what} failed (git exited ${rc}): ${out}${err}")
    endif()
    string(REGEX REPLACE "[\r\n]+$" "" out "${out}")
    set(${outVar} "${out}" PARENT_SCOPE)
endfunction()

## @brief Lists the files of @p dir, relative to it, with @p ARGN excluded.
##
## file(GLOB_RECURSE) matches a leading dot, so .clang-format and .clang-tidy are found too.
function(core_cpp_vendor_list_files dir outVar)
    file(GLOB_RECURSE found LIST_DIRECTORIES false RELATIVE "${dir}" "${dir}/*")
    foreach(excluded IN LISTS ARGN)
        list(REMOVE_ITEM found "${excluded}")
    endforeach()
    list(SORT found)
    set(${outVar} "${found}" PARENT_SCOPE)
endfunction()

# --- arguments -----------------------------------------------------------------

if(NOT DEFINED MODE OR NOT MODE MATCHES "^(sync|check)$")
    message(FATAL_ERROR
        "core-cpp-vendor: MODE must be sync or check, not '${MODE}'.\n"
        "  cmake -DMODE=sync -DREF=<tag> -DDEST=<dir> [-DREPO=<url or path>] [-DMODULES=<a;b>] "
        "-P <core-cpp>/cmake/CoreCppVendor.cmake\n"
        "  cmake -DMODE=check -DDEST=<dir> -P <dir>/cmake/CoreCppVendor.cmake")
endif()
if(NOT DEFINED DEST OR DEST STREQUAL "")
    message(FATAL_ERROR "core-cpp-vendor: DEST is not set; it names the directory the copy lives in.")
endif()
get_filename_component(DEST "${DEST}" ABSOLUTE)

# --- MODE=check ----------------------------------------------------------------
#
# No git, no network, no REF: the copy and its manifest are all this needs.
if(MODE STREQUAL "check")
    set(manifestPath "${DEST}/${CORE_CPP_VENDOR_MANIFEST}")
    if(NOT EXISTS "${manifestPath}")
        message(FATAL_ERROR
            "core-cpp-vendor: ${manifestPath} does not exist, so ${DEST} is not a core-cpp vendored "
            "copy. Re-create it with MODE=sync.")
    endif()

    file(STRINGS "${manifestPath}" manifestLines)
    set(refusals "")
    set(listed "")
    set(statedCount "")
    set(commit "unknown")
    foreach(line IN LISTS manifestLines)
        if(line MATCHES "^# files (.+)$")
            set(statedCount "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^# commit (.+)$")
            set(commit "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^#")
            continue()
        elseif(line MATCHES "^([0-9a-f]+)  (.+)$")
            set(wanted "${CMAKE_MATCH_1}")
            set(path "${CMAKE_MATCH_2}")
            list(APPEND listed "${path}")
            if(NOT EXISTS "${DEST}/${path}")
                string(APPEND refusals "\n  ${path}: is missing")
                continue()
            endif()
            file(SHA256 "${DEST}/${path}" got)
            if(NOT got STREQUAL wanted)
                string(APPEND refusals
                    "\n  ${path}: differs from the manifest (expected ${wanted}, found ${got})")
            endif()
        elseif(NOT line STREQUAL "")
            string(APPEND refusals "\n  ${manifestPath}: line '${line}' is neither a header nor '<sha256>  <path>'")
        endif()
    endforeach()

    core_cpp_vendor_list_files("${DEST}" present "${CORE_CPP_VENDOR_MANIFEST}")
    foreach(path IN LISTS present)
        if(NOT path IN_LIST listed)
            string(APPEND refusals "\n  ${path}: is not in the manifest")
        endif()
    endforeach()

    list(LENGTH listed listedCount)
    if(NOT statedCount STREQUAL "" AND NOT statedCount STREQUAL "${listedCount}")
        string(APPEND refusals
            "\n  ${CORE_CPP_VENDOR_MANIFEST}: says '# files ${statedCount}' but lists ${listedCount}")
    endif()

    if(refusals)
        message(FATAL_ERROR
            "core-cpp-vendor: ${DEST} is not the verbatim copy its manifest describes:${refusals}\n"
            "A vendored copy carries no local change: fix it in core-cpp, release, and re-run "
            "MODE=sync. To restore this copy, re-run MODE=sync with the manifest's ref.")
    endif()
    message(STATUS
        "core-cpp-vendor: ${DEST} matches its manifest: ${listedCount} file(s), commit ${commit}")
    return()
endif()

# --- MODE=sync -----------------------------------------------------------------

if(NOT DEFINED REF OR REF STREQUAL "")
    message(FATAL_ERROR
        "core-cpp-vendor: REF is not set; it names the tag or full commit SHA to copy. "
        "A branch is not a valid source: it changes under the consumer without a commit of its own.")
endif()

find_program(CORE_CPP_VENDOR_GIT NAMES git)
if(NOT CORE_CPP_VENDOR_GIT)
    message(FATAL_ERROR "core-cpp-vendor: MODE=sync needs git, which is not on PATH. (MODE=check does not.)")
endif()

# REPO defaults to the repository this script is part of, which is what a core-cpp checkout's own
# `cmake -DMODE=sync ...` means. Anything that is not a directory is a remote and is cloned, bare
# and once, into the staging area; a local path (a checkout or a bare repository) is read in place.
if(NOT DEFINED REPO OR REPO STREQUAL "")
    get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(stagingDir "${DEST}/${CORE_CPP_VENDOR_STAGING}")
file(REMOVE_RECURSE "${stagingDir}")

# Before anything is written: an existing DEST is only ever emptied when it is a copy of ours, and
# the manifest is what says so. Nothing else may live in a vendored copy anyway -- MODE=check
# refuses an unlisted file -- so a directory with files and no manifest is someone else's work.
if(EXISTS "${DEST}" AND NOT EXISTS "${DEST}/${CORE_CPP_VENDOR_MANIFEST}")
    core_cpp_vendor_list_files("${DEST}" occupants)
    if(occupants)
        list(LENGTH occupants occupantCount)
        message(FATAL_ERROR
            "core-cpp-vendor: ${DEST} holds ${occupantCount} file(s) and no "
            "${CORE_CPP_VENDOR_MANIFEST}, so it is not a core-cpp vendored copy and syncing would "
            "delete someone's work. Empty it, or point DEST at a new directory.")
    endif()
endif()

file(MAKE_DIRECTORY "${stagingDir}")

set(repoPath "${REPO}")
set(clonePath "")
if(NOT IS_DIRECTORY "${REPO}")
    set(clonePath "${stagingDir}/repo.git")
    message(STATUS "core-cpp-vendor: cloning ${REPO} (a local path is read in place instead)")
    execute_process(
        COMMAND "${CORE_CPP_VENDOR_GIT}" -c core.autocrlf=false -c core.eol=lf
                clone --quiet --bare "${REPO}" "${clonePath}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        file(REMOVE_RECURSE "${stagingDir}")
        message(FATAL_ERROR "core-cpp-vendor: cloning ${REPO} failed (git exited ${rc}): ${out}${err}")
    endif()
    set(repoPath "${clonePath}")
endif()

core_cpp_vendor_git(commit "${repoPath}" "resolving REF '${REF}' in ${REPO}" rev-parse --verify "${REF}^{commit}")

# One `ls-tree -r -z` for the whole tree: NUL-separated, so no path needs quoting, and
# file(STRINGS) splits a NUL-separated file into exactly one entry per record.
set(lsTreeFile "${stagingDir}/ls-tree")
execute_process(
    COMMAND "${CORE_CPP_VENDOR_GIT}" -c core.autocrlf=false -c core.eol=lf -C "${repoPath}"
            ls-tree -r -z "${commit}"
    RESULT_VARIABLE rc OUTPUT_FILE "${lsTreeFile}" ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    file(REMOVE_RECURSE "${stagingDir}")
    message(FATAL_ERROR "core-cpp-vendor: listing the tree of ${commit} failed (git exited ${rc}): ${err}")
endif()
file(STRINGS "${lsTreeFile}" entries)

# Pass one: what the ref has. A module is a directory under src/core/; `base` is src/core/ itself.
set(availableModules "base")
foreach(entry IN LISTS entries)
    if(entry MATCHES "^[0-7]+ [a-z]+ [0-9a-f]+\t(src/core/([^/]+)/)")
        if(NOT "${CMAKE_MATCH_2}" IN_LIST availableModules)
            list(APPEND availableModules "${CMAKE_MATCH_2}")
        endif()
    endif()
endforeach()

if(NOT DEFINED MODULES OR MODULES STREQUAL "")
    set(MODULES ${availableModules})
endif()
set(unknownModules "")
foreach(module IN LISTS MODULES)
    if(NOT module IN_LIST availableModules)
        list(APPEND unknownModules "${module}")
    endif()
endforeach()
if(unknownModules)
    file(REMOVE_RECURSE "${stagingDir}")
    list(JOIN availableModules ";" known)
    message(FATAL_ERROR
        "core-cpp-vendor: MODULES names '${unknownModules}', which ${REF} has no module for "
        "(it has: ${known}).")
endif()

# Pass two: copy every selected blob into the staging tree, and collect every reason not to.
set(refusals "")
set(copied "")
foreach(entry IN LISTS entries)
    if(NOT entry MATCHES "^([0-7]+) ([a-z]+) ([0-9a-f]+)\t(.+)$")
        string(APPEND refusals "\n  ${entry}: git ls-tree said this, which is not '<mode> <type> <sha><TAB><path>'")
        continue()
    endif()
    set(mode "${CMAKE_MATCH_1}")
    set(type "${CMAKE_MATCH_2}")
    set(blob "${CMAKE_MATCH_3}")
    set(path "${CMAKE_MATCH_4}")

    core_cpp_vendor_selects("${path}" "${MODULES}" selected)
    if(NOT selected)
        continue()
    endif()

    # A gitlink is a commit of another repository, recorded by SHA and nothing else; copying it
    # would copy a name for content that is not here.
    if(mode STREQUAL "160000" OR type STREQUAL "commit")
        string(APPEND refusals "\n  ${path}: is a submodule (gitlink ${blob}); a vendored copy carries files only")
        continue()
    endif()
    # A symlink's blob is its target path. Written as a file it is a file of a path; restored as a
    # link it depends on a filesystem and a checkout setting the consumer may not have.
    if(mode STREQUAL "120000")
        string(APPEND refusals "\n  ${path}: is a symbolic link; a vendored copy carries regular files only")
        continue()
    endif()

    set(target "${stagingDir}/${path}")
    get_filename_component(targetDir "${target}" DIRECTORY)
    file(MAKE_DIRECTORY "${targetDir}")
    execute_process(
        COMMAND "${CORE_CPP_VENDOR_GIT}" -c core.autocrlf=false -c core.eol=lf -C "${repoPath}"
                cat-file blob "${blob}"
        RESULT_VARIABLE rc OUTPUT_FILE "${target}" ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        string(APPEND refusals "\n  ${path}: reading blob ${blob} failed (git exited ${rc}): ${err}")
        continue()
    endif()

    # The bytes are the commit's, so a CR here is a CR the repository itself records. .gitattributes
    # normalises text to LF, so such a file is one git treated as binary -- which a text import,
    # and this file set, never is.
    #
    # Read as hex, because a plain file(READ) opens the file in text mode on Windows and hands back
    # a string a CRLF has already been taken out of. "0d" also occurs straddling two bytes (0x40
    # 0xd9 reads "40d9"), so the cheap search only decides whether the exact one is worth its cost.
    file(READ "${target}" hexContent HEX)
    string(FIND "${hexContent}" "0d" maybeCarriageReturn)
    if(NOT maybeCarriageReturn EQUAL -1)
        string(REGEX REPLACE "(..)" "\\1;" hexBytes "${hexContent}")
        list(FIND hexBytes "0d" carriageReturnAt)
        if(NOT carriageReturnAt EQUAL -1)
            string(APPEND refusals "\n  ${path}: contains a CR byte (at offset ${carriageReturnAt})")
            continue()
        endif()
    endif()
    list(APPEND copied "${path}")
endforeach()

if(NOT copied AND NOT refusals)
    string(APPEND refusals "\n  ${REF} has no file of the vendored set for modules '${MODULES}'")
endif()
if(refusals)
    file(REMOVE_RECURSE "${stagingDir}")
    message(FATAL_ERROR
        "core-cpp-vendor: refusing to vendor ${REF} from ${REPO}:${refusals}\n"
        "${DEST} is unchanged.")
endif()

# The copy is whole and legal; only now is the old one replaced -- a refusal above left the
# previous copy exactly as it was.
file(GLOB existing LIST_DIRECTORIES true "${DEST}/*")
foreach(entry IN LISTS existing)
    if(NOT entry STREQUAL "${stagingDir}")
        file(REMOVE_RECURSE "${entry}")
    endif()
endforeach()

file(REMOVE "${lsTreeFile}")
if(clonePath)
    file(REMOVE_RECURSE "${clonePath}")
endif()
file(GLOB staged LIST_DIRECTORIES true "${stagingDir}/*")
foreach(entry IN LISTS staged)
    get_filename_component(name "${entry}" NAME)
    file(RENAME "${entry}" "${DEST}/${name}")
endforeach()
file(REMOVE_RECURSE "${stagingDir}")

# The manifest, hashed from the files as they now lie in DEST, sorted by path so the same ref
# always lists the same files in the same order.
#
# Its own line endings are the writing host's -- CMake writes a text file in the host's convention,
# and there is no binary file write in script mode. That decides nothing: MODE=check reads it with
# file(STRINGS), which ignores CR, so a copy made on Windows verifies on Linux and the other way
# round. The FILES are unaffected: each is written from git's blob through a process's stdout,
# which is never translated.
list(SORT copied)
list(LENGTH copied fileCount)
list(JOIN MODULES ";" modulesLine)
set(manifest "# core-cpp vendored copy -- verify it with:\n")
string(APPEND manifest "#   cmake -DMODE=check -DDEST=<this directory> -P <this directory>/cmake/CoreCppVendor.cmake\n")
string(APPEND manifest "# repository ${REPO}\n")
string(APPEND manifest "# ref ${REF}\n")
string(APPEND manifest "# commit ${commit}\n")
string(APPEND manifest "# modules ${modulesLine}\n")
string(APPEND manifest "# files ${fileCount}\n")
foreach(path IN LISTS copied)
    file(SHA256 "${DEST}/${path}" hash)
    string(APPEND manifest "${hash}  ${path}\n")
endforeach()
file(WRITE "${DEST}/${CORE_CPP_VENDOR_MANIFEST}" "${manifest}")

message(STATUS
    "core-cpp-vendor: ${REF} (${commit}) copied into ${DEST}: ${fileCount} file(s), "
    "modules ${modulesLine}")
