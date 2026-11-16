// SPDX-License-Identifier: AGPL-3.0-only
/// Contoured parts (ADR-0008): shape checks and exact-outline interference.
#include "test_support.hpp"

#include <cmath>

using namespace fos;
using namespace fos::validation;
using namespace fos::validation::test;
using geometry::Transform;
using geometry::Vec3;

namespace {
const Vec3 kUp{0, 0, 1}, kBack{0, 1, 0};

/// Horizontal 800 x 400 x 18 shelf at z = 0 with a 60 x 60 cut-out at [100, 160]^2.
domain::Part cutoutShelf() {
    domain::Part p = board("shelf", 800, 400);
    p.name = "Shelf";
    domain::PartShape s;
    s.outer = geometry::rectangleContour(800, 400);
    s.cutouts = {geometry::translated(geometry::rectangleContour(60, 60), {100, 100})};
    p.shape = s;
    return p;
}

/// 600 x 400 shelf, R 50 corners, at z = 0 (corner arc centre (50, 50)).
domain::Part roundShelf(const std::string& id = "round", Transform t = {}) {
    domain::Part p = board(id, 600, 400, t);
    const double r[4] = {50, 50, 50, 50};
    p.shape = domain::PartShape{geometry::roundedRectangleContour(600, 400, r).value(), {}, {}};
    return p;
}

/// Vertical post, cross-section x in [x0, x0 + w], y in [y0, y0 + w], z in [-100, 500].
domain::Part post(double x0, double y0, double w = 40) {
    domain::Part p = board("post", 600, w, Transform::fromAxes(kUp, kBack, {x0 + w, y0, -100}), "mat.pb18", w);
    p.name = "Post";
    return p;
}

std::size_t interferences(const ValidationReport& r) { return count(r, "validation.interference.parts"); }
} // namespace

// ---------------------------------------------------------------- interference

TEST(ShapeInterference, PostThroughCutoutIsNotReported) {
    // Boxes overlap (post inside the shelf's bounding box), material does not: the post
    // (x, y in [110, 150]) stays 10 mm clear of the cut-out walls at 100 and 160.
    const auto r = validateModel(libraryModel({cutoutShelf(), post(110, 110)}));
    EXPECT_EQ(interferences(r), 0u) << toText(r);
    EXPECT_EQ(r.stats.partPairsTested, 1u);
    EXPECT_EQ(r.stats.shapedPairsRefined, 1u);
    EXPECT_EQ(r.stats.shapedPairsCleared, 1u);
}

TEST(ShapeInterference, PostOutsideCutoutIsReportedWithDepth) {
    // Post x in [130, 170]: the material x in [160, 170] x y [110, 150] x z [0, 18] is shared.
    // Largest ball inside 10 x 40 x 18 -> depth 10 mm. Box test: along Z the shelf (half 9,
    // centre 9) and the post (half 300, centre 200) overlap by 9 + 300 - 191 = 118 mm.
    const auto r = validateModel(libraryModel({cutoutShelf(), post(130, 110)}));
    const Finding f = only(r, "validation.interference.parts");
    EXPECT_EQ(f.partId, "post"); // larger extent along Z penetrates
    EXPECT_NE(f.message.find("'Post' penetrates 'Shelf' by 10 mm (diameter of the largest ball"), std::string::npos)
        << f.message;
    EXPECT_NE(f.message.find("box test: 118 mm along Z"), std::string::npos) << f.message;
    EXPECT_EQ(r.stats.shapedPairsCleared, 0u);
}

TEST(ShapeInterference, ExactTestCanBeSwitchedOff) {
    ValidationOptions o;
    o.exactShapeInterference = false;
    const auto r = validateModel(libraryModel({cutoutShelf(), post(110, 110)}), o);
    const Finding f = only(r, "validation.interference.parts");
    EXPECT_NE(f.message.find("by 118 mm along Z."), std::string::npos) << f.message;
    EXPECT_EQ(r.stats.shapedPairsRefined, 0u);
}

TEST(ShapeInterference, RoundedCornerNextToPost) {
    // Post square [-15, 10]^2: its corner (10, 10) is 40*sqrt(2) = 56.57 mm from the arc
    // centre (50, 50) > R 50 -> the rounded corner removed that material: no interference.
    auto r = validateModel(libraryModel({roundShelf(), post(-15, -15, 25)}));
    EXPECT_EQ(interferences(r), 0u) << toText(r);
    EXPECT_EQ(r.stats.shapedPairsCleared, 1u);
    // Post square [-15, 20]^2: the shared region is [0, 20]^2 inside the disc. Largest inscribed
    // circle: centre (t, t) with 20 - t = 50 - (50 - t) sqrt(2) -> t = (50 sqrt2 - 30) / (sqrt2 + 1)
    // = 16.8629, radius 3.1371 -> depth 6.2742 mm (< thickness 18).
    r = validateModel(libraryModel({roundShelf(), post(-15, -15, 35)}));
    const Finding f = only(r, "validation.interference.parts");
    EXPECT_NE(f.message.find("by 6.3 mm (diameter"), std::string::npos) << f.message;
}

TEST(ShapeInterference, TwoShapedPartsInOnePlane) {
    // Two R 50 shelves at the same height, corner to corner. B at (560, 360): corner arc
    // centres (550, 350) and (610, 410), 60 sqrt 2 = 84.85 apart < 100 -> lens of width
    // 100 - 84.853 = 15.147 mm = largest inscribed circle.
    auto r = validateModel(libraryModel({roundShelf("a"), roundShelf("b", Transform::translation({560, 360, 0}))}));
    Finding f = only(r, "validation.interference.parts");
    EXPECT_NE(f.message.find("by 15.1 mm (diameter"), std::string::npos) << f.message;
    // B at (580, 380): centres 80 sqrt 2 = 113.1 apart > 100 -> boxes overlap, outlines do not.
    r = validateModel(libraryModel({roundShelf("a"), roundShelf("b", Transform::translation({580, 380, 0}))}));
    EXPECT_EQ(interferences(r), 0u) << toText(r);
    // Same lens with B turned upside down (rotation of 180 degrees about X, thickness axis
    // anti-parallel -> mirrored outline): local (x, y, z) -> (560 + x, 760 - y, 18 - z).
    const Transform flipped = Transform::fromAxes({1, 0, 0}, {0, -1, 0}, {560, 760, 18});
    r = validateModel(libraryModel({roundShelf("a"), roundShelf("b", flipped)}));
    f = only(r, "validation.interference.parts");
    EXPECT_NE(f.message.find("by 15.1 mm (diameter"), std::string::npos) << f.message;
}

TEST(ShapeInterference, CrossAssemblyUsesExactOutline) {
    domain::Model m = libraryModel({cutoutShelf()});
    domain::Assembly b;
    b.id = domain::AssemblyId("a2");
    b.name = "Posts";
    b.parts = {post(110, 110)};
    m.assemblies.push_back(b);
    auto r = validateModel(m);
    EXPECT_EQ(count(r, "validation.interference.assemblies"), 0u) << toText(r);
    m.assemblies[1].parts = {post(130, 110)};
    r = validateModel(m);
    const Finding f = only(r, "validation.interference.assemblies");
    EXPECT_NE(f.message.find("by 10 mm (diameter"), std::string::npos) << f.message;
}

// ---------------------------------------------------------------- shape checks

TEST(ShapeChecks, ValidShapesProduceNoShapeFindings) {
    domain::Part p = roundShelf();
    p.shape->outerEdges.assign(p.shape->outer.size(), domain::EdgeBandId("eb.abs2")); // R 50 - 2 = 48 >= 20
    const auto r = validateModel(libraryModel({p, cutoutShelf()}));
    for (const auto& f : r.diagnostics) {
        EXPECT_EQ(f.code.rfind("validation.shape.", 0), std::string::npos) << f.message;
        EXPECT_NE(f.code, "validation.edge.radius_too_small");
    }
}

TEST(ShapeChecks, StructuralErrors) {
    domain::Part bounds = roundShelf("bounds");
    bounds.length = 650; // outline only reaches x = 600
    domain::Part bow = roundShelf("bow");
    bow.shape->outer.vertices = {{{0, 0}, 0}, {{600, 400}, 0}, {{600, 0}, 0}, {{0, 400}, 0}};
    domain::Part cw = roundShelf("cw");
    cw.shape->outer = geometry::reversed(cw.shape->outer);
    domain::Part out = cutoutShelf();
    out.id = domain::PartId("out");
    out.shape->cutouts.push_back(geometry::circleContour({790, 200}, 20)); // crosses x = 800
    out.shape->cutouts.push_back(geometry::circleContour({300, 200}, 20));
    out.shape->cutouts.push_back(geometry::circleContour({330, 200}, 20)); // overlaps the previous one
    domain::Part edges = roundShelf("edges");
    edges.edge(domain::EdgeSide::L1) = domain::EdgeBandId("eb.abs2");
    edges.shape->outerEdges.assign(3, std::nullopt); // outline has 8 segments
    const auto r = validateModel(libraryModel({bounds, bow, cw, out, edges}));
    EXPECT_EQ(only(r, "validation.shape.bounds_mismatch").partId, "bounds");
    EXPECT_NE(only(r, "validation.shape.bounds_mismatch").message.find("[0, 600] x [0, 400]"), std::string::npos);
    EXPECT_EQ(only(r, "validation.shape.invalid_outline").partId, "bow");
    EXPECT_EQ(only(r, "validation.shape.clockwise_outline").partId, "cw");
    const Finding o = only(r, "validation.shape.cutout_outside");
    EXPECT_NE(o.message.find("cut-out 1"), std::string::npos) << o.message;
    EXPECT_NE(only(r, "validation.shape.cutouts_overlap").message.find("cut-out 2 and cut-out 3"), std::string::npos);
    EXPECT_EQ(only(r, "validation.shape.edges_with_shape").partId, "edges");
    EXPECT_NE(only(r, "validation.shape.outer_edges_count").message.find("3 outline edge band entries"),
              std::string::npos);
}

TEST(ShapeChecks, BendRadiusAndRawOutline) {
    // R 15 banded with 2 mm ABS: inner face radius 15 - 2 = 13 < 10 x 2 = 20 -> warning.
    domain::Part tight = roundShelf("tight");
    const double r15[4] = {15, 15, 15, 15};
    tight.shape->outer = geometry::roundedRectangleContour(600, 400, r15).value();
    tight.shape->outerEdges.assign(8, std::nullopt);
    tight.shape->outerEdges[1] = domain::EdgeBandId("eb.abs2");
    // R 1.5 banded with 2 mm: the raw arc radius would be -0.5 -> cutContour fails.
    domain::Part tiny = roundShelf("tiny");
    const double r1[4] = {1.5, 1.5, 1.5, 1.5};
    tiny.shape->outer = geometry::roundedRectangleContour(600, 400, r1).value();
    tiny.shape->outerEdges.assign(8, domain::EdgeBandId("eb.abs2"));
    tiny.shape->outerEdges[2] = domain::EdgeBandId("eb.nope");
    const auto r = validateModel(libraryModel({tight, tiny}));
    const auto bends = findingsWithCode(r, "validation.edge.radius_too_small");
    ASSERT_EQ(bends.size(), 5u) << toText(r); // tight: 1 arc, tiny: 4 arcs
    std::string tightMsg;
    for (const auto& f : bends)
        if (f.partId == "tight") tightMsg = f.message;
    EXPECT_NE(tightMsg.find("segment 1 is a convex arc of radius 15 mm"), std::string::npos) << tightMsg;
    EXPECT_NE(tightMsg.find("bend around 13 mm, below the minimum of 20 mm"), std::string::npos) << tightMsg;
    EXPECT_EQ(only(r, "validation.shape.cut_contour").partId, "tiny");
    EXPECT_NE(only(r, "validation.edge.missing_band").message.find("outline segment 2"), std::string::npos);

    ValidationOptions o;
    o.edgeBandMinBendRadiusFactor = 0; // disabled
    EXPECT_EQ(count(validateModel(libraryModel({tight}), o), "validation.edge.radius_too_small"), 0u);
}

TEST(ShapeInterference, RotatedPostThroughCutout) {
    // 30 x 30 post turned 45 degrees about the vertical, centred at (130, 130): its corners
    // reach 15 sqrt 2 = 21.2 mm from the centre, the cut-out walls are 30 mm away -> clear.
    // Shifted 20 mm in +x (centre (150, 130)): corner at x = 171.2 > 160 -> real overlap.
    const double c = std::sqrt(0.5);
    auto rotated = [&](double cx) {
        // local x -> up, local y -> (c, c, 0) (45 degrees), thickness -> (-c, c, 0).
        const Vec3 ydir{c, c, 0};
        const Transform t = Transform::fromAxes(kUp, ydir, {0, 0, -100});
        // Place the box centre (local (300, 15, 15)) at (cx, 130, 200).
        const Vec3 centre = t.applyPoint({300, 15, 15});
        domain::Part p = board("post", 600, 30,
                               Transform::fromAxes(kUp, ydir, {cx - centre.x, 130 - centre.y, -100}), "mat.pb18", 30);
        p.name = "Post";
        return p;
    };
    auto r = validateModel(libraryModel({cutoutShelf(), rotated(130)}));
    EXPECT_EQ(interferences(r), 0u) << toText(r);
    EXPECT_EQ(r.stats.shapedPairsCleared, 1u);
    r = validateModel(libraryModel({cutoutShelf(), rotated(150)}));
    EXPECT_EQ(interferences(r), 1u) << toText(r);
}

TEST(ShapeChecks, MachiningOnShapedPartIsMerged) {
    domain::Part s = cutoutShelf();
    domain::Operation h;
    h.id = domain::OperationId("h");
    h.shape = domain::Hole{domain::Face::A, 130, 130, 8, 10, false}; // inside the cut-out
    s.operations = {h};
    const auto r = validateModel(libraryModel({s}));
    const Finding f = only(r, "machining.hole.outside_material");
    EXPECT_EQ(f.partId, "shelf");
    EXPECT_NE(f.message.find("inside cut-out 0"), std::string::npos) << f.message;
}
