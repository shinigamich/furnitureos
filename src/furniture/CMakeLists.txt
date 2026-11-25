# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(furniture
    SOURCES src/enums.cpp src/definition.cpp src/parameters.cpp src/generate.cpp src/gen_carcass.cpp
            src/gen_back.cpp src/gen_interior.cpp src/gen_fronts.cpp src/gen_hardware.cpp src/edit.cpp
            src/templates.cpp src/panel.cpp src/checks.cpp src/limits.cpp
            src/contour.cpp src/contour_check.cpp src/gen_contour.cpp src/gen_drawers.cpp src/gen_sliding.cpp
            src/gen_corner_blind.cpp src/gen_corner_l.cpp src/gen_appliance.cpp src/gen_hanger.cpp src/gen_frame.cpp src/gen_rail.cpp
    DEPS fos::core fos::geometry fos::parametric fos::domain fos::machining fos::hardware)
fos_add_tests(furniture
    SOURCES tests/fixture_tests.cpp tests/parametric_tests.cpp tests/construction_tests.cpp
            tests/interior_tests.cpp tests/fronts_tests.cpp tests/edit_tests.cpp tests/templates_tests.cpp
            tests/misc_tests.cpp tests/limits_tests.cpp tests/pin_collision_tests.cpp
            tests/contour_tests.cpp tests/contour_panel_tests.cpp tests/drawer_box_tests.cpp
            tests/drawer_joint_tests.cpp tests/flap_inset_tests.cpp tests/sliding_doors_tests.cpp
            tests/corner_tests.cpp tests/appliance_tests.cpp tests/hanger_tests.cpp tests/frame_panel_tests.cpp
            tests/hanging_rail_tests.cpp tests/glass_panel_tests.cpp)
