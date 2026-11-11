# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(machining
    SOURCES src/face.cpp src/validation.cpp src/collision.cpp src/patterns.cpp src/ordering.cpp
            src/report.cpp src/flip.cpp
    DEPS fos::core fos::geometry fos::domain)
fos_add_tests(machining
    SOURCES tests/face_tests.cpp tests/validation_tests.cpp tests/patterns_tests.cpp
            tests/ordering_report_tests.cpp)
