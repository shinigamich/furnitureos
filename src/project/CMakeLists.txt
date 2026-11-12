# SPDX-License-Identifier: AGPL-3.0-only
find_package(nlohmann_json 3.11 REQUIRED)

fos_add_module(project
    SOURCES
        src/project.cpp
        src/resolve.cpp
        src/json_reader.cpp
        src/codec_cabinet.cpp
        src/codec_drawing.cpp
        src/codec_project.cpp
        src/file.cpp
        src/commands.cpp
        src/document.cpp
        src/outputs.cpp
    DEPS fos::core fos::geometry fos::parametric fos::domain fos::machining fos::hardware fos::furniture
         fos::production fos::cutting fos::drawings fos::formats nlohmann_json::nlohmann_json)
target_compile_definitions(fos_project PRIVATE FOS_PROJECT_APP_VERSION="${PROJECT_VERSION}")

fos_add_tests(project
    SOURCES
        tests/resolve_tests.cpp
        tests/file_tests.cpp
        tests/document_tests.cpp
        tests/scenario_tests.cpp)
