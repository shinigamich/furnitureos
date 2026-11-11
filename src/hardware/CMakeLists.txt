# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(hardware
    SOURCES src/catalog.cpp src/placement.cpp src/compatibility.cpp src/corner_joint.cpp
            src/shelf_supports.cpp src/hinges.cpp src/handle.cpp src/hardware_list.cpp
    DEPS fos::core fos::geometry fos::domain fos::machining)
fos_add_tests(hardware
    SOURCES tests/catalog_tests.cpp tests/corner_joint_tests.cpp tests/shelf_supports_tests.cpp
            tests/hinge_handle_tests.cpp)
