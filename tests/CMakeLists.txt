# SPDX-License-Identifier: AGPL-3.0-only
# Cross-module end-to-end scenario tests (A simple cabinet, B parametric change, C drawings,
# D cutting, E manufacturing). They need almost every engineering module; when a required
# module target is not configured (subset build) the target is skipped with a message.
#
# Added by the root CMakeLists for a full configure, and by src/cli/CMakeLists.txt when an
# explicit FOS_MODULES list containing `cli` is used.
if(NOT FOS_BUILD_TESTS OR TARGET fos_scenario_tests)
    return()
endif()

set(_fos_scenario_deps fos_project fos_validation fos_cam fos_machining fos_drawings fos_cutting fos_production
    fos_cli_core)
foreach(_dep IN LISTS _fos_scenario_deps)
    if(NOT TARGET ${_dep})
        message(STATUS "Scenario tests skipped: target ${_dep} is not configured")
        return()
    endif()
endforeach()

add_executable(fos_scenario_tests
    scenario_a_simple_cabinet.cpp
    scenario_b_parametric.cpp
    scenario_c_drawings.cpp
    scenario_d_cutting.cpp
    scenario_e_manufacturing.cpp)
target_include_directories(fos_scenario_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fos_scenario_tests PRIVATE
    fos::project fos::validation fos::cam fos::machining fos::drawings fos::cutting fos::production fos::cli_core
    GTest::gtest GTest::gmock GTest::gtest_main)
fos_set_warnings(fos_scenario_tests PRIVATE)
gtest_discover_tests(fos_scenario_tests DISCOVERY_TIMEOUT 60)
