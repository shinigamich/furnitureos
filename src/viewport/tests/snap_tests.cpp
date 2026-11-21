// SPDX-License-Identifier: AGPL-3.0-only
// Snapping, measurement and section-plane math (Qt-free parts of the viewport).
#include "fos/viewport/snap.hpp"

#include "fos/domain/fixtures.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace fos;
using geometry::Vec3;
using viewport::Camera;
using viewport::Scene;
using viewport::SectionAxis;
using viewport::SectionPlane;
using viewport::SectionSide;
using viewport::SnapKind;

namespace {
// Only the fixture shelf: part frame placed at (18, 20, 351) without rotation, 764 x 540 x 18,
// i.e. world box x 18..782, y 20..560, z 351..369.
Scene shelfScene() {
    auto model = domain::fixtures::basicCabinet();
    auto& parts = model.assemblies[0].parts;
    const auto shelf = parts[4];
    parts = {shelf};
    Scene s;
    s.setModel(model);
    return s;
}
Camera topCamera(const Scene& s) {
    Camera c;
    c.setViewportSize(800, 600);
    c.setProjection(viewport::CameraProjection::Orthographic);
    c.setPreset(Camera::Preset::Top);
    c.fit(s.visibleBounds());
    return c;
}
bool containsPoint(const std::vector<viewport::SnapPoint>& v, Vec3 p, SnapKind k) {
    return std::any_of(v.begin(), v.end(), [&](const auto& s) { return s.kind == k && approxEqual(s.point, p); });
}
} // namespace

TEST(Measurement, DistanceAndDeltas) {
    // (300, 400, 1200) - (0, 0, 0): sqrt(90000 + 160000 + 1440000) = sqrt(1690000) = 1300.
    const viewport::Measurement m{{10, 20, 30}, {310, 420, 1230}};
    EXPECT_DOUBLE_EQ(m.distance(), 1300.0);
    EXPECT_TRUE(approxEqual(m.delta(), Vec3{300, 400, 1200}));
    // Reversed points: same distance, negated deltas.
    const viewport::Measurement r{m.b, m.a};
    EXPECT_DOUBLE_EQ(r.distance(), 1300.0);
    EXPECT_TRUE(approxEqual(r.delta(), Vec3{-300, -400, -1200}));
    EXPECT_DOUBLE_EQ((viewport::Measurement{{5, 5, 5}, {5, 5, 5}}.distance()), 0.0);
}

TEST(Snap, BoxPartCandidates) {
    const Scene s = shelfScene();
    ASSERT_EQ(s.parts().size(), 1u);
    const auto pts = viewport::partSnapPoints(s, 0);
    ASSERT_EQ(pts.size(), 26u); // 8 corners + 12 edge midpoints + 6 face centres
    EXPECT_EQ(std::count_if(pts.begin(), pts.end(), [](const auto& p) { return p.kind == SnapKind::Corner; }), 8);
    // First corner = part-frame origin; last = far corner.
    EXPECT_TRUE(approxEqual(pts[0].point, Vec3{18, 20, 351}));
    EXPECT_TRUE(approxEqual(pts[7].point, Vec3{782, 560, 369}));
    // Edge midpoint of the front-top edge: x = (18 + 782) / 2 = 400.
    EXPECT_TRUE(containsPoint(pts, {400, 20, 369}, SnapKind::EdgeMidpoint));
    // Vertical edge at the origin corner: z = (351 + 369) / 2 = 360.
    EXPECT_TRUE(containsPoint(pts, {18, 20, 360}, SnapKind::EdgeMidpoint));
    // Face A centre (400, 290, 369), edge W1 centre (18, 290, 360).
    EXPECT_TRUE(containsPoint(pts, {400, 290, 369}, SnapKind::FaceCenter));
    EXPECT_TRUE(containsPoint(pts, {18, 290, 360}, SnapKind::FaceCenter));
}

TEST(Snap, SectionDropsRemovedCandidates) {
    Scene s = shelfScene();
    // Keep z <= 360: the four z = 369 corners, the four top edge midpoints and face A go.
    s.setSection(SectionPlane{SectionAxis::Z, 360, false});
    const auto pts = viewport::partSnapPoints(s, 0);
    // 4 bottom corners + (4 bottom + 4 vertical at z = 360) midpoints + 5 face centres = 17.
    EXPECT_EQ(pts.size(), 17u);
    EXPECT_FALSE(containsPoint(pts, {18, 20, 369}, SnapKind::Corner));
    EXPECT_TRUE(containsPoint(pts, {18, 20, 351}, SnapKind::Corner));
}

TEST(Snap, NearestVisibleCandidateWithinRadius) {
    const Scene s = shelfScene();
    const Camera c = topCamera(s);
    const auto corner = c.projectToPixel({18, 20, 369});
    ASSERT_TRUE(corner);
    // Top ortho view: corners (18, 20, 351) and (18, 20, 369) share the pixel; the bottom one
    // comes first in candidate order but is hidden by the shelf, so the top corner wins.
    const auto snap = viewport::snapAt(s, c, corner->x + 4, corner->y + 3);
    ASSERT_TRUE(snap);
    EXPECT_EQ(snap->kind, SnapKind::Corner);
    EXPECT_TRUE(approxEqual(snap->point, Vec3{18, 20, 369}));
    // 5 px from the corner is inside the default 12 px radius, 20 px diagonal outside the
    // shelf is not (nearest other candidate: edge midpoints 382 / 270 mm away).
    const double dx = corner->x - c.projectToPixel({400, 290, 369})->x > 0 ? 20 : -20;
    const double dy = corner->y - c.projectToPixel({400, 290, 369})->y > 0 ? 20 : -20;
    EXPECT_FALSE(viewport::snapAt(s, c, corner->x + dx, corner->y + dy));
    // Face A centre.
    const auto centre = c.projectToPixel({400, 290, 369});
    const auto f = viewport::snapAt(s, c, centre->x + 2, centre->y);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->kind, SnapKind::FaceCenter);
    EXPECT_TRUE(approxEqual(f->point, Vec3{400, 290, 369}));
    // Between candidates (x 200, y 150): free surface point on face A.
    const auto free = c.projectToPixel({200, 150, 369});
    const auto sf = viewport::snapAt(s, c, free->x, free->y);
    ASSERT_TRUE(sf);
    EXPECT_EQ(sf->kind, SnapKind::Surface);
    EXPECT_NEAR(sf->point.z, 369, 1e-6);
    EXPECT_NEAR(sf->point.x, 200, 2.0); // within a pixel (~1.1 mm/px here)
}

TEST(Snap, SectionCapOccludesAndIsPickable) {
    Scene s = shelfScene();
    s.setSection(SectionPlane{SectionAxis::Z, 360, false}); // keep z <= 360
    const Camera c = topCamera(s);
    const auto corner = c.projectToPixel({18, 20, 351});
    ASSERT_TRUE(corner);
    // The bottom corner is hidden by the cap at z = 360; the vertical edge midpoint (18, 20, 360)
    // lies on the cap and is the snap.
    const auto snap = viewport::snapAt(s, c, corner->x, corner->y);
    ASSERT_TRUE(snap);
    EXPECT_EQ(snap->kind, SnapKind::EdgeMidpoint);
    EXPECT_TRUE(approxEqual(snap->point, Vec3{18, 20, 360}));
    // A free point on the cap: z = 360.
    const auto free = c.projectToPixel({200, 150, 360});
    const auto sf = viewport::snapAt(s, c, free->x, free->y);
    ASSERT_TRUE(sf);
    EXPECT_EQ(sf->kind, SnapKind::Surface);
    EXPECT_NEAR(sf->point.z, 360, 1e-6);
}

TEST(Section, PlaneMathAndClassification) {
    const SectionPlane keepBelow{SectionAxis::Y, 100, false}, keepAbove{SectionAxis::Y, 100, true};
    EXPECT_DOUBLE_EQ(keepBelow.signedDistance({0, 40, 0}), 60.0); // 100 - 40
    EXPECT_DOUBLE_EQ(keepAbove.signedDistance({0, 40, 0}), -60.0);
    const auto e = keepAbove.equation(); // y - 100
    EXPECT_DOUBLE_EQ(e[1] * 40 + e[3], -60.0);
    EXPECT_TRUE(keepBelow.keeps({0, 100, 0})); // on the plane counts as kept
    const geometry::Aabb below{{0, 0, 0}, {10, 100, 10}}, across{{0, 50, 0}, {10, 150, 10}},
        above{{0, 100, 0}, {10, 200, 10}};
    EXPECT_EQ(viewport::classify(keepBelow, below), SectionSide::Kept);
    EXPECT_EQ(viewport::classify(keepBelow, across), SectionSide::Cut);
    EXPECT_EQ(viewport::classify(keepBelow, above), SectionSide::Removed); // touches from the removed side
    EXPECT_EQ(viewport::classify(keepAbove, below), SectionSide::Removed);
    EXPECT_EQ(viewport::classify(keepAbove, geometry::Aabb{}), SectionSide::Removed);
}
