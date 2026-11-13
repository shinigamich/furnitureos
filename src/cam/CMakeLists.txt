# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(cam
    SOURCES src/tool.cpp src/machine.cpp src/toolpath.cpp src/setup.cpp src/generate.cpp
            src/heightmap.cpp src/simulate.cpp src/postprocessor.cpp src/generic_iso.cpp src/roundtrip.cpp
    DEPS fos::core fos::geometry fos::domain fos::machining)
fos_add_tests(cam
    SOURCES tests/tool_setup_tests.cpp tests/generate_tests.cpp tests/simulate_tests.cpp tests/post_tests.cpp
            tests/hardening_tests.cpp)
