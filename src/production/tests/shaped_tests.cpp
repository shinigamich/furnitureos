// SPDX-License-Identifier: AGPL-3.0-only
/// Contoured parts (ADR-0008) in the production reports. All expectations hand-computed.
#include "fos/production/production.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace fos;
using namespace fos::production;
using production::testing::panel;
using production::testing::smallModel;

namespace {
constexpr double kPi = 3.14159265358979323846;
const domain::EdgeBandId kBand("eb2");

/// smallModel() plus a 2 mm band "eb2" (1.00 / m) and an 18 mm board with a 2800 x 2070 sheet.
domain::Model model() {
    domain::Model m = smallModel();
    m.materials.front().sheetLength = 2800;
    m.materials.front().sheetWidth = 2070;
    domain::EdgeBand b;
    b.id = kBand;
    b.name = "ABS 2";
    b.thickness = 2;
    b.width = 23;
    b.costPerMeter = 1.0;
    m.edgeBands = {b};
    return m;
}

/// 600 x 400, R 50 at every corner, every one of the 8 segments banded with 2 mm.
domain::Part rounded(const std::string& id, double r = 50, bool banded = true) {
    domain::Part p = panel(id, "Round " + id, 600, 400, 18, "m1");
    const double radii[4] = {r, r, r, r};
    domain::PartShape s;
    s.outer = geometry::roundedRectangleContour(600, 400, radii).value();
    if (banded) s.outerEdges.assign(s.outer.size(), kBand);
    p.shape = s;
    return p;
}

/// 800 x 700 side panel: rectangle 800 x 600 whose top segment is an arc with bulge 0.25
/// (chord 800, rise 0.25 * 400 = 100 mm, radius (400^2 + 100^2) / 200 = 850 mm). Only the
/// arc is banded.
domain::Part arcTop() {
    domain::Part p = panel("arc", "Arc side", 800, 700, 18, "m1");
    domain::PartShape s;
    s.outer.vertices = {{{0, 0}, 0}, {{800, 0}, 0}, {{800, 600}, 0.25}, {{0, 600}, 0}};
    s.outerEdges = {std::nullopt, std::nullopt, kBand, std::nullopt};
    p.shape = s;
    return p;
}

/// Worktop 1200 x 600, sink cut-out 500 x 400 at (350, 100), tap hole R 17.5 at (1000, 300),
/// front edge (segment 0) banded.
domain::Part worktop() {
    domain::Part p = panel("wt", "Worktop", 1200, 600, 18, "m1");
    domain::PartShape s;
    s.outer = geometry::rectangleContour(1200, 600);
    s.cutouts = {geometry::translated(geometry::rectangleContour(500, 400), {350, 100}),
                 geometry::circleContour({1000, 300}, 17.5)};
    s.outerEdges = {kBand, std::nullopt, std::nullopt, std::nullopt};
    p.shape = s;
    return p;
}

domain::Model withParts(std::vector<domain::Part> parts) {
    domain::Model m = model();
    m.assemblies.front().parts = std::move(parts);
    return m;
}

// Hand-computed values.
// Rounded: area 600*400 - (4 - pi)*50^2 = 240000 - 2146.018 = 237853.982 mm2;
//          perimeter 2*(500 + 300) + 2*pi*50 = 1914.159 mm; blank (596 x 396) = 236016 mm2;
//          raw outline: straights 500 / 300 + R 48 arcs: 2*(500 + 300) + 2*pi*48 = 1901.593 mm.
const double kRoundArea = 240000 - (4 - kPi) * 2500;
const double kRoundPerimeter = 1600 + 100 * kPi;
// Arc top: theta = 4 atan(0.25); arc length 850 * theta = 832.927 mm;
//          segment area 850^2 / 2 (theta - sin theta) = 53994.168 mm2 -> 533994.168 mm2.
const double kTheta = 4 * std::atan(0.25);
const double kArcLength = 850 * kTheta;
const double kArcArea = 480000 + 850.0 * 850.0 / 2 * (kTheta - std::sin(kTheta));
// Worktop: 1200*600 - 500*400 - pi*17.5^2 = 519037.887 mm2.
const double kWorktopArea = 720000 - 200000 - kPi * 17.5 * 17.5;
} // namespace

TEST(ShapedProduction, PartListAreasAndCutSize) {
    const auto m = withParts({rounded("r1"), arcTop(), worktop()});
    const PartList pl = partList(m);
    ASSERT_EQ(pl.rows.size(), 3u);
    EXPECT_TRUE(pl.diagnostics.empty());
    const PartRow& r = pl.rows[0];
    EXPECT_TRUE(r.shaped);
    EXPECT_NEAR(r.outlineAreaM2, kRoundArea / 1e6, 1e-12);
    EXPECT_NEAR(r.finishedAreaM2(), kRoundArea / 1e6, 1e-12);
    EXPECT_NEAR(r.cutLength, 596, 1e-9); // blank: 600 - 2*2
    EXPECT_NEAR(r.cutWidth, 396, 1e-9);
    EXPECT_NEAR(r.cutAreaM2(), 0.236016, 1e-12);
    EXPECT_EQ(r.outlineSegments, 8);
    ASSERT_EQ(r.outlineEdges.size(), 8u);
    EXPECT_EQ(r.outlineEdges[3].thickness, 2);
    for (const auto& e : r.edges) EXPECT_FALSE(e.banded());

    const PartRow& a = pl.rows[1];
    EXPECT_NEAR(a.outlineAreaM2 * 1e6, kArcArea, 1e-6);
    EXPECT_NEAR(a.cutLength, 800, 1e-9); // sides raw
    EXPECT_NEAR(a.cutWidth, 698, 1e-9);  // arc R 850 -> R 848 about (400, -150): top at 698

    const PartRow& w = pl.rows[2];
    EXPECT_NEAR(w.outlineAreaM2 * 1e6, kWorktopArea, 1e-6);
    EXPECT_EQ(w.cutoutCount, 2);
    EXPECT_NEAR(w.cutWidth, 598, 1e-9);

    const Table t = partListTable(pl);
    ASSERT_EQ(t.header.size(), 20u); // 17 + Shape, Outline area, Outline edges
    EXPECT_EQ(t.header[17], "Shape");
    EXPECT_EQ(t.rows[0][17], "shaped");
    EXPECT_EQ(t.rows[0][18], "0.2379"); // 0.237854 m2, 4 decimals
    EXPECT_EQ(t.rows[0][19], "11111111");
    EXPECT_EQ(t.rows[1][19], "0010");
    EXPECT_EQ(t.rows[2][18], "0.519");
    EXPECT_EQ(t.rows[2][9], "1200");  // cut length
    EXPECT_EQ(t.rows[2][10], "598");  // cut width
}

TEST(ShapedProduction, RectangularTablesHaveNoShapeColumns) {
    const auto m = withParts({panel("p", "Plain", 600, 400, 18, "m1")});
    EXPECT_EQ(partListTable(partList(m)).header.size(), 17u);
    EXPECT_EQ(cutListTable(cutList(m)).header.size(), 17u);
    EXPECT_EQ(machiningTable(machiningSummary(m)).header.size(), 9u);
}

TEST(ShapedProduction, MaterialReportUsesOutlineAreaAndBlank) {
    const auto m = withParts({rounded("r1"), arcTop(), worktop()});
    const MaterialReport mr = materialReport(m);
    ASSERT_EQ(mr.rows.size(), 1u);
    // Finished: exact outlines. Cut: blanks 596*396 + 800*698 + 1200*598 = 236016 + 558400 + 717600.
    EXPECT_NEAR(mr.rows[0].finishedAreaM2, (kRoundArea + kArcArea + kWorktopArea) / 1e6, 1e-9);
    EXPECT_NEAR(mr.rows[0].cutAreaM2, (236016.0 + 558400.0 + 717600.0) / 1e6, 1e-12);
    const ProjectSummary ps = projectSummary(m);
    EXPECT_NEAR(ps.finishedAreaM2, mr.rows[0].finishedAreaM2, 1e-12);
    EXPECT_NEAR(ps.assemblies[0].finishedAreaM2, mr.rows[0].finishedAreaM2, 1e-12);
    EXPECT_EQ(ps.edgeBandCount, 1);
}

TEST(ShapedProduction, EdgeBandingExactLengthsAndStrips) {
    const auto m = withParts({rounded("r1"), arcTop(), worktop()});
    const EdgeBandingReport er = edgeBandingReport(m);
    ASSERT_EQ(er.rows.size(), 1u);
    const EdgeBandingRow& row = er.rows[0];
    // Rounded: one closed tangent strip of 1914.159 mm; arc top: one strip 832.927 mm;
    // worktop front: one 1200 mm strip. 3 strips -> overhang 3 * 2 * 25 = 150 mm.
    EXPECT_EQ(row.edgeCount, 3);
    EXPECT_NEAR(row.appliedLengthM, (kRoundPerimeter + kArcLength + 1200) / 1000, 1e-12);
    EXPECT_NEAR(row.overhangAllowanceM, 0.15, 1e-12);
    EXPECT_NEAR(row.purchaseLengthM, (kRoundPerimeter + kArcLength + 1200 + 150) / 1000, 1e-12);
    EXPECT_NEAR(row.cost, row.purchaseLengthM * 1.0, 1e-12);
    EXPECT_NEAR(projectSummary(m).edgeBandingAppliedM, row.appliedLengthM, 1e-12);
}

TEST(ShapedProduction, StripsBreakAtSharpCornersAndBandChanges) {
    // Worktop with all 4 straight segments banded: 4 sharp corners -> 4 strips, 2*(1200+600).
    domain::Part w = worktop();
    w.shape->outerEdges.assign(4, kBand);
    auto er = edgeBandingReport(withParts({w}));
    EXPECT_EQ(er.rows[0].edgeCount, 4);
    EXPECT_NEAR(er.rows[0].appliedLengthM, 3.6, 1e-12);
    // Rounded part banded only on the bottom line + both bottom arcs (segments 7, 0, 1):
    // one tangent strip of 500 + 2 * (pi/2 * 50) = 657.080 mm.
    domain::Part r = rounded("r1");
    r.shape->outerEdges.assign(8, std::nullopt);
    r.shape->outerEdges[7] = r.shape->outerEdges[0] = r.shape->outerEdges[1] = kBand;
    er = edgeBandingReport(withParts({r}));
    EXPECT_EQ(er.rows[0].edgeCount, 1);
    EXPECT_NEAR(er.rows[0].appliedLengthM, (500 + 50 * kPi) / 1000, 1e-12);
}

TEST(ShapedProduction, CutListKeepsDifferentShapesApart) {
    domain::Part plain = panel("plain", "Plain", 600, 400, 18, "m1");
    const auto m = withParts({rounded("r1", 50, false), rounded("r2", 50, false), rounded("r3", 40, false), plain});
    const CutList cl = cutList(m);
    // r1 + r2 identical -> one row of 2; r3 (R 40) and the plain rectangle stay separate,
    // although all four have the same 600 x 400 finished and cut size.
    ASSERT_EQ(cl.rows.size(), 3u);
    int two = 0;
    for (const auto& g : cl.rows) {
        if (g.quantity == 2) {
            ++two;
            EXPECT_TRUE(g.shaped);
            EXPECT_NEAR(g.outlineAreaM2, kRoundArea / 1e6, 1e-12);
        }
        EXPECT_EQ(g.shaped, !g.shapeSignature.empty());
    }
    EXPECT_EQ(two, 1);
    EXPECT_EQ(shapeSignature(plain), "");
    EXPECT_NE(shapeSignature(rounded("a")), shapeSignature(rounded("a", 50, false))); // bands differ
    const Table t = cutListTable(cl);
    ASSERT_EQ(t.header.size(), 20u);
    EXPECT_EQ(t.header[18], "Outline area (m2)");
}

TEST(ShapedProduction, LabelsMarkShapedParts) {
    const auto m = withParts({rounded("r1"), panel("p", "Plain", 500, 300, 18, "m1")});
    const LabelSet ls = labels(m);
    ASSERT_EQ(ls.labels.size(), 2u);
    EXPECT_TRUE(ls.labels[0].shaped);
    EXPECT_EQ(ls.labels[0].outlineEdgeCode, "11111111");
    EXPECT_EQ(ls.labels[0].edgeCode, "0000");
    EXPECT_FALSE(ls.labels[1].shaped);
    const Table t = labelTable(ls);
    EXPECT_EQ(t.rows[0][11], "shaped 11111111");
    EXPECT_EQ(t.rows[1][11], "0000");
    const auto svg = renderLabelSheetsSvg(ls.labels);
    ASSERT_TRUE(svg);
    EXPECT_NE(svg.value()[0].find(">Shaped 11111111<"), std::string::npos);
}

TEST(ShapedProduction, MachiningSummaryRoutingLength) {
    const auto m = withParts({rounded("r1"), arcTop(), worktop(), panel("p", "Plain", 500, 300, 18, "m1")});
    const MachiningSummary ms = machiningSummary(m);
    // Routed parts are listed without operations; the plain panel is not.
    ASSERT_EQ(ms.rows.size(), 3u);
    EXPECT_EQ(ms.rows[0].routedContours, 1);
    // Raw outline: straights keep their length (500, 300), arcs keep their centre: R 48.
    EXPECT_NEAR(ms.rows[0].routingLength, 2 * (500 + 300) + 2 * kPi * 48, 1e-6);
    // Arc top raw outline: arc R 848 about (400, -150) meets x = 0 / 800 at
    // y = -150 + sqrt(848^2 - 400^2) = 597.7326; arc length 848 * 2 asin(400/848).
    const double side = -150 + std::sqrt(848.0 * 848.0 - 400.0 * 400.0);
    const double arc = 848 * 2 * std::asin(400.0 / 848.0);
    EXPECT_NEAR(ms.rows[1].routingLength, 800 + 2 * side + arc, 1e-6);
    // Worktop: raw outline 1200 x 598 (front band 2 mm) + sink 2*(500+400) + tap 2*pi*17.5.
    EXPECT_EQ(ms.rows[2].routedContours, 3);
    EXPECT_NEAR(ms.rows[2].routingLength, 3596 + 1800 + 35 * kPi, 1e-6);
    EXPECT_EQ(ms.totalRoutedContours, 5);
    EXPECT_NEAR(ms.totalRoutingLength,
                ms.rows[0].routingLength + ms.rows[1].routingLength + ms.rows[2].routingLength, 1e-9);
    const Table t = machiningTable(ms);
    ASSERT_EQ(t.header.size(), 11u);
    EXPECT_EQ(t.rows[2][9], "3");
    EXPECT_EQ(t.rows[2][10], "5506"); // 5505.956 at 1 decimal? see NumberFormat
}

TEST(ShapedProduction, InvalidShapeIsFlaggedNotFatal) {
    domain::Part p = rounded("bad", 50, false);
    p.shape->outer.vertices = {{{0, 0}, 0}, {{600, 400}, 0}, {{600, 0}, 0}, {{0, 400}, 0}}; // bow tie
    domain::Part q = rounded("missing");
    q.shape->outerEdges[2] = domain::EdgeBandId("nope");
    const auto m = withParts({p, q});
    const PartList pl = partList(m);
    EXPECT_EQ(pl.rows[0].outlineAreaM2, 0);
    int invalid = 0, missing = 0;
    for (const auto& d : pl.diagnostics) {
        invalid += d.code == "production.shape.invalid";
        missing += d.code == "production.edgeband.missing";
    }
    EXPECT_EQ(invalid, 1);
    EXPECT_EQ(missing, 1);
    EXPECT_EQ(labelTable(labels(m)).rows[1][11], "shaped 11?11111");
}
