# SPDX-License-Identifier: AGPL-3.0-only
find_package(Qt6 REQUIRED COMPONENTS Core Gui PrintSupport)

fos_add_module(printing
    SOURCES
        src/print_plan.cpp
        src/paint.cpp
        src/pdf.cpp
        src/print.cpp
    DEPS fos::core fos::drawings Qt6::Gui Qt6::PrintSupport)

# The tests need a QGuiApplication (fonts, QPdfWriter), so they have their own main()
# instead of gtest_main; otherwise the target follows fos_add_tests().
if(FOS_BUILD_TESTS)
    add_executable(fos_printing_tests
        tests/test_main.cpp
        tests/test_support.cpp
        tests/plan_tests.cpp
        tests/raster_tests.cpp
        tests/pdf_tests.cpp
        tests/print_tests.cpp)
    target_link_libraries(fos_printing_tests PRIVATE fos::printing GTest::gtest GTest::gmock)
    fos_set_warnings(fos_printing_tests PRIVATE)
    gtest_discover_tests(fos_printing_tests DISCOVERY_TIMEOUT 60
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
