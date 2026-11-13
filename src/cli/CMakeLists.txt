# SPDX-License-Identifier: AGPL-3.0-only
# Headless command-line front end: library fos::cli_core (all commands, testable in-process)
# and the executable `fos`.
fos_add_module(cli_core
    SOURCES
        src/cli.cpp
        src/common.cpp
        src/demo_cam.cpp
        src/cmd_project.cpp
        src/cmd_output.cpp
        src/cmd_cut.cpp
        src/cmd_drawing.cpp
        src/cmd_cam.cpp
    DEPS fos::core fos::geometry fos::domain fos::machining fos::furniture fos::validation fos::production
         fos::cutting fos::cutting_model fos::drawings fos::cam fos::formats fos::project)
target_compile_definitions(fos_cli_core PRIVATE FOS_CLI_VERSION="${PROJECT_VERSION}")

add_executable(fos src/main.cpp)
target_link_libraries(fos PRIVATE fos::cli_core)
fos_set_warnings(fos PRIVATE)

fos_add_tests(cli
    SOURCES tests/cli_tests.cpp tests/cli_binary_tests.cpp
    DEPS fos::cli_core)
if(FOS_BUILD_TESTS)
    add_dependencies(fos_cli_tests fos)
    target_compile_definitions(fos_cli_tests PRIVATE FOS_CLI_EXECUTABLE="$<TARGET_FILE:fos>")
endif()

# Cross-module scenario tests live in <root>/tests. The root CMakeLists adds that directory
# only for a full configure (FOS_MODULES empty); with an explicit module list that includes
# `cli` (the last module) they are added from here, after every module target exists.
if(FOS_BUILD_TESTS AND FOS_MODULES AND EXISTS "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory("${PROJECT_SOURCE_DIR}/tests" "${PROJECT_BINARY_DIR}/tests")
endif()
