# SPDX-License-Identifier: AGPL-3.0-only
find_package(nlohmann_json 3.11 REQUIRED)

fos_add_module(plugins
    SOURCES
        src/semver.cpp
        src/manifest.cpp
        src/package.cpp
        src/manager.cpp
        src/install.cpp
    DEPS fos::core fos::domain fos::formats nlohmann_json::nlohmann_json)

# Generator of the committed example library (examples/plugins/org.furnitureos.example-materials).
add_executable(fos_plugins_make_example tools/make_example_plugin.cpp tools/example_materials.cpp)
target_link_libraries(fos_plugins_make_example PRIVATE fos::plugins)
fos_set_warnings(fos_plugins_make_example PRIVATE)

fos_add_tests(plugins
    SOURCES
        tests/semver_tests.cpp
        tests/manifest_tests.cpp
        tests/package_tests.cpp
        tests/manager_tests.cpp
        tests/install_tests.cpp
        tests/example_plugin_tests.cpp
        tools/example_materials.cpp)
if(TARGET fos_plugins_tests)
    target_include_directories(fos_plugins_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    target_compile_definitions(fos_plugins_tests PRIVATE
        FOS_EXAMPLE_PLUGINS_DIR="${PROJECT_SOURCE_DIR}/examples/plugins")
endif()
