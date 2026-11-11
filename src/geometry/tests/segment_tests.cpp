// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/segment.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace fos::geometry;
using Kind = SegmentIntersectionKind;

TEST(Segment, OrientationWithTolerance) {
    const Vec2 a{0, 0}, b{10, 0};
    EXPECT_EQ(orientation(a, b, {5, 1}), Orientation::CounterClockwise);
    EXPECT_EQ(orientation(a, b, {5, -1}), Orientation::Clockwise);
    EXPECT_EQ(orientation(a, b, {5, 0}), Orientation::Collinear);
    EXPECT_EQ(orientation(a, b, {25, 0}), Orientation::Collinear); // beyond the segment, still on the line
    // Tolerance is a distance from the line: 0.5e-6 mm is "on", 2e-6 mm is "off" (eps = 1e-6).
    EXPECT_EQ(orientation(a, b, {5, 5e-7}), Orientation::Collinear);
    EXPECT_EQ(orientation(a, b, {5, 2e-6}), Orientation::CounterClockwise);
    EXPECT_EQ(orientation(a, b, {5, -2e-6}), Orientation::Clockwise);
    // The distance does not scale with the segment length: same verdict for a 10 km segment.
    EXPECT_EQ(orientation(a, {1e7, 0}, {5, 2e-6}), Orientation::CounterClockwise);
    EXPECT_EQ(orientation(a, {1e7, 0}, {5, 5e-7}), Orientation::Collinear);
    // Custom tolerance and degenerate base.
    EXPECT_EQ(orientation(a, b, {5, 0.05}, 0.1), Orientation::Collinear);
    EXPECT_EQ(orientation(a, a, {5, 5}), Orientation::Collinear);
}

TEST(Segment, PointDistanceAndClosestPoint) {
    const Segment2 s{{0, 0}, {10, 0}};
    EXPECT_DOUBLE_EQ(distancePointSegment({5, 3}, s), 3.0);
    EXPECT_EQ(closestPointOnSegment({5, 3}, s), (Vec2{5, 0}));
    // Beyond the end: nearest is endpoint (10,0); distance = hypot(3,4) = 5.
    EXPECT_DOUBLE_EQ(distancePointSegment({13, 4}, s), 5.0);
    EXPECT_EQ(closestPointOnSegment({13, 4}, s), (Vec2{10, 0}));
    EXPECT_DOUBLE_EQ(distancePointSegment({-3, -4}, s), 5.0);
    EXPECT_EQ(closestPointOnSegment({-3, -4}, s), (Vec2{0, 0}));
    // Degenerate segment behaves like a point.
    EXPECT_DOUBLE_EQ(distancePointSegment({3, 4}, {{0, 0}, {0, 0}}), 5.0);
    EXPECT_TRUE(pointOnSegment({5, 5e-7}, s));
    EXPECT_FALSE(pointOnSegment({5, 1e-5}, s));
    EXPECT_TRUE(pointOnSegment({10, 0}, s));
    EXPECT_FALSE(pointOnSegment({10.001, 0}, s));
}

TEST(Segment, ProperCrossing) {
    const auto x = intersectSegments({{0, 0}, {10, 10}}, {{0, 10}, {10, 0}});
    ASSERT_EQ(x.kind, Kind::Point);
    EXPECT_TRUE(approxEqual(x.p0, {5, 5}, 1e-12));
    EXPECT_EQ(x.p0, x.p1);
    // Unequal split: y = x/2 meets x = 4 at (4, 2).
    const auto y = intersectSegments({{0, 0}, {10, 5}}, {{4, -10}, {4, 10}});
    ASSERT_EQ(y.kind, Kind::Point);
    EXPECT_TRUE(approxEqual(y.p0, {4, 2}, 1e-12));
}

TEST(Segment, DisjointAndParallel) {
    EXPECT_EQ(intersectSegments({{0, 0}, {10, 0}}, {{0, 1}, {10, 1}}).kind, Kind::None);  // parallel
    EXPECT_EQ(intersectSegments({{0, 0}, {10, 0}}, {{12, -5}, {12, 5}}).kind, Kind::None); // lines cross outside
    EXPECT_EQ(intersectSegments({{0, 0}, {10, 0}}, {{5, 1}, {5, 9}}).kind, Kind::None);    // stops 1 mm short
    EXPECT_EQ(intersectSegments({{0, 0}, {10, 0}}, {{11, 0}, {20, 0}}).kind, Kind::None);  // collinear, apart
}

TEST(Segment, TouchingAtEndpointsReturnsExactPoint) {
    const Segment2 s{{0, 0}, {10, 0}};
    const auto tee = intersectSegments(s, {{5, 0}, {5, 7}}); // T-junction
    ASSERT_EQ(tee.kind, Kind::Point);
    EXPECT_EQ(tee.p0, (Vec2{5, 0}));
    const auto corner = intersectSegments(s, {{10, 0}, {10, 7}}); // shared endpoint
    ASSERT_EQ(corner.kind, Kind::Point);
    EXPECT_EQ(corner.p0, (Vec2{10, 0}));
    const auto abut = intersectSegments(s, {{10, 0}, {20, 0}}); // collinear, end to end
    ASSERT_EQ(abut.kind, Kind::Point);
    EXPECT_EQ(abut.p0, (Vec2{10, 0}));
    const auto abutFront = intersectSegments(s, {{-5, 0}, {0, 0}});
    ASSERT_EQ(abutFront.kind, Kind::Point);
    EXPECT_EQ(abutFront.p0, (Vec2{0, 0}));
}

TEST(Segment, ToleranceOnNearTouch) {
    const Segment2 s{{0, 0}, {10, 0}};
    // Endpoint 0.5e-6 above the segment: touches. 1e-5 above: does not.
    const auto nearHit = intersectSegments(s, {{5, 5e-7}, {5, 3}});
    ASSERT_EQ(nearHit.kind, Kind::Point);
    EXPECT_EQ(nearHit.p0, (Vec2{5, 5e-7}));
    EXPECT_EQ(intersectSegments(s, {{5, 1e-5}, {5, 3}}).kind, Kind::None);
    // With a coarse tolerance of 0.1 mm a 0.05 mm gap closes.
    EXPECT_EQ(intersectSegments(s, {{5, 0.05}, {5, 3}}, 0.1).kind, Kind::Point);
}

TEST(Segment, CollinearOverlap) {
    const Segment2 s{{0, 0}, {10, 0}};
    auto x = intersectSegments(s, {{4, 0}, {15, 0}});
    ASSERT_EQ(x.kind, Kind::Overlap);
    EXPECT_EQ(x.p0, (Vec2{4, 0}));
    EXPECT_EQ(x.p1, (Vec2{10, 0}));
    x = intersectSegments(s, {{15, 0}, {4, 0}}); // direction of s2 does not matter
    ASSERT_EQ(x.kind, Kind::Overlap);
    EXPECT_EQ(x.p0, (Vec2{4, 0}));
    EXPECT_EQ(x.p1, (Vec2{10, 0}));
    x = intersectSegments({{10, 0}, {0, 0}}, {{4, 0}, {15, 0}}); // p0 -> p1 follows s1
    ASSERT_EQ(x.kind, Kind::Overlap);
    EXPECT_EQ(x.p0, (Vec2{10, 0}));
    EXPECT_EQ(x.p1, (Vec2{4, 0}));
    x = intersectSegments(s, {{2, 0}, {3, 0}}); // s2 inside s1
    ASSERT_EQ(x.kind, Kind::Overlap);
    EXPECT_EQ(x.p0, (Vec2{2, 0}));
    EXPECT_EQ(x.p1, (Vec2{3, 0}));
    x = intersectSegments({{2, 0}, {3, 0}}, s); // s1 inside s2
    ASSERT_EQ(x.kind, Kind::Overlap);
    EXPECT_EQ(x.p0, (Vec2{2, 0}));
    EXPECT_EQ(x.p1, (Vec2{3, 0}));
    x = intersectSegments(s, s); // identical
    ASSERT_EQ(x.kind, Kind::Overlap);
    EXPECT_EQ(x.p0, (Vec2{0, 0}));
    EXPECT_EQ(x.p1, (Vec2{10, 0}));
    // 0.5e-6 mm off the line is still collinear; 1e-3 mm off is parallel and apart.
    EXPECT_EQ(intersectSegments(s, {{4, 5e-7}, {15, 5e-7}}).kind, Kind::Overlap);
    EXPECT_EQ(intersectSegments(s, {{4, 1e-3}, {15, 1e-3}}).kind, Kind::None);
    // Diagonal overlap: (0,0)-(4,4) with (1,1)-(9,9) share (1,1)-(4,4).
    x = intersectSegments({{0, 0}, {4, 4}}, {{9, 9}, {1, 1}});
    ASSERT_EQ(x.kind, Kind::Overlap);
    EXPECT_EQ(x.p0, (Vec2{1, 1}));
    EXPECT_EQ(x.p1, (Vec2{4, 4}));
}

TEST(Segment, DegenerateSegments) {
    const Segment2 s{{0, 0}, {10, 0}};
    const Segment2 onIt{{3, 0}, {3, 0}}, offIt{{3, 2}, {3, 2}};
    EXPECT_EQ(intersectSegments(s, onIt).kind, Kind::Point);
    EXPECT_EQ(intersectSegments(onIt, s).p0, (Vec2{3, 0}));
    EXPECT_EQ(intersectSegments(s, offIt).kind, Kind::None);
    EXPECT_EQ(intersectSegments(onIt, onIt).kind, Kind::Point);
    EXPECT_EQ(intersectSegments(onIt, offIt).kind, Kind::None);
}

TEST(Segment, ClosestPointsBetweenSegments) {
    const Segment2 s{{0, 0}, {10, 0}};
    // Endpoint to endpoint: (10,0) and (12,1): distance sqrt(4 + 1) = sqrt(5).
    auto c = closestPointsSegments(s, {{12, 1}, {12, 5}});
    EXPECT_EQ(c.onFirst, (Vec2{10, 0}));
    EXPECT_EQ(c.onSecond, (Vec2{12, 1}));
    EXPECT_DOUBLE_EQ(c.distance, std::sqrt(5.0));
    // Endpoint of the second above the interior of the first: (4,3) -> (4,0), distance 3.
    c = closestPointsSegments(s, {{4, 3}, {8, 9}});
    EXPECT_EQ(c.onFirst, (Vec2{4, 0}));
    EXPECT_EQ(c.onSecond, (Vec2{4, 3}));
    EXPECT_DOUBLE_EQ(c.distance, 3.0);
    // Parallel, 2 mm apart.
    EXPECT_DOUBLE_EQ(closestPointsSegments(s, {{3, 2}, {7, 2}}).distance, 2.0);
    // Crossing: distance exactly 0 at the crossing point (5,0).
    c = closestPointsSegments(s, {{5, -5}, {5, 5}});
    EXPECT_EQ(c.distance, 0.0);
    EXPECT_TRUE(approxEqual(c.onFirst, {5, 0}, 1e-12));
    EXPECT_EQ(c.onFirst, c.onSecond);
    // No tolerance in the distance: a 1e-7 gap is reported as 1e-7.
    EXPECT_NEAR(closestPointsSegments(s, {{5, 1e-7}, {5, 3}}).distance, 1e-7, 1e-15);
}
