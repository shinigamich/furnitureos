# Third-party dependencies of FurnitureOS (see THIRD_PARTY_LICENSES.md).
# SPDX-License-Identifier: AGPL-3.0-only
#
# Every dependency is first looked up as an installed package. When it is missing
# (typical on Windows, or when cross-compiling) it is built from source with
# FetchContent. For offline builds point FetchContent at a local copy, e.g.
#   -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/usr/src/googletest
#   -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=<dir containing include/nlohmann/json.hpp>
include(FetchContent)

# nlohmann/json (MIT, header-only) — used by formats, project, plugins.
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json 3.11 QUIET)
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/include.zip)
    FetchContent_GetProperties(nlohmann_json)
    if(NOT nlohmann_json_POPULATED)
        FetchContent_Populate(nlohmann_json)
    endif()
    add_library(fos_nlohmann_json INTERFACE)
    target_include_directories(fos_nlohmann_json SYSTEM INTERFACE "${nlohmann_json_SOURCE_DIR}/include")
    add_library(nlohmann_json::nlohmann_json ALIAS fos_nlohmann_json)
    message(STATUS "FurnitureOS: using nlohmann/json from ${nlohmann_json_SOURCE_DIR}")
endif()

# GoogleTest (BSD-3-Clause) — tests only.
if(FOS_BUILD_TESTS AND NOT TARGET GTest::gtest)
    if(NOT CMAKE_CROSSCOMPILING)
        find_package(GTest QUIET)
    endif()
    if(NOT TARGET GTest::gtest)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
        foreach(_t gtest gtest_main gmock gmock_main)
            if(TARGET ${_t} AND NOT TARGET GTest::${_t})
                add_library(GTest::${_t} ALIAS ${_t})
            endif()
        endforeach()
    endif()
endif()
