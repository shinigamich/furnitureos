# Base headers (vec, transform, aabb) are header-only. Additional compiled sources
# (polygons, meshing, intersections) are listed in sources.cmake when present.
set(FOS_GEOMETRY_SOURCES "")
set(FOS_GEOMETRY_TEST_SOURCES tests/base_tests.cpp)
include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake OPTIONAL)
fos_add_module(geometry SOURCES ${FOS_GEOMETRY_SOURCES} DEPS fos::core)
fos_add_tests(geometry SOURCES ${FOS_GEOMETRY_TEST_SOURCES})
