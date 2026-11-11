// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/aabb.hpp"
#include "fos/geometry/transform.hpp"

#include <gtest/gtest.h>

using namespace fos::geometry;

TEST(Vec, CrossIsRightHanded) {
    EXPECT_EQ(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}), (Vec3{0, 0, 1}));
    EXPECT_DOUBLE_EQ(cross(Vec2{1, 0}, Vec2{0, 1}), 1.0);
    EXPECT_DOUBLE_EQ(length(Vec3{3, 4, 12}), 13.0);
}

TEST(Transform, RotationZIsCounterClockwise) {
    const Transform r = Transform::rotationZ(90);
    EXPECT_TRUE(approxEqual(r.applyPoint({1, 0, 0}), Vec3{0, 1, 0}));
    EXPECT_TRUE(approxEqual(r.applyPoint({0, 1, 0}), Vec3{-1, 0, 0}));
    EXPECT_TRUE(r.isRigid());
    EXPECT_TRUE(approxEqual(Transform::rotationX(90).applyPoint({0, 1, 0}), Vec3{0, 0, 1}));
    EXPECT_TRUE(approxEqual(Transform::rotationY(90).applyPoint({0, 0, 1}), Vec3{1, 0, 0}));
}

TEST(Transform, ComposeAndInverse) {
    const Transform a = Transform::translation({10, 20, 30}) * Transform::rotationZ(37);
    const Transform b = Transform::translation({-5, 2, 1}) * Transform::rotationX(-112);
    const Transform ab = a * b;
    const Vec3 p{3, -7, 11};
    EXPECT_TRUE(approxEqual(ab.applyPoint(p), a.applyPoint(b.applyPoint(p)), 1e-9));
    EXPECT_TRUE(approxEqual(ab.inverse().applyPoint(ab.applyPoint(p)), p, 1e-9));
    EXPECT_TRUE(ab.isRigid());
    EXPECT_TRUE(approxEqual(ab * ab.inverse(), Transform::identity(), 1e-9));
}

TEST(Transform, FromAxesRejectsNothingButReportsRigidity) {
    // Vertical side panel: length -> +Z, width -> +Y, thickness -> -X.
    const Transform side = Transform::fromAxes({0, 0, 1}, {0, 1, 0}, {18, 0, 0});
    EXPECT_TRUE(side.isRigid());
    EXPECT_EQ(side.zAxis, (Vec3{-1, 0, 0}));
    Transform skew = side;
    skew.zAxis = {1, 0, 0}; // mirrored
    EXPECT_FALSE(skew.isRigid());
}

TEST(Aabb, TransformAndOverlap) {
    const Aabb panel = Aabb::fromSize({720, 560, 18});
    const Aabb world = panel.transformed(Transform::fromAxes({0, 0, 1}, {0, 1, 0}, {18, 0, 0}));
    EXPECT_TRUE(approxEqual(world.min, Vec3{0, 0, 0}));
    EXPECT_TRUE(approxEqual(world.max, Vec3{18, 560, 720}));
    const Aabb bottom = Aabb::fromSize({764, 560, 18}).transformed(Transform::translation({18, 0, 0}));
    EXPECT_FALSE(world.overlaps(bottom)); // touching faces are not an overlap
    const Aabb bad = Aabb::fromSize({764, 560, 18}).transformed(Transform::translation({15, 0, 0}));
    EXPECT_TRUE(world.overlaps(bad));
    EXPECT_TRUE(Aabb{}.empty());
    EXPECT_FALSE(Aabb{}.overlaps(world));
}
