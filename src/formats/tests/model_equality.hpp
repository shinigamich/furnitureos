// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Field-by-field deep equality of domain objects for round-trip tests. Doubles are
// compared exactly: the JSON codecs are required to be lossless.
#include "fos/domain/model.hpp"

#include <gtest/gtest.h>

namespace fos::formats::test {

inline void expectEqual(const geometry::Vec3& a, const geometry::Vec3& b, const std::string& where) {
    EXPECT_EQ(a.x, b.x) << where << ".x";
    EXPECT_EQ(a.y, b.y) << where << ".y";
    EXPECT_EQ(a.z, b.z) << where << ".z";
}

inline void expectEqual(const geometry::Transform& a, const geometry::Transform& b, const std::string& where) {
    expectEqual(a.xAxis, b.xAxis, where + ".xAxis");
    expectEqual(a.yAxis, b.yAxis, where + ".yAxis");
    expectEqual(a.zAxis, b.zAxis, where + ".zAxis");
    expectEqual(a.origin, b.origin, where + ".origin");
}

inline void expectEqual(const domain::Material& a, const domain::Material& b, const std::string& where) {
    EXPECT_EQ(a.id, b.id) << where;
    EXPECT_EQ(a.name, b.name) << where;
    EXPECT_EQ(a.manufacturer, b.manufacturer) << where;
    EXPECT_EQ(a.code, b.code) << where;
    EXPECT_EQ(a.supplier, b.supplier) << where;
    EXPECT_EQ(a.kind, b.kind) << where;
    EXPECT_EQ(a.thickness, b.thickness) << where;
    EXPECT_EQ(a.sheetLength, b.sheetLength) << where;
    EXPECT_EQ(a.sheetWidth, b.sheetWidth) << where;
    EXPECT_EQ(a.hasGrain, b.hasGrain) << where;
    EXPECT_EQ(a.color, b.color) << where;
    EXPECT_EQ(a.texture, b.texture) << where;
    EXPECT_EQ(a.costPerSheet, b.costPerSheet) << where;
    EXPECT_EQ(a.available, b.available) << where;
    EXPECT_EQ(a.custom, b.custom) << where;
}

inline void expectEqual(const domain::EdgeBand& a, const domain::EdgeBand& b, const std::string& where) {
    EXPECT_EQ(a.id, b.id) << where;
    EXPECT_EQ(a.name, b.name) << where;
    EXPECT_EQ(a.manufacturer, b.manufacturer) << where;
    EXPECT_EQ(a.code, b.code) << where;
    EXPECT_EQ(a.kind, b.kind) << where;
    EXPECT_EQ(a.thickness, b.thickness) << where;
    EXPECT_EQ(a.width, b.width) << where;
    EXPECT_EQ(a.color, b.color) << where;
    EXPECT_EQ(a.costPerMeter, b.costPerMeter) << where;
    EXPECT_EQ(a.custom, b.custom) << where;
}

inline void expectEqual(const domain::HardwareDefinition& a, const domain::HardwareDefinition& b,
                        const std::string& where) {
    EXPECT_EQ(a.id, b.id) << where;
    EXPECT_EQ(a.kind, b.kind) << where;
    EXPECT_EQ(a.name, b.name) << where;
    EXPECT_EQ(a.manufacturer, b.manufacturer) << where;
    EXPECT_EQ(a.article, b.article) << where;
    EXPECT_EQ(a.dimensionsVerified, b.dimensionsVerified) << where;
    EXPECT_EQ(a.unitCost, b.unitCost) << where;
    EXPECT_EQ(a.params, b.params) << where;
    EXPECT_EQ(a.custom, b.custom) << where;
}

inline void expectEqual(const domain::HardwareInstance& a, const domain::HardwareInstance& b,
                        const std::string& where) {
    EXPECT_EQ(a.id, b.id) << where;
    EXPECT_EQ(a.definition, b.definition) << where;
    EXPECT_EQ(a.quantity, b.quantity) << where;
    EXPECT_EQ(a.parts, b.parts) << where;
    expectEqual(a.position, b.position, where + ".position");
    EXPECT_EQ(a.note, b.note) << where;
}

inline void expectEqual(const domain::Operation& a, const domain::Operation& b, const std::string& where) {
    EXPECT_EQ(a.id, b.id) << where;
    EXPECT_EQ(a.shape.index(), b.shape.index()) << where;
    EXPECT_TRUE(a.shape == b.shape) << where << ": shapes differ"; // defaulted member-wise ==
    EXPECT_EQ(a.source, b.source) << where;
    EXPECT_EQ(a.note, b.note) << where;
}

inline void expectEqual(const domain::Part& a, const domain::Part& b, const std::string& where) {
    EXPECT_EQ(a.id, b.id) << where;
    EXPECT_EQ(a.name, b.name) << where;
    EXPECT_EQ(a.role, b.role) << where;
    EXPECT_EQ(a.length, b.length) << where;
    EXPECT_EQ(a.width, b.width) << where;
    EXPECT_EQ(a.thickness, b.thickness) << where;
    EXPECT_EQ(a.material, b.material) << where;
    for (std::size_t i = 0; i < 4; ++i) EXPECT_EQ(a.edges[i], b.edges[i]) << where << " edge " << i;
    EXPECT_EQ(a.grain, b.grain) << where;
    expectEqual(a.placement, b.placement, where + ".placement");
    ASSERT_EQ(a.operations.size(), b.operations.size()) << where;
    for (std::size_t i = 0; i < a.operations.size(); ++i)
        expectEqual(a.operations[i], b.operations[i], where + ".operations[" + std::to_string(i) + "]");
    EXPECT_EQ(a.custom, b.custom) << where;
}

inline void expectEqual(const domain::Assembly& a, const domain::Assembly& b, const std::string& where) {
    EXPECT_EQ(a.id, b.id) << where;
    EXPECT_EQ(a.name, b.name) << where;
    expectEqual(a.placement, b.placement, where + ".placement");
    ASSERT_EQ(a.parts.size(), b.parts.size()) << where;
    for (std::size_t i = 0; i < a.parts.size(); ++i)
        expectEqual(a.parts[i], b.parts[i], where + ".parts[" + std::to_string(i) + "]");
    ASSERT_EQ(a.hardware.size(), b.hardware.size()) << where;
    for (std::size_t i = 0; i < a.hardware.size(); ++i)
        expectEqual(a.hardware[i], b.hardware[i], where + ".hardware[" + std::to_string(i) + "]");
}

template <class T>
void expectEqualLists(const std::vector<T>& a, const std::vector<T>& b, const std::string& where) {
    ASSERT_EQ(a.size(), b.size()) << where;
    for (std::size_t i = 0; i < a.size(); ++i) expectEqual(a[i], b[i], where + "[" + std::to_string(i) + "]");
}

inline void expectEqual(const domain::Model& a, const domain::Model& b) {
    expectEqualLists(a.materials, b.materials, "materials");
    expectEqualLists(a.edgeBands, b.edgeBands, "edgeBands");
    expectEqualLists(a.hardwareCatalog, b.hardwareCatalog, "hardwareCatalog");
    expectEqualLists(a.assemblies, b.assemblies, "assemblies");
}

} // namespace fos::formats::test
