# SPDX-License-Identifier: AGPL-3.0-only
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

fos_add_module(viewport
    SOURCES
        include/fos/viewport/viewport_widget.hpp
        src/camera.cpp
        src/scene.cpp
        src/gl_renderer.cpp
        src/viewport_widget.cpp
    DEPS fos::geometry fos::domain Qt6::Widgets Qt6::OpenGLWidgets
    PRIVATE_DEPS Qt6::OpenGL)
# moc (Qt 6.4) cannot parse the C++20 concepts in libstdc++ headers reached through the
# domain headers when it sees the compiler predefines (__cplusplus = 202002L).
set_target_properties(fos_viewport PROPERTIES AUTOMOC ON AUTOMOC_COMPILER_PREDEFINES OFF)

if(FOS_BUILD_TESTS)
    # Camera, scene and widget picking: no GL context needed (offscreen Qt platform).
    add_executable(fos_viewport_tests
        tests/test_main.cpp
        tests/camera_tests.cpp
        tests/scene_tests.cpp
        tests/picking_tests.cpp)
    target_link_libraries(fos_viewport_tests PRIVATE fos::viewport GTest::gtest GTest::gmock)
    fos_set_warnings(fos_viewport_tests PRIVATE)
    gtest_discover_tests(fos_viewport_tests DISCOVERY_TIMEOUT 60
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # GL smoke test: needs a windowing platform with OpenGL 3.3. Run under xvfb-run when
    # available; the tests GTEST_SKIP themselves when no suitable context can be created.
    add_executable(fos_viewport_gl_tests tests/test_main.cpp tests/gl_tests.cpp)
    target_link_libraries(fos_viewport_gl_tests PRIVATE fos::viewport fos::domain GTest::gtest Qt6::OpenGL)
    fos_set_warnings(fos_viewport_gl_tests PRIVATE)
    find_program(FOS_XVFB_RUN xvfb-run)
    if(FOS_XVFB_RUN)
        add_test(NAME viewport_gl COMMAND ${FOS_XVFB_RUN} -a $<TARGET_FILE:fos_viewport_gl_tests>)
    else()
        add_test(NAME viewport_gl COMMAND fos_viewport_gl_tests)
    endif()
    set_tests_properties(viewport_gl PROPERTIES LABELS gl TIMEOUT 300
        SKIP_REGULAR_EXPRESSION "\\[  SKIPPED \\]")
endif()
