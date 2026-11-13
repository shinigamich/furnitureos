// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Shared helpers of the cross-module end-to-end scenario tests (tests/README in CMakeLists).
#include "fos/formats/codec.hpp"
#include "fos/furniture/templates.hpp"
#include "fos/project/document.hpp"
#include "fos/project/file.hpp"
#include "fos/project/outputs.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <random>
#include <string>

namespace fos::scenario {

inline project::Metadata meta(const std::string& name = "Scenario") {
    project::Metadata m;
    m.id = "6d3c1f0e-2b4a-4c5d-8e9f-0a1b2c3d4e5f";
    m.name = name;
    m.author = "Scenario tests";
    m.createdAt = "2026-11-13T08:00:00Z";
    m.modifiedAt = "2026-11-13T08:00:00Z";
    return m;
}

inline project::Item templateItem(furniture::CabinetDefinition (*fn)(domain::AssemblyId,
                                                                    const furniture::templates::TemplateMaterials&),
                                  const std::string& id) {
    project::Item item;
    item.definition = fn(domain::AssemblyId(id), project::defaultTemplateMaterials());
    return item;
}

inline project::Resolved resolved(const project::Project& p) {
    auto r = project::resolve(p);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.ok() ? r.value() : project::Resolved{};
}

inline const domain::Part* findPart(const project::Resolved& r, const std::string& id) {
    for (const auto& a : r.model.assemblies)
        if (const auto* p = a.findPart(domain::PartId(id))) return p;
    return nullptr;
}

/// Canonical text of a model (formats codec): equal text = identical model.
inline std::string modelText(const domain::Model& m) { return formats::toJson(m).dump(); }

inline const production::CutListRow* cutRow(const production::CutList& list, const std::string& partId) {
    for (const auto& row : list.rows)
        for (const auto& id : row.partIds)
            if (id.str() == partId) return &row;
    return nullptr;
}

inline const production::MaterialRow* materialRow(const production::MaterialReport& r, const std::string& id) {
    for (const auto& row : r.rows)
        if (row.materialId.str() == id) return &row;
    return nullptr;
}

inline const drawings::Dimension* findDimension(const drawings::DrawingResult& d, const std::string& id) {
    for (const auto& v : d.views)
        for (const auto& dim : v.annotations.dimensions)
            if (dim.id == id) return &dim;
    return nullptr;
}

inline bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

/// Fixed save clock so that files are reproducible.
inline formats::SaveOptions fixedClock() {
    formats::SaveOptions o;
    o.clock = [] { return std::chrono::system_clock::time_point(std::chrono::seconds(1794556800)); };
    return o;
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::random_device rd;
        path = std::filesystem::temp_directory_path() /
               ("fos_scenario_" + std::to_string(rd()) + "_" + std::to_string(rd()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace fos::scenario
