// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/validation/obb.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace fos::validation {
namespace {
using geometry::Aabb;
using geometry::Transform;
using geometry::Vec3;

Obb cube(double x0) { return makeObb(Aabb{{x0, 0, 0}, {x0 + 10, 10, 10}}, Transform::identity()); }

TEST(Obb, MakeObbPlacesCentreAndAxes) {
    // Box 0..100 x 0..10 x 0..18 rotated +90 deg about Z, then moved by (5, 0, 0):
    // local centre (50, 5, 9) -> R (−5, 50, 9) -> + t = (0, 50, 9).
    Transform t = Transform::rotationZ(90);
    t.origin = {5, 0, 0};
    const Obb o = makeObb(Aabb{{0, 0, 0}, {100, 10, 18}}, t);
    EXPECT_NEAR(o.center.x, 0, 1e-9);
    EXPECT_NEAR(o.center.y, 50, 1e-9);
    EXPECT_NEAR(o.center.z, 9, 1e-9);
    EXPECT_NEAR(o.half.x, 50, 1e-12);
    EXPECT_NEAR(projectedExtent(o, {0, 1, 0}), 100, 1e-9); // its length now runs along Y
    EXPECT_NEAR(projectedExtent(o, {1, 0, 0}), 10, 1e-9);
}

TEST(Obb, AxisAlignedOverlapTouchAndGap) {
    // [0,10] vs [7,17] along X: overlap 3 (Y and Z overlap 10).
    ObbOverlap r = intersectObb(cube(0), cube(7));
    EXPECT_TRUE(r.intersects);
    EXPECT_NEAR(r.depth, 3, 1e-12);
    EXPECT_NEAR(r.axis.x, 1, 1e-12);
    // Direction is from a to b.
    r = intersectObb(cube(7), cube(0));
    EXPECT_NEAR(r.axis.x, -1, 1e-12);
    // Touching at x = 10: overlap 0 -> no intersection.
    r = intersectObb(cube(0), cube(10));
    EXPECT_FALSE(r.intersects);
    EXPECT_NEAR(r.depth, 0, 1e-12);
    // Gap of 2 mm: overlap -2 on the separating axis.
    r = intersectObb(cube(0), cube(12));
    EXPECT_FALSE(r.intersects);
    EXPECT_NEAR(r.depth, -2, 1e-12);
    // Overlap 0.005 is within a 0.01 tolerance.
    EXPECT_FALSE(intersectObb(cube(0), cube(9.995), 0.01).intersects);
    EXPECT_TRUE(intersectObb(cube(0), cube(9.98), 0.01).intersects);
}

TEST(Obb, RotatedBoxHandDerived) {
    // A: 100 x 100 x 18, axis aligned, centre (50, 50, 9), half (50, 50, 9).
    // B: 100 x 10 x 18 rotated 30 deg about Z: u = (c, s, 0), v = (-s, c, 0), c = cos30, s = 0.5;
    //    half (50, 5, 9); centre = A centre + 70 v.
    // Overlaps (rA + rB - |d|):
    //   A.x: 50 + (50c + 5s) - 70s       = 50 + 45.80127 - 35    = 60.80127
    //   A.y: 50 + (50s + 5c) - 70c       = 50 + 29.33013 - 60.62178 = 18.70835
    //   B.u: (50c + 50s) + 50 - 0        = 118.30127
    //   B.v: (50s + 50c) + 5 - 70        = 25 + 43.30127 + 5 - 70 = 3.30127  <- minimum
    //   Z:   9 + 9 - 0                   = 18; edge-edge axes repeat these directions.
    const double c = std::cos(core::degToRad(30)), s = 0.5;
    const Vec3 u{c, s, 0}, v{-s, c, 0}, z{0, 0, 1};
    const Vec3 centre = Vec3{50, 50, 9} + v * 70;
    const Transform t{u, v, z, centre - u * 50 - v * 5 - z * 9};
    const Obb a = makeObb(Aabb{{0, 0, 0}, {100, 100, 18}}, Transform::identity());
    const Obb b = makeObb(Aabb{{0, 0, 0}, {100, 10, 18}}, t);
    const ObbOverlap r = intersectObb(a, b);
    ASSERT_TRUE(r.intersects);
    EXPECT_NEAR(r.depth, 25.0 * std::sqrt(3.0) - 40.0, 1e-9); // 3.30127
    EXPECT_NEAR(r.axis.x, -0.5, 1e-9);
    EXPECT_NEAR(r.axis.y, c, 1e-9);
    // Moved 4 mm further along v: overlap on B.v becomes -0.69873 -> separated.
    const Transform t2{u, v, z, t.origin + v * 4};
    const ObbOverlap r2 = intersectObb(a, makeObb(Aabb{{0, 0, 0}, {100, 10, 18}}, t2));
    EXPECT_FALSE(r2.intersects);
    EXPECT_NEAR(r2.depth, 25.0 * std::sqrt(3.0) - 44.0, 1e-9);
}

TEST(Obb, EdgeEdgeAxisSeparates) {
    // Two unit cubes (half 0.5). A: centred at the origin, rotated 45 deg about Z; it reaches
    // h = sqrt(0.5) = 0.70711 along +X with its vertical edge. B: rotated 45 deg about Y, centre
    // (d, 0, 0); it reaches h along -X with an edge parallel to Y. Along X (= A.z x B.y, an
    // edge-edge axis) the overlap is 2h - d.
    // Face normals for d = 2h + 0.1 = 1.51421 (c = s = 0.70711):
    //   A (c, s, 0): rA 0.5, rB 0.5 (c^2 + s + s c) = 0.85355, |dist| d c = 1.07071 -> overlap 0.28284 > 0
    //   B (c, 0, -s): symmetric, 0.28284 > 0;  A/B Y and Z normals overlap even more.
    // So ONLY the edge-edge axis X separates (overlap -0.1). For d = 2h - 0.1 the face overlaps
    // are 0.5 + 0.85355 - 0.92929 = 0.42426 and the minimum is X with 0.1.
    const double h = std::sqrt(0.5);
    Transform ta = Transform::rotationZ(45);
    ta.origin = ta.applyVector({-0.5, -0.5, -0.5});
    Transform tb = Transform::rotationY(45);
    tb.origin = Vec3{2 * h + 0.1, 0, 0} + tb.applyVector({-0.5, -0.5, -0.5});
    const Obb a = makeObb(Aabb{{0, 0, 0}, {1, 1, 1}}, ta);
    const Obb b = makeObb(Aabb{{0, 0, 0}, {1, 1, 1}}, tb);
    const ObbOverlap gap = intersectObb(a, b);
    EXPECT_FALSE(gap.intersects);
    EXPECT_NEAR(gap.depth, -0.1, 1e-9);
    EXPECT_NEAR(std::fabs(gap.axis.x), 1, 1e-9);
    tb.origin = tb.origin - Vec3{0.2, 0, 0};
    const ObbOverlap r = intersectObb(a, makeObb(Aabb{{0, 0, 0}, {1, 1, 1}}, tb));
    ASSERT_TRUE(r.intersects);
    EXPECT_NEAR(r.depth, 0.1, 1e-9);
    EXPECT_NEAR(std::fabs(r.axis.x), 1, 1e-9);
}

} // namespace
} // namespace fos::validation
