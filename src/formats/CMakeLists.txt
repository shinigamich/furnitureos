# SPDX-License-Identifier: AGPL-3.0-only
# nlohmann_json::nlohmann_json is provided by cmake/FosDependencies.cmake

fos_add_module(formats
    SOURCES
        src/sha256.cpp
        src/container.cpp
        src/file_read.cpp
        src/atomic_file.cpp
        src/recovery.cpp
        src/enums.cpp
        src/codec_reader.cpp
        src/codec_catalog.cpp
        src/codec_model.cpp
        src/codec_shape.cpp
        src/shape_check.cpp
        src/library.cpp
        src/obj_export.cpp
        src/dxf_import.cpp
        src/dxf_chain.cpp
        src/panel_shape_import.cpp
        src/csv_materials.cpp
        src/dxf_export.cpp
    DEPS fos::core fos::geometry fos::domain nlohmann_json::nlohmann_json)

fos_add_tests(formats
    SOURCES
        tests/sha256_tests.cpp
        tests/container_tests.cpp
        tests/atomic_file_tests.cpp
        tests/codec_tests.cpp
        tests/library_tests.cpp
        tests/obj_export_tests.cpp
        tests/csv_materials_tests.cpp
        tests/limits_tests.cpp
        tests/shape_codec_tests.cpp
        tests/obj_shape_tests.cpp
        tests/dxf_import_tests.cpp
        tests/dxf_export_tests.cpp)
