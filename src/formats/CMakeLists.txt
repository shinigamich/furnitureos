# SPDX-License-Identifier: AGPL-3.0-only
find_package(nlohmann_json 3.11 REQUIRED)

fos_add_module(formats
    SOURCES
        src/sha256.cpp
        src/container.cpp
        src/atomic_file.cpp
        src/recovery.cpp
        src/enums.cpp
        src/codec_reader.cpp
        src/codec_catalog.cpp
        src/codec_model.cpp
        src/library.cpp
        src/obj_export.cpp
        src/csv_materials.cpp
    DEPS fos::core fos::geometry fos::domain nlohmann_json::nlohmann_json)

fos_add_tests(formats
    SOURCES
        tests/sha256_tests.cpp
        tests/container_tests.cpp
        tests/atomic_file_tests.cpp
        tests/codec_tests.cpp
        tests/library_tests.cpp
        tests/obj_export_tests.cpp
        tests/csv_materials_tests.cpp)
