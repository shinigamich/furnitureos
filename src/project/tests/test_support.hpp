// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/formats/codec.hpp"
#include "fos/furniture/templates.hpp"
#include "fos/project/document.hpp"
#include "fos/project/file.hpp"
#include "fos/project/outputs.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <random>

namespace fos::project::test {

inline Metadata meta() {
    Metadata m;
    m.id = "0b6f6a52-6a4e-4c8e-9a3b-6f1d2c3e4f50";
    m.name = "Kitchen";
    m.author = "Test";
    m.createdAt = "2026-11-12T10:00:00Z";
    m.modifiedAt = "2026-11-12T10:00:00Z";
    return m;
}

/// Base-cabinet template (600 x 820 x 560) with the generic default materials.
inline Item baseCabinetItem(const std::string& id = "cab1") {
    Item item;
    item.definition = furniture::templates::baseCabinet(domain::AssemblyId(id), defaultTemplateMaterials());
    return item;
}

inline const domain::Part* findPart(const Resolved& r, const std::string& id) {
    for (const auto& a : r.model.assemblies)
        if (const auto* p = a.findPart(domain::PartId(id))) return p;
    return nullptr;
}

inline std::string modelText(const domain::Model& m) { return formats::toJson(m).dump(); }

inline Resolved resolved(const Project& p) {
    auto r = resolve(p);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.ok() ? r.value() : Resolved{};
}

/// Fresh temporary directory removed on destruction.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::random_device rd;
        path = std::filesystem::temp_directory_path() / ("fos_project_test_" + std::to_string(rd()) + std::to_string(rd()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

inline formats::SaveOptions fixedClock() {
    formats::SaveOptions o;
    o.clock = [] { return std::chrono::system_clock::time_point(std::chrono::seconds(1794484800)); };
    return o;
}

} // namespace fos::project::test
