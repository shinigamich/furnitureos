// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/curve_intersect.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace fos;
using namespace fos::geometry;

namespace {
const double kQ = std::tan(core::kPi / 8); // 90 degree arc

/// Segment 0 of a two-vertex contour a -> b with the given bulge.
ContourSegmentInfo seg(Vec2 a, Vec2 b, double bulge) { return segmentInfo(Contour{{{a, bulge}, {b, 0.0}}}, 0); }

void expectPoint(Vec2 p, double x, double y, double tol = 1e-9) {
    EXPECT_NEAR(p.x, x, tol);
    EXPECT_NEAR(p.y, y, tol);
}
} // namespace

TEST(CurveIntersect, InfiniteLines) {
    auto p = intersectLines({0, 0}, {1, 1}, {10, 0}, {-1, 1});
    ASSERT_TRUE(p.has_value());
    expectPoint(*p, 5, 5);
    EXPECT_FALSE(intersectLines({0, 0}, {1, 0}, {0, 1}, {2, 0}).has_value()); // parallel
    EXPECT_FALSE(intersectLines({0, 0}, {0, 0}, {0, 1}, {2, 0}).has_value()); // zero direction
}

TEST(CurveIntersect, LineCircle) {
    // y = 3 against r = 5 about the origin: x = +/- sqrt(25 - 9) = +/- 4, ordered along d.
    auto pts = intersectLineCircle({-10, 3}, {1, 0}, {0, 0}, 5);
    ASSERT_EQ(pts.size(), 2u);
    expectPoint(pts[0], -4, 3);
    expectPoint(pts[1], 4, 3);
    pts = intersectLineCircle({-10, 5}, {1, 0}, {0, 0}, 5); // tangent
    ASSERT_EQ(pts.size(), 1u);
    expectPoint(pts[0], 0, 5);
    pts = intersectLineCircle({-10, 5 + 5e-7}, {1, 0}, {0, 0}, 5); // misses by less than eps
    ASSERT_EQ(pts.size(), 1u);
    EXPECT_TRUE(intersectLineCircle({-10, 5.01}, {1, 0}, {0, 0}, 5).empty());
    EXPECT_TRUE(intersectLineCircle({-10, 5 + 5e-7}, {1, 0}, {0, 0}, 5, 0.0).empty());
}

TEST(CurveIntersect, Circles) {
    // r 5 at (0,0) and (8,0): radical line x = (64 + 25 - 25) / 16 = 4, half chord sqrt(25-16) = 3.
    auto pts = intersectCircles({0, 0}, 5, {8, 0}, 5);
    ASSERT_EQ(pts.size(), 2u);
    expectPoint(pts[0], 4, 3); // left of (0,0) -> (8,0) first
    expectPoint(pts[1], 4, -3);
    pts = intersectCircles({0, 0}, 5, {10, 0}, 5); // external tangency
    ASSERT_EQ(pts.size(), 1u);
    expectPoint(pts[0], 5, 0);
    pts = intersectCircles({0, 0}, 10, {5, 0}, 5); // internal tangency at (10, 0)
    ASSERT_EQ(pts.size(), 1u);
    expectPoint(pts[0], 10, 0);
    pts = intersectCircles({5, 0}, 5, {0, 0}, 10); // same, smaller circle first
    ASSERT_EQ(pts.size(), 1u);
    expectPoint(pts[0], 10, 0);
    EXPECT_TRUE(intersectCircles({0, 0}, 10, {1, 0}, 2).empty());  // nested
    EXPECT_TRUE(intersectCircles({0, 0}, 5, {11, 0}, 5).empty());  // apart
    EXPECT_TRUE(intersectCircles({0, 0}, 5, {0, 0}, 5).empty());   // concentric
}

TEST(CurveIntersect, PointToArcDistance) {
    const ContourSegmentInfo a = seg({10, 0}, {0, 10}, kQ); // quarter arc r 10, 0..90 deg
    EXPECT_NEAR(distancePointContourSegment({20, 20}, a), std::sqrt(800.0) - 10, 1e-12); // radial
    EXPECT_NEAR(distancePointContourSegment({0, 0}, a), 10, 1e-12);
    // (10,-10) is at -45 deg, outside the sweep: nearest is the endpoint (10,0), distance 10.
    EXPECT_NEAR(distancePointContourSegment({10, -10}, a), 10, 1e-12);
    expectPoint(closestPointOnContourSegment({3, 4}, a), 6, 8); // 10 * (3,4)/5
}

TEST(CurveIntersect, ContourSegments) {
    // Line y = 6 from x = -20 to 20 against the upper half circle r 10: x = +/- 8.
    ContourSegmentInfo line = seg({-20, 6}, {20, 6}, 0);
    ContourSegmentInfo arc = seg({10, 0}, {-10, 0}, 1.0);
    CurveIntersection x = intersectContourSegments(line, arc);
    ASSERT_EQ(x.kind, CurveIntersectionKind::Points);
    ASSERT_EQ(x.points.size(), 2u);
    expectPoint(x.points[0], -8, 6);
    expectPoint(x.points[1], 8, 6);
    // Against the LOWER half circle there is no contact.
    EXPECT_EQ(intersectContourSegments(line, seg({10, 0}, {-10, 0}, -1.0)).kind, CurveIntersectionKind::None);
    // A line ending within eps of the arc touches it at its endpoint.
    line = seg({0, 20}, {0, 10 + 5e-7}, 0);
    x = intersectContourSegments(line, arc);
    ASSERT_EQ(x.kind, CurveIntersectionKind::Points);
    ASSERT_EQ(x.points.size(), 1u);
    expectPoint(x.points[0], 0, 10, 1e-6);
    // Arc-arc crossing: quarter arcs of r 5 about (0,0) and (8,0) meet at (4,3).
    const double a0 = std::atan2(3.0, 4.0);
    const ContourSegmentInfo p = seg({5, 0}, {0, 5}, kQ);
    const ContourSegmentInfo q = seg({8, 5}, {3, 0}, kQ); // about (8,0): 90 -> 180 deg
    x = intersectContourSegments(p, q);
    ASSERT_EQ(x.kind, CurveIntersectionKind::Points);
    ASSERT_EQ(x.points.size(), 1u);
    expectPoint(x.points[0], 5 * std::cos(a0), 5 * std::sin(a0));
    // Arcs of one circle: overlapping ranges -> Overlap; consecutive ranges -> shared endpoint.
    const ContourSegmentInfo upper = seg({10, 0}, {-10, 0}, 1.0);
    EXPECT_EQ(intersectContourSegments(upper, seg({0, 10}, {-10, 0}, kQ)).kind, CurveIntersectionKind::Overlap);
    x = intersectContourSegments(upper, seg({-10, 0}, {0, -10}, kQ));
    ASSERT_EQ(x.kind, CurveIntersectionKind::Points);
    ASSERT_EQ(x.points.size(), 1u);
    expectPoint(x.points[0], -10, 0);
    // Collinear overlapping lines.
    x = intersectContourSegments(seg({0, 0}, {10, 0}, 0), seg({5, 0}, {20, 0}, 0));
    EXPECT_EQ(x.kind, CurveIntersectionKind::Overlap);
}
