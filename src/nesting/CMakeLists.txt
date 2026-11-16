# SPDX-License-Identifier: AGPL-3.0-only
# True-shape nesting heuristic (CNC routing). Depends only on fos::core and fos::geometry.
fos_add_module(nesting
    SOURCES src/job_check.cpp src/shapes.cpp src/placer.cpp src/nest.cpp src/validate.cpp src/svg.cpp
    DEPS fos::core fos::geometry)
fos_add_tests(nesting
    SOURCES tests/test_support.cpp tests/basic_tests.cpp tests/shape_tests.cpp tests/constraint_tests.cpp
            tests/large_job_tests.cpp)
# Model adapter for shaped parts (fos/nesting/from_model.hpp: nestingJobsFromModel). Separate target so
# that fos::nesting itself stays free of the domain model. Created only when fos::domain is built
# (domain precedes nesting in the module order, so the target exists at this point).
if(TARGET fos_domain)
    fos_add_module(nesting_model
        SOURCES src/from_model.cpp
        DEPS fos::nesting fos::domain)
    set(_nesting_model_tests tests/test_support.cpp tests/from_model_tests.cpp)
    set(_nesting_model_test_deps fos::nesting fos::domain)
    if(TARGET fos_cutting_model)
        # Routing split end-to-end: guillotine jobs (fos::cutting_model) + nesting jobs for shaped parts.
        list(APPEND _nesting_model_tests tests/routing_split_tests.cpp)
        list(APPEND _nesting_model_test_deps fos::cutting_model)
    endif()
    fos_add_tests(nesting_model SOURCES ${_nesting_model_tests} DEPS ${_nesting_model_test_deps})
endif()
