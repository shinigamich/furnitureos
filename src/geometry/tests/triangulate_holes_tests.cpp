// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/triangulate.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace fos::geometry;

namespace {
double triangleArea(const TriangulationWithHoles& t, const TriangleIndices& tri) {
    const Vec2 a = t.points[tri[0]], b = t.points[tri[1]], c = t.points[tri[2]];
    return 0.5 * cross(b - a, c - a);
}
Polygon2 rect(double x0, double y0, double x1, double y1) {
    return Polygon2{{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}};
}
} // namespace

TEST(TriangulateWithHoles, WorktopWithSinkCoversExactlyTheMaterial) {
    // 1200 x 600 with a 500 x 400 sink at (350,100): area 720000 - 200000 = 520000.
    const auto r = triangulateWithHoles(rect(0, 0, 1200, 600), {rect(350, 100, 850, 500)});
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto& t = r.value();
    // n + 2h - 2 triangles for n points and h holes: 8 + 2 - 2 = 8.
    EXPECT_EQ(t.triangles.size(), 8u);
    double sum = 0;
    for (const auto& tri : t.triangles) {
        const double a = triangleArea(t, tri);
        EXPECT_GT(a, 0.0);
        sum += a;
        // No triangle lies inside the sink: its centroid is outside the hole.
        const Vec2 c = (t.points[tri[0]] + t.points[tri[1]] + t.points[tri[2]]) * (1.0 / 3.0);
        EXPECT_FALSE(c.x > 350 && c.x < 850 && c.y > 100 && c.y < 500);
    }
    EXPECT_NEAR(sum, 520000.0, 1e-6);
    ASSERT_EQ(t.ringStart.size(), 3u);
    EXPECT_EQ(t.ringStart.back(), t.points.size());
}

TEST(TriangulateWithHoles, ClockwiseInputIsNormalisedAndTwoHolesWork) {
    Polygon2 outer = rect(0, 0, 100, 100);
    std::reverse(outer.vertices.begin(), outer.vertices.end()); // clockwise input
    const auto r = triangulateWithHoles(outer, {rect(10, 10, 30, 30), rect(60, 60, 90, 80)});
    ASSERT_TRUE(r.ok()) << r.error().message;
    double sum = 0;
    for (const auto& tri : r.value().triangles) sum += triangleArea(r.value(), tri);
    EXPECT_NEAR(sum, 10000.0 - 400.0 - 600.0, 1e-6);
}

TEST(TriangulateWithHoles, RejectsDegenerateRings) {
    EXPECT_FALSE(triangulateWithHoles(Polygon2{{{0, 0}, {1, 0}}}, {}).ok());
    EXPECT_FALSE(triangulateWithHoles(rect(0, 0, 10, 10), {Polygon2{{{1, 1}, {2, 2}, {3, 3}}}}).ok()); // zero-area hole
}
