# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(production
    SOURCES src/collect.cpp src/part_list.cpp src/cut_list.cpp src/reports.cpp src/labels.cpp
            src/label_svg.cpp src/cost.cpp src/tables.cpp src/csv.cpp src/html.cpp src/text.cpp
    DEPS fos::core fos::geometry fos::domain)
fos_add_tests(production
    SOURCES tests/reports_tests.cpp tests/cut_list_tests.cpp tests/labels_tests.cpp
            tests/writers_tests.cpp tests/robustness_tests.cpp)
