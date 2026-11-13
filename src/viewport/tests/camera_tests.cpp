// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/viewport/camera.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace fos;
using geometry::Vec3;
using viewport::Camera;
using viewport::CameraProjection;

namespace {
void expectVec(Vec3 a, Vec3 b, double eps = 1e-9) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

// Applies proj * view (column-major) to a point and maps NDC to pixels exactly like OpenGL
// with glViewport(0, 0, w, h) and a top-left pixel origin.
geometry::Vec2 glPixel(const Camera& c, Vec3 p) {
    const auto v = c.viewMatrix(), pr = c.projectionMatrix();
    double e[4], clip[4];
    for (int r = 0; r < 4; ++r)
        e[r] = v[0 * 4 + r] * p.x + v[1 * 4 + r] * p.y + v[2 * 4 + r] * p.z + v[3 * 4 + r];
    for (int r = 0; r < 4; ++r)
        clip[r] = pr[0 * 4 + r] * e[0] + pr[1 * 4 + r] * e[1] + pr[2 * 4 + r] * e[2] + pr[3 * 4 + r] * e[3];
    const double nx = clip[0] / clip[3], ny = clip[1] / clip[3];
    return {(nx + 1) * 0.5 * c.viewportWidth(), (1 - ny) * 0.5 * c.viewportHeight()};
}
} // namespace

TEST(Camera, PresetsGiveExpectedViewDirections) {
    Camera c;
    const double s = 1 / std::sqrt(3.0);
    struct Case {
        Camera::Preset preset;
        Vec3 dir, up;
    } cases[] = {
        {Camera::Preset::Front, {0, 1, 0}, {0, 0, 1}},   // from y < 0 towards the front at y = 0
        {Camera::Preset::Back, {0, -1, 0}, {0, 0, 1}},
        {Camera::Preset::Left, {1, 0, 0}, {0, 0, 1}},    // observer at -X
        {Camera::Preset::Right, {-1, 0, 0}, {0, 0, 1}},
        {Camera::Preset::Top, {0, 0, -1}, {0, 1, 0}},    // front (y = 0) at the bottom of the picture
        {Camera::Preset::Bottom, {0, 0, 1}, {0, -1, 0}},
        {Camera::Preset::Isometric, {-s, s, -s}, {0, 0, 0}}, // up checked below
    };
    for (const auto& k : cases) {
        c.setPreset(k.preset);
        expectVec(c.viewDirection(), k.dir, 1e-12);
        EXPECT_TRUE(c.basis().isOrthonormal());
        if (k.preset != Camera::Preset::Isometric) expectVec(c.basis().up, k.up, 1e-12);
    }
    // Isometric: Z axis points up on screen (up . Z > 0) and right is horizontal (right.z == 0).
    c.setPreset(Camera::Preset::Isometric);
    EXPECT_GT(c.basis().up.z, 0.8);
    EXPECT_NEAR(c.basis().right.z, 0.0, 1e-12);
    // The fixed presets coincide with the drawing views of fos::geometry.
    c.setPreset(Camera::Preset::Front);
    expectVec(c.basis().right, geometry::frontView().right, 1e-12);
    expectVec(c.basis().towardViewer, geometry::frontView().towardViewer, 1e-12);
    c.setPreset(Camera::Preset::Left);
    expectVec(c.basis().right, geometry::leftView().right, 1e-12);
    c.setPreset(Camera::Preset::Top);
    expectVec(c.basis().right, geometry::topView().right, 1e-12);
    expectVec(c.basis().up, geometry::topView().up, 1e-12);
}

TEST(Camera, ProjectsKnownPointToPixels) {
    Camera c;
    c.setPreset(Camera::Preset::Front);
    c.setTarget({0, 0, 0});
    c.setDistance(1000);
    c.setViewportSize(800, 600);
    // Half height at target = 1000 * tan(15 deg) = 267.949192; half width = * 800/600 = 357.265590.
    // Point (100, 0, 50) lies in the target plane: ndc = (100/357.265590, 50/267.949192)
    //   = (0.279904, 0.186603) -> px = 1.279904 * 400 = 511.9615, py = (1 - 0.186603) * 300 = 244.0192.
    for (auto proj : {CameraProjection::Orthographic, CameraProjection::Perspective}) {
        c.setProjection(proj);
        auto p = c.projectToPixel({100, 0, 50});
        ASSERT_TRUE(p);
        EXPECT_NEAR(p->x, 511.9615, 1e-3);
        EXPECT_NEAR(p->y, 244.0192, 1e-3);
    }
    // Perspective, point 500 mm behind the target plane: eye depth 1500.
    //   ndc x = 100 / (1500 * 0.267949 * 4/3) = 0.186603 -> px = 474.641
    //   ndc y =  50 / (1500 * 0.267949)       = 0.124402 -> py = 262.679
    c.setProjection(CameraProjection::Perspective);
    auto p = c.projectToPixel({100, 500, 50});
    ASSERT_TRUE(p);
    EXPECT_NEAR(p->x, 474.641, 1e-3);
    EXPECT_NEAR(p->y, 262.679, 1e-3);
    // Behind the eye (eye at y = -1000): not projectable in perspective.
    EXPECT_FALSE(c.projectToPixel({0, -1500, 0}));
}

TEST(Camera, GlMatricesAgreeWithPixelProjectionAndRays) {
    Camera c;
    c.setViewportSize(1280, 800);
    c.setSceneBounds({{-1000, -1000, 0}, {2000, 2000, 1000}});
    c.setOrientation(30, 25);
    c.setTarget({400, 280, 360});
    c.setDistance(2500);
    const Vec3 pts[] = {{0, 0, 0}, {800, 563, 720}, {123, 456, 78}};
    for (auto proj : {CameraProjection::Perspective, CameraProjection::Orthographic}) {
        c.setProjection(proj);
        for (Vec3 p : pts) {
            const auto a = c.projectToPixel(p);
            ASSERT_TRUE(a);
            const auto b = glPixel(c, p);
            EXPECT_NEAR(a->x, b.x, 1e-2);
            EXPECT_NEAR(a->y, b.y, 1e-2);
            // The picking ray through that pixel passes through the point.
            const auto ray = c.rayThroughPixel(a->x, a->y);
            const double t = dot(p - ray.origin, ray.direction);
            EXPECT_GT(t, 0);
            EXPECT_LT(length(ray.at(t) - p), 1e-6);
        }
        // The centre ray looks along the view direction through the target.
        const auto r = c.rayThroughPixel(640, 400);
        expectVec(r.direction, c.viewDirection(), 1e-12);
        EXPECT_LT(length(cross(c.target() - r.origin, r.direction)), 1e-6);
    }
}

TEST(Camera, FitAllFramesABox) {
    const geometry::Aabb box{{0, 0, 0}, {800, 563, 720}}; // the basic cabinet
    for (auto proj : {CameraProjection::Perspective, CameraProjection::Orthographic}) {
        for (auto preset : {Camera::Preset::Front, Camera::Preset::Isometric, Camera::Preset::Top}) {
            Camera c;
            c.setViewportSize(1280, 800);
            c.setProjection(proj);
            c.setPreset(preset);
            c.fit(box);
            expectVec(c.target(), {400, 281.5, 360});
            double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
            for (int i = 0; i < 8; ++i) {
                const Vec3 p{(i & 1) ? 800.0 : 0.0, (i & 2) ? 563.0 : 0.0, (i & 4) ? 720.0 : 0.0};
                const auto px = c.projectToPixel(p);
                ASSERT_TRUE(px);
                minX = std::min(minX, px->x);
                maxX = std::max(maxX, px->x);
                minY = std::min(minY, px->y);
                maxY = std::max(maxY, px->y);
            }
            EXPECT_GE(minX, 0);
            EXPECT_LE(maxX, 1280);
            EXPECT_GE(minY, 0);
            EXPECT_LE(maxY, 800);
            // It fills the view: orthographic exactly 1/1.05 = 95.2 % of the limiting dimension;
            // perspective is conservative (nearest-corner rule) but still > 55 %.
            const double fill = std::max((maxX - minX) / 1280, (maxY - minY) / 800);
            EXPECT_GT(fill, proj == CameraProjection::Orthographic ? 0.95 : 0.55);
        }
    }
    // Front, orthographic: height 720 limits (800/1280*800 = 500 < 720): half height = 360 * 1.05.
    Camera c;
    c.setViewportSize(1280, 800);
    c.setProjection(CameraProjection::Orthographic);
    c.setPreset(Camera::Preset::Front);
    c.fit(box);
    EXPECT_NEAR(c.halfHeightAtTarget(), 378.0, 1e-9);
}

TEST(Camera, ZoomKeepsPointUnderCursorAndPanFollowsCursor) {
    for (auto proj : {CameraProjection::Perspective, CameraProjection::Orthographic}) {
        Camera c;
        c.setViewportSize(800, 600);
        c.setProjection(proj);
        c.setPreset(Camera::Preset::Front);
        c.setTarget({0, 0, 0});
        c.setDistance(2000);
        // Target-plane point under pixel (600, 150).
        const auto ray = c.rayThroughPixel(600, 150);
        const double t = dot(c.target() - ray.origin, c.viewDirection()) / dot(ray.direction, c.viewDirection());
        const Vec3 p = ray.at(t);
        c.zoomAt(600, 150, 0.5);
        EXPECT_NEAR(c.distance(), 1000, 1e-9);
        auto px = c.projectToPixel(p);
        ASSERT_TRUE(px);
        EXPECT_NEAR(px->x, 600, 1e-6);
        EXPECT_NEAR(px->y, 150, 1e-6);
        c.pan(30, -20);
        px = c.projectToPixel(p);
        ASSERT_TRUE(px);
        EXPECT_NEAR(px->x, 630, 1e-6);
        EXPECT_NEAR(px->y, 130, 1e-6);
        // Invalid zoom factors are ignored.
        c.zoomAt(10, 10, 0);
        c.zoomAt(10, 10, std::nan(""));
        EXPECT_NEAR(c.distance(), 1000, 1e-9);
    }
}

TEST(Camera, OrbitClampsPitchAndDistanceLimits) {
    Camera c;
    c.setPreset(Camera::Preset::Front);
    c.orbit(-100, 0); // drag left by 100 px at 0.4 deg/px: yaw +40
    EXPECT_NEAR(c.yaw(), 40, 1e-12);
    c.orbit(0, 1000); // pitch would be 400 -> clamped
    EXPECT_NEAR(c.pitch(), 90, 1e-12);
    EXPECT_TRUE(c.basis().isOrthonormal());
    c.orbit(0, -10000);
    EXPECT_NEAR(c.pitch(), -90, 1e-12);
    c.setDistance(-5);
    EXPECT_EQ(c.distance(), Camera::kMinDistance);
    c.setDistance(1e12);
    EXPECT_EQ(c.distance(), Camera::kMaxDistance);
}
