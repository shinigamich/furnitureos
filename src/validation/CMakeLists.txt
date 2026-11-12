# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(validation
    SOURCES src/obb.cpp src/util.cpp src/report.cpp src/validate.cpp
            src/check_parts.cpp src/check_interference.cpp src/check_machining.cpp
    DEPS fos::core fos::geometry fos::domain fos::machining)
fos_add_tests(validation
    SOURCES tests/obb_tests.cpp tests/check_tests.cpp tests/interference_tests.cpp
            tests/report_tests.cpp tests/large_model_tests.cpp)
# Cross-check against generated cabinets; only when the furniture module is configured too
# (test-only dependency: the validation library itself never depends on furniture).
if(FOS_BUILD_TESTS AND TARGET fos::furniture)
    target_sources(fos_validation_tests PRIVATE tests/furniture_tests.cpp)
    target_link_libraries(fos_validation_tests PRIVATE fos::furniture)
endif()
