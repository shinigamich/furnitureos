# FurnitureOS — free and open-source furniture CAD/CAM
# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.24)
project(FurnitureOS VERSION 0.1.0 LANGUAGES CXX
        DESCRIPTION "Open-source parametric furniture CAD/CAM")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(FOS_BUILD_TESTS "Build unit and integration tests" ON)
option(FOS_BUILD_GUI "Build Qt-based modules (desktop application, printing)" ON)
option(FOS_WERROR "Treat compiler warnings as errors" OFF)
# Semicolon-separated subset of modules to configure (developer convenience, e.g.
# -DFOS_MODULES="core;geometry;domain;cutting"). Empty = all modules.
set(FOS_MODULES "" CACHE STRING "Subset of modules to build (empty = all)")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FosModule)

if(FOS_BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    include(GoogleTest)
endif()

# Dependency order (lower layers first). See ARCHITECTURE.md for the allowed
# dependency direction; a module may only depend on modules listed above it.
set(FOS_ALL_MODULES
    core
    geometry
    parametric
    domain
    machining
    hardware
    furniture
    validation
    production
    cutting
    nesting
    drawings
    cam
    formats
    plugins
    project
    cli
)
set(FOS_GUI_MODULES printing app)

if(FOS_MODULES)
    set(_fos_modules ${FOS_MODULES})
else()
    set(_fos_modules ${FOS_ALL_MODULES})
    if(FOS_BUILD_GUI)
        list(APPEND _fos_modules ${FOS_GUI_MODULES})
    endif()
endif()

foreach(_m IN LISTS _fos_modules)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/${_m}/CMakeLists.txt")
        add_subdirectory(src/${_m})
    endif()
endforeach()

if(FOS_BUILD_TESTS AND NOT FOS_MODULES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
