# SPDX-License-Identifier: AGPL-3.0-only
# Compiled sources and tests of the geometry kernel (included by CMakeLists.txt).
list(APPEND FOS_GEOMETRY_SOURCES
    src/segment.cpp
    src/polygon.cpp
    src/polygon_relations.cpp
    src/offset.cpp
    src/triangulate.cpp
    src/mesh.cpp
    src/ray.cpp
    src/projection.cpp
)
list(APPEND FOS_GEOMETRY_TEST_SOURCES
    tests/segment_tests.cpp
    tests/polygon_tests.cpp
    tests/polygon_relations_tests.cpp
    tests/offset_tests.cpp
    tests/triangulate_tests.cpp
    tests/mesh_tests.cpp
    tests/ray_tests.cpp
    tests/projection_tests.cpp
)
