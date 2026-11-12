# SPDX-License-Identifier: AGPL-3.0-only
# Two targets:
#  * fos::cutting        the optimiser core (types, optimizer, validate, svg); depends on fos::core ONLY.
#  * fos::cutting_model  the domain adapter (fos/cutting/from_model.hpp: jobsFromModel); depends on
#                        fos::cutting and fos::domain (and, through it, fos::geometry).
# Code that calls jobsFromModel() must link fos::cutting_model.
fos_add_module(cutting
    SOURCES src/job_check.cpp src/packer.cpp src/layout.cpp src/optimizer.cpp src/validate.cpp src/svg.cpp
    DEPS fos::core)
fos_add_module(cutting_model
    SOURCES src/from_model.cpp
    DEPS fos::cutting fos::domain)
fos_add_tests(cutting
    SOURCES tests/test_support.cpp tests/kerf_tests.cpp tests/constraint_tests.cpp tests/stock_tests.cpp
            tests/validator_tests.cpp tests/large_job_tests.cpp tests/svg_tests.cpp)
fos_add_tests(cutting_model
    SOURCES tests/test_support.cpp tests/from_model_tests.cpp)
