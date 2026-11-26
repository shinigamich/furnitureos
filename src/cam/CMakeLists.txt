# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(cam
    SOURCES src/tool.cpp src/machine.cpp src/toolpath.cpp src/setup.cpp src/generate.cpp
            src/heightmap.cpp src/simulate.cpp src/postprocessor.cpp src/generic_iso.cpp src/roundtrip.cpp
            src/arc_math.cpp src/contour.cpp src/contour_gen.cpp src/contour_sim.cpp src/pocket.cpp src/pocket_gen.cpp
    DEPS fos::core fos::geometry fos::domain fos::machining)
fos_add_tests(cam
    SOURCES tests/tool_setup_tests.cpp tests/generate_tests.cpp tests/simulate_tests.cpp tests/post_tests.cpp
            tests/hardening_tests.cpp
            tests/contour_tests.cpp tests/pocket_tests.cpp)
# End-to-end grip test against the furniture generator; only when that module is configured.
if(TARGET fos_cam_tests AND TARGET fos_furniture)
    target_sources(fos_cam_tests PRIVATE tests/sliding_grip_tests.cpp)
    target_link_libraries(fos_cam_tests PRIVATE fos::furniture)
endif()
