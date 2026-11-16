// SPDX-License-Identifier: AGPL-3.0-only
// OBJ export of shaped parts (ADR-0008) and the cap triangulation with holes behind it.
#include "../src/obj_mesh.hpp"
#include "fos/formats/obj_export.hpp"
#include "fos/formats/sha256.hpp"
#include "obj_parse.hpp"
#include "shape_fixtures.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <numbers>

using namespace fos;
using namespace fos::formats;
using namespace fos::formats::test;
using geometry::Vec2;
using geometry::Vec3;
using ::testing::HasSubstr;

namespace {

constexpr double kPi = std::numbers::pi;

/// Every undirected edge of the object's faces is used exactly twice, once in each direction:
/// the mesh is closed and consistently oriented (no gaps, no T-junctions, no flipped face).
void expectClosedAndConsistent(const ObjObject& object, const std::string& name) {
    std::map<std::pair<int, int>, int> directed;
    for (const ObjFace& f : object.faces)
        for (std::size_t i = 0; i < f.vertices.size(); ++i)
            ++directed[{f.vertices[i], f.vertices[(i + 1) % f.vertices.size()]}];
    for (const auto& [edge, count] : directed) {
        EXPECT_EQ(count, 1) << name << ": edge " << edge.first << "->" << edge.second << " used " << count << " times";
        const auto reverse = directed.find({edge.second, edge.first});
        EXPECT_TRUE(reverse != directed.end() && reverse->second == 1)
            << name << ": edge " << edge.first << "->" << edge.second << " has no opposite";
    }
}

/// Every face normal agrees with its winding: n . ((p1 - p0) x (p2 - p0)) > 0.
void expectNormalsMatchWinding(const ParsedObj& obj, const ObjObject& object, const std::string& name) {
    for (const ObjFace& f : object.faces) {
        const Vec3 p0 = obj.vertices[f.vertices[0]], p1 = obj.vertices[f.vertices[1]], p2 = obj.vertices[f.vertices[2]];
        const Vec3 n = obj.normals[f.normal];
        EXPECT_NEAR(length(n), 1.0, 1e-12) << name;
        EXPECT_GT(dot(n, cross(p1 - p0, p2 - p0)), 0.0) << name;
    }
}

domain::Model singlePartModel(domain::Part part) {
    domain::Model m = domain::fixtures::basicCabinet();
    domain::Assembly a;
    a.id = domain::AssemblyId("asm.one");
    a.name = "One";
    a.parts = {std::move(part)};
    m.assemblies = {std::move(a)};
    return m;
}

double exportedVolume(const domain::Part& part) {
    auto exported = exportObj(singlePartModel(part));
    EXPECT_TRUE(exported.ok()) << exported.error().message;
    if (!exported.ok()) return 0;
    const ParsedObj obj = parseObj(exported.value().obj);
    EXPECT_EQ(obj.objects.size(), 1u);
    expectClosedAndConsistent(obj.objects[0], part.id.str());
    expectNormalsMatchWinding(obj, obj.objects[0], part.id.str());
    return signedVolume(obj, obj.objects[0]);
}

/// T * (area of the tessellated outer - areas of the tessellated cut-outs): what the mesh
/// must enclose exactly.
double tessellatedVolume(const domain::Part& part) {
    double area = geometry::area(geometry::tessellate(part.shape->outer, kObjChordTolerance));
    for (const auto& c : part.shape->cutouts) area -= geometry::area(geometry::tessellate(c, kObjChordTolerance));
    return area * part.thickness;
}

} // namespace

// ---------------------------------------------------------------- volumes

TEST(ObjShape, RoundedPanelVolume) {
    // Exact area: 600 * 400 - (4 - pi) * 50^2 (four corner squares minus four quarter discs)
    //           = 240000 - 2146.01836... = 237853.98163...
    // Chords lie inside the convex arcs, at most 0.05 mm from them, over the total arc length
    // 2 * pi * 50 = 314.159 mm: the exported volume is in [V - 18 * 0.05 * 314.159, V].
    const double exact = (600.0 * 400.0 - (4.0 - kPi) * 2500.0) * 18.0; // 4 281 371.669...
    const double bound = 18.0 * 0.05 * 2.0 * kPi * 50.0;                 // 282.74 mm^3
    const double volume = exportedVolume(roundedPanel());
    EXPECT_LE(volume, exact);
    EXPECT_GE(volume, exact - bound);
    EXPECT_NEAR(volume, tessellatedVolume(roundedPanel()), 1e-6 * exact);
}

TEST(ObjShape, ArcTopSideVolume) {
    // Rectangle 800 x 600 plus the circular segment over the 800 mm chord with bulge 0.25:
    //   theta = 4 atan(0.25) = 0.979915 rad, rise h = 0.25 * 400 = 100,
    //   R = (400^2 + 100^2) / (2 * 100) = 850,
    //   segment area = R^2 / 2 * (theta - sin theta) = 361250 * (0.979915 - 0.830450) = 53994.2
    // Arc length R * theta = 832.93 mm.
    const double theta = 4.0 * std::atan(0.25);
    const double R = 850.0;
    const double exact = (800.0 * 600.0 + R * R / 2.0 * (theta - std::sin(theta))) * 18.0;
    EXPECT_NEAR(exact / 18.0, 533994.2, 0.1);
    const double bound = 18.0 * 0.05 * R * theta;
    const double volume = exportedVolume(arcTopSide());
    EXPECT_LE(volume, exact);
    EXPECT_GE(volume, exact - bound);
    EXPECT_NEAR(volume, tessellatedVolume(arcTopSide()), 1e-6 * exact);
}

TEST(ObjShape, WorktopWithSinkAndHobHoleVolume) {
    // 1200 * 600 - 500 * 400 - pi * 30^2 = 720000 - 200000 - 2827.433 = 517172.567 mm^2, T = 38.
    // The tessellated hole is inscribed in the circle (smaller), so the solid is up to
    // 38 * 0.05 * 2 pi 30 = 358.1 mm^3 larger than exact.
    const double exact = (1200.0 * 600.0 - 500.0 * 400.0 - kPi * 900.0) * 38.0; // 19 652 557.5
    const double bound = 38.0 * 0.05 * 2.0 * kPi * 30.0;
    const double volume = exportedVolume(worktop());
    EXPECT_GE(volume, exact);
    EXPECT_LE(volume, exact + bound);
    EXPECT_NEAR(volume, tessellatedVolume(worktop()), 1e-6 * exact);
}

TEST(ObjShape, ManyCutoutsOnOneRowAndAConcaveOutline) {
    // Comb-shaped outline (U with a 200 x 300 notch from the top) of 1000 x 500, with nine
    // 40 x 40 square holes on one row (all rightmost vertices at the same y as each other)
    // and a circular hole: bridges must not cross each other or the notch.
    //   outer area = 1000 * 500 - 200 * 300 = 440000
    //   holes      = 9 * 1600 + pi * 25^2 (tessellated) -> checked against the tessellation.
    domain::PartShape shape;
    shape.outer.vertices = {{{0, 0}, 0},     {{1000, 0}, 0},   {{1000, 500}, 0}, {{600, 500}, 0},
                            {{600, 200}, 0}, {{400, 200}, 0}, {{400, 500}, 0},   {{0, 500}, 0}};
    for (int k = 0; k < 9; ++k)
        shape.cutouts.push_back(geometry::translated(geometry::rectangleContour(40, 40), {50.0 + 100.0 * k, 80}));
    shape.cutouts.push_back(geometry::circleContour({800, 350}, 25));
    const domain::Part part = shapedPart("shaped/comb", 1000, 500, 19, shape);
    const double volume = exportedVolume(part);
    EXPECT_NEAR(volume, tessellatedVolume(part), 1e-6 * volume);
    const double exactArea = 440000.0 - 9.0 * 1600.0 - kPi * 625.0;
    EXPECT_GE(volume, exactArea * 19.0);
    EXPECT_LE(volume, exactArea * 19.0 + 19.0 * 0.05 * 2.0 * kPi * 25.0);
}

// ---------------------------------------------------------------- cap triangulation

TEST(ObjShape, CapTrianglesTileTheRegionExactlyOnce) {
    for (const domain::Part& part : {roundedPanel(), arcTopSide(), worktop()}) {
        std::vector<geometry::Polygon2> holes;
        for (const auto& c : part.shape->cutouts) holes.push_back(geometry::tessellate(c, kObjChordTolerance));
        const geometry::Polygon2 outer = geometry::tessellate(part.shape->outer, kObjChordTolerance);
        auto cap = detail::triangulateWithHoles(outer, holes);
        ASSERT_TRUE(cap.ok()) << cap.error().message;
        double expected = geometry::area(outer), sum = 0;
        for (const auto& h : holes) expected -= geometry::area(h);
        std::size_t points = outer.size();
        for (const auto& h : holes) points += h.size();
        EXPECT_EQ(cap.value().points.size(), points);
        // A polygon with n points and h holes triangulates into n + 2h - 2 triangles.
        EXPECT_EQ(cap.value().triangles.size(), points + 2 * holes.size() - 2) << part.id.str();
        for (const auto& t : cap.value().triangles) {
            const Vec2 a = cap.value().points[t[0]], b = cap.value().points[t[1]], c = cap.value().points[t[2]];
            const double twice = cross(b - a, c - a);
            EXPECT_GT(twice, 0.0);
            sum += twice / 2;
            // The centroid lies in the material: inside the outline, outside every cut-out.
            const Vec2 g{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3};
            EXPECT_EQ(geometry::locatePoint(outer, g), geometry::PointLocation::Inside) << part.id.str();
            for (const auto& h : holes) EXPECT_EQ(geometry::locatePoint(h, g), geometry::PointLocation::Outside);
        }
        EXPECT_NEAR(sum, expected, 1e-9 * expected) << part.id.str();
    }
}

TEST(ObjShape, CapTriangulationRejectsBadRings) {
    geometry::Polygon2 square{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    EXPECT_FALSE(detail::triangulateWithHoles(geometry::Polygon2{{{0, 0}, {1, 0}}}, {}).ok());
    EXPECT_FALSE(detail::triangulateWithHoles(geometry::Polygon2{{{0, 0}, {1, 0}, {2, 0}}}, {}).ok()); // zero area
    // A "hole" outside the outer ring cannot be bridged.
    auto outside = detail::triangulateWithHoles(square, {geometry::Polygon2{{{20, 2}, {22, 2}, {22, 4}}}});
    EXPECT_FALSE(outside.ok());
    // Clockwise input is accepted and normalised.
    geometry::Polygon2 cw{{{0, 0}, {0, 10}, {10, 10}, {10, 0}}};
    auto cap = detail::triangulateWithHoles(cw, {});
    ASSERT_TRUE(cap.ok());
    EXPECT_EQ(cap.value().points.front(), (Vec2{10, 0}));
    EXPECT_EQ(cap.value().triangles.size(), 2u);
}

// ---------------------------------------------------------------- file content

TEST(ObjShape, RectangularModelExportIsUnchanged) {
    // Golden values recorded with the exporter BEFORE shapes existed.
    auto exported = exportObj(domain::fixtures::basicCabinet());
    ASSERT_TRUE(exported.ok());
    EXPECT_EQ(exported.value().obj.size(), 2742u);
    EXPECT_EQ(sha256Hex(exported.value().obj), "60bf3ddc923842061a8070e6187f11b40875d76a22475c8911e81da34302e9a0");
    EXPECT_EQ(sha256Hex(exported.value().mtl), "24e2916b3a0f5ec412980c6b6e9f2c94d6cdfec07be199b1eecc5ea10dcdfe01");
    EXPECT_THAT(exported.value().obj, ::testing::Not(HasSubstr("Shaped parts")));
}

TEST(ObjShape, MixedModelHeaderCountsAndPlacement) {
    auto exported = exportObj(shapedModel());
    ASSERT_TRUE(exported.ok()) << exported.error().message;
    const std::string& text = exported.value().obj;
    EXPECT_THAT(text, HasSubstr("# Shaped parts: finished outline with cut-outs extruded over the thickness; arcs as "
                                "chords with a maximum deviation of 0.05 mm.\n"));
    EXPECT_THAT(text, HasSubstr("# shaped/worktop: 1200 x 600 x 38 mm, assembly asm.shaped, shaped outline with 2 "
                                "cut-out(s)\n"));
    const ParsedObj obj = parseObj(text);
    ASSERT_EQ(obj.objects.size(), 9u);
    // The six cabinet boxes are still boxes: 8 vertices, 6 quads each.
    EXPECT_EQ(find(obj, "cab1/side.left").faces.size(), 6u);
    // Worktop: outer 4 points, sink 4, hob circle n_c points -> n = 8 + n_c; faces =
    // 2 caps * (n + 2*2 - 2) triangles + n side quads.
    const ObjObject top = find(obj, "shaped/worktop");
    const std::size_t nc = geometry::tessellate(worktop().shape->cutouts[1], kObjChordTolerance).size();
    const std::size_t n = 8 + nc;
    EXPECT_EQ(top.faces.size(), 2 * (n + 2) + n);
    // Placement: assembly translated by (2000, 0, 0), part placement identity.
    const geometry::Aabb box = boundsOf(obj, top);
    EXPECT_NEAR(box.min.x, 2000, 1e-9);
    EXPECT_NEAR(box.max.x, 3200, 1e-9);
    EXPECT_NEAR(box.max.y, 600, 1e-9);
    EXPECT_NEAR(box.max.z, 38, 1e-9);
    for (const auto& o : obj.objects) {
        expectClosedAndConsistent(o, o.name);
        expectNormalsMatchWinding(obj, o, o.name);
        EXPECT_GT(signedVolume(obj, o), 0.0) << o.name;
    }
}

TEST(ObjShape, RotatedPlacementKeepsVolumeAndOrientation) {
    domain::Part part = worktop();
    part.placement = geometry::Transform::translation({10, 20, 30}) * geometry::Transform::rotationZ(30.0);
    const double volume = exportedVolume(part);
    EXPECT_NEAR(volume, tessellatedVolume(part), 1e-6 * volume);
}

TEST(ObjShape, InvalidShapeFailsTheExportNamingThePart) {
    domain::Part part = worktop();
    part.width = 650; // outline is 600 high
    auto exported = exportObj(singlePartModel(part));
    ASSERT_FALSE(exported.ok());
    EXPECT_EQ(exported.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_THAT(exported.error().message,
                HasSubstr("OBJ export: part 'shaped/worktop' (shaped/worktop) has an invalid shape: /shape/outer: the "
                          "bounding box"));
    part = worktop();
    part.shape->cutouts[1] = geometry::circleContour({1190, 300}, 30); // crosses the right edge
    exported = exportObj(singlePartModel(part));
    ASSERT_FALSE(exported.ok());
    EXPECT_THAT(exported.error().message, HasSubstr("/shape/cutouts/1"));
}
