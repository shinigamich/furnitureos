// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/viewport/manipulator.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace fos::viewport {
namespace {

using geometry::Vec3;

/// A 800 x 560 x 720 cabinet envelope placed at (1000, 0, 0), rotated 90 deg about Z
/// (assembly X -> world +Y, assembly Y -> world -X).
ResizeTarget rotatedCabinet() {
    ResizeTarget t;
    t.itemId = "cab";
    t.frame = geometry::Transform::rotationZ(90);
    t.frame.origin = {1000, 0, 0};
    t.box = geometry::Aabb::fromSize({800, 560, 720});
    t.handles = {{HandleAxis::X, false, 800, "W"}, {HandleAxis::X, true, 800, "W"},
                 {HandleAxis::Y, true, 560, "D"}, {HandleAxis::Z, false, 720, "H"}};
    return t;
}

TEST(Manipulator, AnchorsAndDirections) {
    const auto t = rotatedCabinet();
    // +X face centre in the assembly frame (800, 280, 360) -> world: origin + 800*(0,1,0) + 280*(-1,0,0)
    //   = (1000 - 280, 800, 360) = (720, 800, 360).
    const Vec3 a = handleAnchor(t, 0);
    EXPECT_NEAR(a.x, 720, 1e-9);
    EXPECT_NEAR(a.y, 800, 1e-9);
    EXPECT_NEAR(a.z, 360, 1e-9);
    // -X face centre (0, 280, 360) -> (720, 0, 360); outward = -(assembly X) = world (0,-1,0).
    EXPECT_NEAR(handleAnchor(t, 1).y, 0, 1e-9);
    const Vec3 d = handleDirection(t, 1);
    EXPECT_NEAR(d.x, 0, 1e-12);
    EXPECT_NEAR(d.y, -1, 1e-12);
    // -Y face (front, y = 0) -> outward = -(assembly Y) = world (+1, 0, 0).
    EXPECT_NEAR(handleDirection(t, 2).x, 1, 1e-12);
    // +Z top face centre (400, 280, 720) -> (720, 400, 720).
    EXPECT_NEAR(handleAnchor(t, 3).z, 720, 1e-9);
}

TEST(Manipulator, ClosestAxisParameterOfPerpendicularRay) {
    // Axis through (0,0,0) along +X. A vertical ray straight down through (37, 5, 100):
    // the closest axis point is (37, 0, 0) -> s = 37 (independent of the 5 mm offset in y).
    const geometry::Ray3 ray{{37, 5, 100}, {0, 0, -1}};
    const auto s = closestAxisParameter(ray, {0, 0, 0}, {1, 0, 0});
    ASSERT_TRUE(s.has_value());
    EXPECT_NEAR(*s, 37, 1e-9);
}

TEST(Manipulator, ClosestAxisParameterOfObliqueRay) {
    // Ray from (0, -100, 50) towards (1, 1, 0)/sqrt2 (unnormalised direction (2, 2, 0) is fine);
    // axis = x-axis. Hand computation: d = (2,2,0), u = (1,0,0), w = (0,-100,50);
    // a = 8, b = 2, c = 1, p = d.w = -200, q = u.w = 0;
    // s = (a q - b p) / (a c - b^2) = (0 + 400) / (8 - 4) = 100.
    // Check: the ray reaches y = 0 at x = 100, directly above (100, 0, 0).
    const auto s = closestAxisParameter({{0, -100, 50}, {2, 2, 0}}, {0, 0, 0}, {1, 0, 0});
    ASSERT_TRUE(s.has_value());
    EXPECT_NEAR(*s, 100, 1e-9);
}

TEST(Manipulator, ParallelRayHasNoParameter) {
    EXPECT_FALSE(closestAxisParameter({{0, 5, 5}, {-3, 0, 0}}, {0, 0, 0}, {1, 0, 0}).has_value());
    EXPECT_FALSE(closestAxisParameter({{0, 5, 5}, {0, 0, 0}}, {0, 0, 0}, {1, 0, 0}).has_value());
}

TEST(Manipulator, AxisDragFromTwoRaysGivesOffset) {
    // +X handle of the rotated cabinet: anchor (720, 800, 360), outward world +Y.
    // Two vertical rays (as from a top view) through (720, 800, *) and (733, 912.4, *):
    // the axis parameters are 0 and 112.4 -> 800 + 112.4 = 912.4, snapped to 1 mm -> 912.
    const auto t = rotatedCabinet();
    const Vec3 a = handleAnchor(t, 0), u = handleDirection(t, 0);
    const auto s0 = closestAxisParameter({{720, 800, 2000}, {0, 0, -1}}, a, u);
    const auto s1 = closestAxisParameter({{733, 912.4, 2000}, {0, 0, -1}}, a, u);
    ASSERT_TRUE(s0 && s1);
    EXPECT_NEAR(*s0, 0, 1e-9);
    EXPECT_NEAR(*s1, 112.4, 1e-9);
    EXPECT_DOUBLE_EQ(dragValue(800, *s0, *s1, 1.0, 1.0), 912);
    EXPECT_DOUBLE_EQ(dragValue(800, *s0, *s1, 10.0, 10.0), 910); // Shift: 912.4 -> 910

    // -X handle: outward is world -Y; moving the cursor to y = -95.6 (95.6 mm outwards)
    // enlarges: 800 + 95.6 = 895.6 -> 896.
    const Vec3 b = handleAnchor(t, 1), v = handleDirection(t, 1);
    const auto r0 = closestAxisParameter({{720, 0, 2000}, {0, 0, -1}}, b, v);
    const auto r1 = closestAxisParameter({{720, -95.6, 2000}, {0, 0, -1}}, b, v);
    ASSERT_TRUE(r0 && r1);
    EXPECT_DOUBLE_EQ(dragValue(800, *r0, *r1, 1.0, 1.0), 896);
    // Dragging far inwards stops at the minimum.
    EXPECT_DOUBLE_EQ(dragValue(800, 0, -5000, 1.0, 1.0), 1);
}

TEST(Manipulator, Snapping) {
    EXPECT_DOUBLE_EQ(snapToStep(912.4, 1), 912);
    EXPECT_DOUBLE_EQ(snapToStep(912.5, 1), 913);   // halves away from zero
    EXPECT_DOUBLE_EQ(snapToStep(915, 10), 920);
    EXPECT_DOUBLE_EQ(snapToStep(914.9, 10), 910);
    EXPECT_DOUBLE_EQ(snapToStep(912.4, 0), 912.4); // no snapping
    EXPECT_DOUBLE_EQ(snapToStep(912.4, -1), 912.4);
    EXPECT_DOUBLE_EQ(snapToStep(-12.6, 5), -15);
}

TEST(Manipulator, GhostBoxKeepsOppositeFace) {
    const auto t = rotatedCabinet();
    const auto plus = ghostBox(t, 0, 900); // +X: max.x = 0 + 900
    EXPECT_DOUBLE_EQ(plus.min.x, 0);
    EXPECT_DOUBLE_EQ(plus.max.x, 900);
    const auto minus = ghostBox(t, 1, 900); // -X: min.x = 800 - 900 = -100
    EXPECT_DOUBLE_EQ(minus.min.x, -100);
    EXPECT_DOUBLE_EQ(minus.max.x, 800);
    const auto front = ghostBox(t, 2, 600); // -Y: min.y = 560 - 600 = -40
    EXPECT_DOUBLE_EQ(front.min.y, -40);
    EXPECT_DOUBLE_EQ(front.max.z, 720);
}

TEST(Manipulator, HandleAtPixel) {
    ResizeTarget t;
    t.box = geometry::Aabb::fromSize({800, 560, 720});
    t.handles = {{HandleAxis::X, false, 800, "W"}, {HandleAxis::Z, false, 720, "H"}};
    Camera cam;
    cam.setViewportSize(800, 600);
    cam.setPreset(Camera::Preset::Front);
    cam.fit(t.box);
    const auto px = cam.projectToPixel(handleAnchor(t, 0));
    ASSERT_TRUE(px.has_value());
    EXPECT_EQ(handleAtPixel(t, cam, px->x + 3, px->y - 2, 10), std::optional<std::size_t>(0));
    EXPECT_FALSE(handleAtPixel(t, cam, px->x + 30, px->y, 10).has_value());
    EXPECT_FALSE(handleAtPixel(ResizeTarget{}, cam, px->x, px->y, 10).has_value());
}

} // namespace
} // namespace fos::viewport
