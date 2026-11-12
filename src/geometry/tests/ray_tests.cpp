// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/ray.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace fos::geometry;

TEST(RayAabb, SlabRanges) {
    const Aabb box{{0, 0, 0}, {10, 20, 30}};
    // Along +X from x = -5: enters at x = 0 (t = 5), leaves at x = 10 (t = 15).
    auto r = intersectRayAabb({{-5, 5, 5}, {1, 0, 0}}, box);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->tMin, 5.0);
    EXPECT_DOUBLE_EQ(r->tMax, 15.0);
    // Origin inside: tMin clamps to 0, exit at x = 10 (t = 5).
    r = intersectRayAabb({{5, 5, 5}, {1, 0, 0}}, box);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->tMin, 0.0);
    EXPECT_DOUBLE_EQ(r->tMax, 5.0);
    // Diagonal in XY from (-10,-10,15): x in [0,10] for t in [10,20], y in [0,20] for t in [10,30].
    r = intersectRayAabb({{-10, -10, 15}, {1, 1, 0}}, box);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->tMin, 10.0);
    EXPECT_DOUBLE_EQ(r->tMax, 20.0);
    // t is in units of the direction length: direction (2,0,0) halves the parameters.
    r = intersectRayAabb({{-5, 5, 5}, {2, 0, 0}}, box);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->tMin, 2.5);
    EXPECT_DOUBLE_EQ(r->tMax, 7.5);
    // Negative direction components: from (15,25,35) towards (-1,-1,-1): x in box for t in [5,15],
    // y for [5,25], z for [5,35] -> [5,15].
    r = intersectRayAabb({{15, 25, 35}, {-1, -1, -1}}, box);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->tMin, 5.0);
    EXPECT_DOUBLE_EQ(r->tMax, 15.0);
}

TEST(RayAabb, MissesAndDegenerateCases) {
    const Aabb box{{0, 0, 0}, {10, 20, 30}};
    EXPECT_FALSE(intersectRayAabb({{-5, 25, 5}, {1, 0, 0}}, box).has_value());  // passes beside (y = 25)
    EXPECT_FALSE(intersectRayAabb({{-5, 5, 5}, {-1, 0, 0}}, box).has_value());  // points away
    EXPECT_FALSE(intersectRayAabb({{-5, 5, 5}, {0, 0, 0}}, box).has_value());   // no direction
    EXPECT_FALSE(intersectRayAabb({{-5, 5, 5}, {1, 0, 0}}, Aabb{}).has_value()); // empty box
    EXPECT_FALSE(intersectRayAabb({{-5, 5, 5}, {std::nan(""), 0, 0}}, box).has_value());
    // Grazing along the face y = 20 counts (boundary inclusive).
    const auto graze = intersectRayAabb({{-5, 20, 5}, {1, 0, 0}}, box);
    ASSERT_TRUE(graze.has_value());
    EXPECT_DOUBLE_EQ(graze->tMin, 5.0);
    // Origin on the face x = 10 looking outwards: touches at t = 0 only.
    const auto leaving = intersectRayAabb({{10, 5, 5}, {1, 0, 0}}, box);
    ASSERT_TRUE(leaving.has_value());
    EXPECT_DOUBLE_EQ(leaving->tMin, 0.0);
    EXPECT_DOUBLE_EQ(leaving->tMax, 0.0);
}

TEST(RayTriangle, HitWithBarycentrics) {
    const Vec3 a{0, 0, 0}, b{10, 0, 0}, c{0, 10, 0}; // CCW seen from +Z
    // Straight down from z = 7 onto (2,3,0): t = 7, u = 2/10, v = 3/10.
    auto h = intersectRayTriangle({{2, 3, 7}, {0, 0, -1}}, a, b, c);
    ASSERT_TRUE(h.has_value());
    EXPECT_DOUBLE_EQ(h->t, 7.0);
    EXPECT_DOUBLE_EQ(h->u, 0.2);
    EXPECT_DOUBLE_EQ(h->v, 0.3);
    EXPECT_TRUE(h->frontFace);
    // From below: back face, t = 4; culled when requested.
    h = intersectRayTriangle({{2, 3, -4}, {0, 0, 1}}, a, b, c);
    ASSERT_TRUE(h.has_value());
    EXPECT_DOUBLE_EQ(h->t, 4.0);
    EXPECT_FALSE(h->frontFace);
    EXPECT_FALSE(intersectRayTriangle({{2, 3, -4}, {0, 0, 1}}, a, b, c, true).has_value());
    EXPECT_TRUE(intersectRayTriangle({{2, 3, 7}, {0, 0, -1}}, a, b, c, true).has_value());
    // Oblique, not normalised: along (3,4,-8) from (2,3,8) the plane z = 0 is reached at t = 1
    // in (5,7,0): u + v = 1.2 > 1 -> outside. Along (1,1,-8): (3,4,0) at t = 1 -> u = 0.3, v = 0.4.
    EXPECT_FALSE(intersectRayTriangle({{2, 3, 8}, {3, 4, -8}}, a, b, c).has_value());
    h = intersectRayTriangle({{2, 3, 8}, {1, 1, -8}}, a, b, c);
    ASSERT_TRUE(h.has_value());
    EXPECT_NEAR(h->t, 1.0, 1e-12);
    EXPECT_NEAR(h->u, 0.3, 1e-12);
    EXPECT_NEAR(h->v, 0.4, 1e-12);
}

TEST(RayTriangle, MissesAndEdges) {
    const Vec3 a{0, 0, 0}, b{10, 0, 0}, c{0, 10, 0};
    EXPECT_FALSE(intersectRayTriangle({{8, 8, 7}, {0, 0, -1}}, a, b, c).has_value());  // beyond the hypotenuse
    EXPECT_FALSE(intersectRayTriangle({{-1, 3, 7}, {0, 0, -1}}, a, b, c).has_value()); // u < 0
    EXPECT_FALSE(intersectRayTriangle({{2, 3, 7}, {0, 0, 1}}, a, b, c).has_value());   // triangle is behind
    EXPECT_FALSE(intersectRayTriangle({{2, 3, 7}, {1, 0, 0}}, a, b, c).has_value());   // parallel to the plane
    EXPECT_FALSE(intersectRayTriangle({{-5, 3, 0}, {1, 0, 0}}, a, b, c).has_value());  // in the plane: no hit
    EXPECT_FALSE(intersectRayTriangle({{2, 3, 7}, {0, 0, 0}}, a, b, c).has_value());
    EXPECT_FALSE(intersectRayTriangle({{2, 3, 7}, {0, 0, -1}}, a, a, c).has_value()); // degenerate triangle
    // Exactly on an edge and on a vertex: inclusive.
    EXPECT_TRUE(intersectRayTriangle({{5, 0, 7}, {0, 0, -1}}, a, b, c).has_value());
    EXPECT_TRUE(intersectRayTriangle({{5, 5, 7}, {0, 0, -1}}, a, b, c).has_value());
    EXPECT_TRUE(intersectRayTriangle({{0, 0, 7}, {0, 0, -1}}, a, b, c).has_value());
    // Origin on the triangle: t = 0.
    const auto h = intersectRayTriangle({{2, 3, 0}, {0, 0, -1}}, a, b, c);
    ASSERT_TRUE(h.has_value());
    EXPECT_DOUBLE_EQ(h->t, 0.0);
}

TEST(RayMesh, NearestHitOnBox) {
    const TriangleMesh box = makeBox({10, 20, 30});
    // From above at (7,10): top face z = 30 is 70 below z = 100. Top face (ZMax) owns triangles
    // 0 = (0,0),(10,0),(10,20) and 1 = (0,0),(10,20),(0,20); (7,10) is under the diagonal
    // y = 2x (10 < 14) -> triangle 0.
    auto h = intersectRayMesh({{7, 10, 100}, {0, 0, -1}}, box);
    ASSERT_TRUE(h.has_value());
    EXPECT_DOUBLE_EQ(h->t, 70.0);
    EXPECT_EQ(h->triangle, 0u);
    EXPECT_TRUE(approxEqual(h->point, Vec3{7, 10, 30}, 1e-12));
    EXPECT_EQ(h->normal, (Vec3{0, 0, 1}));
    EXPECT_TRUE(h->frontFace);
    // (2,15) is above the diagonal (15 > 4) -> triangle 1.
    h = intersectRayMesh({{2, 15, 100}, {0, 0, -1}}, box);
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(h->triangle, 1u);
    // From the +X side: XMax face is face 5 -> triangles 10/11, t = 50 - 10 = 40.
    h = intersectRayMesh({{50, 5, 5}, {-1, 0, 0}}, box);
    ASSERT_TRUE(h.has_value());
    EXPECT_DOUBLE_EQ(h->t, 40.0);
    EXPECT_EQ(h->triangle / 2, static_cast<std::uint32_t>(BoxFace::XMax));
    EXPECT_EQ(h->normal, (Vec3{1, 0, 0}));
    // From inside: the far wall is hit from behind (t = 15 down to z = 0, face ZMin = 1).
    h = intersectRayMesh({{7, 10, 15}, {0, 0, -1}}, box);
    ASSERT_TRUE(h.has_value());
    EXPECT_DOUBLE_EQ(h->t, 15.0);
    EXPECT_EQ(h->triangle / 2, static_cast<std::uint32_t>(BoxFace::ZMin));
    EXPECT_FALSE(h->frontFace);
    EXPECT_FALSE(intersectRayMesh({{7, 10, 15}, {0, 0, -1}}, box, true).has_value());
    // Misses.
    EXPECT_FALSE(intersectRayMesh({{50, 50, 100}, {0, 0, -1}}, box).has_value());
    EXPECT_FALSE(intersectRayMesh({{7, 10, 100}, {0, 0, 1}}, box).has_value());
    EXPECT_FALSE(intersectRayMesh({{7, 10, 100}, {0, 0, -1}}, TriangleMesh{}).has_value());
    // A ray through a box edge / along a triangle diagonal is not lost.
    EXPECT_TRUE(intersectRayMesh({{5, 10, 100}, {0, 0, -1}}, box).has_value());  // on the top diagonal
    EXPECT_TRUE(intersectRayMesh({{10, 10, 100}, {0, 0, -1}}, box).has_value()); // along the face x = 10
}

TEST(RayMesh, TransformedPrism) {
    // L-shaped prism 18 thick, stood up so that its thickness runs along -X (part z -> -X), at x = 18.
    const Polygon2 l{{{0, 0}, {100, 0}, {100, 40}, {40, 40}, {40, 100}, {0, 100}}};
    const auto prism = makePrism(l, 18.0);
    ASSERT_TRUE(prism.ok());
    const Transform placement = Transform::fromAxes({0, 0, 1}, {0, 1, 0}, {18, 0, 0});
    const TriangleMesh world = transformed(prism.value(), placement);
    // World point = (18 - z, y, x). Looking along +X at world (y, z) = (20, 20): inside the L
    // (local x = 20, y = 20); first surface is the top cap (local z = 18) at world x = 0 -> t = 50.
    auto h = intersectRayMesh({{-50, 20, 20}, {1, 0, 0}}, world);
    ASSERT_TRUE(h.has_value());
    EXPECT_NEAR(h->t, 50.0, 1e-9);
    EXPECT_TRUE(approxEqual(h->normal, Vec3{-1, 0, 0}, 1e-12));
    // At world (y, z) = (70, 70) -> local (70, 70): in the notch, the ray passes through.
    EXPECT_FALSE(intersectRayMesh({{-50, 70, 70}, {1, 0, 0}}, world).has_value());
    // Looking down -Z at world (x, y) = (9, 70): local y = 70, mid thickness. Coming from
    // z = 200 the ray first meets the notch wall local x = 40 (world z = 40): t = 160.
    h = intersectRayMesh({{9, 70, 200}, {0, 0, -1}}, world);
    ASSERT_TRUE(h.has_value());
    EXPECT_NEAR(h->t, 160.0, 1e-9);
    EXPECT_TRUE(approxEqual(h->normal, Vec3{0, 0, 1}, 1e-12));
}

TEST(RayOrientedBox, FaceIndexAndExactT) {
    // Box 10 x 20 x 30 rotated 90 deg about Z and moved to x = 100:
    // local (x, y, z) -> world (100 - y, x, z): occupies [80,100] x [0,10] x [0,30].
    const Vec3 size{10, 20, 30};
    const Transform placement = Transform::translation({100, 0, 0}) * Transform::rotationZ(90);
    // Along +Y from y = -50: hits world y = 0 at t = 50, which is the local face x = 0 (XMin).
    // World (90,0,15) -> local (0, 100 - 90, 15) = (0,10,15).
    auto h = intersectRayOrientedBox({{90, -50, 15}, {0, 1, 0}}, size, placement);
    ASSERT_TRUE(h.has_value());
    EXPECT_NEAR(h->t, 50.0, 1e-9);
    EXPECT_EQ(h->face, BoxFace::XMin);
    EXPECT_TRUE(approxEqual(h->point, Vec3{90, 0, 15}, 1e-9));
    EXPECT_TRUE(approxEqual(h->localPoint, Vec3{0, 10, 15}, 1e-9));
    EXPECT_EQ(h->localPoint.x, 0.0); // snapped onto the face
    EXPECT_TRUE(approxEqual(h->normal, Vec3{0, -1, 0}, 1e-12));
    // Non-unit direction: t halves.
    h = intersectRayOrientedBox({{90, -50, 15}, {0, 2, 0}}, size, placement);
    ASSERT_TRUE(h.has_value());
    EXPECT_NEAR(h->t, 25.0, 1e-9);
    // From above: top face (ZMax, part face A) at z = 30: t = 70; world (90,5,30) -> local (5,10,30).
    h = intersectRayOrientedBox({{90, 5, 100}, {0, 0, -1}}, size, placement);
    ASSERT_TRUE(h.has_value());
    EXPECT_NEAR(h->t, 70.0, 1e-9);
    EXPECT_EQ(h->face, BoxFace::ZMax);
    EXPECT_TRUE(approxEqual(h->localPoint, Vec3{5, 10, 30}, 1e-9));
    EXPECT_TRUE(approxEqual(h->normal, Vec3{0, 0, 1}, 1e-12));
    // Along -X from x = 150: world x = 100 is local y = 0 (YMin): t = 50.
    h = intersectRayOrientedBox({{150, 5, 15}, {-1, 0, 0}}, size, placement);
    ASSERT_TRUE(h.has_value());
    EXPECT_NEAR(h->t, 50.0, 1e-9);
    EXPECT_EQ(h->face, BoxFace::YMin);
    EXPECT_TRUE(approxEqual(h->normal, Vec3{1, 0, 0}, 1e-12));
    // Along +X from x = 0: world x = 80 is local y = 20 (YMax): t = 80.
    h = intersectRayOrientedBox({{0, 5, 15}, {1, 0, 0}}, size, placement);
    ASSERT_TRUE(h.has_value());
    EXPECT_NEAR(h->t, 80.0, 1e-9);
    EXPECT_EQ(h->face, BoxFace::YMax);
    // Origin inside, looking +X: leaves through world x = 100 (YMin) after 10.
    h = intersectRayOrientedBox({{90, 5, 15}, {1, 0, 0}}, size, placement);
    ASSERT_TRUE(h.has_value());
    EXPECT_NEAR(h->t, 10.0, 1e-9);
    EXPECT_EQ(h->face, BoxFace::YMin);
    // Misses and invalid input.
    EXPECT_FALSE(intersectRayOrientedBox({{70, -50, 15}, {0, 1, 0}}, size, placement).has_value());
    EXPECT_FALSE(intersectRayOrientedBox({{90, -50, 15}, {0, -1, 0}}, size, placement).has_value());
    EXPECT_FALSE(intersectRayOrientedBox({{90, -50, 15}, {0, 0, 0}}, size, placement).has_value());
    EXPECT_FALSE(intersectRayOrientedBox({{90, -50, 15}, {0, 1, 0}}, {10, 0, 30}, placement).has_value());
}

TEST(RayOrientedBox, AgreesWithMeshAndAabb) {
    // Identity placement: the three tests must agree. Oblique ray from (-20,-10,50) along
    // (2,1.5,-2): x = 0 and z = 30 are both reached at t = 10 in (0, 5, 30), a point on the
    // box edge between the faces XMin and ZMax.
    const Vec3 size{10, 20, 30};
    const Ray3 ray{{-20, -10, 50}, {2, 1.5, -2}};
    const auto boxHit = intersectRayOrientedBox(ray, size, Transform::identity());
    const auto meshHit = intersectRayMesh(ray, makeBox(size));
    const auto range = intersectRayAabb(ray, Aabb::fromSize(size));
    ASSERT_TRUE(boxHit.has_value());
    ASSERT_TRUE(meshHit.has_value());
    ASSERT_TRUE(range.has_value());
    EXPECT_DOUBLE_EQ(boxHit->t, 10.0);
    EXPECT_NEAR(meshHit->t, 10.0, 1e-9);
    EXPECT_DOUBLE_EQ(range->tMin, 10.0);
    // x and z slabs are entered at the same t: the lower axis (X) names the face.
    EXPECT_EQ(boxHit->face, BoxFace::XMin);
    // A clean face hit: from (-20,5,10) along (4,1,1): x = 0 at t = 5 -> (0, 10, 15).
    const Ray3 clean{{-20, 5, 10}, {4, 1, 1}};
    const auto b2 = intersectRayOrientedBox(clean, size, Transform::identity());
    const auto m2 = intersectRayMesh(clean, makeBox(size));
    ASSERT_TRUE(b2.has_value());
    ASSERT_TRUE(m2.has_value());
    EXPECT_DOUBLE_EQ(b2->t, 5.0);
    EXPECT_NEAR(m2->t, 5.0, 1e-12);
    EXPECT_EQ(b2->face, BoxFace::XMin);
    EXPECT_EQ(m2->triangle / 2, static_cast<std::uint32_t>(BoxFace::XMin));
    EXPECT_TRUE(approxEqual(b2->point, Vec3{0, 10, 15}, 1e-12));
}
