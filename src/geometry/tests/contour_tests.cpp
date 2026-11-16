// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/contour.hpp"
#include "fos/geometry/curve_intersect.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace fos;
using namespace fos::geometry;

namespace {
constexpr double kPi = core::kPi;
const double kQ = std::tan(kPi / 8);       // bulge of a 90 degree arc: tan(22.5 deg) = 0.41421356...
const double kQ3 = std::tan(3 * kPi / 8);  // bulge of a 270 degree arc: tan(67.5 deg) = 2.41421356...

Contour roundedShelf() {
    const double r[4] = {50, 50, 50, 50};
    return roundedRectangleContour(600, 400, r).value();
}

/// Two-segment contour: an arc from `a` to `b` with bulge `bulge`, closed by a line back to `a`.
Contour arcAndChord(Vec2 a, Vec2 b, double bulge) { return Contour{{{a, bulge}, {b, 0.0}}}; }
} // namespace

TEST(ContourSegment, QuarterArcsBothDirections) {
    EXPECT_NEAR(kQ, 0.41421356237, 1e-10);
    // CCW quarter arc (10,0) -> (0,10): centre (0,0), r 10, start 0 deg, sweep +90, length 10*pi/2.
    // Check of the centre formula: chord c = 10*sqrt2, b^2 = 3 - 2 sqrt2, 1 - b^2 = 2b, so
    // h = c(1-b^2)/(4b) = c/2 = 5 sqrt2 along the chord's left normal (-1,-1)/sqrt2 from (5,5).
    ContourSegmentInfo s = segmentInfo(arcAndChord({10, 0}, {0, 10}, kQ), 0);
    ASSERT_TRUE(s.isArc);
    EXPECT_NEAR(s.center.x, 0, 1e-9);
    EXPECT_NEAR(s.center.y, 0, 1e-9);
    EXPECT_NEAR(s.radius, 10, 1e-9);
    EXPECT_NEAR(s.startAngle, 0, 1e-9);
    EXPECT_NEAR(s.sweep, 90, 1e-9);
    EXPECT_NEAR(s.length, 5 * kPi, 1e-9);
    // CW quarter arc (10,0) -> (0,-10): same circle, sweep -90.
    s = segmentInfo(arcAndChord({10, 0}, {0, -10}, -kQ), 0);
    EXPECT_NEAR(s.center.x, 0, 1e-9);
    EXPECT_NEAR(s.center.y, 0, 1e-9);
    EXPECT_NEAR(s.radius, 10, 1e-9);
    EXPECT_NEAR(s.sweep, -90, 1e-9);
    EXPECT_NEAR(s.length, 5 * kPi, 1e-9);
    // The closing line of the two-segment contour is a plain line of length 10 sqrt2.
    const ContourSegmentInfo l = segmentInfo(arcAndChord({10, 0}, {0, 10}, kQ), 1);
    EXPECT_FALSE(l.isArc);
    EXPECT_NEAR(l.length, 10 * std::sqrt(2.0), 1e-12);
}

TEST(ContourSegment, HalfAndThreeQuarterArcs) {
    // bulge 1: half circle (10,0) -> (-10,0) CCW through (0,10): centre = chord midpoint.
    ContourSegmentInfo s = segmentInfo(arcAndChord({10, 0}, {-10, 0}, 1.0), 0);
    EXPECT_NEAR(s.center.x, 0, 1e-12);
    EXPECT_NEAR(s.center.y, 0, 1e-12);
    EXPECT_NEAR(s.radius, 10, 1e-12);
    EXPECT_NEAR(s.startAngle, 0, 1e-12);
    EXPECT_NEAR(s.sweep, 180, 1e-12);
    EXPECT_NEAR(s.length, 10 * kPi, 1e-9);
    s = segmentInfo(arcAndChord({10, 0}, {-10, 0}, -1.0), 0); // CW through (0,-10)
    EXPECT_NEAR(s.sweep, -180, 1e-12);
    EXPECT_NEAR(s.center.y, 0, 1e-12);
    // bulge tan(67.5 deg) = 1 + sqrt2: 270 deg CCW (10,0) -> (0,-10) about (0,0).
    // b^2 = 3 + 2 sqrt2, 1 - b^2 = -2b -> h = -c/2: the centre is on the chord's RIGHT.
    EXPECT_NEAR(kQ3, 1 + std::sqrt(2.0), 1e-12);
    s = segmentInfo(arcAndChord({10, 0}, {0, -10}, kQ3), 0);
    EXPECT_NEAR(s.center.x, 0, 1e-9);
    EXPECT_NEAR(s.center.y, 0, 1e-9);
    EXPECT_NEAR(s.radius, 10, 1e-9);
    EXPECT_NEAR(s.sweep, 270, 1e-9);
    EXPECT_NEAR(s.length, 15 * kPi, 1e-9); // 10 * 3pi/2
    // CW 270 deg (10,0) -> (0,10) through (0,-10), (-10,0).
    s = segmentInfo(arcAndChord({10, 0}, {0, 10}, -kQ3), 0);
    EXPECT_NEAR(s.center.x, 0, 1e-9);
    EXPECT_NEAR(s.center.y, 0, 1e-9);
    EXPECT_NEAR(s.sweep, -270, 1e-9);
    EXPECT_NEAR(s.startAngle, 0, 1e-9);
}

TEST(ContourMeasure, RoundedRectangleAreaAndPerimeter) {
    const Contour c = roundedShelf();
    ASSERT_EQ(c.size(), 8u); // 4 lines + 4 arcs
    ASSERT_TRUE(validateContour(c).ok());
    // Each corner removes the square minus the quarter disc: 50^2 - pi 50^2 / 4 = 2500 - 1963.4954
    // = 536.5046. Area = 240000 - 4 * 536.5046 = 237853.98.
    const double expectedArea = 240000.0 - 4.0 * (2500.0 - kPi * 2500.0 / 4.0);
    EXPECT_NEAR(expectedArea, 237853.98, 0.01);
    EXPECT_NEAR(signedArea(c), expectedArea, 1e-6);
    // Perimeter = 2 (600 + 400) - 8 * 50 + 2 pi 50 = 1600 + 314.159 = 1914.159.
    EXPECT_NEAR(perimeter(c), 2000.0 - 400.0 + 100.0 * kPi, 1e-9);
    const Rect2 b = bounds(c);
    EXPECT_NEAR(b.min.x, 0, 1e-12);
    EXPECT_NEAR(b.min.y, 0, 1e-12);
    EXPECT_NEAR(b.max.x, 600, 1e-12);
    EXPECT_NEAR(b.max.y, 400, 1e-12);
    // Segment 0 is the bottom line, segment 1 the arc at corner (600,0).
    EXPECT_FALSE(segmentInfo(c, 0).isArc);
    EXPECT_NEAR(segmentInfo(c, 0).length, 500, 1e-12);
    const ContourSegmentInfo a = segmentInfo(c, 1);
    ASSERT_TRUE(a.isArc);
    EXPECT_NEAR(a.center.x, 550, 1e-9);
    EXPECT_NEAR(a.center.y, 50, 1e-9);
    EXPECT_NEAR(a.radius, 50, 1e-9);
    EXPECT_NEAR(a.sweep, 90, 1e-9);
}

TEST(ContourMeasure, CircleAndOrientation) {
    const Contour c = circleContour({20, 30}, 100);
    ASSERT_TRUE(validateContour(c).ok());
    EXPECT_NEAR(signedArea(c), kPi * 1e4, 1e-7); // pi r^2 = 31415.93
    EXPECT_NEAR(perimeter(c), 200 * kPi, 1e-9);
    const Rect2 b = bounds(c);
    EXPECT_NEAR(b.min.x, -80, 1e-12);
    EXPECT_NEAR(b.max.x, 120, 1e-12);
    EXPECT_NEAR(b.min.y, -70, 1e-12);
    EXPECT_NEAR(b.max.y, 130, 1e-12);
    const Contour r = reversed(c);
    EXPECT_NEAR(signedArea(r), -kPi * 1e4, 1e-7);
    EXPECT_EQ(normalizedCcw(r), reversed(r));
    EXPECT_EQ(normalizedCcw(c), c);
    EXPECT_EQ(reversed(reversed(c)), c);
    const Contour t = translated(c, {-20, -30});
    EXPECT_NEAR(bounds(t).max.x, 100, 1e-12);
}

TEST(ContourMeasure, ReversedShiftsAndNegatesBulges) {
    // v0 (0,0) b0=0, v1 (10,0) b1=0.5, v2 (10,10) b2=0 -> reversed keeps v0 first:
    // w = [v0, v2, v1]; bulges w0 = -b2 = 0 (v0->v2), w1 = -b1 = -0.5 (v2->v1), w2 = -b0 = 0.
    const Contour c{{{{0, 0}, 0.0}, {{10, 0}, 0.5}, {{10, 10}, 0.0}}};
    const Contour r = reversed(c);
    ASSERT_EQ(r.size(), 3u);
    EXPECT_EQ(r.vertices[0], (ContourVertex{{0, 0}, 0.0}));
    EXPECT_EQ(r.vertices[1], (ContourVertex{{10, 10}, -0.5}));
    EXPECT_EQ(r.vertices[2], (ContourVertex{{10, 0}, 0.0}));
    EXPECT_NEAR(signedArea(r), -signedArea(c), 1e-12);
    EXPECT_NEAR(perimeter(r), perimeter(c), 1e-12);
}

TEST(ContourMeasure, BoundsIncludeArcExtrema) {
    const double s = 10 * std::sqrt(0.5); // 7.0710678
    // Arc 45 -> 135 deg (r 10 about the origin) crosses 90 deg: top at y = 10, not 7.07.
    Rect2 b = bounds(arcAndChord({s, s}, {-s, s}, kQ));
    EXPECT_NEAR(b.max.y, 10, 1e-12);
    EXPECT_NEAR(b.min.y, s, 1e-12);
    EXPECT_NEAR(b.min.x, -s, 1e-12);
    EXPECT_NEAR(b.max.x, s, 1e-12);
    // Arc -45 -> 45 deg crosses 0 deg (angle wrap-around): right extreme x = 10.
    b = bounds(arcAndChord({s, -s}, {s, s}, kQ));
    EXPECT_NEAR(b.max.x, 10, 1e-12);
    EXPECT_NEAR(b.min.x, s, 1e-12);
    // Clockwise arc 225 -> -45 (= 315) deg through 270: bottom at y = -10.
    b = bounds(arcAndChord({-s, -s}, {s, -s}, kQ));   // CCW 225 -> 315 crosses 270
    EXPECT_NEAR(b.min.y, -10, 1e-12);
    b = bounds(arcAndChord({s, -s}, {-s, -s}, -kQ));  // the same arc traversed clockwise
    EXPECT_NEAR(b.min.y, -10, 1e-12);
    EXPECT_NEAR(b.max.y, -s, 1e-12);
    // Arc 45 -> 135 deg that does NOT cross 180: left extreme stays at -s.
    b = bounds(arcAndChord({s, s}, {-s, s}, kQ));
    EXPECT_NEAR(b.min.x, -s, 1e-12);
}

TEST(ContourMeasure, HalfDiscArea) {
    // Half circle (10,0) -> (-10,0) via (0,10) plus the diameter: the polygon part has 2 vertices
    // (shoelace 0), the circular segment is the half disc pi 10^2 / 2 = 157.0796.
    const Contour d = arcAndChord({10, 0}, {-10, 0}, 1.0);
    ASSERT_TRUE(validateContour(d).ok());
    EXPECT_NEAR(signedArea(d), 50 * kPi, 1e-9);
    EXPECT_NEAR(perimeter(d), 10 * kPi + 20, 1e-9);
}

TEST(ContourLocate, PointsAgainstArcs) {
    const Contour d = arcAndChord({10, 0}, {-10, 0}, 1.0); // half disc above the x axis
    EXPECT_EQ(locatePoint(d, {0, 5}), PointLocation::Inside);
    EXPECT_EQ(locatePoint(d, {0, 10}), PointLocation::OnBoundary);      // top of the arc
    EXPECT_EQ(locatePoint(d, {0, 10.001}), PointLocation::Outside);
    EXPECT_EQ(locatePoint(d, {7, 7}), PointLocation::Inside);           // 49 + 49 = 98 < 100
    EXPECT_EQ(locatePoint(d, {7.1, 7.1}), PointLocation::Outside);      // 2 * 50.41 = 100.82 > 100
    EXPECT_EQ(locatePoint(d, {6, 8}), PointLocation::OnBoundary);       // 36 + 64 = 100
    EXPECT_EQ(locatePoint(d, {0, 0}), PointLocation::OnBoundary);       // on the diameter
    EXPECT_EQ(locatePoint(d, {0, -1}), PointLocation::Outside);
    EXPECT_EQ(locatePoint(d, {-20, 5}), PointLocation::Outside);        // ray crosses the arc twice

    const Contour c = roundedShelf();
    EXPECT_EQ(locatePoint(c, {2, 2}), PointLocation::Outside);   // |(2,2)-(50,50)| = 67.9 > 50
    EXPECT_EQ(locatePoint(c, {20, 20}), PointLocation::Inside);  // |(20,20)-(50,50)| = 42.4 < 50
    const double k = 50 - 50 * std::sqrt(0.5);                    // 14.6447: the arc at 225 deg
    EXPECT_EQ(locatePoint(c, {k, k}), PointLocation::OnBoundary);
    EXPECT_EQ(locatePoint(c, {300, 200}), PointLocation::Inside);
    EXPECT_EQ(locatePoint(c, {599, 399}), PointLocation::Outside);

    // Rays through the circle's split vertices and tangent to its top are counted correctly.
    const Contour o = circleContour({0, 0}, 100);
    EXPECT_EQ(locatePoint(o, {0, 0}), PointLocation::Inside);      // ray through vertex (100,0)
    EXPECT_EQ(locatePoint(o, {-200, 0}), PointLocation::Outside);  // ray through both vertices
    EXPECT_EQ(locatePoint(o, {-200, 100}), PointLocation::Outside); // ray tangent at (0,100)
    EXPECT_EQ(locatePoint(o, {-10, -99}), PointLocation::Inside);  // 100 + 9801 < 10000
    EXPECT_EQ(locatePoint(o, {-50, -99.9}), PointLocation::Outside);
    EXPECT_EQ(locatePoint(reversed(o), {0, 0}), PointLocation::Inside);
}

TEST(ContourValidate, AcceptsValidContours) {
    EXPECT_TRUE(validateContour(rectangleContour(100, 50)).ok());
    EXPECT_TRUE(validateContour(roundedShelf()).ok()); // tangent line/arc continuations
    EXPECT_TRUE(validateContour(circleContour({0, 0}, 5)).ok());
    // Stadium: two lines, two half circles, all junctions tangent.
    const Contour stadium{{{{0, 0}, 0.0}, {{100, 0}, 1.0}, {{100, 40}, 0.0}, {{0, 40}, 1.0}}};
    EXPECT_TRUE(validateContour(stadium).ok());
    EXPECT_NEAR(signedArea(stadium), 4000 + kPi * 400, 1e-9);
    // S-shaped tangent arc-arc continuation: CCW quarter then CW quarter (both r 10).
    const Contour s{{{{0, 0}, 0.0}, {{10, 0}, kQ}, {{20, 10}, -kQ}, {{30, 20}, 0.0}, {{0, 20}, 0.0}}};
    EXPECT_TRUE(validateContour(s).ok());
    // A clockwise contour is valid too.
    EXPECT_TRUE(validateContour(reversed(roundedShelf())).ok());
}

TEST(ContourValidate, RejectsStructuralProblems) {
    EXPECT_FALSE(validateContour(Contour{}).ok());
    EXPECT_FALSE(validateContour(Contour{{{{0, 0}, 0.0}, {{1, 0}, 0.0}}}).ok()); // 2 lines
    const auto dup = validateContour(Contour{{{{0, 0}, 0.0}, {{10, 0}, 0.0}, {{10, 0}, 0.0}, {{0, 10}, 0.0}}});
    ASSERT_FALSE(dup.ok());
    EXPECT_NE(dup.error().message.find("segment 1"), std::string::npos) << dup.error().message;
    EXPECT_NE(dup.error().message.find("zero length"), std::string::npos);
    EXPECT_FALSE(validateContour(Contour{{{{0, 0}, 1e6}, {{10, 0}, 0.0}, {{0, 10}, 0.0}}}).ok());
    EXPECT_FALSE(validateContour(Contour{{{{0, 0}, 0.0}, {{NAN, 0}, 0.0}, {{0, 10}, 0.0}}}).ok());
    // Fold back: (0,0) -> (10,0) -> (5,0): segments 0 and 1 overlap.
    const auto fold = validateContour(Contour{{{{0, 0}, 0.0}, {{10, 0}, 0.0}, {{5, 0}, 0.0}}});
    ASSERT_FALSE(fold.ok());
    EXPECT_NE(fold.error().message.find("overlap"), std::string::npos) << fold.error().message;
}

TEST(ContourValidate, RejectsSelfIntersections) {
    // Figure eight / bow tie: segments 0 and 2 cross at (5,5).
    auto r = validateContour(Contour{{{{0, 0}, 0.0}, {{10, 10}, 0.0}, {{10, 0}, 0.0}, {{0, 10}, 0.0}}});
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("segment 0 and segment 2"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("(5, 5)"), std::string::npos) << r.error().message;

    // Arc crossing a line: the top of a 100 x 50 rectangle is a CW arc with bulge -1.5, sagitta
    // |b| c / 2 = 75 > 50, so it dips below the bottom line (segment 0).
    r = validateContour(Contour{{{{0, 0}, 0.0}, {{100, 0}, 0.0}, {{100, 50}, -1.5}, {{0, 50}, 0.0}}});
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("segment 0 and segment 2"), std::string::npos) << r.error().message;
    // With bulge -1 (sagitta 50) the arc exactly TOUCHES the bottom line at (50, 0).
    r = validateContour(Contour{{{{0, 0}, 0.0}, {{100, 0}, 0.0}, {{100, 50}, -1.0}, {{0, 50}, 0.0}}});
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("touch at (50, 0)"), std::string::npos) << r.error().message;

    // Touching non-adjacent segments: vertex (5,0) of segment 2/3 lies on segment 0.
    r = validateContour(
        Contour{{{{0, 0}, 0.0}, {{10, 0}, 0.0}, {{10, 10}, 0.0}, {{5, 0}, 0.0}, {{0, 10}, 0.0}}});
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("segment 0 and segment 2"), std::string::npos) << r.error().message;

    // An arc that curls back over its neighbour: line (0,0) -> (100,0), then a CCW arc about
    // C = (95,10) (r = sqrt(5^2 + 10^2) = 11.18) starting at (100,0) and sweeping 330 deg. The
    // circle cuts y = 0 at x = 95 +/- 5, so the arc passes (90,0) on segment 0 again.
    const Vec2 cc{95, 10};
    const double rr = std::sqrt(125.0);
    const double a0 = std::atan2(-10.0, 5.0);
    const double a1 = a0 + 330.0 * kPi / 180.0;
    const Vec2 end{cc.x + rr * std::cos(a1), cc.y + rr * std::sin(a1)};
    const double bulge = std::tan(330.0 * kPi / 180.0 / 4.0);
    const Contour curl{{{{0, 0}, 0.0}, {{100, 0}, bulge}, {end, 0.0}}};
    ASSERT_NEAR(segmentInfo(curl, 1).center.x, 95, 1e-9);
    ASSERT_NEAR(segmentInfo(curl, 1).center.y, 10, 1e-9);
    r = validateContour(curl);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("segment 0 and segment 1 meet again at (90, 0)"), std::string::npos)
        << r.error().message;

    // Two half circles of the same circle traversed in the same angular range overlap.
    r = validateContour(Contour{{{{10, 0}, 1.0}, {{-10, 0}, -1.0}}});
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("overlap"), std::string::npos) << r.error().message;
}

TEST(ContourTessellate, ChordToleranceIsGuaranteed) {
    const Contour c = roundedShelf();
    const double tol = 0.01;
    const Polygon2 p = tessellate(c, tol);
    // r 50, tol 0.01: max step 2 acos(1 - 0.0002) = 0.0400007 rad; a quarter (1.5707963 rad)
    // needs ceil(39.27) = 40 chords, i.e. 39 added points per corner: 8 + 4 * 39 = 164 vertices.
    ASSERT_EQ(p.size(), 164u);
    EXPECT_TRUE(isSimple(p));
    EXPECT_GT(signedArea(p), 0); // orientation preserved
    // Original vertices kept exactly and in order.
    std::size_t k = 0;
    for (Vec2 v : p.vertices)
        if (k < c.size() && v == c.vertices[k].point) ++k;
    EXPECT_EQ(k, c.size());
    // Every point of a chord deviates most at its midpoint; measure it against the true contour.
    double maxDev = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const Segment2 e = p.edge(i);
        const Vec2 m = (e.a + e.b) * 0.5;
        double best = 1e9;
        for (std::size_t s = 0; s < c.size(); ++s) best = std::min(best, distancePointContourSegment(m, segmentInfo(c, s)));
        maxDev = std::max(maxDev, best);
    }
    EXPECT_LE(maxDev, tol + 1e-9);
    EXPECT_GT(maxDev, 0.5 * tol); // chords are not needlessly short
    // Polygon area is below the exact area by less than perimeter * tol.
    EXPECT_LT(signedArea(c) - signedArea(p), perimeter(c) * tol);
    EXPECT_GT(signedArea(c) - signedArea(p), 0);
}

TEST(ContourTessellate, CoarseToleranceAndOrientation) {
    // Tolerance larger than the radius: steps are capped at 90 degrees -> a square-ish polygon.
    const Polygon2 p = tessellate(circleContour({0, 0}, 1), 5.0);
    EXPECT_EQ(p.size(), 4u);
    EXPECT_TRUE(isSimple(p));
    const Polygon2 q = tessellate(reversed(circleContour({0, 0}, 10)), 0.1);
    EXPECT_TRUE(isSimple(q));
    EXPECT_LT(signedArea(q), 0);
    // Rectangle: no arcs, polygon = the vertices.
    EXPECT_EQ(tessellate(rectangleContour(3, 2), 0.1), makeRectPolygon(Rect2::fromSize(3, 2)));
}

TEST(ContourBuilders, RoundedRectangleVariants) {
    // Mixed radii: corner (0,0) sharp, others 10, 20, 30 (counter-clockwise from (0,0)).
    const double radii[4] = {0, 10, 20, 30};
    const auto r = roundedRectangleContour(200, 100, radii);
    ASSERT_TRUE(r.ok());
    const Contour& c = r.value();
    ASSERT_EQ(c.size(), 7u); // 4 lines + 3 arcs
    EXPECT_EQ(c.vertices[0].point, (Vec2{0, 0}));
    EXPECT_TRUE(validateContour(c).ok());
    // Area = 20000 - (4 - pi)/4 (10^2 + 20^2 + 30^2) = 20000 - 0.2146018 * 1400 = 19699.56.
    EXPECT_NEAR(signedArea(c), 20000 - (4 - kPi) / 4 * 1400, 1e-9);
    // Radii filling a side exactly drop the zero-length straight: a 100 x 100 square with r 50
    // everywhere is a circle made of 4 quarter arcs.
    const double half[4] = {50, 50, 50, 50};
    const auto circ = roundedRectangleContour(100, 100, half);
    ASSERT_TRUE(circ.ok());
    EXPECT_EQ(circ.value().size(), 4u);
    EXPECT_NEAR(signedArea(circ.value()), kPi * 2500, 1e-9);
    // Radii that do not fit, negative radii, bad sizes.
    const double big[4] = {60, 50, 0, 0};
    const auto bad = roundedRectangleContour(100, 200, big);
    ASSERT_FALSE(bad.ok());
    EXPECT_NE(bad.error().message.find("bottom"), std::string::npos) << bad.error().message;
    const double neg[4] = {-1, 0, 0, 0};
    EXPECT_FALSE(roundedRectangleContour(100, 100, neg).ok());
    const double none[4] = {0, 0, 0, 0};
    EXPECT_FALSE(roundedRectangleContour(0, 100, none).ok());
    EXPECT_EQ(roundedRectangleContour(30, 20, none).value(), rectangleContour(30, 20));
}
