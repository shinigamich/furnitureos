// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/contour.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace fos;
using namespace fos::geometry;

namespace {
constexpr double kPi = core::kPi;

Contour roundedShelf(double r = 50) {
    const double radii[4] = {r, r, r, r};
    return roundedRectangleContour(600, 400, radii).value();
}

void expectPoint(Vec2 p, double x, double y, double tol = 1e-9) {
    EXPECT_NEAR(p.x, x, tol);
    EXPECT_NEAR(p.y, y, tol);
}
} // namespace

TEST(ContourOffset, RoundedRectangleUniformInwards) {
    const Contour c = roundedShelf();
    const auto r = offsetContour(c, -2.0);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Contour& o = r.value();
    ASSERT_EQ(o.size(), 8u);
    for (std::size_t i = 0; i < 8; ++i) {
        const ContourSegmentInfo a = segmentInfo(c, i), b = segmentInfo(o, i);
        ASSERT_EQ(a.isArc, b.isArc) << i;
        if (b.isArc) {
            EXPECT_NEAR(b.radius, 48, 1e-9) << i; // convex CCW arc, inwards: 50 - 2
            expectPoint(b.center, a.center.x, a.center.y);
            EXPECT_NEAR(b.sweep, 90, 1e-9);
        } else {
            EXPECT_NEAR(b.length, a.length, 1e-9) << i; // straights keep their length (500 / 300)
        }
    }
    expectPoint(o.vertices[0].point, 50, 2); // bottom line now at y = 2 from x = 50 to 550
    const Rect2 b = bounds(o);
    expectPoint(b.min, 2, 2);
    expectPoint(b.max, 598, 398);
    // Area = 596 x 396 minus 4 corners of (48^2 - pi 48^2 / 4):
    // 236016 - (4 - pi) * 2304 = 236016 - 1977.77 = 234038.23.
    EXPECT_NEAR(signedArea(o), 596.0 * 396.0 - (4.0 - kPi) * 2304.0, 1e-6);
    // Perimeter: 2 (500 + 300) + 2 pi 48.
    EXPECT_NEAR(perimeter(o), 1600 + 96 * kPi, 1e-9);
}

TEST(ContourOffset, RoundedRectangleUniformOutwardsAndClockwise) {
    const auto out = offsetContour(roundedShelf(), 3.0);
    ASSERT_TRUE(out.ok()) << out.error().message;
    EXPECT_NEAR(segmentInfo(out.value(), 1).radius, 53, 1e-9);
    expectPoint(bounds(out.value()).min, -3, -3);
    // A clockwise contour: "outwards" is still away from the enclosed area.
    const auto cw = offsetContour(reversed(roundedShelf()), -2.0);
    ASSERT_TRUE(cw.ok()) << cw.error().message;
    EXPECT_NEAR(signedArea(cw.value()), -(596.0 * 396.0 - (4.0 - kPi) * 2304.0), 1e-6);
    expectPoint(bounds(cw.value()).max, 598, 398);
}

TEST(ContourOffset, RectanglePerEdgeEqualsAdr0004) {
    // Segments 0..3 of rectangleContour are L1 (bottom), W2 (right), L2 (top), W1 (left).
    // L1 2 mm, W1 and W2 0.4 mm: raw rectangle [0.4, 719.6] x [2, 560].
    const auto r = offsetContour(rectangleContour(720, 560), std::vector<double>{-2, -0.4, 0, -0.4});
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Contour& o = r.value();
    ASSERT_EQ(o.size(), 4u);
    expectPoint(o.vertices[0].point, 0.4, 2, 1e-12);
    expectPoint(o.vertices[1].point, 719.6, 2, 1e-12);
    expectPoint(o.vertices[2].point, 719.6, 560, 1e-12);
    expectPoint(o.vertices[3].point, 0.4, 560, 1e-12);
}

TEST(ContourOffset, SingleBandedStraightOfRoundedRectangle) {
    // Only segment 0 (bottom straight, x 50..550) gets a 2 mm band: it moves in to y = 2. The
    // neighbouring corner arcs are tangent to it and keep r 50, so the offset line no longer meets
    // them: by the tangent-junction rule each segment keeps its own offset over its own extent and
    // a 2 mm step along the common normal joins them: (50,0) -> (50,2) and (550,2) -> (550,0).
    // (Trimming the arcs at y = 2 instead would cut a sliver out of the unbanded corners that the
    // straight band cannot fill.)
    std::vector<double> d(8, 0.0);
    d[0] = -2.0;
    const Contour c = roundedShelf();
    const auto r = offsetContour(c, d);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Contour& o = r.value();
    ASSERT_EQ(o.size(), 10u);
    expectPoint(o.vertices[0].point, 50, 2);
    expectPoint(o.vertices[1].point, 550, 2);  // step down ...
    expectPoint(o.vertices[2].point, 550, 0);  // ... to the untouched corner arc
    EXPECT_EQ(o.vertices[1].bulge, 0.0);
    EXPECT_NEAR(segmentInfo(o, 2).radius, 50, 1e-9);
    expectPoint(o.vertices[9].point, 50, 0);   // last: step (50,0) -> (50,2)
    // Area = finished area - 500 x 2; bounds unchanged (the corners still reach y = 0).
    EXPECT_NEAR(signedArea(o), signedArea(c) - 1000.0, 1e-6);
    EXPECT_NEAR(perimeter(o), perimeter(c) + 4.0, 1e-9);
    EXPECT_EQ(bounds(o), bounds(c));
}

TEST(ContourOffset, ArcTopPanelOffsetOnArcOnly) {
    // 800 wide panel, straight sides 300 high, top arc with a 100 mm rise over the 800 mm chord.
    // Sagitta s = |b| c / 2  ->  b = 2 s / c = 0.25 (theta = 4 atan 0.25 = 56.145 deg).
    // r = (c^2/4 + s^2) / (2 s) = (160000 + 10000) / 200 = 850, centre (400, 400 - 850) = (400, -450).
    const Contour c{{{{0, 0}, 0.0}, {{800, 0}, 0.0}, {{800, 300}, 0.25}, {{0, 300}, 0.0}}};
    const ContourSegmentInfo top = segmentInfo(c, 2);
    EXPECT_NEAR(top.radius, 850, 1e-9);
    expectPoint(top.center, 400, -450);
    EXPECT_NEAR(bounds(c).max.y, 400, 1e-9);
    // A 0.4 mm band on the arc only: r' = 849.6 (convex arc, inwards). The vertical sides x = 0 and
    // x = 800 meet the smaller circle at y = -450 + sqrt(849.6^2 - 400^2)
    // = -450 + sqrt(721820.16 - 160000) = -450 + sqrt(561820.16) = -450 + 749.54664 = 299.54664.
    const auto r = offsetContour(c, std::vector<double>{0, 0, -0.4, 0});
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Contour& o = r.value();
    ASSERT_EQ(o.size(), 4u);
    const double yj = -450 + std::sqrt(849.6 * 849.6 - 400.0 * 400.0);
    EXPECT_NEAR(yj, 299.54664, 1e-5);
    expectPoint(o.vertices[0].point, 0, 0);
    expectPoint(o.vertices[1].point, 800, 0);
    expectPoint(o.vertices[2].point, 800, yj);
    expectPoint(o.vertices[3].point, 0, yj);
    const ContourSegmentInfo t2 = segmentInfo(o, 2);
    EXPECT_NEAR(t2.radius, 849.6, 1e-9);
    expectPoint(t2.center, 400, -450, 1e-8);
    EXPECT_NEAR(bounds(o).max.y, 399.6, 1e-9); // apex 0.4 lower
    // Area = 800 yj + circular segment r'^2/2 (th - sin th), th = 2 asin(400 / 849.6).
    const double th = 2 * std::asin(400 / 849.6);
    EXPECT_NEAR(signedArea(o), 800 * yj + 0.5 * 849.6 * 849.6 * (th - std::sin(th)), 1e-6);
}

TEST(ContourOffset, CircleStaysConcentric) {
    const auto in = offsetContour(circleContour({5, 5}, 100), -10.0);
    ASSERT_TRUE(in.ok()) << in.error().message;
    EXPECT_NEAR(signedArea(in.value()), kPi * 8100, 1e-6);
    const auto out = offsetContour(circleContour({5, 5}, 100), 10.0);
    ASSERT_TRUE(out.ok());
    EXPECT_NEAR(segmentInfo(out.value(), 0).radius, 110, 1e-9);
    EXPECT_FALSE(offsetContour(circleContour({0, 0}, 10), -10.0).ok()); // radius would be 0
}

TEST(ContourOffset, ConcaveArcOutwardMiterAndRoundJoin) {
    // 100 x 100 square whose right side is a concave (CW) arc, bulge -0.5:
    // r = c (1 + b^2) / (4 |b|) = 100 * 1.25 / 2 = 62.5, centre (100 + 37.5, 50) = (137.5, 50).
    const Contour c{{{{0, 0}, 0.0}, {{100, 0}, -0.5}, {{100, 100}, 0.0}, {{0, 100}, 0.0}}};
    ASSERT_TRUE(validateContour(c).ok());
    expectPoint(segmentInfo(c, 1).center, 137.5, 50);
    // Outwards by d: bottom line y = -d, the concave arc shrinks to r = 62.5 - d. They still meet
    // while 50 + d <= 62.5 - d, i.e. d <= 6.25. d = 5: (x - 137.5)^2 = 57.5^2 - 55^2 = 281.25,
    // the solution nearest to the corner (100,0) is x = 137.5 - sqrt(281.25) = 120.7295.
    auto r = offsetContour(c, 5.0);
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.value().size(), 4u);
    expectPoint(r.value().vertices[1].point, 137.5 - std::sqrt(281.25), -5);
    EXPECT_NEAR(segmentInfo(r.value(), 1).radius, 57.5, 1e-9);
    // d = 10: no intersection -> round join of radius 10 about (100,0) from (100,-10) to
    // V + 10 * outward normal of the arc = (100,0) + 10 * (0.6, 0.8) = (106, 8). The turning angle
    // is atan2(0.6, -0.8) = 143.13 deg, bulge tan(143.13 / 4 deg).
    r = offsetContour(c, 10.0);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Contour& o = r.value();
    ASSERT_EQ(o.size(), 6u);
    expectPoint(o.vertices[1].point, 100, -10);
    EXPECT_NEAR(o.vertices[1].bulge, std::tan(std::atan2(0.6, -0.8) / 4), 1e-12);
    const ContourSegmentInfo join = segmentInfo(o, 1);
    expectPoint(join.center, 100, 0);
    EXPECT_NEAR(join.radius, 10, 1e-9);
    expectPoint(o.vertices[2].point, 106, 8);
    EXPECT_NEAR(segmentInfo(o, 2).radius, 52.5, 1e-9);
}

TEST(ContourOffset, Errors) {
    // A 1.5 mm corner radius cannot take a 2 mm inward offset.
    const auto small = offsetContour(roundedShelf(1.5), -2.0);
    ASSERT_FALSE(small.ok());
    EXPECT_NE(small.error().message.find("radius"), std::string::npos) << small.error().message;
    // A 100 x 10 strip offset inwards by 5 collapses (the short sides vanish), by 6 they reverse.
    EXPECT_FALSE(offsetContour(rectangleContour(100, 10), -5.0).ok());
    const auto rev = offsetContour(rectangleContour(100, 10), -6.0);
    ASSERT_FALSE(rev.ok());
    EXPECT_NE(rev.error().message.find("vanish or reverse"), std::string::npos) << rev.error().message;
    // Wrong distance count, non-finite distance, invalid input contour.
    EXPECT_FALSE(offsetContour(rectangleContour(10, 10), std::vector<double>{1, 1}).ok());
    EXPECT_FALSE(offsetContour(rectangleContour(10, 10), std::vector<double>{1, 1, NAN, 1}).ok());
    EXPECT_FALSE(offsetContour(Contour{{{{0, 0}, 0.0}, {{10, 10}, 0.0}, {{10, 0}, 0.0}, {{0, 10}, 0.0}}}, 1.0).ok());
    // Non-local self-intersection: two faces 5 mm apart (y = 40 and y = 45) each move 3 mm towards
    // each other; no segment reverses but the result would cross itself.
    const Contour g{{{{0, 0}, 0.0},
                     {{50, 0}, 0.0},
                     {{50, 40}, 0.0},
                     {{45, 40}, 0.0},
                     {{45, 5}, 0.0},
                     {{5, 5}, 0.0},
                     {{5, 45}, 0.0},
                     {{50, 45}, 0.0},
                     {{50, 50}, 0.0},
                     {{0, 50}, 0.0}}};
    ASSERT_TRUE(validateContour(g).ok());
    const auto gx = offsetContour(g, 3.0);
    ASSERT_FALSE(gx.ok());
    EXPECT_NE(gx.error().message.find("invalid"), std::string::npos) << gx.error().message;
    EXPECT_TRUE(offsetContour(g, 2.0).ok()); // 2 + 2 < 5: still valid
}
