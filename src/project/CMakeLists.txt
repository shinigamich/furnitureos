# SPDX-License-Identifier: AGPL-3.0-only
# nlohmann_json::nlohmann_json is provided by cmake/FosDependencies.cmake

fos_add_module(project
    SOURCES
        src/project.cpp
        src/resolve.cpp
        src/json_reader.cpp
        src/codec_cabinet.cpp
        src/codec_cabinet_contour.cpp
        src/codec_cabinet_sections.cpp
        src/codec_drawing.cpp
        src/codec_project.cpp
        src/file.cpp
        src/commands.cpp
        src/commands_nesting.cpp
        src/room.cpp
        src/document.cpp
        src/outputs.cpp
        src/limits.cpp
        src/dxf_panel.cpp
        src/components.cpp
        src/component_insert.cpp
        src/component_library.cpp
        src/batch_document.cpp
        src/batch_merge.cpp
        src/batch_outputs.cpp
        src/worktop.cpp
        src/cabinet_run.cpp
        src/plinth.cpp
        src/merge.cpp
        src/diff.cpp
        src/diff_model.cpp
        src/diff_format.cpp
        src/remnant_store.cpp
        src/remnant_use.cpp
    DEPS fos::core fos::geometry fos::parametric fos::domain fos::machining fos::hardware fos::furniture
         fos::production fos::cutting fos::cutting_model fos::nesting fos::nesting_model fos::drawings fos::formats nlohmann_json::nlohmann_json)
target_compile_definitions(fos_project PRIVATE FOS_PROJECT_APP_VERSION="${PROJECT_VERSION}")

fos_add_tests(project
    SOURCES
        tests/resolve_tests.cpp
        tests/file_tests.cpp
        tests/document_tests.cpp
        tests/scenario_tests.cpp
        tests/limits_tests.cpp
        tests/contour_tests.cpp
        tests/bought_in_tests.cpp
        tests/nesting_tests.cpp
        tests/dxf_panel_tests.cpp
        tests/svg_panel_tests.cpp
        tests/component_tests.cpp
        tests/batch_tests.cpp
        tests/worktop_tests.cpp
        tests/plinth_tests.cpp
        tests/room_tests.cpp
        tests/merge_tests.cpp
        tests/diff_tests.cpp
        tests/remnant_store_tests.cpp)
