# SPDX-License-Identifier: Apache-2.0
#
# Refuses a release tag that does not name the tree it releases (Part I §4 of the design spec):
#
#   * the tag is v<X.Y.Z>, a numeric triple and nothing else;
#   * X.Y.Z equals the version literal of project(core-cpp VERSION ...) in CMakeLists.txt, which
#     is the source of truth;
#   * CHANGELOG.md has a "## [X.Y.Z]" section, so the release says what it contains.
#
# Every refusal that applies is reported, not only the first. The release workflow runs this on
# the pushed tag; check-release-selftest.cmake proves each refusal.
#
# Usage: cmake -DTAG=vX.Y.Z [-DROOT=<source tree>] -P tests/cmake/check-release.cmake

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ROOT OR ROOT STREQUAL "")
    get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
if(NOT DEFINED TAG OR TAG STREQUAL "")
    message(FATAL_ERROR "check-release: TAG is not set. Usage: cmake -DTAG=vX.Y.Z -P tests/cmake/check-release.cmake")
endif()

# A string rather than a list: a reason may contain ';' or '['.
set(refusals "")

set(version "")
if(TAG MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)$")
    set(version "${CMAKE_MATCH_1}")
else()
    string(APPEND refusals "\n  tag '${TAG}' is not vX.Y.Z: a release tag is 'v' followed by a numeric triple")
endif()

# The version literal: the first project(core-cpp ...) call, up to its closing parenthesis.
set(projectVersion "")
file(READ "${ROOT}/CMakeLists.txt" cmakeLists)
string(FIND "${cmakeLists}" "project(core-cpp" callAt)
if(NOT callAt EQUAL -1)
    string(SUBSTRING "${cmakeLists}" ${callAt} -1 call)
    string(FIND "${call}" ")" callEnd)
    string(SUBSTRING "${call}" 0 ${callEnd} call)
    if(call MATCHES "[ \t\r\n]VERSION[ \t\r\n]+([0-9]+\\.[0-9]+\\.[0-9]+)([ \t\r\n]|$)")
        set(projectVersion "${CMAKE_MATCH_1}")
    endif()
endif()
if(projectVersion STREQUAL "")
    string(APPEND refusals "\n  ${ROOT}/CMakeLists.txt has no project(core-cpp ... VERSION X.Y.Z) call")
elseif(version AND NOT version STREQUAL projectVersion)
    string(APPEND refusals
        "\n  tag '${TAG}' does not match project(VERSION ${projectVersion}) in CMakeLists.txt; the version literal is the source of truth")
endif()

# The CHANGELOG section of the version CMakeLists.txt states, a heading at the start of a line.
set(sectionVersion "${projectVersion}")
if(sectionVersion STREQUAL "")
    set(sectionVersion "${version}")
endif()
if(NOT EXISTS "${ROOT}/CHANGELOG.md")
    string(APPEND refusals "\n  ${ROOT}/CHANGELOG.md does not exist")
elseif(sectionVersion)
    file(READ "${ROOT}/CHANGELOG.md" changelog)
    string(REPLACE "." "\\." sectionPattern "${sectionVersion}")
    if(NOT "\n${changelog}" MATCHES "\n## \\[${sectionPattern}\\]")
        string(APPEND refusals
            "\n  CHANGELOG.md has no '## [${sectionVersion}]' section; move the [Unreleased] entries under it")
    endif()
endif()

if(refusals)
    message(FATAL_ERROR "check-release: refusing to release '${TAG}':${refusals}")
endif()
message(STATUS "check-release: ${TAG} matches project(VERSION ${projectVersion}) and CHANGELOG.md's ## [${projectVersion}] section")
