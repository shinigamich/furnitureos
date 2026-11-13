// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/viewport/scene.hpp"

#include "fos/domain/fixtures.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace fos;
using geometry::Vec3;
using viewport::Scene;

TEST(Scene, FlattensPartsWithAssemblyPlacementAndMaterialColour) {
    auto model = domain::fixtures::basicCabinet();
    model.assemblies[0].placement = geometry::Transform::translation({1000, 2000, 0});
    model.assemblies[0].parts[3].material = domain::MaterialId("mat.missing"); // top
    Scene s;
    s.setModel(model);
    ASSERT_EQ(s.parts().size(), 6u);
    EXPECT_EQ(s.parts()[0].id, "cab1/side.left");
    EXPECT_EQ(s.parts()[5].id, "cab1/back");
    // Particleboard colour (222, 204, 170), HDF (240, 240, 235), missing material -> fallback grey.
    EXPECT_EQ(s.parts()[0].color, (domain::Color{222, 204, 170}));
    EXPECT_EQ(s.parts()[5].color, (domain::Color{240, 240, 235}));
    EXPECT_EQ(s.parts()[3].color, viewport::kFallbackColor);
    // Shelf: placed at (18, 20, 351) in the assembly, assembly at (1000, 2000, 0).
    EXPECT_TRUE(approxEqual(s.parts()[4].world.origin, Vec3{1018, 2020, 351}));
    // Cabinet envelope 800 x 563 x 720 shifted by the assembly placement.
    const auto b = s.allBounds();
    EXPECT_TRUE(approxEqual(b.min, Vec3{1000, 2000, 0}));
    EXPECT_TRUE(approxEqual(b.max, Vec3{1800, 2563, 720}));
}

TEST(Scene, VisibilityHiddenIsolatedAndInvalidParts) {
    auto model = domain::fixtures::basicCabinet();
    model.assemblies[0].parts[1].length = 0; // side.right: invalid size, never drawn/picked
    Scene s;
    s.setModel(model);
    EXPECT_FALSE(s.isVisible(1));
    EXPECT_TRUE(s.isVisible(0));
    EXPECT_FALSE(s.isVisible(99));
    s.setHidden({"cab1/top"});
    EXPECT_FALSE(s.isVisible(3));
    s.setIsolated({"cab1/shelf.1", "cab1/top"});
    EXPECT_TRUE(s.isVisible(4));
    EXPECT_FALSE(s.isVisible(3)); // hidden wins over isolated
    EXPECT_FALSE(s.isVisible(0));
    // Visible bounds = the shelf only: x 18..782, y 20..560, z 351..369.
    const auto b = s.visibleBounds();
    EXPECT_TRUE(approxEqual(b.min, Vec3{18, 20, 351}));
    EXPECT_TRUE(approxEqual(b.max, Vec3{782, 560, 369}));
    s.setIsolated({"does/not/exist"});
    EXPECT_TRUE(s.visibleBounds().empty());
    s.setSelected({"cab1/shelf.1"});
    EXPECT_TRUE(s.isSelected(4));
    EXPECT_FALSE(s.isSelected(0));
}

TEST(Scene, BuffersHaveOneBoxPerValidPart) {
    auto model = domain::fixtures::basicCabinet();
    model.assemblies[0].parts[1].width = std::nan("");
    Scene s;
    s.setModel(model);
    const auto buf = s.buildBuffers();
    // 5 valid parts: 24 vertices, 36 indices, 12 edges (24 line vertices) each.
    EXPECT_EQ(buf.vertices.size(), 5u * 24u);
    EXPECT_EQ(buf.indices.size(), 5u * 36u);
    EXPECT_EQ(buf.lines.size(), 5u * 24u);
    for (auto i : buf.indices) EXPECT_LT(i, buf.vertices.size());
    // Part index attribute skips the invalid part: vertices 24..47 belong to part 2 (bottom).
    EXPECT_EQ(buf.vertices[24].part, 2.0f);
    // The bottom (764 x 560 x 18 at (18,0,0)): all its vertices within its box.
    for (std::size_t v = 24; v < 48; ++v) {
        EXPECT_GE(buf.vertices[v].px, 18.f);
        EXPECT_LE(buf.vertices[v].px, 782.f);
        EXPECT_LE(buf.vertices[v].pz, 18.f);
    }
}

TEST(Scene, PickReturnsNearestVisiblePart) {
    Scene s;
    s.setModel(domain::fixtures::basicCabinet());
    // Ray from the front through (400, *, 360) along +Y: shelf front at y = 20 (z 351..369),
    // then the back at y = 560.
    const geometry::Ray3 ray{{400, -1000, 360}, {0, 1, 0}};
    auto hit = s.pick(ray);
    ASSERT_TRUE(hit);
    EXPECT_EQ(s.parts()[*hit].id, "cab1/shelf.1");
    s.setHidden({"cab1/shelf.1"});
    hit = s.pick(ray);
    ASSERT_TRUE(hit);
    EXPECT_EQ(s.parts()[*hit].id, "cab1/back");
    s.setHidden({"cab1/shelf.1", "cab1/back"});
    EXPECT_FALSE(s.pick(ray));
    EXPECT_FALSE(s.pick({{2000, -1000, 360}, {0, 1, 0}}));
}
