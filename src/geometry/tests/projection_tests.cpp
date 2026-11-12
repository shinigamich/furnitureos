// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/projection.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace fos::geometry;

namespace {

void expectPolygonNear(const Polygon2& actual, const Polygon2& expected, double tol = 1e-9) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i)
        EXPECT_TRUE(approxEqual(actual[i], expected[i], tol))
            << "vertex " << i << ": (" << actual[i].x << ", " << actual[i].y << ") expected (" << expected[i].x
            << ", " << expected[i].y << ")";
}

} // namespace

TEST(Projection, StandardViewsAreRightHandedAndOrthonormal) {
    for (const ViewBasis& v : {frontView(), backView(), leftView(), rightView(), topView(), bottomView()}) {
        EXPECT_TRUE(v.isOrthonormal());
        EXPECT_EQ(cross(v.right, v.up), v.towardViewer);
    }
    EXPECT_EQ(ViewBasis{}, frontView());
    ViewBasis mirrored = frontView();
    mirrored.towardViewer = {0, 1, 0};
    EXPECT_FALSE(mirrored.isOrthonormal());
    ViewBasis scaled = frontView();
    scaled.right = {2, 0, 0};
    EXPECT_FALSE(scaled.isOrthonormal());
}

TEST(Projection, PointsInStandardViews) {
    const Vec3 p{3, 4, 5}; // x = 3 (width), y = 4 (depth), z = 5 (height)
    // Front: picture (x, z); the front (y = 0) is nearest, so depth = -y.
    EXPECT_EQ(project(frontView(), p).position, (Vec2{3, 5}));
    EXPECT_DOUBLE_EQ(project(frontView(), p).depth, -4.0);
    // Back: picture (-x, z), depth = +y.
    EXPECT_EQ(project(backView(), p).position, (Vec2{-3, 5}));
    EXPECT_DOUBLE_EQ(project(backView(), p).depth, 4.0);
    // Left (observer at -X): the cabinet front appears on the right -> picture (-y, z), depth -x.
    EXPECT_EQ(project(leftView(), p).position, (Vec2{-4, 5}));
    EXPECT_DOUBLE_EQ(project(leftView(), p).depth, -3.0);
    // Right: picture (y, z), depth x.
    EXPECT_EQ(project(rightView(), p).position, (Vec2{4, 5}));
    EXPECT_DOUBLE_EQ(project(rightView(), p).depth, 3.0);
    // Top: picture (x, y), depth z. Bottom: picture (x, -y), depth -z.
    EXPECT_EQ(project(topView(), p).position, (Vec2{3, 4}));
    EXPECT_DOUBLE_EQ(project(topView(), p).depth, 5.0);
    EXPECT_EQ(project(bottomView(), p).position, (Vec2{3, -4}));
    EXPECT_DOUBLE_EQ(project(bottomView(), p).depth, -5.0);
}

TEST(Projection, MakeViewBasis) {
    const auto front = makeViewBasis({0, -3, 0}, {0, 0, 10});
    ASSERT_TRUE(front.ok());
    EXPECT_TRUE(approxEqual(front.value().right, frontView().right, 1e-12));
    EXPECT_TRUE(approxEqual(front.value().up, frontView().up, 1e-12));
    EXPECT_TRUE(approxEqual(front.value().towardViewer, frontView().towardViewer, 1e-12));
    // Isometric-style view from the front-left-top corner; up hint +Z.
    // towardViewer = (-1,-1,1)/sqrt(3); right = up x toward = (0,0,1) x (-1,-1,1) = (1,-1,0)/sqrt(2).
    const auto iso = makeViewBasis({-1, -1, 1}, {0, 0, 1});
    ASSERT_TRUE(iso.ok());
    EXPECT_TRUE(iso.value().isOrthonormal());
    EXPECT_TRUE(approxEqual(iso.value().right, Vec3{1 / std::sqrt(2.0), -1 / std::sqrt(2.0), 0}, 1e-12));
    EXPECT_GT(iso.value().up.z, 0.0);
    // Errors: up parallel to the view direction, zero vectors.
    const auto bad = makeViewBasis({0, 0, 1}, {0, 0, -5});
    ASSERT_FALSE(bad.ok());
    EXPECT_EQ(bad.error().code, fos::core::ErrorCode::InvalidArgument);
    EXPECT_FALSE(makeViewBasis({0, 0, 0}, {0, 0, 1}).ok());
    EXPECT_FALSE(makeViewBasis({0, 1, 0}, {0, 0, 0}).ok());
}

TEST(Projection, AxisAlignedBoxSilhouette) {
    // Shelf 600 x 400 x 18 at (10,20,30).
    const Vec3 size{600, 400, 18};
    const Transform at = Transform::translation({10, 20, 30});
    // Front: x in [10,610], z in [30,48].
    expectPolygonNear(projectBoxSilhouette(frontView(), size, at), {{{10, 30}, {610, 30}, {610, 48}, {10, 48}}});
    // Top: x in [10,610], y in [20,420].
    expectPolygonNear(projectBoxSilhouette(topView(), size, at), {{{10, 20}, {610, 20}, {610, 420}, {10, 420}}});
    // Left: picture x = -y in [-420,-20], z in [30,48].
    expectPolygonNear(projectBoxSilhouette(leftView(), size, at),
                      {{{-420, 30}, {-20, 30}, {-20, 48}, {-420, 48}}});
    // Depth in the front view is -y: nearest -20 (front edge), farthest -420.
    const DepthRange d = projectBoxDepthRange(frontView(), size, at);
    EXPECT_DOUBLE_EQ(d.nearest, -20.0);
    EXPECT_DOUBLE_EQ(d.farthest, -420.0);
    const DepthRange dt = projectBoxDepthRange(topView(), size, at);
    EXPECT_DOUBLE_EQ(dt.nearest, 48.0);
    EXPECT_DOUBLE_EQ(dt.farthest, 30.0);
}

TEST(Projection, RotatedBoxSilhouette) {
    // Board 100 x 50 x 20 rotated 30 deg about Z (cos = 0.8660254, sin = 0.5).
    const Vec3 size{100, 50, 20};
    const Transform rot = Transform::rotationZ(30);
    const double c = std::sqrt(3.0) / 2.0, s = 0.5;
    // Top view: the rotated rectangle itself. Corners: (0,0), (100c, 100s) = (86.603, 50),
    // (100c - 50s, 100s + 50c) = (61.603, 93.301), (-50s, 50c) = (-25, 43.301).
    // Hull starts at the lowest x: (-25, 43.301), then counter-clockwise.
    const Polygon2 top = projectBoxSilhouette(topView(), size, rot);
    expectPolygonNear(top, {{{-50 * s, 50 * c}, {0, 0}, {100 * c, 100 * s}, {100 * c - 50 * s, 100 * s + 50 * c}}});
    EXPECT_NEAR(signedArea(top), 5000.0, 1e-9);
    // Front view: x from -25 to 86.603 (width 111.603), z from 0 to 20: a rectangle; the
    // projections of the other vertical edges fall on its top/bottom sides and are dropped.
    const Polygon2 front = projectBoxSilhouette(frontView(), size, rot);
    expectPolygonNear(front, {{{-25, 0}, {100 * c, 0}, {100 * c, 20}, {-25, 20}}});
    // Depth in the front view = -y: nearest 0 (corner at the origin), farthest -(100s + 50c) = -93.301.
    const DepthRange d = projectBoxDepthRange(frontView(), size, rot);
    EXPECT_NEAR(d.nearest, 0.0, 1e-12);
    EXPECT_NEAR(d.farthest, -(100 * s + 50 * c), 1e-9);
}

TEST(Projection, GeneralViewGivesHexagon) {
    // Unit cube seen along its body diagonal: regular hexagon. Projected area of a convex body =
    // sum over the three visible faces of |n . view| = 3 * (1 / sqrt(3)) = sqrt(3).
    const auto view = makeViewBasis({1, 1, 1}, {0, 0, 1});
    ASSERT_TRUE(view.ok());
    const Polygon2 hex = projectBoxSilhouette(view.value(), {1, 1, 1}, Transform::identity());
    ASSERT_EQ(hex.size(), 6u);
    EXPECT_NEAR(signedArea(hex), std::sqrt(3.0), 1e-12);
    EXPECT_TRUE(isConvex(hex));
    // All hexagon corners are sqrt(2/3) from the projected cube centre.
    const Vec2 centre = project(view.value(), {0.5, 0.5, 0.5}).position;
    for (Vec2 v : hex.vertices) EXPECT_NEAR(length(v - centre), std::sqrt(2.0 / 3.0), 1e-12);
    // A 200 x 300 x 400 box in the same view: area = (300*400 + 200*400 + 200*300) / sqrt(3).
    const Polygon2 big = projectBoxSilhouette(view.value(), {200, 300, 400}, Transform::translation({5, 6, 7}));
    EXPECT_EQ(big.size(), 6u);
    EXPECT_NEAR(signedArea(big), 260000.0 / std::sqrt(3.0), 1e-6);
}

TEST(Projection, DegenerateBoxSilhouette) {
    // A zero-thickness panel seen edge-on projects to a line: fewer than 3 vertices.
    const Polygon2 line = projectBoxSilhouette(frontView(), {600, 400, 0}, Transform::identity());
    EXPECT_LT(line.size(), 3u);
    // The same panel from the top is a proper rectangle.
    EXPECT_EQ(projectBoxSilhouette(topView(), {600, 400, 0}, Transform::identity()).size(), 4u);
}
