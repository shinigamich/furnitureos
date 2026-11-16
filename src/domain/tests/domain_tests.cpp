// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/fixtures.hpp"

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>

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

// ---- Contoured parts (ADR-0008) -------------------------------------------------------------

namespace {
/// The rectangular part expressed as a shape: plain rectangle contour with the part's edges
/// moved onto the segments (segment 0..3 = L1, W2, L2, W1) and Part::edges cleared.
Part asRectangleShape(const Part& p) {
    Part s = p;
    PartShape sh;
    sh.outer = geometry::rectangleContour(p.length, p.width);
    sh.outerEdges = {p.edge(EdgeSide::L1), p.edge(EdgeSide::W2), p.edge(EdgeSide::L2), p.edge(EdgeSide::W1)};
    s.shape = sh;
    s.edges = {};
    return s;
}

Part roundedShelf(const Model& m, double radius, std::optional<EdgeBandId> band) {
    Part p = part(m, "cab1/shelf.1");
    p.length = 600;
    p.width = 400;
    p.edges = {};
    const double r[4] = {radius, radius, radius, radius};
    PartShape sh;
    sh.outer = geometry::roundedRectangleContour(600, 400, r).value();
    sh.outerEdges.assign(sh.outer.size(), band);
    p.shape = sh;
    return p;
}
} // namespace

TEST(ContouredPart, FixtureCutSizesAreUnchanged) {
    const Model m = fixtures::basicCabinet();
    // Hand-specified table of fixtures.hpp (ADR-0004 formulas).
    const std::map<std::string, std::pair<double, double>> expected = {
        {"cab1/side.left", {719.2, 558}}, {"cab1/side.right", {719.2, 558}}, {"cab1/bottom", {764, 558}},
        {"cab1/top", {764, 558}},         {"cab1/shelf.1", {764, 538}},      {"cab1/back", {796, 716}}};
    ASSERT_EQ(m.assemblies.at(0).parts.size(), expected.size());
    for (const Part& p : m.assemblies.at(0).parts) {
        EXPECT_FALSE(hasShape(p));
        EXPECT_TRUE(isRectangularOutline(p));
        const auto& e = expected.at(p.id.str());
        const CutSize c = cutSize(p, m);
        EXPECT_NEAR(c.length, e.first, 1e-9) << p.id.str();
        EXPECT_NEAR(c.width, e.second, 1e-9) << p.id.str();
        // The raw contour of the rectangular path is exactly [t(W1), L - t(W2)] x [t(L1), W - t(L2)].
        const auto raw = cutContour(p, m);
        ASSERT_TRUE(raw.ok()) << p.id.str();
        const geometry::Rect2 b = geometry::bounds(raw.value());
        EXPECT_NEAR(b.min.x, edgeBandThickness(p, EdgeSide::W1, m), 1e-9) << p.id.str();
        EXPECT_NEAR(b.max.x, p.length - edgeBandThickness(p, EdgeSide::W2, m), 1e-9) << p.id.str();
        EXPECT_NEAR(b.min.y, edgeBandThickness(p, EdgeSide::L1, m), 1e-9) << p.id.str();
        EXPECT_NEAR(b.max.y, p.width - edgeBandThickness(p, EdgeSide::L2, m), 1e-9) << p.id.str();
    }
}

TEST(ContouredPart, RectangleShapeWithSegmentBandsMatchesRectangularPath) {
    const Model m = fixtures::basicCabinet();
    for (const Part& p : m.assemblies.at(0).parts) {
        const Part s = asRectangleShape(p);
        ASSERT_TRUE(hasShape(s));
        EXPECT_TRUE(isRectangularOutline(s)) << p.id.str();
        const CutSize a = cutSize(p, m), b = cutSize(s, m);
        EXPECT_NEAR(b.length, a.length, 1e-9) << p.id.str();
        EXPECT_NEAR(b.width, a.width, 1e-9) << p.id.str();
        for (std::size_t i = 0; i < 4; ++i)
            EXPECT_DOUBLE_EQ(contourBandThickness(s, i, m), contourBandThickness(p, i, m)) << p.id.str() << " " << i;
    }
    // side.left: L1 abs2, W1 + W2 abs04 -> 720 - 0.8 = 719.2 by 560 - 2 = 558 through the offset.
    const CutSize side = cutSize(asRectangleShape(part(m, "cab1/side.left")), m);
    EXPECT_NEAR(side.length, 719.2, 1e-9);
    EXPECT_NEAR(side.width, 558.0, 1e-9);
}

TEST(ContouredPart, RoundedShelfBandedAllRound) {
    const Model m = fixtures::basicCabinet();
    const Part p = roundedShelf(m, 50, EdgeBandId("eb.abs2"));
    EXPECT_FALSE(isRectangularOutline(p));
    const auto raw = cutContour(p, m);
    ASSERT_TRUE(raw.ok()) << raw.error().message;
    ASSERT_EQ(raw.value().size(), 8u);
    // Every segment moves in by 2 mm: corner radius 50 -> 48 (centres unchanged).
    for (std::size_t i = 1; i < 8; i += 2) {
        const geometry::ContourSegmentInfo a = geometry::segmentInfo(raw.value(), i);
        ASSERT_TRUE(a.isArc);
        EXPECT_NEAR(a.radius, 48.0, 1e-9);
    }
    // Cut size = 600 - 2*2 by 400 - 2*2.
    const CutSize c = cutSize(p, m);
    EXPECT_NEAR(c.length, 596.0, 1e-9);
    EXPECT_NEAR(c.width, 396.0, 1e-9);
    // Unbanded: the raw contour is the finished one.
    const Part bare = roundedShelf(m, 50, std::nullopt);
    EXPECT_EQ(cutContour(bare, m).value(), bare.shape->outer);
    EXPECT_NEAR(cutSize(bare, m).length, 600.0, 1e-12);
    // Banding that the corner radius cannot take: r 1.5 with 2 mm band -> no raw contour, and
    // cutSize falls back to the finished size (validation reports the shape).
    const Part tight = roundedShelf(m, 1.5, EdgeBandId("eb.abs2"));
    EXPECT_FALSE(cutContour(tight, m).ok());
    EXPECT_NEAR(cutSize(tight, m).length, 600.0, 1e-12);
}

TEST(ContouredPart, RectangularOutlineDetection) {
    const Model m = fixtures::basicCabinet();
    Part p = part(m, "cab1/shelf.1"); // 764 x 540
    EXPECT_TRUE(isRectangularOutline(p)); // no shape
    p.shape = PartShape{geometry::rectangleContour(764, 540), {}, {}};
    EXPECT_TRUE(isRectangularOutline(p));
    // Same rectangle starting at another corner / clockwise: still a plain rectangle.
    p.shape->outer = geometry::reversed(geometry::rectangleContour(764, 540));
    EXPECT_TRUE(isRectangularOutline(p));
    // Rectangle not covering [0,L]x[0,W].
    p.shape->outer = geometry::rectangleContour(700, 540);
    EXPECT_FALSE(isRectangularOutline(p));
    // An arc segment.
    p.shape->outer = geometry::rectangleContour(764, 540);
    p.shape->outer.vertices[2].bulge = 0.1;
    EXPECT_FALSE(isRectangularOutline(p));
    // A cut-out.
    p.shape->outer = geometry::rectangleContour(764, 540);
    p.shape->cutouts.push_back(geometry::circleContour({100, 100}, 20));
    EXPECT_FALSE(isRectangularOutline(p));
    // Rounded corners.
    EXPECT_FALSE(isRectangularOutline(roundedShelf(m, 10, std::nullopt)));
}

TEST(ContouredPart, BandThicknessMappingForRectangles) {
    Model m = fixtures::basicCabinet();
    Part p = part(m, "cab1/side.left"); // L1 abs2 (2.0), W1 + W2 abs04 (0.4), L2 none
    // finishedContour: CCW from (0,0): segments L1 (bottom), W2 (right), L2 (top), W1 (left).
    EXPECT_EQ(finishedContour(p), geometry::rectangleContour(720, 560));
    p.edge(EdgeSide::W2) = EdgeBandId("eb.abs2"); // make W1 and W2 distinguishable
    EXPECT_DOUBLE_EQ(contourBandThickness(p, 0, m), 2.0); // L1
    EXPECT_DOUBLE_EQ(contourBandThickness(p, 1, m), 2.0); // W2
    EXPECT_DOUBLE_EQ(contourBandThickness(p, 2, m), 0.0); // L2
    EXPECT_DOUBLE_EQ(contourBandThickness(p, 3, m), 0.4); // W1
    EXPECT_DOUBLE_EQ(contourBandThickness(p, 4, m), 0.0); // out of range
    // cut contour = [0.4, 718] x [2, 560].
    const auto raw = cutContour(p, m);
    ASSERT_TRUE(raw.ok());
    const geometry::Rect2 b = geometry::bounds(raw.value());
    EXPECT_NEAR(b.min.x, 0.4, 1e-12);
    EXPECT_NEAR(b.max.x, 718.0, 1e-12);
    EXPECT_NEAR(b.min.y, 2.0, 1e-12);
    EXPECT_NEAR(b.max.y, 560.0, 1e-12);
    // Shaped part: per-segment bands; missing band entries and unknown bands count as 0.
    Part s = roundedShelf(m, 50, std::nullopt);
    s.shape->outerEdges[2] = EdgeBandId("eb.abs04");
    s.shape->outerEdges[3] = EdgeBandId("eb.missing");
    s.shape->outerEdges.resize(4);
    EXPECT_DOUBLE_EQ(contourBandThickness(s, 2, m), 0.4);
    EXPECT_DOUBLE_EQ(contourBandThickness(s, 3, m), 0.0);
    EXPECT_DOUBLE_EQ(contourBandThickness(s, 6, m), 0.0);
}
