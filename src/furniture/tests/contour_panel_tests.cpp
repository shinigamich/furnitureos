// SPDX-License-Identifier: AGPL-3.0-only
// Parametric contours of free panels: arc edges, custom (L-shaped) outlines, cut-outs.
#include "fos/furniture/contour.hpp"
#include "fos/furniture/panel.hpp"
#include "test_support.hpp"

#include <cmath>

namespace fos::furniture::test {
using ::testing::HasSubstr;

namespace {
constexpr double kPi = 3.14159265358979323846;

PanelDefinition panel(double length, double width) {
    PanelDefinition p = makePanel(domain::PartId("p/panel"), "Panel", domain::MaterialId("mat.pb18"));
    EXPECT_TRUE(p.parameters.setValue(panel_param::kLength, length).ok());
    EXPECT_TRUE(p.parameters.setValue(panel_param::kWidth, width).ok());
    return p;
}

ContourSpec arcEdge(domain::EdgeSide edge, std::string rise, ArcDirection dir) {
    ContourSpec s;
    s.kind = ContourKind::ArcEdge;
    s.arcEdge = edge;
    s.arcRise = std::move(rise);
    s.arcDirection = dir;
    return s;
}

domain::Part generated(const PanelDefinition& p) {
    auto r = generatePanel(p, libs());
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.ok() ? r.value().part : domain::Part{};
}

std::string failure(const PanelDefinition& p) {
    auto r = generatePanel(p, libs());
    EXPECT_FALSE(r.ok());
    return r.ok() ? std::string() : r.error().message;
}

/// Area of the circular segment of a chord c with sagitta s: R = (c^2 / 4 + s^2) / (2 s),
/// theta = 2 asin(c / (2 R)), area = R^2 / 2 (theta - sin theta).
double segmentArea(double c, double s) {
    const double R = (c * c / 4 + s * s) / (2 * s);
    const double theta = 2 * std::asin(c / (2 * R));
    return R * R / 2 * (theta - std::sin(theta));
}

std::optional<domain::EdgeBandId> abs2() { return domain::EdgeBandId("eb.abs2"); }
} // namespace

TEST(ContourPanel, ConvexArcEdgeIsCutIntoTheBox) {
    // 800 x 720 panel, edge L2 (y = 720, chord 800) convex with rise 50: the ends move down to
    // y = 670, the apex stays at (400, 720). Bulge = sagitta / half chord = 50 / 400 = 0.125;
    // radius = (400^2 + 50^2) / (2 * 50) = 162500 / 100 = 1625, centre (400, 720 - 1625).
    PanelDefinition p = panel(800, 720);
    p.edges[static_cast<std::size_t>(domain::EdgeSide::L2)] = abs2();
    p.contour = arcEdge(domain::EdgeSide::L2, "50", ArcDirection::Convex);
    const domain::Part part = generated(p);
    expectSize(part, 800, 720, 18);
    ASSERT_TRUE(part.shape.has_value());
    const auto& c = part.shape->outer;
    ASSERT_EQ(c.size(), 4u);
    EXPECT_TRUE(geometry::approxEqual(c.vertices[0].point, {0, 0}));
    EXPECT_TRUE(geometry::approxEqual(c.vertices[1].point, {800, 0}));
    EXPECT_TRUE(geometry::approxEqual(c.vertices[2].point, {800, 670}));
    EXPECT_TRUE(geometry::approxEqual(c.vertices[3].point, {0, 670}));
    EXPECT_DOUBLE_EQ(c.vertices[2].bulge, 0.125);
    const auto arc = geometry::segmentInfo(c, 2);
    EXPECT_NEAR(arc.radius, 1625, 1e-9);
    EXPECT_TRUE(geometry::approxEqual(arc.center, {400, -905}, 1e-9));
    const geometry::Rect2 b = geometry::bounds(c);
    EXPECT_NEAR(b.max.y, 720, 1e-9);
    EXPECT_NEAR(b.max.x, 800, 1e-9);
    // Area = 800 * 670 + segment(800, 50).
    EXPECT_NEAR(geometry::signedArea(c), 800.0 * 670.0 + segmentArea(800, 50), 1e-6);
    // The arc takes the band of the edge it replaces.
    const std::vector<std::optional<domain::EdgeBandId>> bands{{}, {}, abs2(), {}};
    EXPECT_EQ(part.shape->outerEdges, bands);
}

TEST(ContourPanel, ConcaveArcEdgeRunsBetweenTheCorners) {
    PanelDefinition p = panel(800, 720);
    p.contour = arcEdge(domain::EdgeSide::L2, "50", ArcDirection::Concave);
    const domain::Part part = generated(p);
    ASSERT_TRUE(part.shape.has_value());
    const auto& c = part.shape->outer;
    EXPECT_TRUE(geometry::approxEqual(c.vertices[2].point, {800, 720}));
    EXPECT_DOUBLE_EQ(c.vertices[2].bulge, -0.125);
    // Apex at (400, 670): centre (400, 670 + 1625).
    EXPECT_TRUE(geometry::approxEqual(geometry::segmentInfo(c, 2).center, {400, 2295}, 1e-9));
    EXPECT_NEAR(geometry::signedArea(c), 800.0 * 720.0 - segmentArea(800, 50), 1e-6);
    EXPECT_TRUE(part.shape->outerEdges.empty()); // all raw
}

TEST(ContourPanel, ArcRiseOutOfRange) {
    PanelDefinition p = panel(800, 720);
    p.contour = arcEdge(domain::EdgeSide::L2, "401", ArcDirection::Convex);
    EXPECT_THAT(failure(p), HasSubstr("arc rise of edge L2 is 401 mm, more than half the 800 mm edge"));
    // W1 has a chord of 720 (half 360) but the panel is only 300 mm across it when 300 long.
    PanelDefinition q = panel(300, 720);
    q.contour = arcEdge(domain::EdgeSide::W1, "320", ArcDirection::Concave);
    EXPECT_THAT(failure(q), HasSubstr("arc rise of edge W1 is 320 mm but the part is only 300 mm across that edge"));
    p.contour = arcEdge(domain::EdgeSide::L2, "length - 800", ArcDirection::Convex);
    EXPECT_THAT(failure(p), HasSubstr("it must be greater than 0"));
}

TEST(ContourPanel, LShapedCustomTop) {
    // 1200 x 600 top with a 600 x 300 notch at the rear left (x 0..600, y 300..600):
    // (0,0), (L,0), (L,W), (L-600,W), (L-600,300), (0,300). It still spans the whole box.
    // Area = 1200 * 600 - 600 * 300 = 540000.
    PanelDefinition p = panel(1200, 600);
    p.edges[static_cast<std::size_t>(domain::EdgeSide::L1)] = abs2();
    p.edges[static_cast<std::size_t>(domain::EdgeSide::L2)] = abs2();
    ContourSpec s;
    s.kind = ContourKind::Custom;
    s.vertices = {{"0", "0", 0.0, {}},
                  {"contour.length", "0", 0.0, {}},
                  {"contour.length", "contour.width", 0.0, {}},
                  {"contour.length - 600", "contour.width", 0.0, {}},
                  {"contour.length - 600", "300", 0.0, domain::EdgeSide::L2},
                  {"0", "300", 0.0, {}}};
    p.contour = s;
    const domain::Part part = generated(p);
    ASSERT_TRUE(part.shape.has_value());
    EXPECT_NEAR(geometry::signedArea(part.shape->outer), 540000, 1e-6);
    EXPECT_TRUE(geometry::approxEqual(part.shape->outer.vertices[3].point, {600, 600}));
    // Bands: segment 0 on y = 0 -> L1; 2 on y = W -> L2; 4 hinted from L2; 1, 3, 5 raw
    // (segment 3 is the notch's inner side, segment 5 lies on x = 0 = W1, raw).
    const std::vector<std::optional<domain::EdgeBandId>> bands{abs2(), {}, abs2(), {}, abs2(), {}};
    EXPECT_EQ(part.shape->outerEdges, bands);
    for (const auto& e : part.edges) EXPECT_FALSE(e.has_value());
    // Follows the panel length.
    ASSERT_TRUE(p.parameters.setValue(panel_param::kLength, 1500).ok());
    EXPECT_NEAR(geometry::signedArea(generated(p).shape->outer), 1500.0 * 300 + 600.0 * 300, 1e-6);
}

TEST(ContourPanel, CustomOutlineErrors) {
    PanelDefinition p = panel(1200, 600);
    ContourSpec s;
    s.kind = ContourKind::Custom;
    // Clockwise rectangle.
    s.vertices = {{"0", "0", 0.0, {}}, {"0", "600", 0.0, {}}, {"1200", "600", 0.0, {}}, {"1200", "0", 0.0, {}}};
    p.contour = s;
    EXPECT_THAT(failure(p), HasSubstr("runs clockwise"));
    // Does not span the box.
    s.vertices = {{"0", "0", 0.0, {}}, {"1000", "0", 0.0, {}}, {"1000", "600", 0.0, {}}, {"0", "600", 0.0, {}}};
    p.contour = s;
    EXPECT_THAT(failure(p), HasSubstr("spans (0, 0) .. (1000, 600) but must span exactly (0, 0) .. (1200, 600)"));
    // Self-intersecting bow tie.
    s.vertices = {{"0", "0", 0.0, {}}, {"1200", "600", 0.0, {}}, {"1200", "0", 0.0, {}}, {"0", "600", 0.0, {}}};
    p.contour = s;
    EXPECT_THAT(failure(p), HasSubstr("not a valid contour"));
    // Too few vertices; unknown name.
    s.vertices = {{"0", "0", 0.0, {}}, {"1200", "0", 0.0, {}}};
    p.contour = s;
    EXPECT_THAT(failure(p), HasSubstr("at least 3 vertices"));
    s.vertices = {{"0", "0", 0.0, {}}, {"nope", "0", 0.0, {}}, {"1200", "600", 0.0, {}}};
    p.contour = s;
    EXPECT_THAT(failure(p), HasSubstr("panel 'p/panel': vertex 2 x expression 'nope' cannot be evaluated"));
}

TEST(ContourPanel, WorktopWithSinkAndTapHole) {
    // 1200 x 600 worktop, sink 500 x 400 at (350, 100) with R 10 corners, tap hole d 35 at
    // (600, 540). Material area = 720000 - (200000 - 4 * (100 - 25 pi)) - pi 17.5^2.
    PanelDefinition p = panel(1200, 600);
    p.role = domain::PartRole::Worktop;
    p.edges[static_cast<std::size_t>(domain::EdgeSide::L1)] = abs2();
    ASSERT_TRUE(p.parameters.defineConstant("sink_x", 350, parametric::ParameterKind::Length).ok());
    ContourSpec s; // Rectangle kind: the outline stays the rectangle
    s.cutouts = {{CutoutKind::Rectangle, "sink_x", "100", "500", "400", "10"},
                 {CutoutKind::Circle, "600", "contour.width - 60", "", "", "17.5"}};
    p.contour = s;
    const domain::Part part = generated(p);
    ASSERT_TRUE(part.shape.has_value());
    ASSERT_EQ(part.shape->cutouts.size(), 2u);
    EXPECT_FALSE(domain::isRectangularOutline(part));
    EXPECT_NEAR(geometry::signedArea(part.shape->outer), 720000, 1e-6);
    const double sink = 500.0 * 400.0 - 4 * (100.0 - kPi * 100.0 / 4.0);
    EXPECT_NEAR(geometry::signedArea(part.shape->cutouts[0]), sink, 1e-6);
    EXPECT_NEAR(geometry::signedArea(part.shape->cutouts[1]), kPi * 17.5 * 17.5, 1e-6);
    const geometry::Rect2 sb = geometry::bounds(part.shape->cutouts[0]);
    EXPECT_TRUE(geometry::approxEqual(sb.min, {350, 100}) && geometry::approxEqual(sb.max, {850, 500}));
    // Rectangle outline bands L1, W2, L2, W1.
    const std::vector<std::optional<domain::EdgeBandId>> bands{abs2(), {}, {}, {}};
    EXPECT_EQ(part.shape->outerEdges, bands);
}

TEST(ContourPanel, CutoutErrors) {
    PanelDefinition p = panel(1200, 600);
    ContourSpec s;
    s.cutouts = {{CutoutKind::Rectangle, "900", "100", "400", "400", ""}}; // reaches x = 1300
    p.contour = s;
    EXPECT_THAT(failure(p), HasSubstr("cut-out 1 (Rectangle) at (900, 100) .. (1300, 500) is not inside the part's outline"));
    s.cutouts = {{CutoutKind::Circle, "100", "100", "", "", "50"}, {CutoutKind::Circle, "180", "100", "", "", "50"}};
    p.contour = s;
    EXPECT_THAT(failure(p), HasSubstr("cut-out 2 (Circle) at (130, 50) .. (230, 150) overlaps or touches cut-out 1"));
    s.cutouts = {{CutoutKind::Rectangle, "0", "100", "400", "400", ""}}; // touches the W1 edge
    p.contour = s;
    EXPECT_THAT(failure(p), HasSubstr("not inside"));
    s.cutouts = {{CutoutKind::Rectangle, "100", "100", "100", "50", "30"}};
    p.contour = s;
    EXPECT_THAT(failure(p), HasSubstr("corner radius is 30 mm; it must be between 0 and half the smaller side (25 mm)"));
    // A cut-out inside a rounded corner's removed area is outside the outline.
    ContourSpec r;
    r.kind = ContourKind::RoundedCorners;
    r.radii = {"200", "", "", ""};
    r.cutouts = {{CutoutKind::Circle, "20", "20", "", "", "10"}};
    p.contour = r;
    EXPECT_THAT(failure(p), HasSubstr("is not inside the part's outline"));
}

TEST(ContourPanel, MachiningCheckOfAShapedPart) {
    // Direct check: holes in a rounded (R 100 at (0,0)) 600 x 400 part with a circular cut-out.
    domain::Part part;
    part.id = domain::PartId("x");
    part.name = "X";
    part.length = 600;
    part.width = 400;
    part.thickness = 18;
    part.shape = domain::PartShape{};
    const double radii[4] = {100, 0, 0, 0};
    part.shape->outer = geometry::roundedRectangleContour(600, 400, radii).value();
    part.shape->cutouts = {geometry::circleContour({300, 200}, 50)};
    const auto hole = [](domain::Face f, double u, double v, double d, double depth) {
        domain::Operation op;
        op.id = domain::OperationId("op");
        op.shape = domain::Hole{f, u, v, d, depth, false};
        return op;
    };
    // (20, 20): distance from (100, 100) = 113 > 100 -> outside. (50, 50): 70.7 + 2.5 < 100 -> inside.
    // (300, 240) d 10 lies in the cut-out; (300, 255) d 10 touches its rim exactly (allowed).
    // L1 edge hole at u = 50 (x 46..54) bores from y = 0, where the arc has removed material.
    // W2 edge hole at u = 200, depth 30: x 570..600, y 196..204 -> inside.
    part.operations = {hole(domain::Face::A, 20, 20, 5, 10), hole(domain::Face::A, 50, 50, 5, 10),
                       hole(domain::Face::B, 300, 240, 10, 10), hole(domain::Face::B, 300, 255, 10, 10),
                       hole(domain::Face::L1, 50, 9, 8, 30), hole(domain::Face::W2, 200, 9, 8, 30)};
    const auto found = checkOperationsInsideOutline(part);
    ASSERT_EQ(found.size(), 3u);
    EXPECT_THAT(found[0].message, HasSubstr("Hole d 5 at (20, 20) on face A"));
    EXPECT_THAT(found[0].message, HasSubstr("leaves the contoured outline"));
    EXPECT_THAT(found[1].message, HasSubstr("at (300, 240) on face B"));
    EXPECT_THAT(found[1].message, HasSubstr("enters a cut-out"));
    EXPECT_THAT(found[2].message, HasSubstr("on face L1"));
    // Parts without a shape are never reported.
    part.shape.reset();
    EXPECT_TRUE(checkOperationsInsideOutline(part).empty());
}

} // namespace fos::furniture::test
