// SPDX-License-Identifier: AGPL-3.0-only
// Display meshes and picking of contoured parts (phase 12, ADR-0008). Expected numbers are
// derived by hand in the comments.
#include "fos/viewport/scene.hpp"
#include "fos/viewport/shape_mesh.hpp"

#include "fos/domain/fixtures.hpp"
#include "fos/geometry/triangulate.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace fos;
using geometry::Vec2;
using geometry::Vec3;
using namespace fos::viewport;

namespace {

constexpr double kPi = std::numbers::pi;

/// Chords per arc of geometry::tessellate (geometry README):
///   n = ceil(|theta| / min(2 acos(1 - tol / r), 90 deg)).
int chords(double theta, double r, double tol) {
    return static_cast<int>(std::ceil(theta / std::min(2 * std::acos(1 - tol / r), kPi / 2)));
}
/// Area between an arc of angle theta and its n equal chords: n x r^2 / 2 (phi - sin phi), phi = theta / n.
double chordLoss(double theta, double r, int n) {
    const double phi = theta / n;
    return n * r * r * 0.5 * (phi - std::sin(phi));
}

domain::Part part(const std::string& id, double L, double W, double T, domain::PartShape s) {
    domain::Part p;
    p.id = domain::PartId(id);
    p.name = id;
    p.length = L;
    p.width = W;
    p.thickness = T;
    p.material = domain::MaterialId("mat.pb18");
    p.shape = std::move(s);
    return p;
}
domain::Part roundedShelf() {
    domain::PartShape s;
    s.outer = geometry::roundedRectangleContour(600, 400, {50, 50, 50, 50}).value();
    return part("shelf.round", 600, 400, 18, std::move(s));
}
domain::Part worktop() {
    domain::PartShape s;
    s.outer = geometry::rectangleContour(1200, 600);
    s.cutouts.push_back(geometry::translated(geometry::rectangleContour(500, 400), {350, 100}));
    s.cutouts.push_back(geometry::circleContour({600, 540}, 17.5));
    return part("worktop", 1200, 600, 38, std::move(s));
}
domain::Part arcTopSide() {
    domain::PartShape s;
    s.outer.vertices = {{{0, 0}, 0}, {{800, 0}, 0}, {{800, 600}, 0.25}, {{0, 600}, 0}};
    return part("side.arc", 800, 700, 18, std::move(s));
}

ShapedPartGeometry mesh(const domain::Part& p, double tol) {
    auto g = makeShapedPartGeometry(domain::finishedContour(p), p.shape->cutouts, p.thickness, tol);
    EXPECT_TRUE(g.ok()) << (g.ok() ? "" : g.error().message);
    return g.ok() ? std::move(g).value() : ShapedPartGeometry{};
}

std::size_t verticalEdges(const ShapedPartGeometry& g) {
    std::size_t n = 0;
    for (const auto& e : g.edges) n += (e.a.x == e.b.x && e.a.y == e.b.y) ? 1 : 0;
    return n;
}

geometry::Ray3 down(double x, double y) { return {{x, y, 1000}, {0, 0, -1}}; }

} // namespace

TEST(ShapeMesh, TriangulateSquareWithSquareHole) {
    const geometry::Polygon2 outer{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    const geometry::Polygon2 hole{{{3, 3}, {7, 3}, {7, 7}, {3, 7}}}; // CCW given; reversed internally
    const auto t = geometry::triangulateWithHoles(outer, {hole});
    ASSERT_TRUE(t.ok()) << t.error().message;
    const std::vector<Vec2>& v = t.value().points;
    ASSERT_EQ(v.size(), 8u);
    // 8 vertices + 2 x 1 hole - 2 = 8 triangles; area 100 - 16 = 84; all CCW.
    ASSERT_EQ(t.value().triangles.size(), 8u);
    double area = 0;
    for (const auto& tri : t.value().triangles) {
        const Vec2 a = v[tri[0]], b = v[tri[1]], c = v[tri[2]];
        const double ar = 0.5 * ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
        EXPECT_GT(ar, 0);
        area += ar;
    }
    EXPECT_NEAR(area, 84, 1e-12);
    // Hole outside the outline -> error, not a wrong mesh.
    const geometry::Polygon2 outside{{{20, 3}, {24, 3}, {24, 7}, {20, 7}}};
    EXPECT_FALSE(geometry::triangulateWithHoles(outer, {outside}).ok());
    EXPECT_FALSE(geometry::triangulateWithHoles(geometry::Polygon2{{{0, 0}, {1, 0}}}, {}).ok());
}

TEST(ShapeMesh, ChordToleranceRule) {
    EXPECT_DOUBLE_EQ(shapeChordTolerance(1200, 600), 0.2);   // 1200 / 3000 = 0.4 -> capped at 0.2
    EXPECT_DOUBLE_EQ(shapeChordTolerance(300, 150), 0.1);    // 300 / 3000
    EXPECT_DOUBLE_EQ(shapeChordTolerance(10, 5), 0.01);      // floor
}

TEST(ShapeMesh, RoundedShelfVolumeWatertightAndEdges) {
    const double tol = 0.2;
    const ShapedPartGeometry g = mesh(roundedShelf(), tol);
    ASSERT_TRUE(geometry::isWatertight(g.mesh));
    // Exact area: 600 x 400 - (4 - pi) 50^2 = 240000 - 2146.018 = 237853.98 mm^2.
    const double exact = 600.0 * 400.0 - (4 - kPi) * 2500.0;
    // Tessellated: each quarter arc (theta = pi/2, r = 50) has n chords (n = 9 for 0.2 mm:
    // 2 acos(1 - 0.004) = 0.1790 rad -> ceil(1.5708 / 0.1790) = 9); the chords cut off chordLoss.
    const int n = chords(kPi / 2, 50, tol);
    EXPECT_EQ(n, 9);
    const double polyArea = exact - 4 * chordLoss(kPi / 2, 50, n);
    EXPECT_NEAR(geometry::volume(g.mesh), polyArea * 18, 1e-6 * polyArea * 18);
    // Hand bound: 0 <= exact volume - mesh volume <= tol x arc length x T
    //   = 0.2 x (4 x 25 pi) x 18 = 1131 mm^3.
    const double dv = exact * 18 - geometry::volume(g.mesh);
    EXPECT_GE(dv, 0);
    EXPECT_LE(dv, tol * 100 * kPi * 18);
    // Ring: 4 corners x 9 chords = 36 edges at z = 0 and 36 at z = 18 (+ 4 straights each);
    // smooth outline -> no vertical edges.
    EXPECT_EQ(verticalEdges(g), 0u);
    EXPECT_EQ(g.edges.size(), 2u * (36 + 4));
    // Triangles: caps 2 x (40 - 2) + sides 2 x 40 = 156.
    EXPECT_EQ(g.mesh.triangleCount(), 156u);
}

TEST(ShapeMesh, WorktopWithSinkAndTapHole) {
    const double tol = 0.2;
    const ShapedPartGeometry g = mesh(worktop(), tol);
    ASSERT_TRUE(geometry::isWatertight(g.mesh));
    // Exact: 1200 x 600 - 500 x 400 - pi 17.5^2 = 720000 - 200000 - 962.113 = 519037.89 mm^2.
    const double exact = 720000.0 - 200000.0 - kPi * 17.5 * 17.5;
    // The tap hole is two half circles of n chords each; the hole polygon is SMALLER than the
    // circle, so the mesh has MORE material: 0 <= V - V_exact <= tol x 2 pi r x T = 835.7 mm^3.
    const int n = chords(kPi, 17.5, tol);
    const double polyArea = exact + 2 * chordLoss(kPi, 17.5, n);
    const double v = geometry::volume(g.mesh);
    EXPECT_NEAR(v, polyArea * 38, 1e-6 * polyArea * 38);
    EXPECT_GE(v - exact * 38, 0);
    EXPECT_LE(v - exact * 38, tol * 2 * kPi * 17.5 * 38);
    // Vertical edges: 4 outline corners + 4 sink corners; none on the round hole.
    EXPECT_EQ(verticalEdges(g), 8u);
    // Caps: (4 + 4 + 2n) + 2 x 2 holes - 2 triangles each.
    EXPECT_EQ(g.mesh.triangleCount(), 2u * (8 + 2 * n + 2) + 2u * (8 + 2 * n));
}

TEST(ShapeMesh, ArcTopSideVolume) {
    const double tol = 0.2;
    const ShapedPartGeometry g = mesh(arcTopSide(), tol);
    ASSERT_TRUE(geometry::isWatertight(g.mesh));
    // Exact area: 800 x 600 + circular segment r^2 / 2 (theta - sin theta),
    // r = 850, theta = 4 atan(0.25) = 0.979915 rad -> segment = 54 427.6 mm^2 (top at y = 700).
    const double theta = 4 * std::atan(0.25), r = 850;
    const double exact = 480000.0 + r * r * 0.5 * (theta - std::sin(theta));
    const double dv = exact * 18 - geometry::volume(g.mesh);
    EXPECT_NEAR(dv, chordLoss(theta, r, chords(theta, r, tol)) * 18, 1e-6 * exact * 18);
    EXPECT_GE(dv, 0);
    EXPECT_LE(dv, tol * r * theta * 18); // 0.2 x 832.9 x 18 = 2998.5 mm^3
    // 4 real corners: two at the bottom, two where the top arc meets the sides.
    EXPECT_EQ(verticalEdges(g), 4u);
}

TEST(ShapeScene, PickingFollowsTheRealOutline) {
    domain::Model m = domain::fixtures::basicCabinet();
    m.assemblies[0].parts.clear();
    m.assemblies[0].parts.push_back(worktop());            // index 0, z 0..38
    domain::Part shelf = roundedShelf();
    shelf.placement = geometry::Transform::translation({2000, 0, 0});
    m.assemblies[0].parts.push_back(shelf);                // index 1, x 2000..2600
    Scene s;
    s.setModel(m);
    ASSERT_TRUE(s.diagnostics().empty());
    EXPECT_TRUE(s.parts()[0].contoured);
    EXPECT_TRUE(s.parts()[1].contoured);
    // On the worktop: hit at z = 38 -> t = 1000 - 38 = 962.
    const auto hit = s.pick(down(100, 300));
    ASSERT_TRUE(hit);
    EXPECT_EQ(*hit, 0u);
    // Through the sink opening [350,850] x [100,500] and through the tap hole centre: miss
    // (the bounding box would have been hit).
    EXPECT_FALSE(s.pick(down(600, 300)));
    EXPECT_FALSE(s.pick(down(600, 540)));
    EXPECT_FALSE(s.pick(down(352, 102)));
    // Just beside the sink, on material: hit.
    EXPECT_TRUE(s.pick(down(348, 300)));
    // Rounded corner of the shelf: (2003, 3) is outside the R 50 arc (distance to (2050, 50) is
    // 66.5 > 50) -> miss; (2020, 20) is inside (distance 42.4) -> hit.
    EXPECT_FALSE(s.pick(down(2003, 3)));
    const auto corner = s.pick(down(2020, 20));
    ASSERT_TRUE(corner);
    EXPECT_EQ(*corner, 1u);
    // Horizontal ray through the sink at mid-thickness z = 19 enters the worktop at the left
    // edge x = 0 (t = 100 from x = -100): nearest hit is the outer side wall, not the sink walls.
    const geometry::Ray3 side{{-100, 300, 19}, {1, 0, 0}};
    EXPECT_TRUE(s.pick(side));
}

TEST(ShapeScene, BuffersUseTheExtrusionAndFallBackToBoxOnBadShapes) {
    domain::Model m = domain::fixtures::basicCabinet();
    m.assemblies[0].parts.clear();
    m.assemblies[0].parts.push_back(roundedShelf());
    domain::Part bad = worktop();
    bad.id = domain::PartId("bad");
    bad.shape->cutouts.push_back(geometry::translated(geometry::rectangleContour(100, 100), {5000, 0})); // outside
    m.assemblies[0].parts.push_back(bad);
    Scene s;
    s.setModel(m);
    const auto b = s.buildBuffers();
    const std::size_t shelfV = s.localMesh(0).positions.size();
    // Shelf: caps 2 x 40 vertices + sides 4 x 40 = 240 vertices; then the fallback box (24).
    EXPECT_EQ(shelfV, 240u);
    EXPECT_EQ(b.vertices.size(), shelfV + 24u);
    EXPECT_EQ(b.indices.size(), 156u * 3 + 36u);
    EXPECT_EQ(b.lines.size(), 2u * 80 + 24u);
    EXPECT_FALSE(s.parts()[1].contoured);
    ASSERT_EQ(s.diagnostics().size(), 1u);
    EXPECT_EQ(s.diagnostics()[0].code, "viewport.shape.fallback_box");
    EXPECT_EQ(s.diagnostics()[0].entityId, "bad");
}
