// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/polygon.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace fos::geometry;

namespace {

Polygon2 box(double x, double y, double w, double h) { return makeRectPolygon(Rect2::fromOriginSize({x, y}, w, h)); }
/// 100 x 100 square with a 60 x 60 notch removed from the top-right corner.
Polygon2 lShape() { return {{{0, 0}, {100, 0}, {100, 40}, {40, 40}, {40, 100}, {0, 100}}}; }

} // namespace

TEST(PolygonRelations, TouchingRectanglesDoNotOverlap) {
    const Polygon2 a = box(0, 0, 100, 50);
    EXPECT_FALSE(polygonsOverlap(a, box(100, 0, 100, 50)));  // full shared edge
    EXPECT_FALSE(polygonsOverlap(a, box(100, 20, 30, 100))); // partial shared edge
    EXPECT_FALSE(polygonsOverlap(a, box(100, 50, 10, 10)));  // shared corner only
    EXPECT_FALSE(polygonsOverlap(a, box(150, 0, 10, 10)));   // apart
    EXPECT_DOUBLE_EQ(polygonDistance(a, box(100, 0, 100, 50)), 0.0);
    EXPECT_DOUBLE_EQ(polygonDistance(a, box(100, 50, 10, 10)), 0.0);
    // Tolerance: 0.5e-6 mm penetration is touching, 0.01 mm is an overlap.
    EXPECT_FALSE(polygonsOverlap(a, box(100 - 5e-7, 0, 100, 50)));
    EXPECT_TRUE(polygonsOverlap(a, box(99.99, 0, 100, 50)));
    EXPECT_TRUE(polygonsOverlap(box(99.99, 0, 100, 50), a));
    // A coarser tolerance forgives a 0.01 mm penetration.
    EXPECT_FALSE(polygonsOverlap(a, box(99.99, 0, 100, 50), 0.05));
}

TEST(PolygonRelations, OverlapConfigurations) {
    const Polygon2 a = box(0, 0, 100, 50);
    EXPECT_TRUE(polygonsOverlap(a, box(90, 40, 100, 50)));          // corner in corner
    EXPECT_TRUE(polygonsOverlap(a, a));                             // identical
    EXPECT_TRUE(polygonsOverlap(a, reversed(a)));                   // identical, other winding
    EXPECT_TRUE(polygonsOverlap(a, box(10, 10, 20, 20)));           // b strictly inside a
    EXPECT_TRUE(polygonsOverlap(box(10, 10, 20, 20), a));           // a strictly inside b
    EXPECT_TRUE(polygonsOverlap(a, box(0, 0, 20, 20)));             // inside, sharing a corner and two edges
    // Cross: no vertex of either rectangle is inside the other.
    EXPECT_TRUE(polygonsOverlap(box(0, 40, 100, 20), box(40, 0, 20, 100)));
    // Triangle whose edge enters the square exactly through the corner (0,0).
    EXPECT_TRUE(polygonsOverlap(box(0, 0, 10, 10), Polygon2{{{-5, -5}, {5, 5}, {-5, 5}}}));
    // Triangle touching the square only with its tip at the corner (0,0).
    EXPECT_FALSE(polygonsOverlap(box(0, 0, 10, 10), Polygon2{{{-5, -5}, {0, 0}, {-5, 5}}}));
    // Tip of a triangle touching the middle of an edge from outside.
    EXPECT_FALSE(polygonsOverlap(box(0, 0, 10, 10), Polygon2{{{5, 0}, {8, -6}, {2, -6}}}));
    // Degenerate input never overlaps.
    EXPECT_FALSE(polygonsOverlap(a, Polygon2{}));
    EXPECT_FALSE(polygonsOverlap(Polygon2{{{0, 0}, {10, 10}}}, a));
}

TEST(PolygonRelations, ConcaveNotchNesting) {
    const Polygon2 l = lShape();
    // A 60 x 60 part sits exactly in the notch: bounding boxes overlap, interiors do not.
    EXPECT_FALSE(polygonsOverlap(l, box(40, 40, 60, 60)));
    EXPECT_FALSE(polygonsOverlap(box(40, 40, 60, 60), l));
    EXPECT_DOUBLE_EQ(polygonDistance(l, box(40, 40, 60, 60)), 0.0);
    // 1 mm clear of both notch walls.
    EXPECT_FALSE(polygonsOverlap(l, box(41, 41, 59, 59)));
    EXPECT_DOUBLE_EQ(polygonDistance(l, box(41, 41, 59, 59)), 1.0);
    // Shifted 2 mm into the upper arm.
    EXPECT_TRUE(polygonsOverlap(l, box(38, 41, 59, 59)));
    EXPECT_DOUBLE_EQ(polygonDistance(l, box(38, 41, 59, 59)), 0.0);
    // Two L-shapes interlocking: the second is the first rotated 180 deg about (70,70); its
    // arms occupy [40,140]x[100,140] and [100,140]x[40,140], so the pair only touches.
    const Polygon2 l2 = rotated(l, 180, {70, 70});
    EXPECT_FALSE(polygonsOverlap(l, l2));
    EXPECT_TRUE(polygonsOverlap(l, translated(l2, {-5, -5})));
    // Rotated 180 deg about (50,50) the second L covers the notch and both arms' ends.
    EXPECT_TRUE(polygonsOverlap(l, rotated(l, 180, {50, 50})));
}

TEST(PolygonRelations, Containment) {
    const Polygon2 l = lShape();
    EXPECT_TRUE(polygonContainsPolygon(l, l));                    // itself
    EXPECT_TRUE(polygonContainsPolygon(l, reversed(l)));
    EXPECT_TRUE(polygonContainsPolygon(l, box(5, 5, 30, 90)));    // in the upper arm
    EXPECT_TRUE(polygonContainsPolygon(l, box(10, 10, 80, 20)));  // in the lower arm
    EXPECT_TRUE(polygonContainsPolygon(l, box(0, 0, 40, 100)));   // touching three sides
    EXPECT_TRUE(polygonContainsPolygon(l, box(0, 0, 40 + 5e-7, 100))); // within tolerance
    EXPECT_FALSE(polygonContainsPolygon(l, box(0, 0, 40.01, 100)));
    EXPECT_FALSE(polygonContainsPolygon(l, box(50, 50, 10, 10))); // in the notch
    EXPECT_FALSE(polygonContainsPolygon(l, box(30, 30, 20, 20))); // straddles the reflex corner
    EXPECT_FALSE(polygonContainsPolygon(l, box(-10, -10, 200, 200)));
    // All three vertices are inside the L but the edge (30,90)-(90,30) crosses the notch.
    EXPECT_FALSE(polygonContainsPolygon(l, Polygon2{{{30, 90}, {20, 20}, {90, 30}}}));
    // Convex outer.
    EXPECT_TRUE(polygonContainsPolygon(box(0, 0, 100, 100), l));
    EXPECT_FALSE(polygonContainsPolygon(box(0, 0, 100, 99), l));
    EXPECT_FALSE(polygonContainsPolygon(l, Polygon2{}));
    EXPECT_FALSE(polygonContainsPolygon(Polygon2{}, l));
}

TEST(PolygonRelations, ContainmentWithHoles) {
    const PolygonWithHoles plate{box(0, 0, 100, 100), {box(20, 20, 20, 20)}};
    EXPECT_TRUE(polygonContainsPolygon(plate, box(50, 50, 30, 30)));
    EXPECT_TRUE(polygonContainsPolygon(plate, box(40, 20, 20, 20)));  // touches the hole edge
    EXPECT_FALSE(polygonContainsPolygon(plate, box(25, 25, 5, 5)));   // inside the hole
    EXPECT_FALSE(polygonContainsPolygon(plate, box(30, 30, 30, 30))); // partly over the hole
    EXPECT_FALSE(polygonContainsPolygon(plate, box(10, 10, 60, 60))); // swallows the hole
    EXPECT_FALSE(polygonContainsPolygon(plate, box(90, 90, 20, 20))); // leaves the plate
}

TEST(PolygonRelations, Distance) {
    const Polygon2 a = box(0, 0, 10, 10);
    // Corner (10,10) to corner (13,14): hypot(3,4) = 5.
    EXPECT_DOUBLE_EQ(polygonDistance(a, box(13, 14, 7, 6)), 5.0);
    EXPECT_DOUBLE_EQ(polygonDistance(box(13, 14, 7, 6), a), 5.0);
    // Facing edges 2.5 mm apart.
    EXPECT_DOUBLE_EQ(polygonDistance(a, box(12.5, 3, 5, 5)), 2.5);
    // Vertex of a diamond pointing at the right edge: tip (14,5) -> edge x = 10: 4 mm.
    EXPECT_DOUBLE_EQ(polygonDistance(a, Polygon2{{{14, 5}, {18, 1}, {22, 5}, {18, 9}}}), 4.0);
    // Overlap and containment give 0.
    EXPECT_DOUBLE_EQ(polygonDistance(a, box(5, 5, 10, 10)), 0.0);
    EXPECT_DOUBLE_EQ(polygonDistance(a, box(2, 2, 3, 3)), 0.0);
    EXPECT_DOUBLE_EQ(polygonDistance(box(2, 2, 3, 3), a), 0.0);
    // No tolerance: a 1e-7 gap is reported.
    EXPECT_NEAR(polygonDistance(a, box(10 + 1e-7, 0, 5, 5)), 1e-7, 1e-12);
    EXPECT_EQ(polygonDistance(a, Polygon2{}), std::numeric_limits<double>::infinity());
}
