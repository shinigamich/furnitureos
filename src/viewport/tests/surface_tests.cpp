// SPDX-License-Identifier: AGPL-3.0-only
// Realistic display mode: per-vertex surface attributes of the scene buffers (phase 20).
#include "fos/viewport/scene.hpp"

#include "fos/domain/fixtures.hpp"
#include "fos/geometry/contour.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace fos;
using geometry::Vec3;
using viewport::Scene;
using viewport::SurfaceKind;

namespace {

constexpr domain::Color kDecor{222, 204, 170};
constexpr domain::Color kBand{40, 90, 160};

domain::Model bandedModel() {
    domain::Model m;
    domain::Material mat;
    mat.id = domain::MaterialId("mat.oak");
    mat.thickness = 18;
    mat.hasGrain = true;
    mat.color = kDecor;
    domain::EdgeBand band;
    band.id = domain::EdgeBandId("eb.blue");
    band.color = kBand;
    m.materials = {mat};
    m.edgeBands = {band};
    domain::Assembly a;
    a.id = domain::AssemblyId("a");
    domain::Part p;
    p.id = domain::PartId("a/shelf");
    p.name = "Shelf";
    p.length = 600;
    p.width = 300;
    p.thickness = 18;
    p.material = mat.id;
    p.grain = domain::GrainDirection::AlongLength;
    p.edge(domain::EdgeSide::L1) = band.id; // front edge (y = 0) banded, the other three raw
    a.parts = {p};
    m.assemblies = {a};
    return m;
}

bool colourIs(const viewport::SurfaceVertex& s, domain::Color c) {
    return std::abs(s.r - c.r / 255.f) < 1e-6f && std::abs(s.g - c.g / 255.f) < 1e-6f &&
           std::abs(s.b - c.b / 255.f) < 1e-6f;
}

} // namespace

TEST(SceneSurface, RawEdgeColourIsDarkParticleboardCore) {
    // 0.75 * (0.7 * (176, 146, 108) + 0.3 * (222, 204, 170)) = 0.75 * (189.8, 163.4, 126.6)
    //   = (142.35, 122.55, 94.95) -> (142, 123, 95)
    EXPECT_EQ(viewport::rawEdgeColor(kDecor), (domain::Color{142, 123, 95}));
}

TEST(SceneSurface, BandedPartCarriesBandColourAndGrainAlongLength) {
    Scene s;
    s.setModel(bandedModel());
    const auto buf = s.buildBuffers();
    ASSERT_EQ(buf.vertices.size(), 24u); // geometry::makeBox
    ASSERT_EQ(buf.surface.size(), buf.vertices.size());
    int faces = 0, banded = 0, raw = 0;
    for (std::size_t i = 0; i < buf.vertices.size(); ++i) {
        const auto& v = buf.vertices[i];
        const auto& a = buf.surface[i];
        EXPECT_EQ(a.texture, -1.f);
        if (std::abs(v.nz) > 0.5f) { // faces A/B: decor colour, grain +X (along length), u = x, v = y
            ++faces;
            EXPECT_EQ(a.kind, float(SurfaceKind::BoardFace));
            EXPECT_TRUE(colourIs(a, kDecor));
            EXPECT_FLOAT_EQ(a.gx, 1.f);
            EXPECT_FLOAT_EQ(a.gy, 0.f);
            EXPECT_FLOAT_EQ(a.gz, 0.f);
            EXPECT_FLOAT_EQ(a.u, v.px);
            EXPECT_FLOAT_EQ(a.v, v.py);
        } else if (v.ny < -0.5f) { // L1 (y = 0): banded, along the edge = +X, v = z
            ++banded;
            EXPECT_EQ(a.kind, float(SurfaceKind::BandedEdge));
            EXPECT_TRUE(colourIs(a, kBand));
            EXPECT_FLOAT_EQ(a.gx, 1.f);
            EXPECT_FLOAT_EQ(a.v, v.pz);
        } else { // L2, W1, W2: raw core colour
            ++raw;
            EXPECT_EQ(a.kind, float(SurfaceKind::RawEdge));
            EXPECT_TRUE(colourIs(a, viewport::rawEdgeColor(kDecor)));
        }
    }
    EXPECT_EQ(faces, 8);  // 2 faces x 4 vertices
    EXPECT_EQ(banded, 4); // 1 edge
    EXPECT_EQ(raw, 12);   // 3 edges
}

TEST(SceneSurface, GrainFollowsPartFrameAndAlongWidth) {
    auto m = bandedModel();
    auto& p = m.assemblies[0].parts[0];
    // Upright panel like a cabinet side: local x -> +Z, local y -> +Y.
    p.placement = geometry::Transform::fromAxes({0, 0, 1}, {0, 1, 0}, {18, 0, 0});
    Scene s;
    s.setModel(m);
    auto buf = s.buildBuffers();
    for (std::size_t i = 0; i < buf.vertices.size(); ++i)
        if (buf.surface[i].kind == float(SurfaceKind::BoardFace)) {
            EXPECT_NEAR(buf.surface[i].gz, 1.f, 1e-6f); // along length = world +Z
        }
    p.grain = domain::GrainDirection::AlongWidth;
    s.setModel(m);
    buf = s.buildBuffers();
    for (const auto& a : buf.surface)
        if (a.kind == float(SurfaceKind::BoardFace)) {
            EXPECT_NEAR(a.gy, 1.f, 1e-6f); // along width = +Y
        }
    // No grain on plain materials.
    m.materials[0].hasGrain = false;
    s.setModel(m);
    for (const auto& a : s.buildBuffers().surface) EXPECT_EQ(a.gx * a.gx + a.gy * a.gy + a.gz * a.gz, 0.f);
}

TEST(SceneSurface, ContouredPartUsesPerSegmentBands) {
    auto m = bandedModel();
    auto& p = m.assemblies[0].parts[0];
    p.edges = {};
    const double radii[4] = {0, 0, 80, 0}; // round the back-right corner only
    auto c = geometry::roundedRectangleContour(600, 300, radii);
    ASSERT_TRUE(c.ok());
    domain::PartShape shape;
    shape.outer = c.value();
    for (std::size_t k = 0; k < shape.outer.size(); ++k) {
        const auto info = geometry::segmentInfo(shape.outer, k);
        const bool front = !info.isArc && info.start.y == 0 && info.end.y == 0;
        shape.outerEdges.push_back(front ? std::optional(m.edgeBands[0].id) : std::nullopt);
    }
    p.shape = shape;
    Scene s;
    s.setModel(m);
    ASSERT_TRUE(s.parts()[0].contoured);
    const auto buf = s.buildBuffers();
    ASSERT_EQ(buf.surface.size(), buf.vertices.size());
    int banded = 0, raw = 0;
    for (std::size_t i = 0; i < buf.vertices.size(); ++i) {
        const auto& v = buf.vertices[i];
        if (std::abs(v.nz) > 0.5f) continue;
        const auto& a = buf.surface[i];
        if (v.ny < -0.99f) {
            ++banded;
            EXPECT_TRUE(colourIs(a, kBand));
        } else {
            ++raw;
            EXPECT_EQ(a.kind, float(SurfaceKind::RawEdge));
        }
    }
    EXPECT_EQ(banded, 4); // the straight front edge is one wall quad
    EXPECT_GT(raw, 4 * 3); // left, right, back, plus the tessellated arc
}
