// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/triangulate.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <set>

using namespace fos::geometry;

namespace {

/// Checks the invariants of a triangulation and returns the summed triangle area.
double checkedAreaSum(const Polygon2& p, const std::vector<TriangleIndices>& tris) {
    EXPECT_EQ(tris.size(), p.size() - 2);
    double sum = 0.0;
    std::set<std::uint32_t> used;
    for (const TriangleIndices& t : tris) {
        for (std::uint32_t i : t) {
            EXPECT_LT(i, p.size());
            used.insert(i);
        }
        const double a = cross(p[t[1]] - p[t[0]], p[t[2]] - p[t[0]]) * 0.5;
        EXPECT_GT(a, 0.0) << "triangle " << t[0] << "," << t[1] << "," << t[2] << " is not CCW";
        sum += a;
        // No triangle may reach outside the polygon: its centroid must be inside.
        const Vec2 c = (p[t[0]] + p[t[1]] + p[t[2]]) * (1.0 / 3.0);
        EXPECT_EQ(locatePoint(p, c), PointLocation::Inside);
    }
    EXPECT_EQ(used.size(), p.size()) << "every polygon vertex must be a triangle corner";
    return sum;
}

Polygon2 lShape() { return {{{0, 0}, {100, 0}, {100, 40}, {40, 40}, {40, 100}, {0, 100}}}; }

} // namespace

TEST(Triangulate, TriangleAndSquare) {
    const Polygon2 tri{{{0, 0}, {10, 0}, {0, 10}}};
    const auto t = triangulate(tri);
    ASSERT_TRUE(t.ok());
    ASSERT_EQ(t.value().size(), 1u);
    EXPECT_EQ(t.value()[0], (TriangleIndices{0, 1, 2}));

    const Polygon2 square{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    const auto s = triangulate(square);
    ASSERT_TRUE(s.ok());
    EXPECT_DOUBLE_EQ(checkedAreaSum(square, s.value()), 100.0);
    // Deterministic: first ear is at vertex 0 (triangle 3,0,1), the rest is (1,2,3).
    EXPECT_EQ(s.value()[0], (TriangleIndices{3, 0, 1}));
    EXPECT_EQ(s.value()[1], (TriangleIndices{1, 2, 3}));
}

TEST(Triangulate, LShapeAreaSumEqualsPolygonArea) {
    const auto t = triangulate(lShape());
    ASSERT_TRUE(t.ok());
    EXPECT_DOUBLE_EQ(checkedAreaSum(lShape(), t.value()), 6400.0); // 100*100 - 60*60
}

TEST(Triangulate, ClockwiseInputGivesCcwTriangles) {
    const Polygon2 cw = reversed(lShape());
    const auto t = triangulate(cw);
    ASSERT_TRUE(t.ok());
    EXPECT_DOUBLE_EQ(checkedAreaSum(cw, t.value()), 6400.0);
}

TEST(Triangulate, CollinearVerticesAreKept) {
    // Square 10 x 10 with a midpoint on every edge: 8 vertices -> 6 triangles, area 100.
    const Polygon2 p{{{0, 0}, {5, 0}, {10, 0}, {10, 5}, {10, 10}, {5, 10}, {0, 10}, {0, 5}}};
    const auto t = triangulate(p);
    ASSERT_TRUE(t.ok());
    EXPECT_DOUBLE_EQ(checkedAreaSum(p, t.value()), 100.0);
    // Several collinear points in a row on one edge, and on the edges next to a reflex corner.
    const Polygon2 l{{{0, 0}, {25, 0}, {50, 0}, {75, 0}, {100, 0}, {100, 40}, {70, 40},
                      {40, 40}, {40, 70}, {40, 100}, {0, 100}, {0, 50}}};
    const auto tl = triangulate(l);
    ASSERT_TRUE(tl.ok());
    EXPECT_DOUBLE_EQ(checkedAreaSum(l, tl.value()), 6400.0);
}

TEST(Triangulate, ConcaveShapes) {
    // Comb with three teeth (teeth 10 wide, 30 high on a 50 x 10 spine):
    // area = 50*10 + 3 * 10*30 = 1400.
    const Polygon2 comb{{{0, 0}, {50, 0}, {50, 40}, {40, 40}, {40, 10}, {30, 10}, {30, 40}, {20, 40},
                         {20, 10}, {10, 10}, {10, 40}, {0, 40}}};
    ASSERT_TRUE(isSimple(comb));
    const auto t = triangulate(comb);
    ASSERT_TRUE(t.ok());
    EXPECT_DOUBLE_EQ(checkedAreaSum(comb, t.value()), 1400.0);

    // Four-pointed star: outer radius 10 on the axes, inner points (+-2, +-2).
    // Area = 8 triangles (0,0),(10,0),(2,2) of area 10*2/2 = 10 each -> 80.
    const Polygon2 star{{{10, 0}, {2, 2}, {0, 10}, {-2, 2}, {-10, 0}, {-2, -2}, {0, -10}, {2, -2}}};
    const auto ts = triangulate(star);
    ASSERT_TRUE(ts.ok());
    EXPECT_NEAR(checkedAreaSum(star, ts.value()), 80.0, 1e-12);

    // Arc approximated by 32 segments closed through the centre region (a "pac-man"):
    // triangle areas must add up to the shoelace area.
    Polygon2 pac;
    pac.vertices.push_back({0, 0});
    for (int i = 0; i <= 32; ++i) {
        const double a = fos::core::degToRad(20.0 + 320.0 * i / 32.0);
        pac.vertices.push_back({50 * std::cos(a), 50 * std::sin(a)});
    }
    const auto tp = triangulate(pac);
    ASSERT_TRUE(tp.ok());
    EXPECT_NEAR(checkedAreaSum(pac, tp.value()), signedArea(pac), 1e-9);
}

TEST(Triangulate, RejectsNonSimpleInput) {
    const auto bow = triangulate(Polygon2{{{0, 0}, {10, 10}, {10, 0}, {0, 10}}});
    ASSERT_FALSE(bow.ok());
    EXPECT_EQ(bow.error().code, fos::core::ErrorCode::InvalidArgument);
    EXPECT_FALSE(triangulate(Polygon2{}).ok());
    EXPECT_FALSE(triangulate(Polygon2{{{0, 0}, {10, 0}}}).ok());
    EXPECT_FALSE(triangulate(Polygon2{{{0, 0}, {10, 0}, {20, 0}}}).ok());                    // zero area
    EXPECT_FALSE(triangulate(Polygon2{{{0, 0}, {10, 0}, {10, 0}, {10, 10}, {0, 10}}}).ok()); // repeated vertex
    // After cleaning the repeated vertex the polygon triangulates.
    EXPECT_TRUE(triangulate(cleaned(Polygon2{{{0, 0}, {10, 0}, {10, 0}, {10, 10}, {0, 10}}})).ok());
}
