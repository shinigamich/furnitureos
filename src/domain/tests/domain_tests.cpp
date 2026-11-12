// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/fixtures.hpp"

#include <gtest/gtest.h>

using namespace fos;
using namespace fos::domain;

namespace {
const Part& part(const Model& m, const char* id) {
    const Part* p = m.assemblies.at(0).findPart(PartId(id));
    if (!p) throw std::runtime_error(std::string("missing part ") + id);
    return *p;
}
} // namespace

TEST(CutSize, BandingReducesTheOppositeDimension) {
    const Model m = fixtures::basicCabinet();
    const CutSize side = cutSize(part(m, "cab1/side.left"), m);
    EXPECT_NEAR(side.length, 719.2, 1e-9); // 720 - 0.4 - 0.4 (W1, W2)
    EXPECT_NEAR(side.width, 558.0, 1e-9);  // 560 - 2 (L1)
    const CutSize shelf = cutSize(part(m, "cab1/shelf.1"), m);
    EXPECT_NEAR(shelf.length, 764.0, 1e-9);
    EXPECT_NEAR(shelf.width, 538.0, 1e-9);
    const CutSize back = cutSize(part(m, "cab1/back"), m);
    EXPECT_NEAR(back.length, 796.0, 1e-9);
    EXPECT_NEAR(back.width, 716.0, 1e-9);
}

TEST(CutSize, UnknownEdgeBandCountsAsZero) {
    Model m = fixtures::basicCabinet();
    Part p = part(m, "cab1/bottom");
    p.edge(EdgeSide::L2) = EdgeBandId("eb.missing");
    EXPECT_NEAR(cutSize(p, m).width, 558.0, 1e-9);
    EXPECT_DOUBLE_EQ(edgeBandThickness(p, EdgeSide::L2, m), 0.0);
    EXPECT_DOUBLE_EQ(edgeLength(p, EdgeSide::L1), 764.0);
    EXPECT_DOUBLE_EQ(edgeLength(p, EdgeSide::W2), 560.0);
}

TEST(Fixture, PlacementsAreRigidAndPartsDoNotInterpenetrate) {
    const Model m = fixtures::basicCabinet();
    const Assembly& a = m.assemblies.at(0);
    ASSERT_EQ(a.parts.size(), 6u);
    for (const Part& p : a.parts) {
        EXPECT_TRUE(p.placement.isRigid()) << p.id.str();
        const Material* mat = m.findMaterial(p.material);
        ASSERT_NE(mat, nullptr) << p.id.str();
        EXPECT_DOUBLE_EQ(mat->thickness, p.thickness) << p.id.str();
    }
    for (std::size_t i = 0; i < a.parts.size(); ++i)
        for (std::size_t j = i + 1; j < a.parts.size(); ++j)
            EXPECT_FALSE(a.parts[i].bounds().overlaps(a.parts[j].bounds()))
                << a.parts[i].id.str() << " overlaps " << a.parts[j].id.str();
}

TEST(Fixture, EnvelopeAndKeyPositions) {
    const Model m = fixtures::basicCabinet();
    const geometry::Aabb box = m.assemblies.at(0).bounds();
    EXPECT_TRUE(geometry::approxEqual(box.min, {0, 0, 0}));
    EXPECT_TRUE(geometry::approxEqual(box.max, {800, 563, 720}));
    const geometry::Aabb right = part(m, "cab1/side.right").bounds();
    EXPECT_TRUE(geometry::approxEqual(right.min, {782, 0, 0}));
    EXPECT_TRUE(geometry::approxEqual(right.max, {800, 560, 720}));
    const geometry::Aabb shelf = part(m, "cab1/shelf.1").bounds();
    EXPECT_TRUE(geometry::approxEqual(shelf.min, {18, 20, 351}));
    EXPECT_TRUE(geometry::approxEqual(shelf.max, {782, 560, 369}));
    const geometry::Aabb back = part(m, "cab1/back").bounds();
    EXPECT_TRUE(geometry::approxEqual(back.min, {2, 560, 2}));
    EXPECT_TRUE(geometry::approxEqual(back.max, {798, 563, 718}));
}

TEST(Fixture, ShelfPinHoleLandsOnInnerFaceBelowShelf) {
    const Model m = fixtures::basicCabinet();
    const Part& left = part(m, "cab1/side.left");
    ASSERT_EQ(left.operations.size(), 2u);
    const Hole& h = std::get<Hole>(left.operations[0].shape);
    EXPECT_EQ(faceOf(left.operations[0]), Face::B);
    // Face B is local z = 0; (u,v) = local (x,y).
    const geometry::Vec3 world = left.placement.applyPoint({h.u, h.v, 0});
    EXPECT_TRUE(geometry::approxEqual(world, {18, 37, 346})); // inner face of the left side
    EXPECT_LT(h.depth, left.thickness);
}

TEST(Model, Lookups) {
    const Model m = fixtures::basicCabinet();
    EXPECT_NE(m.findMaterial(MaterialId("mat.hdf3")), nullptr);
    EXPECT_EQ(m.findMaterial(MaterialId("nope")), nullptr);
    EXPECT_NE(m.findEdgeBand(EdgeBandId("eb.abs04")), nullptr);
    EXPECT_NE(m.findHardware(HardwareId("hw.shelf_pin_5")), nullptr);
    EXPECT_NE(m.findAssembly(AssemblyId("cab1")), nullptr);
    EXPECT_STREQ(toString(PartRole::Shelf), "Shelf");
    EXPECT_STREQ(toString(EdgeSide::W2), "W2");
}

TEST(SheetRotation, TruthTableOfTheGrainRule) {
    Material grained;
    grained.hasGrain = true;
    Material plain;
    plain.hasGrain = false;
    Part p;
    // Part without grain: always free, whatever the material.
    p.grain = GrainDirection::None;
    EXPECT_TRUE(canRotateOnSheet(p, &grained));
    EXPECT_TRUE(canRotateOnSheet(p, &plain));
    EXPECT_TRUE(canRotateOnSheet(p, nullptr));
    EXPECT_EQ(sheetAlignment(p, &grained), SheetAlignment::Free);
    // Grained part on a grained material: fixed; AlongLength keeps length along the sheet.
    p.grain = GrainDirection::AlongLength;
    EXPECT_FALSE(canRotateOnSheet(p, &grained));
    EXPECT_EQ(sheetAlignment(p, &grained), SheetAlignment::LengthAlongSheet);
    // ... AlongWidth puts the part width along the sheet length.
    p.grain = GrainDirection::AlongWidth;
    EXPECT_FALSE(canRotateOnSheet(p, &grained));
    EXPECT_EQ(sheetAlignment(p, &grained), SheetAlignment::LengthAcrossSheet);
    // Material without grain: the part's grain does not matter.
    EXPECT_TRUE(canRotateOnSheet(p, &plain));
    EXPECT_EQ(sheetAlignment(p, &plain), SheetAlignment::Free);
    // Missing material: the part's own grain is respected (conservative).
    EXPECT_FALSE(canRotateOnSheet(p, nullptr));
    EXPECT_EQ(sheetAlignment(p, nullptr), SheetAlignment::LengthAcrossSheet);
}

TEST(SheetRotation, FixtureParts) {
    const Model m = fixtures::basicCabinet();
    // Fixture: the side is AlongLength on grained particleboard; the back is grain None on HDF.
    const Part& side = part(m, "cab1/side.left");
    const Part& back = part(m, "cab1/back");
    EXPECT_FALSE(canRotateOnSheet(side, m.findMaterial(side.material)));
    EXPECT_EQ(sheetAlignment(side, m.findMaterial(side.material)), SheetAlignment::LengthAlongSheet);
    EXPECT_TRUE(canRotateOnSheet(back, m.findMaterial(back.material)));
}
