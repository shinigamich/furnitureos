# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(drawings
    SOURCES
        src/geom2d.cpp
        src/primitives.cpp
        src/hlr.cpp
        src/view.cpp
        src/dimension.cpp
        src/edits.cpp
        src/render.cpp
        src/part_view.cpp
        src/sheet.cpp
        src/drawing.cpp
        src/format.cpp
        src/svg.cpp
        src/dxf.cpp
    DEPS fos::core fos::geometry fos::domain)
fos_add_tests(drawings
    SOURCES
        tests/view_tests.cpp
        tests/dimension_tests.cpp
        tests/sheet_tests.cpp
        tests/export_tests.cpp
        tests/part_drawing_tests.cpp)
