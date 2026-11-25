# SPDX-License-Identifier: AGPL-3.0-only
# Edge-band roll planning (fos/production/edge_band_rolls.hpp) uses the 1D cutting optimiser
# fos::linear. `linear` is configured after this directory (root module order), which is fine for
# linking (targets are resolved at generate time); it is only compiled in when `linear` is part
# of the configured module set, so subset builds without it keep working.
set(_production_linear_sources "")
set(_production_linear_deps "")
set(_production_linear_tests "")
if("linear" IN_LIST _fos_modules)
    set(_production_linear_sources src/edge_band_rolls.cpp)
    set(_production_linear_deps fos::linear)
    set(_production_linear_tests tests/edge_band_rolls_tests.cpp)
endif()
fos_add_module(production
    SOURCES src/collect.cpp src/part_list.cpp src/cut_list.cpp src/reports.cpp src/labels.cpp
            src/label_svg.cpp src/code128.cpp src/cost.cpp src/tables.cpp src/csv.cpp src/html.cpp src/text.cpp
            src/shape.cpp src/quote.cpp src/quote_writers.cpp src/packing.cpp src/packing_writers.cpp
            src/assembly_sheet.cpp src/edge_banding_jobs.cpp src/edge_banding_jobs_writers.cpp src/edge_banding_jobs_svg.cpp
            ${_production_linear_sources}
    DEPS fos::core fos::geometry fos::domain ${_production_linear_deps})
fos_add_tests(production
    SOURCES tests/reports_tests.cpp tests/cut_list_tests.cpp tests/labels_tests.cpp tests/code128_tests.cpp
            tests/writers_tests.cpp tests/robustness_tests.cpp
            tests/rect_golden_tests.cpp tests/shaped_tests.cpp tests/quote_tests.cpp
            tests/packing_tests.cpp tests/assembly_sheet_tests.cpp tests/linear_hardware_tests.cpp tests/edge_banding_jobs_tests.cpp
            ${_production_linear_tests})
if(TARGET fos_production_tests)
    target_compile_definitions(fos_production_tests PRIVATE
        FOS_PRODUCTION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
endif()
