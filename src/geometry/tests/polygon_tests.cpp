// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/polygon.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace fos::geometry;

namespace {

/// L-shape: 100 x 100 square with a 60 x 60 notch removed from the top-right corner (CCW).
Polygon2 lShape() { return {{{0, 0}, {100, 0}, {100, 40}, {40, 40}, {40, 100}, {0, 100}}}; }
Polygon2 rect100x50() { return makeRectPolygon(Rect2::fromSize(100, 50)); }
/// Self-intersecting "bow tie".
Polygon2 bowTie() { return {{{0, 0}, {10, 10}, {10, 0}, {0, 10}}}; }

} // namespace

TEST(Rect2, BasicsAndExpand) {
    Rect2 r;
    EXPECT_TRUE(r.empty());
    EXPECT_DOUBLE_EQ(r.width(), 0.0);
    EXPECT_DOUBLE_EQ(r.area(), 0.0);
    r.expand({3, 4});
    r.expand({-1, 10});
    EXPECT_FALSE(r.empty());
    EXPECT_EQ(r.min, (Vec2{-1, 4}));
    EXPECT_EQ(r.max, (Vec2{3, 10}));
    EXPECT_DOUBLE_EQ(r.width(), 4.0);
    EXPECT_DOUBLE_EQ(r.height(), 6.0);
    EXPECT_DOUBLE_EQ(r.area(), 24.0);
    EXPECT_EQ(r.center(), (Vec2{1, 7}));
    EXPECT_EQ(Rect2::fromCorners({5, 1}, {2, 8}), (Rect2{{2, 1}, {5, 8}}));
    EXPECT_EQ(Rect2::fromOriginSize({10, 20}, 30, 40), (Rect2{{10, 20}, {40, 60}}));
    EXPECT_EQ(Rect2::fromSize(10, 20).inflated(2), (Rect2{{-2, -2}, {12, 22}}));
    EXPECT_TRUE(Rect2::fromSize(10, 20).inflated(-6).empty()); // 10 - 12 < 0
    EXPECT_TRUE(r.contains({3, 10}));
    EXPECT_TRUE(r.contains({3 + 5e-7, 10}));
    EXPECT_FALSE(r.contains({3.001, 10}));
}

TEST(Rect2, OverlapTouchingAndGap) {
    const Rect2 a = Rect2::fromSize(100, 50);
    // Touching along x = 100: not an overlap.
    EXPECT_FALSE(rectsOverlap(a, Rect2::fromOriginSize({100, 0}, 100, 50)));
    EXPECT_FALSE(rectsOverlap(a, Rect2::fromOriginSize({100, 50}, 10, 10))); // corner contact
    // Penetration of 0.5e-6 mm is within tolerance, 0.01 mm is an overlap.
    EXPECT_FALSE(rectsOverlap(a, Rect2::fromOriginSize({100 - 5e-7, 0}, 100, 50)));
    EXPECT_TRUE(rectsOverlap(a, Rect2::fromOriginSize({99.99, 0}, 100, 50)));
    EXPECT_TRUE(rectsOverlap(a, Rect2::fromOriginSize({20, 10}, 10, 10))); // inside
    EXPECT_FALSE(rectsOverlap(a, Rect2::fromOriginSize({20, 60}, 10, 10)));
    EXPECT_FALSE(rectsOverlap(a, Rect2{}));
    // Saw kerf 4 mm: a neighbour 3 mm away violates the gap, 4 mm away is fine,
    // 4 mm minus 0.5e-6 is fine within tolerance.
    EXPECT_TRUE(rectsOverlap(a, Rect2::fromOriginSize({103, 0}, 100, 50), 4.0));
    EXPECT_FALSE(rectsOverlap(a, Rect2::fromOriginSize({104, 0}, 100, 50), 4.0));
    EXPECT_FALSE(rectsOverlap(a, Rect2::fromOriginSize({104 - 5e-7, 0}, 100, 50), 4.0));
    // Gap is per axis: diagonal neighbour 3 mm away in x and y violates a 4 mm gap,
    // but one that is 3 mm away in x and 5 mm in y does not.
    EXPECT_TRUE(rectsOverlap(a, Rect2::fromOriginSize({103, 53}, 10, 10), 4.0));
    EXPECT_FALSE(rectsOverlap(a, Rect2::fromOriginSize({103, 55}, 10, 10), 4.0));
}

TEST(Rect2, ContainsRectWithMargin) {
    const Rect2 sheet = Rect2::fromSize(2800, 2070);
    EXPECT_TRUE(rectContainsRect(sheet, Rect2::fromSize(2800, 2070)));            // identical
    EXPECT_TRUE(rectContainsRect(sheet, Rect2::fromOriginSize({0, 0}, 600, 400))); // touching two sides
    EXPECT_FALSE(rectContainsRect(sheet, Rect2::fromOriginSize({2300, 0}, 600, 400))); // 100 mm over
    EXPECT_TRUE(rectContainsRect(sheet, Rect2::fromOriginSize({2200 + 5e-7, 0}, 600, 400)));
    // Trim margin 10 mm.
    EXPECT_FALSE(rectContainsRect(sheet, Rect2::fromOriginSize({0, 0}, 600, 400), 10.0));
    EXPECT_FALSE(rectContainsRect(sheet, Rect2::fromOriginSize({9, 10}, 600, 400), 10.0));
    EXPECT_TRUE(rectContainsRect(sheet, Rect2::fromOriginSize({10, 10}, 600, 400), 10.0));
    EXPECT_TRUE(rectContainsRect(sheet, Rect2::fromOriginSize({2190, 1660}, 600, 400), 10.0)); // far corner exactly
    EXPECT_FALSE(rectContainsRect(sheet, Rect2{}));
}

TEST(Polygon, RectangleMeasures) {
    const Polygon2 r = rect100x50();
    ASSERT_EQ(r.size(), 4u);
    EXPECT_EQ(r[0], (Vec2{0, 0}));
    EXPECT_EQ(r.edge(3), (Segment2{{0, 50}, {0, 0}}));
    EXPECT_DOUBLE_EQ(signedArea(r), 5000.0);
    EXPECT_DOUBLE_EQ(perimeter(r), 300.0);
    EXPECT_EQ(winding(r), Winding::CounterClockwise);
    EXPECT_TRUE(approxEqual(centroid(r), {50, 25}, 1e-12));
    EXPECT_EQ(bounds(r), (Rect2{{0, 0}, {100, 50}}));
    EXPECT_TRUE(makeRectPolygon(Rect2{}).empty());
}

TEST(Polygon, LShapeAreaAndCentroid) {
    const Polygon2 l = lShape();
    // Area: 100*100 - 60*60 = 6400. Perimeter: 100 + 40 + 60 + 60 + 40 + 100 = 400.
    EXPECT_DOUBLE_EQ(signedArea(l), 6400.0);
    EXPECT_DOUBLE_EQ(area(l), 6400.0);
    EXPECT_DOUBLE_EQ(perimeter(l), 400.0);
    // Centroid: square (area 10000, centroid 50) minus notch [40,100]^2 (area 3600, centroid 70):
    // x = (10000*50 - 3600*70) / 6400 = (500000 - 252000) / 6400 = 38.75; same for y by symmetry.
    EXPECT_TRUE(approxEqual(centroid(l), {38.75, 38.75}, 1e-12));
    EXPECT_EQ(bounds(l), (Rect2{{0, 0}, {100, 100}}));
    // Far from the origin the centroid simply shifts (no loss of precision).
    EXPECT_TRUE(approxEqual(centroid(translated(l, {1e6, -2e6})), {1e6 + 38.75, -2e6 + 38.75}, 1e-8));
}

TEST(Polygon, ClockwiseInputAndNormalize) {
    const Polygon2 cw = reversed(lShape());
    EXPECT_EQ(cw[0], (Vec2{0, 0})); // vertex 0 stays first
    EXPECT_EQ(cw[1], (Vec2{0, 100}));
    EXPECT_DOUBLE_EQ(signedArea(cw), -6400.0);
    EXPECT_DOUBLE_EQ(area(cw), 6400.0);
    EXPECT_EQ(winding(cw), Winding::Clockwise);
    EXPECT_TRUE(approxEqual(centroid(cw), {38.75, 38.75}, 1e-12)); // independent of winding
    EXPECT_EQ(normalizedCcw(cw), lShape());
    EXPECT_EQ(normalizedCcw(lShape()), lShape());
}

TEST(Polygon, DegenerateRings) {
    EXPECT_EQ(winding(Polygon2{}), Winding::Degenerate);
    EXPECT_EQ(winding(Polygon2{{{0, 0}, {10, 0}}}), Winding::Degenerate);
    const Polygon2 line{{{0, 0}, {10, 0}, {20, 0}}};
    EXPECT_EQ(winding(line), Winding::Degenerate);
    EXPECT_DOUBLE_EQ(signedArea(line), 0.0);
    EXPECT_TRUE(approxEqual(centroid(line), {10, 0}, 1e-12)); // vertex mean
    EXPECT_EQ(centroid(Polygon2{}), (Vec2{0, 0}));
    EXPECT_TRUE(bounds(Polygon2{}).empty());
    EXPECT_DOUBLE_EQ(perimeter(Polygon2{{{0, 0}, {3, 4}}}), 10.0); // there and back
    EXPECT_FALSE(isSimple(line));
    EXPECT_FALSE(isConvex(line));
    EXPECT_EQ(locatePoint(Polygon2{}, {0, 0}), PointLocation::Outside);
}

TEST(Polygon, PointInPolygon) {
    const Polygon2 l = lShape();
    EXPECT_EQ(locatePoint(l, {20, 20}), PointLocation::Inside);
    EXPECT_EQ(locatePoint(l, {20, 90}), PointLocation::Inside);  // upper arm
    EXPECT_EQ(locatePoint(l, {90, 20}), PointLocation::Inside);  // right arm
    EXPECT_EQ(locatePoint(l, {70, 70}), PointLocation::Outside); // in the notch
    EXPECT_EQ(locatePoint(l, {150, 20}), PointLocation::Outside);
    EXPECT_EQ(locatePoint(l, {-5, 40}), PointLocation::Outside); // ray passes through vertex (40,40) and an edge
    EXPECT_EQ(locatePoint(l, {20, 40}), PointLocation::Inside);  // ray runs along the edge y = 40
    EXPECT_EQ(locatePoint(l, {20, 100.5}), PointLocation::Outside);
    // Boundary: on an edge, on a vertex, on the reflex corner.
    EXPECT_EQ(locatePoint(l, {40, 70}), PointLocation::OnBoundary);
    EXPECT_EQ(locatePoint(l, {100, 40}), PointLocation::OnBoundary);
    EXPECT_EQ(locatePoint(l, {40, 40}), PointLocation::OnBoundary);
    EXPECT_EQ(locatePoint(l, {0, 0}), PointLocation::OnBoundary);
    // Tolerance: 0.5e-6 off the edge x = 40 is on the boundary; 1e-3 off is decided.
    EXPECT_EQ(locatePoint(l, {40 + 5e-7, 70}), PointLocation::OnBoundary);
    EXPECT_EQ(locatePoint(l, {40 - 5e-7, 70}), PointLocation::OnBoundary);
    EXPECT_EQ(locatePoint(l, {40.001, 70}), PointLocation::Outside);
    EXPECT_EQ(locatePoint(l, {39.999, 70}), PointLocation::Inside);
    EXPECT_EQ(locatePoint(l, {39.95, 70}, 0.1), PointLocation::OnBoundary); // coarse tolerance
    // Winding does not matter.
    EXPECT_EQ(locatePoint(reversed(l), {20, 20}), PointLocation::Inside);
    EXPECT_EQ(locatePoint(reversed(l), {70, 70}), PointLocation::Outside);
}

TEST(Polygon, Simplicity) {
    EXPECT_TRUE(isSimple(rect100x50()));
    EXPECT_TRUE(isSimple(lShape()));
    EXPECT_TRUE(isSimple(reversed(lShape())));
    EXPECT_TRUE(isSimple(Polygon2{{{0, 0}, {10, 0}, {0, 10}}}));
    EXPECT_FALSE(isSimple(bowTie()));
    EXPECT_FALSE(isSimple(Polygon2{{{0, 0}, {10, 0}}}));
    // Repeated vertex (zero-length edge).
    EXPECT_FALSE(isSimple(Polygon2{{{0, 0}, {10, 0}, {10, 0}, {10, 10}, {0, 10}}}));
    // Zero-width spike: (10,10) -> (15,10) -> (12,10) folds back onto itself.
    EXPECT_FALSE(isSimple(Polygon2{{{0, 0}, {10, 0}, {10, 10}, {15, 10}, {12, 10}, {0, 10}}}));
    // A vertex touching a non-adjacent edge: (5,0) lies on the bottom edge.
    EXPECT_FALSE(isSimple(Polygon2{{{0, 0}, {10, 0}, {10, 10}, {5, 0}, {0, 10}}}));
    // Collinear vertices are fine.
    EXPECT_TRUE(isSimple(Polygon2{{{0, 0}, {5, 0}, {10, 0}, {10, 10}, {0, 10}}}));
}

TEST(Polygon, Convexity) {
    EXPECT_TRUE(isConvex(rect100x50()));
    EXPECT_TRUE(isConvex(reversed(rect100x50())));
    EXPECT_FALSE(isConvex(lShape()));
    EXPECT_FALSE(isConvex(bowTie()));
    // Collinear and repeated vertices do not spoil convexity.
    EXPECT_TRUE(isConvex(Polygon2{{{0, 0}, {5, 0}, {10, 0}, {10, 0}, {10, 10}, {0, 10}}}));
    // Pentagram: every turn is to the left but the outline self-intersects.
    Polygon2 star;
    for (int i = 0; i < 5; ++i) {
        const double a = fos::core::degToRad(90.0 + 144.0 * i);
        star.vertices.push_back({10 * std::cos(a), 10 * std::sin(a)});
    }
    EXPECT_FALSE(isConvex(star));
}

TEST(Polygon, CleanedRemovesDuplicatesAndCollinear) {
    const Polygon2 square{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    // Midpoint, repeated vertex and closing vertex repeated.
    EXPECT_EQ(cleaned(Polygon2{{{0, 0}, {5, 0}, {10, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}}}), square);
    // Zero-width spike out to (15,10) and back.
    EXPECT_EQ(cleaned(Polygon2{{{0, 0}, {10, 0}, {10, 10}, {15, 10}, {10, 10}, {0, 10}}}), square);
    // Near-duplicates within tolerance (0.5e-6) go, a real 0.01 mm jog stays.
    EXPECT_EQ(cleaned(Polygon2{{{0, 0}, {10, 0}, {10 + 5e-7, 5e-7}, {10, 10}, {0, 10}}}).size(), 4u);
    EXPECT_EQ(cleaned(Polygon2{{{0, 0}, {10, 0}, {10.01, 5}, {10, 10}, {0, 10}}}).size(), 5u);
    EXPECT_EQ(cleaned(square), square);
    EXPECT_EQ(cleaned(lShape()), lShape());
    // Order and winding are preserved.
    EXPECT_EQ(cleaned(reversed(Polygon2{{{0, 0}, {5, 0}, {10, 0}, {10, 10}, {0, 10}}})), reversed(square));
    // Fully degenerate input collapses to fewer than 3 vertices.
    EXPECT_LT(cleaned(Polygon2{{{0, 0}, {5, 0}, {10, 0}}}).size(), 3u);
    EXPECT_TRUE(cleaned(Polygon2{}).empty());
    EXPECT_EQ(cleaned(Polygon2{{{1, 1}, {1, 1}, {1, 1}}}).size(), 1u);
}

TEST(Polygon, ConvexHull) {
    // Corners of a 10 x 10 square, interior points, points on the edges and a repeated corner.
    const std::vector<Vec2> pts{{5, 5}, {10, 10}, {0, 0}, {5, 0}, {10, 0}, {3, 7}, {0, 10},
                                {0, 5}, {10, 10}, {10, 5}, {2, 2}};
    const Polygon2 hull = convexHull(pts);
    EXPECT_EQ(hull, (Polygon2{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}})); // CCW from lowest x, then lowest y
    // Hull of the L-shape fills the notch with the diagonal (100,40)-(40,100):
    // area = 6400 + 60*60/2 = 8200.
    const Polygon2 lHull = convexHull(lShape().vertices);
    EXPECT_EQ(lHull, (Polygon2{{{0, 0}, {100, 0}, {100, 40}, {40, 100}, {0, 100}}}));
    EXPECT_DOUBLE_EQ(signedArea(lHull), 8200.0);
    EXPECT_TRUE(isConvex(lHull));
    // Degenerate inputs.
    EXPECT_TRUE(convexHull({}).empty());
    EXPECT_EQ(convexHull({{3, 4}, {3, 4}}).size(), 1u);
    EXPECT_EQ(convexHull({{0, 0}, {5, 5}, {10, 10}, {2, 2}}), (Polygon2{{{0, 0}, {10, 10}}}));
    // A triangle given clockwise comes back counter-clockwise.
    EXPECT_EQ(convexHull({{0, 0}, {0, 10}, {10, 0}}), (Polygon2{{{0, 0}, {10, 0}, {0, 10}}}));
}

TEST(Polygon, TranslateRotateMirror) {
    const Polygon2 r = rect100x50();
    EXPECT_EQ(translated(r, {10, -5}), (Polygon2{{{10, -5}, {110, -5}, {110, 45}, {10, 45}}}));
    // 90 deg CCW about the origin: (x, y) -> (-y, x): the rectangle now spans [-50,0] x [0,100].
    const Polygon2 q = rotated(r, 90);
    ASSERT_EQ(q.size(), 4u);
    EXPECT_TRUE(approxEqual(q[1], {0, 100}, 1e-9));
    EXPECT_TRUE(approxEqual(q[2], {-50, 100}, 1e-9));
    EXPECT_NEAR(signedArea(q), 5000.0, 1e-9); // rotation keeps winding
    // 180 deg about the centre maps the rectangle onto itself: vertex 0 -> opposite corner.
    EXPECT_TRUE(approxEqual(rotated(r, 180, {50, 25})[0], {100, 50}, 1e-9));
    // Mirror the L across x = 50: x -> 100 - x. Area sign is preserved (still CCW) and the
    // centroid moves to (100 - 38.75, 38.75).
    const Polygon2 m = mirroredX(lShape(), 50);
    EXPECT_DOUBLE_EQ(signedArea(m), 6400.0);
    EXPECT_TRUE(approxEqual(centroid(m), {61.25, 38.75}, 1e-12));
    EXPECT_EQ(m[0], (Vec2{100, 0}));
    EXPECT_EQ(locatePoint(m, {30, 70}), PointLocation::Outside); // the notch is now top-left
    EXPECT_EQ(locatePoint(m, {70, 70}), PointLocation::Inside);
    // Mirror across y = 0.
    const Polygon2 my = mirroredY(lShape());
    EXPECT_DOUBLE_EQ(signedArea(my), 6400.0);
    EXPECT_TRUE(approxEqual(centroid(my), {38.75, -38.75}, 1e-12));
    EXPECT_EQ(bounds(my), (Rect2{{0, -100}, {100, 0}}));
}

TEST(PolygonWithHoles, AreaCentroidLocate) {
    // 100 x 100 plate with a 20 x 20 cut-out at [20,40]^2; the hole is given CCW on purpose.
    PolygonWithHoles p{makeRectPolygon(Rect2::fromSize(100, 100)),
                       {makeRectPolygon(Rect2::fromOriginSize({20, 20}, 20, 20))}};
    EXPECT_DOUBLE_EQ(area(p), 9600.0); // 10000 - 400
    // Centroid: (10000*50 - 400*30) / 9600 = 488000 / 9600 = 50.8333...
    EXPECT_TRUE(approxEqual(centroid(p), {488000.0 / 9600.0, 488000.0 / 9600.0}, 1e-12));
    const PolygonWithHoles n = normalized(p);
    EXPECT_EQ(winding(n.outer), Winding::CounterClockwise);
    EXPECT_EQ(winding(n.holes[0]), Winding::Clockwise);
    EXPECT_DOUBLE_EQ(area(n), 9600.0);
    EXPECT_TRUE(approxEqual(centroid(n), centroid(p), 1e-12));
    EXPECT_EQ(locatePoint(p, {50, 50}), PointLocation::Inside);
    EXPECT_EQ(locatePoint(p, {30, 30}), PointLocation::Outside); // in the hole
    EXPECT_EQ(locatePoint(p, {20, 30}), PointLocation::OnBoundary);
    EXPECT_EQ(locatePoint(p, {100, 30}), PointLocation::OnBoundary);
    EXPECT_EQ(locatePoint(p, {130, 30}), PointLocation::Outside);
}
