# SPDX-License-Identifier: Apache-2.0
#
# Proves tests/cmake/check-release.cmake: it accepts a tree whose tag, project(VERSION) and
# CHANGELOG agree, and refuses each way they can disagree, by the phrase that names that refusal
# and no other. A checker that refused everything would pass the refusing cases alone, so the
# accepting cases are half of the proof.
#
# Usage: cmake -DCHECKER=<path to check-release.cmake> -DWORK_DIR=<scratch directory>
#              -P tests/cmake/check-release-selftest.cmake

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CHECKER OR NOT EXISTS "${CHECKER}")
    message(FATAL_ERROR "check-release-selftest: CHECKER ('${CHECKER}') is not set or does not exist.")
endif()
if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
    message(FATAL_ERROR "check-release-selftest: WORK_DIR is not set.")
endif()

# Every phrase the checker refuses with, so a case can assert that no other refusal fired.
set(refusalPhrases
    "is not vX.Y.Z"
    "has no project(core-cpp ... VERSION"
    "does not match project(VERSION"
    "CHANGELOG.md does not exist"
    "has no '## [")

# The top-level CMakeLists.txt, laid out the way the real one is: the call spans several lines.
set(projectAt010 "# SPDX-License-Identifier: Apache-2.0\ncmake_minimum_required(VERSION 3.25...3.31)\n\nproject(core-cpp\n    VERSION 0.1.0\n    DESCRIPTION \"A description\"\n    LANGUAGES CXX)\n")
set(projectWithoutVersion "cmake_minimum_required(VERSION 3.25...3.31)\nproject(core-cpp LANGUAGES CXX)\n")
set(changelogReleased "# Changelog\n\n## [Unreleased]\n\n## [0.1.0] - 2026-10-01\n\n### Added\n- Everything.\n")
set(changelogUnreleasedOnly "# Changelog\n\n## [Unreleased]\n\n### Added\n- Everything.\n")
set(changelogLongerVersion "# Changelog\n\n## [Unreleased]\n\n## [0.1.01] - 2026-10-01\n")
set(changelogCodeMention "# Changelog\n\n## [Unreleased]\n\nThe next section will be `## [0.1.0]`.\n")

set(caseCount 0)
set(failures "")

## @brief Stages a tree from @p cmakeLists and @p changelog (NONE: no CHANGELOG.md), runs the
## checker on it with @p tag, and requires @p want: ACCEPT, or the refusal phrase expected.
function(core_cpp_release_case name tag cmakeLists changelog want)
    set(root "${WORK_DIR}/${name}")
    file(REMOVE_RECURSE "${root}")
    file(MAKE_DIRECTORY "${root}")
    file(WRITE "${root}/CMakeLists.txt" "${cmakeLists}")
    if(NOT changelog STREQUAL "NONE")
        file(WRITE "${root}/CHANGELOG.md" "${changelog}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DTAG=${tag}" "-DROOT=${root}" -P "${CHECKER}"
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    # CMake wraps a FATAL_ERROR message at about 76 columns, so a phrase can straddle a line break.
    string(REGEX REPLACE "[ \t\r\n]+" " " said "${out} ${err}")

    set(problems "")
    if(want STREQUAL "ACCEPT")
        if(NOT rc EQUAL 0)
            string(APPEND problems " refused (exit ${rc}) a tree it must accept:${said}")
        elseif(NOT said MATCHES "check-release: v[0-9.]+ matches")
            string(APPEND problems " exited 0 without saying what it checked:${said}")
        endif()
    else()
        if(rc EQUAL 0)
            string(APPEND problems " accepted a tree it must refuse:${said}")
        else()
            string(FIND "${said}" "${want}" at)
            if(at EQUAL -1)
                string(APPEND problems " refused, but not with '${want}':${said}")
            endif()
        endif()
        foreach(other IN LISTS refusalPhrases)
            if(other STREQUAL want)
                continue()
            endif()
            string(FIND "${said}" "${other}" at)
            if(NOT at EQUAL -1)
                string(APPEND problems " also refused with '${other}', which this tree does not deserve")
            endif()
        endforeach()
    endif()

    math(EXPR count "${caseCount} + 1")
    set(caseCount ${count} PARENT_SCOPE)
    if(problems)
        set(failures "${failures}\n  ${name}:${problems}" PARENT_SCOPE)
    endif()
endfunction()

core_cpp_release_case(accepts-matching-tree v0.1.0 "${projectAt010}" "${changelogReleased}" ACCEPT)
core_cpp_release_case(refuses-other-version v0.2.0 "${projectAt010}" "${changelogReleased}"
    "does not match project(VERSION")
core_cpp_release_case(refuses-missing-changelog-section v0.1.0 "${projectAt010}" "${changelogUnreleasedOnly}"
    "has no '## [")
core_cpp_release_case(refuses-longer-version-section v0.1.0 "${projectAt010}" "${changelogLongerVersion}"
    "has no '## [")
core_cpp_release_case(refuses-section-mentioned-in-text v0.1.0 "${projectAt010}" "${changelogCodeMention}"
    "has no '## [")
core_cpp_release_case(refuses-missing-changelog v0.1.0 "${projectAt010}" NONE
    "CHANGELOG.md does not exist")
core_cpp_release_case(refuses-project-without-version v0.1.0 "${projectWithoutVersion}" "${changelogReleased}"
    "has no project(core-cpp ... VERSION")
core_cpp_release_case(refuses-tag-without-v 0.1.0 "${projectAt010}" "${changelogReleased}" "is not vX.Y.Z")
core_cpp_release_case(refuses-release-candidate-tag v0.1.0-rc1 "${projectAt010}" "${changelogReleased}"
    "is not vX.Y.Z")
core_cpp_release_case(refuses-two-part-tag v0.1 "${projectAt010}" "${changelogReleased}" "is not vX.Y.Z")

if(failures)
    message(FATAL_ERROR "check-release-selftest: ${caseCount} case(s) run, these went wrong:${failures}")
endif()
message(STATUS "check-release-selftest: all ${caseCount} case(s) were accepted or refused as expected")
