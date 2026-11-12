# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(validation
    SOURCES src/obb.cpp src/solid.cpp src/interference_engine.cpp src/util.cpp src/registry.cpp src/report.cpp
            src/check_parts.cpp src/check_edge_banding.cpp src/check_part_interference.cpp
            src/check_assembly_interference.cpp src/check_machining.cpp src/check_hardware.cpp
            src/check_completeness.cpp
    DEPS fos::core fos::geometry fos::domain fos::machining)
fos_add_tests(validation
    SOURCES tests/obb_tests.cpp tests/fixture_tests.cpp tests/part_edge_tests.cpp tests/interference_tests.cpp
            tests/machining_hardware_tests.cpp tests/report_tests.cpp tests/large_model_tests.cpp
            tests/test_support.cpp)
