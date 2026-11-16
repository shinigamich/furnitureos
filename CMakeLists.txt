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

if(MSVC)
    # Conforming preprocessor everywhere (also for third-party test code): the legacy one
    # mis-parses raw string literals inside macro arguments (e.g. EXPECT_EQ(x, R"(...)")).
    add_compile_options(/Zc:preprocessor /utf-8)
endif()
if(MINGW)
    # Self-contained executables: no libstdc++/libgcc/winpthread DLLs to ship.
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()
# Qt tests without a window: the offscreen platform on X11/Wayland systems. On Windows (and
# macOS) the native platform is used — Qt's offscreen platform has no font database there,
# so text would not render.
if(WIN32 OR APPLE)
    set(FOS_QT_TEST_PLATFORM_ENV "FOS_QT_NATIVE_PLATFORM=1")
else()
    set(FOS_QT_TEST_PLATFORM_ENV "QT_QPA_PLATFORM=offscreen")
endif()
if(CMAKE_CROSSCOMPILING)
    # Test binaries cannot run on the build host; discover tests when ctest runs
    # (through CMAKE_CROSSCOMPILING_EMULATOR, e.g. wine).
    set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)
endif()
if(WIN32)
    # Portable Windows layout: executables at the top level next to the Qt DLLs that
    # windeployqt places there; documentation in docs\, data in share\.
    set(CMAKE_INSTALL_BINDIR "." CACHE STRING "Executables (relative to the prefix)")
    set(CMAKE_INSTALL_DOCDIR "docs" CACHE STRING "Documentation")
endif()
include(GNUInstallDirs)
include(FosModule)

if(FOS_BUILD_TESTS)
    enable_testing()
endif()
include(FosDependencies)
if(FOS_BUILD_TESTS)
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
set(FOS_GUI_MODULES printing viewport app)

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

# ---------------------------------------------------------------- installation / packaging
install(FILES LICENSE README.md THIRD_PARTY_LICENSES.md CHANGELOG.md KNOWN_LIMITATIONS.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs/user-guide DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY examples/plugins DESTINATION ${CMAKE_INSTALL_DATADIR}/furnitureos/examples)
if(WIN32)
    install(FILES packaging/windows/README-portable.txt DESTINATION . RENAME README.txt)
endif()
include(FosPackaging)

# Windows: every executable (applications AND test binaries) gets the application manifest —
# UTF-8 process code page (std::filesystem::path(std::string) then reads UTF-8 like on Linux)
# and long paths. Listing the .manifest as a source is CMake's supported way for MSVC in every
# configuration (it is merged with the linker-generated manifest).
if(MSVC)
    function(_fos_collect_executables dir out)
        get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        set(_result "")
        foreach(_t IN LISTS _targets)
            get_target_property(_type ${_t} TYPE)
            if(_type STREQUAL "EXECUTABLE")
                list(APPEND _result ${_t})
            endif()
        endforeach()
        get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach(_d IN LISTS _subdirs)
            _fos_collect_executables("${_d}" _sub)
            list(APPEND _result ${_sub})
        endforeach()
        set(${out} ${_result} PARENT_SCOPE)
    endfunction()
    _fos_collect_executables("${CMAKE_CURRENT_SOURCE_DIR}" _fos_executables)
    foreach(_exe IN LISTS _fos_executables)
        target_sources(${_exe} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/app/resources/furnitureos.manifest")
    endforeach()
endif()
