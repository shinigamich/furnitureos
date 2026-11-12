// SPDX-License-Identifier: AGPL-3.0-only
// White-box tests of the internal shape helpers (src/shapes.hpp).
#include "../src/shapes.hpp"
#include "test_support.hpp"

using namespace fos::nesting;
using namespace fos::nesting::detail;
using namespace fos::nesting::test;

TEST(NestShapes, ConvexPiecesOfLShape) {
    // L of area 187500 (see basic_tests) splits into exactly 2 convex pieces (one reflex vertex).
    const auto L = poly({{0, 0}, {500, 0}, {500, 250}, {250, 250}, {250, 500}, {0, 500}});
    const auto pieces = convexPieces(L);
    ASSERT_EQ(pieces.size(), 2u);
    double a = 0;
    for (const auto& p : pieces) {
        EXPECT_TRUE(fos::geometry::isConvex(p));
        a += fos::geometry::area(p);
    }
    EXPECT_NEAR(a, 187500.0, 1e-6);
    // A U shape (2 reflex vertices) needs 3 convex pieces.
    const auto U = poly({{0, 0}, {300, 0}, {300, 300}, {200, 300}, {200, 100}, {100, 100}, {100, 300}, {0, 300}});
    const auto up = convexPieces(U);
    EXPECT_EQ(up.size(), 3u);
    double au = 0;
    for (const auto& p : up) au += fos::geometry::area(p);
    EXPECT_NEAR(au, 300.0 * 300 - 100.0 * 200, 1e-6);
}

TEST(NestShapes, MinkowskiSumOfSquares) {
    // [0,1]^2 (+) [-1,0]^2 = [-1,1]^2: area 4.
    const auto s = minkowskiSumConvex(rectPoly(1, 1), poly({{-1, -1}, {0, -1}, {0, 0}, {-1, 0}}));
    EXPECT_EQ(s.size(), 4u);
    EXPECT_NEAR(fos::geometry::area(s), 4.0, 1e-12);
    EXPECT_NEAR(fos::geometry::bounds(s).min.x, -1.0, 1e-12);
}

TEST(NestShapes, SymmetricOrientationsAreDeduplicated) {
    Part sq = makePart("sq", rectPoly(500, 500), 1, RotationRule::Quarter);
    EXPECT_EQ(buildShape(sq, {0, 90, 180, 270}).orients.size(), 1u);
    Part r = makePart("r", rectPoly(500, 300), 1, RotationRule::Quarter);
    EXPECT_EQ(buildShape(r, {0, 90, 180, 270}).orients.size(), 2u);
    Part tri = makePart("t", poly({{0, 0}, {1000, 0}, {0, 500}}), 1, RotationRule::Half);
    tri.mirrorAllowed = true;
    // 0, 180, mirrored 0 and mirrored 180 are four different placements of a right triangle.
    EXPECT_EQ(buildShape(tri, {0, 180}).orients.size(), 4u);
}
