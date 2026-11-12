# SPDX-License-Identifier: AGPL-3.0-only
# True-shape nesting heuristic (CNC routing). Depends only on fos::core and fos::geometry.
fos_add_module(nesting
    SOURCES src/job_check.cpp src/shapes.cpp src/placer.cpp src/nest.cpp src/validate.cpp src/svg.cpp
    DEPS fos::core fos::geometry)
fos_add_tests(nesting
    SOURCES tests/test_support.cpp tests/basic_tests.cpp tests/shape_tests.cpp tests/constraint_tests.cpp
            tests/large_job_tests.cpp)
