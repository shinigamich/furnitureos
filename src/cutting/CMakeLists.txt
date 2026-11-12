# SPDX-License-Identifier: AGPL-3.0-only
# The optimiser core (types, optimizer, validate, svg) uses only fos::core.
# Only from_model.{hpp,cpp} touches fos::domain (and, through it, fos::geometry).
fos_add_module(cutting
    SOURCES src/job_check.cpp src/packer.cpp src/layout.cpp src/optimizer.cpp src/validate.cpp
            src/svg.cpp src/from_model.cpp
    DEPS fos::core fos::geometry fos::domain)
fos_add_tests(cutting
    SOURCES tests/test_support.cpp tests/kerf_tests.cpp tests/constraint_tests.cpp tests/stock_tests.cpp
            tests/validator_tests.cpp tests/large_job_tests.cpp tests/svg_tests.cpp tests/from_model_tests.cpp)
