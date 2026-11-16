// SPDX-License-Identifier: AGPL-3.0-only
/// machining::validateOperations on contoured parts (ADR-0008). Hand-computed expectations.
#include "fos/machining/material_region.hpp"
#include "fos/machining/validation.hpp"

#include <gtest/gtest.h>

using namespace fos;
using namespace fos::machining;
using domain::Face;

namespace {

domain::Part shelfWithCutout() {
    // 800 x 400 x 18, square cut-out [100, 160] x [100, 160].
    domain::Part p;
    p.id = domain::PartId("s");
    p.name = "Shelf";
    p.length = 800;
    p.width = 400;
    p.thickness = 18;
    domain::PartShape s;
    s.outer = geometry::rectangleContour(800, 400);
    s.cutouts = {geometry::translated(geometry::rectangleContour(60, 60), {100, 100})};
    p.shape = s;
    return p;
}

domain::Part roundedPanel() {
    // 600 x 400 x 18, R 50 corners (arc centres (50, 50), (550, 50), (550, 350), (50, 350)).
    domain::Part p = shelfWithCutout();
    p.name = "Round";
    p.length = 600;
    const double r[4] = {50, 50, 50, 50};
    p.shape = domain::PartShape{geometry::roundedRectangleContour(600, 400, r).value(), {}, {}};
    return p;
}

domain::Operation hole(const char* id, Face f, double u, double v, double d, double depth) {
    domain::Operation op;
    op.id = domain::OperationId(id);
    op.shape = domain::Hole{f, u, v, d, depth, false};
    return op;
}
domain::Operation groove(const char* id, Face f, double u1, double v1, double u2, double v2, double w, double depth) {
    domain::Operation op;
    op.id = domain::OperationId(id);
    op.shape = domain::Groove{f, u1, v1, u2, v2, w, depth};
    return op;
}
domain::Operation pocket(const char* id, std::vector<geometry::Vec2> pts) {
    domain::Operation op;
    op.id = domain::OperationId(id);
    domain::Pocket pk;
    pk.face = Face::A;
    pk.contour = std::move(pts);
    pk.depth = 5;
    op.shape = pk;
    return op;
}

std::vector<std::string> codesFor(const core::Diagnostics& ds, const std::string& opId) {
    std::vector<std::string> out;
    for (const auto& d : ds)
        if (d.message.find("'" + opId + "'") != std::string::npos) out.push_back(d.code);
    return out;
}
std::string messageFor(const core::Diagnostics& ds, const std::string& opId) {
    for (const auto& d : ds)
        if (d.message.find("'" + opId + "'") != std::string::npos) return d.message;
    return {};
}
using Codes = std::vector<std::string>;

} // namespace

TEST(ShapedMachining, HolesInsideCutoutAndNearCutout) {
    domain::Part p = shelfWithCutout();
    p.operations = {hole("in", Face::A, 130, 130, 8, 10),   // centre inside the cut-out
                    hole("ok", Face::A, 170, 110, 8, 10),   // 10 mm from x = 160: wall 6 mm >= 3
                    hole("near", Face::A, 165, 130, 8, 10), // 5 - 4 = 1 mm of material
                    hole("brk", Face::A, 162, 150, 8, 10)}; // 2 - 4 = -2: breaks into the cut-out
    const auto d = validateOperations(p);
    EXPECT_EQ(codesFor(d, "in"), Codes{"machining.hole.outside_material"});
    EXPECT_NE(messageFor(d, "in").find("inside cut-out 0"), std::string::npos) << messageFor(d, "in");
    EXPECT_TRUE(codesFor(d, "ok").empty());
    EXPECT_EQ(codesFor(d, "near"), Codes{"machining.hole.too_close_to_edge"});
    EXPECT_NE(messageFor(d, "near").find("leaves only 1 mm of material to cut-out 0"), std::string::npos)
        << messageFor(d, "near");
    EXPECT_NE(messageFor(d, "brk").find("breaks out of cut-out 0 by 2 mm"), std::string::npos) << messageFor(d, "brk");
}

TEST(ShapedMachining, HolesNearRoundedCorner) {
    domain::Part p = roundedPanel();
    // (10, 10): 40*sqrt(2) = 56.57 > 50 from the arc centre -> outside the outline.
    // (20, 20): 30*sqrt(2) = 42.43 -> 7.57 to the arc, minus radius 2.5 = 5.07 mm: fine.
    // (15, 15): 35*sqrt(2) = 49.497 -> 0.503 - 2.5 = -1.997: breaks out by 2 mm.
    p.operations = {hole("out", Face::A, 10, 10, 5, 10), hole("ok", Face::A, 20, 20, 5, 10),
                    hole("brk", Face::A, 15, 15, 5, 10)};
    const auto d = validateOperations(p);
    EXPECT_EQ(codesFor(d, "out"), Codes{"machining.hole.outside_material"});
    EXPECT_NE(messageFor(d, "out").find("outside the shaped outline"), std::string::npos);
    EXPECT_TRUE(codesFor(d, "ok").empty());
    EXPECT_NE(messageFor(d, "brk").find("breaks out of the shaped outline by 2 mm"), std::string::npos)
        << messageFor(d, "brk");
}

TEST(ShapedMachining, GroovesAndPockets) {
    domain::Part p = roundedPanel();
    p.shape->cutouts = {geometry::translated(geometry::rectangleContour(60, 60), {100, 100})};
    p.operations = {
        groove("cross", Face::A, 50, 130, 300, 130, 8, 8),  // runs through the cut-out
        groove("fine", Face::A, 200, 250, 500, 250, 8, 8),  // inside the material
        groove("thru", Face::B, 0, 300, 600, 300, 8, 8),    // runs out at the straight sides x = 0 / 600
        groove("corner", Face::A, 0, 380, 600, 380, 8, 8),  // y = 376..384 lies in the R 50 corner zone
        pocket("encl", {{90, 90}, {170, 90}, {170, 170}, {90, 170}}),  // surrounds the cut-out
        pocket("good", {{300, 100}, {400, 100}, {400, 200}, {300, 200}})};
    const auto d = validateOperations(p);
    EXPECT_EQ(codesFor(d, "cross"), Codes{"machining.groove.outside_material"});
    EXPECT_NE(messageFor(d, "cross").find("crosses cut-out 0"), std::string::npos);
    EXPECT_TRUE(codesFor(d, "fine").empty());
    EXPECT_TRUE(codesFor(d, "thru").empty()) << messageFor(d, "thru");
    EXPECT_EQ(codesFor(d, "corner"), Codes{"machining.groove.outside_material"});
    EXPECT_NE(messageFor(d, "corner").find("crosses the shaped outline"), std::string::npos);
    EXPECT_EQ(codesFor(d, "encl"), Codes{"machining.pocket.outside_material"});
    EXPECT_NE(messageFor(d, "encl").find("encloses cut-out 0"), std::string::npos);
    EXPECT_TRUE(codesFor(d, "good").empty());
}

TEST(ShapedMachining, EdgeHoles) {
    domain::Part p = shelfWithCutout();
    const double r[4] = {50, 50, 0, 0};
    p.shape->outer = geometry::roundedRectangleContour(800, 400, r).value();
    // L1 (y = 0): straight only for x in [50, 750].
    p.operations = {hole("ok", Face::L1, 400, 9, 8, 30),
                    hole("arc", Face::L1, 20, 9, 8, 30),    // entry on the corner arc: in the air
                    hole("cut", Face::L1, 130, 9, 8, 120)}; // drilled y 0..120 enters cut-out y >= 100
    const auto d = validateOperations(p);
    EXPECT_TRUE(codesFor(d, "ok").empty()) << messageFor(d, "ok");
    EXPECT_EQ(codesFor(d, "arc"), Codes{"machining.hole.outside_material"});
    EXPECT_EQ(codesFor(d, "cut"), Codes{"machining.hole.outside_material"});
    EXPECT_NE(messageFor(d, "cut").find("crosses cut-out 0"), std::string::npos) << messageFor(d, "cut");
}

TEST(ShapedMachining, RectangularOutlineShapeBehavesLikePlainPanel) {
    domain::Part plain = shelfWithCutout();
    plain.shape.reset();
    plain.operations = {hole("a", Face::A, 4, 200, 8, 10), hole("b", Face::A, 900, 200, 8, 10),
                        groove("g", Face::A, 0, 380, 800, 380, 8, 20), hole("e", Face::L1, 300, 9, 8, 30)};
    domain::Part shaped = plain;
    shaped.shape = domain::PartShape{geometry::rectangleContour(800, 400), {}, {}};
    EXPECT_FALSE(shapedMaterialRegion(shaped).has_value());
    auto text = [](const core::Diagnostics& ds) {
        std::string s;
        for (const auto& d : ds) s += d.code + " " + d.message + "\n";
        return s;
    };
    EXPECT_EQ(text(validateOperations(plain)), text(validateOperations(shaped)));
    EXPECT_EQ(validateOperations(plain).size(), 3u); // too close, out of bounds, too deep
}

TEST(ShapedMachining, MaterialRegionPrimitives) {
    domain::Part p = shelfWithCutout();
    const auto r = shapedMaterialRegion(p);
    ASSERT_TRUE(r);
    EXPECT_EQ(locateInMaterial(*r, {130, 130}).location, MaterialLocation::InCutout);
    EXPECT_EQ(locateInMaterial(*r, {300, 300}).location, MaterialLocation::Material);
    EXPECT_EQ(locateInMaterial(*r, {160, 130}).location, MaterialLocation::Boundary);
    EXPECT_EQ(locateInMaterial(*r, {900, 130}).location, MaterialLocation::OutsideOutline);
    const BoundaryDistance bd = distanceToBoundary(*r, {170, 130});
    EXPECT_NEAR(bd.distance, 10, 1e-12);
    EXPECT_TRUE(bd.onCutout);
    // A square that sits inside the cut-out does not intersect the material.
    MaterialRegion sq{geometry::translated(geometry::rectangleContour(40, 40), {110, 110}), {}};
    EXPECT_FALSE(regionsIntersect(*r, sq));
    sq.outer = geometry::translated(geometry::rectangleContour(40, 40), {130, 110});
    EXPECT_TRUE(regionsIntersect(*r, sq));
}
