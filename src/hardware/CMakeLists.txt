# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(hardware
    SOURCES src/catalog.cpp src/placement.cpp src/compatibility.cpp src/corner_joint.cpp
            src/shelf_supports.cpp src/hinges.cpp src/lift.cpp src/handle.cpp src/hardware_list.cpp src/drawer_slides.cpp
            src/sliding_doors.cpp src/cabinet_hanger.cpp src/clothes_rail.cpp
    DEPS fos::core fos::geometry fos::domain fos::machining)
fos_add_tests(hardware
    SOURCES tests/catalog_tests.cpp tests/corner_joint_tests.cpp tests/shelf_supports_tests.cpp
            tests/hinge_handle_tests.cpp tests/drawer_slides_tests.cpp tests/crank_lift_tests.cpp
            tests/sliding_doors_tests.cpp tests/cabinet_hanger_tests.cpp tests/clothes_rail_tests.cpp)
