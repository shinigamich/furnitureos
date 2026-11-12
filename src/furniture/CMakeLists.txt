# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(furniture
    SOURCES src/enums.cpp src/definition.cpp src/parameters.cpp src/generate.cpp src/gen_carcass.cpp
            src/gen_back.cpp src/gen_interior.cpp src/gen_fronts.cpp src/gen_hardware.cpp src/edit.cpp
            src/templates.cpp src/panel.cpp
    DEPS fos::core fos::geometry fos::parametric fos::domain fos::machining fos::hardware)
fos_add_tests(furniture
    SOURCES tests/fixture_tests.cpp tests/parametric_tests.cpp tests/construction_tests.cpp
            tests/interior_tests.cpp tests/fronts_tests.cpp tests/edit_tests.cpp tests/templates_tests.cpp
            tests/misc_tests.cpp)
