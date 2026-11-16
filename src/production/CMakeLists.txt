# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(production
    SOURCES src/collect.cpp src/part_list.cpp src/cut_list.cpp src/reports.cpp src/labels.cpp
            src/label_svg.cpp src/cost.cpp src/tables.cpp src/csv.cpp src/html.cpp src/text.cpp
            src/shape.cpp
    DEPS fos::core fos::geometry fos::domain)
fos_add_tests(production
    SOURCES tests/reports_tests.cpp tests/cut_list_tests.cpp tests/labels_tests.cpp
            tests/writers_tests.cpp tests/robustness_tests.cpp
            tests/rect_golden_tests.cpp)
if(TARGET fos_production_tests)
    target_compile_definitions(fos_production_tests PRIVATE
        FOS_PRODUCTION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
endif()
