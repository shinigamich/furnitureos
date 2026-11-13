# SPDX-License-Identifier: AGPL-3.0-only
# Module `app`: the desktop application `furnitureos` plus the widget-free, testable
# view-model library `fos_app_core` (see README.md).
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets PrintSupport Svg LinguistTools)

set(FOS_APP_SOURCE_URL "https://github.com/furnitureos/furnitureos" CACHE STRING
    "Public location of the corresponding source code, shown in Help > About (AGPL-3.0 section 13)")

# ---------------------------------------------------------------- fos_app_core
add_library(fos_app_core STATIC
    include/fos/app/session.hpp
    src/core/version.cpp
    src/core/window_title.cpp
    src/core/recent_files.cpp
    src/core/autosave.cpp
    src/core/templates.cpp
    src/core/command_text.cpp
    src/core/session.cpp
    src/core/parameter_rows.cpp
    src/core/browser_tree.cpp
    src/core/diagnostics_rows.cpp
    src/core/cutting_rows.cpp
    src/core/production_text.cpp
    src/core/drawing_request.cpp)
add_library(fos::app_core ALIAS fos_app_core)
# moc (Qt 6.4) cannot parse libstdc++ C++20 concepts with the compiler predefines (see viewport).
set_target_properties(fos_app_core PROPERTIES AUTOMOC ON AUTOMOC_COMPILER_PREDEFINES OFF)
target_include_directories(fos_app_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(fos_app_core PUBLIC fos::project fos::validation fos::printing Qt6::Core Qt6::Gui)
target_compile_definitions(fos_app_core PRIVATE
    FOS_APP_VERSION="${PROJECT_VERSION}" FOS_APP_SOURCE_URL="${FOS_APP_SOURCE_URL}")
fos_set_warnings(fos_app_core PRIVATE)

if(FOS_BUILD_TESTS)
    add_executable(fos_app_core_tests
        tests/core/test_main.cpp
        tests/core/basics_tests.cpp
        tests/core/session_tests.cpp
        tests/core/parameter_tests.cpp
        tests/core/views_tests.cpp)
    target_link_libraries(fos_app_core_tests PRIVATE fos::app_core GTest::gtest GTest::gmock)
    fos_set_warnings(fos_app_core_tests PRIVATE)
    gtest_discover_tests(fos_app_core_tests DISCOVERY_TIMEOUT 60
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# ---------------------------------------------------------------- furnitureos (GUI)
# The 3D viewport is module `viewport` (contract: src/viewport/include/fos/viewport/
# viewport_widget.hpp). Without it the executable cannot be linked, so it is skipped.
if(NOT TARGET fos::viewport)
    message(STATUS "app: fos::viewport is not configured - building fos_app_core only (no furnitureos executable)")
    return()
endif()

set(FOS_APP_GUI_SOURCES
    src/gui/main.cpp
    src/gui/main_window.hpp
    src/gui/main_window.cpp
    src/gui/main_window_files.cpp
    src/gui/main_window_actions.cpp
    src/gui/project_browser.hpp
    src/gui/project_browser.cpp
    src/gui/inspector_panel.hpp
    src/gui/inspector_panel.cpp
    src/gui/inspector_cabinet.cpp
    src/gui/diagnostics_panel.hpp
    src/gui/diagnostics_panel.cpp
    src/gui/drawings_workspace.hpp
    src/gui/drawings_workspace.cpp
    src/gui/production_workspace.hpp
    src/gui/production_workspace.cpp
    src/gui/cutting_workspace.hpp
    src/gui/cutting_workspace.cpp
    src/gui/dialogs.hpp
    src/gui/dialogs.cpp
    src/gui/settings.hpp
    src/gui/settings.cpp
    src/gui/enum_text.hpp
    src/gui/enum_text.cpp)

# Everything except main.cpp goes into a static library shared by the executable and the
# GUI smoke test.
list(FILTER FOS_APP_GUI_SOURCES EXCLUDE REGEX "main\\.cpp$")
add_library(fos_app_gui STATIC ${FOS_APP_GUI_SOURCES})
set_target_properties(fos_app_gui PROPERTIES AUTOMOC ON AUTOMOC_COMPILER_PREDEFINES OFF)
target_include_directories(fos_app_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/gui)
target_link_libraries(fos_app_gui PUBLIC fos::app_core fos::viewport
    Qt6::Widgets Qt6::OpenGLWidgets Qt6::PrintSupport Qt6::Svg)
fos_set_warnings(fos_app_gui PRIVATE)

qt_add_executable(furnitureos src/gui/main.cpp)
target_link_libraries(furnitureos PRIVATE fos_app_gui)
fos_set_warnings(furnitureos PRIVATE)

# Translations: `cmake --build <dir> --target update_translations` runs lupdate over the
# sources of fos_app_core and the GUI; the .qm files are compiled by lrelease and embedded
# as resources under :/i18n/.
set(FOS_APP_TS_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/furnitureos_uk.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/furnitureos_en.ts)
get_target_property(_fos_core_sources fos_app_core SOURCES)
list(TRANSFORM _fos_core_sources PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
set(_fos_gui_sources ${FOS_APP_GUI_SOURCES} src/gui/main.cpp)
list(TRANSFORM _fos_gui_sources PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
qt_add_translations(furnitureos
    TS_FILES ${FOS_APP_TS_FILES}
    SOURCES ${_fos_core_sources} ${_fos_gui_sources}
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
    LUPDATE_OPTIONS -no-obsolete -locations none
    RESOURCE_PREFIX /i18n
    OUTPUT_TARGETS _fos_qm_targets)

if(FOS_BUILD_TESTS)
    # GUI smoke test: needs an X display; run under xvfb-run when available. Without a
    # display the test skips itself (label `gui`).
    add_executable(fos_app_gui_tests tests/gui/gui_smoke_tests.cpp)
    target_link_libraries(fos_app_gui_tests PRIVATE fos_app_gui GTest::gtest)
    fos_set_warnings(fos_app_gui_tests PRIVATE)
    # The test loads the compiled Ukrainian catalogue from the build tree.
    target_compile_definitions(fos_app_gui_tests PRIVATE
        FOS_APP_QM_DIR="${CMAKE_CURRENT_BINARY_DIR}")
    add_dependencies(fos_app_gui_tests furnitureos)
    find_program(FOS_XVFB_RUN xvfb-run)
    if(FOS_XVFB_RUN)
        add_test(NAME app_gui_smoke
                 COMMAND ${FOS_XVFB_RUN} -a -s "-screen 0 1600x1000x24" $<TARGET_FILE:fos_app_gui_tests>)
    else()
        add_test(NAME app_gui_smoke COMMAND fos_app_gui_tests)
    endif()
    set_tests_properties(app_gui_smoke PROPERTIES LABELS gui SKIP_RETURN_CODE 77 TIMEOUT 300)
endif()
