// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/mesh.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <tuple>
#include <utility>

using namespace fos::geometry;

namespace {

Polygon2 lShape() { return {{{0, 0}, {100, 0}, {100, 40}, {40, 40}, {40, 100}, {0, 100}}}; }

using Key = std::tuple<double, double, double>;
Key key(Vec3 p) { return {p.x, p.y, p.z}; }

/// Independent watertightness check: every undirected edge (by position) is used by exactly
/// two triangles, once in each direction.
bool everyEdgeSharedByTwoTriangles(const TriangleMesh& m) {
    std::map<std::pair<Key, Key>, int> directed;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3)
        for (int e = 0; e < 3; ++e) {
            const Key a = key(m.positions[m.indices[i + static_cast<std::size_t>(e)]]);
            const Key b = key(m.positions[m.indices[i + static_cast<std::size_t>((e + 1) % 3)]]);
            ++directed[{a, b}];
        }
    if (directed.empty()) return false;
    for (const auto& [edge, count] : directed) {
        if (count != 1) return false;
        const auto opposite = directed.find({edge.second, edge.first});
        if (opposite == directed.end() || opposite->second != 1) return false;
    }
    return true;
}

/// Every vertex normal must be unit length and equal the geometric normal of its triangles.
void expectFlatNormals(const TriangleMesh& m) {
    ASSERT_EQ(m.positions.size(), m.normals.size());
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const Vec3 a = m.positions[m.indices[i]], b = m.positions[m.indices[i + 1]], c = m.positions[m.indices[i + 2]];
        const Vec3 n = normalized(cross(b - a, c - a));
        for (std::size_t k = 0; k < 3; ++k)
            EXPECT_TRUE(approxEqual(m.normals[m.indices[i + k]], n, 1e-12)) << "triangle " << i / 3;
    }
}

double totalLength(const std::vector<Edge3>& edges) {
    double sum = 0;
    for (const Edge3& e : edges) sum += length(e.b - e.a);
    return sum;
}

} // namespace

TEST(Mesh, BoxLayoutVolumeAndNormals) {
    const TriangleMesh box = makeBox({2, 3, 4});
    EXPECT_EQ(box.positions.size(), 24u);
    EXPECT_EQ(box.normals.size(), 24u);
    EXPECT_EQ(box.triangleCount(), 12u);
    EXPECT_DOUBLE_EQ(volume(box), 24.0); // 2 * 3 * 4
    expectFlatNormals(box);              // CCW seen from outside <=> geometric normal == stored outward normal
    const Aabb b = bounds(box);
    EXPECT_EQ(b.min, (Vec3{0, 0, 0}));
    EXPECT_EQ(b.max, (Vec3{2, 3, 4}));
    // Face f owns vertices 4f..4f+3 and triangles 2f, 2f+1, all on the face plane.
    const Vec3 size{2, 3, 4};
    for (int f = 0; f < 6; ++f) {
        const Vec3 n = boxFaceNormal(static_cast<BoxFace>(f));
        // Plane offset: dot(n, p) = size component for max faces, 0 for min faces.
        const double offset = std::max(0.0, dot(n, size));
        for (std::size_t k = 0; k < 6; ++k) {
            const std::uint32_t idx = box.indices[static_cast<std::size_t>(f) * 6 + k];
            EXPECT_GE(idx, static_cast<std::uint32_t>(4 * f));
            EXPECT_LT(idx, static_cast<std::uint32_t>(4 * f + 4));
            EXPECT_EQ(box.normals[idx], n);
            EXPECT_DOUBLE_EQ(dot(box.positions[idx], n), offset);
        }
    }
    EXPECT_EQ(boxFaceNormal(BoxFace::ZMax), (Vec3{0, 0, 1}));
    EXPECT_EQ(boxFaceNormal(BoxFace::XMin), (Vec3{-1, 0, 0}));
    EXPECT_EQ(static_cast<int>(BoxFace::YMin), 2); // A, B, L1, L2, W1, W2 order
}

TEST(Mesh, BoxIsWatertight) {
    const TriangleMesh box = makeBox({764, 560, 18});
    EXPECT_TRUE(everyEdgeSharedByTwoTriangles(box));
    EXPECT_TRUE(isWatertight(box));
    EXPECT_DOUBLE_EQ(volume(box), 764.0 * 560.0 * 18.0);
    // Remove one triangle: open mesh.
    TriangleMesh open = box;
    open.indices.resize(open.indices.size() - 3);
    EXPECT_FALSE(everyEdgeSharedByTwoTriangles(open));
    EXPECT_FALSE(isWatertight(open));
    // Flip one triangle: closed but inconsistently oriented.
    TriangleMesh flipped = box;
    std::swap(flipped.indices[1], flipped.indices[2]);
    EXPECT_FALSE(isWatertight(flipped));
    // Corrupt index / empty mesh.
    TriangleMesh bad = box;
    bad.indices[0] = 1000;
    EXPECT_FALSE(isWatertight(bad));
    EXPECT_FALSE(isWatertight(TriangleMesh{}));
}

TEST(Mesh, InvalidBoxIsEmpty) {
    EXPECT_TRUE(makeBox({0, 3, 4}).empty());
    EXPECT_TRUE(makeBox({2, -3, 4}).empty());
    EXPECT_TRUE(makeBox({2, 3, std::nan("")}).empty());
    EXPECT_TRUE(makeBoxEdges({2, 0, 4}).empty());
    EXPECT_TRUE(bounds(TriangleMesh{}).empty());
    EXPECT_DOUBLE_EQ(volume(TriangleMesh{}), 0.0);
}

TEST(Mesh, BoxEdges) {
    const auto edges = makeBoxEdges({2, 3, 4});
    ASSERT_EQ(edges.size(), 12u);
    EXPECT_DOUBLE_EQ(totalLength(edges), 4 * (2.0 + 3.0 + 4.0)); // 36
    EXPECT_EQ(edges[0], (Edge3{{0, 0, 0}, {2, 0, 0}}));
    EXPECT_EQ(edges[4], (Edge3{{0, 0, 0}, {0, 3, 0}}));
    EXPECT_EQ(edges[11], (Edge3{{2, 3, 0}, {2, 3, 4}}));
    for (std::size_t i = 0; i < 12; ++i) {
        const Vec3 d = edges[i].b - edges[i].a;
        EXPECT_EQ(d, (i < 4 ? Vec3{2, 0, 0} : i < 8 ? Vec3{0, 3, 0} : Vec3{0, 0, 4}));
    }
}

TEST(Mesh, PrismOfLShape) {
    const auto prism = makePrism(lShape(), 18.0);
    ASSERT_TRUE(prism.ok());
    const TriangleMesh& m = prism.value();
    // 6 base vertices: caps 2 * (6 - 2) = 8 triangles, sides 2 * 6 = 12 -> 20 triangles;
    // vertices 6 + 6 + 4 * 6 = 36.
    EXPECT_EQ(m.triangleCount(), 20u);
    EXPECT_EQ(m.positions.size(), 36u);
    // Volume = base area * height = 6400 * 18 = 115200.
    EXPECT_NEAR(volume(m), 115200.0, 1e-7);
    EXPECT_TRUE(everyEdgeSharedByTwoTriangles(m));
    EXPECT_TRUE(isWatertight(m));
    expectFlatNormals(m);
    EXPECT_EQ(bounds(m).min, (Vec3{0, 0, 0}));
    EXPECT_EQ(bounds(m).max, (Vec3{100, 100, 18}));
    // Layout: first 4 triangles are the bottom cap (normal -Z), next 4 the top cap (+Z).
    for (std::size_t t = 0; t < 8; ++t)
        for (std::size_t k = 0; k < 3; ++k) {
            const std::uint32_t idx = m.indices[3 * t + k];
            EXPECT_EQ(m.normals[idx], (t < 4 ? Vec3{0, 0, -1} : Vec3{0, 0, 1}));
            EXPECT_DOUBLE_EQ(m.positions[idx].z, t < 4 ? 0.0 : 18.0);
        }
    // Side of edge 2, (100,40) -> (40,40), faces +Y (into the notch).
    EXPECT_EQ(m.normals[m.indices[3 * (8 + 2 * 2)]], (Vec3{0, 1, 0}));
}

TEST(Mesh, PrismClockwiseBaseAndCollinearVertices) {
    const auto cw = makePrism(reversed(lShape()), 18.0);
    ASSERT_TRUE(cw.ok());
    EXPECT_NEAR(volume(cw.value()), 115200.0, 1e-7); // still positive: outward orientation
    EXPECT_TRUE(isWatertight(cw.value()));
    expectFlatNormals(cw.value());
    // Rectangle 100 x 50 with a midpoint on the bottom and top edges: no T-junction cracks.
    const Polygon2 base{{{0, 0}, {50, 0}, {100, 0}, {100, 50}, {50, 50}, {0, 50}}};
    const auto p = makePrism(base, 10.0);
    ASSERT_TRUE(p.ok());
    EXPECT_NEAR(volume(p.value()), 100.0 * 50.0 * 10.0, 1e-8);
    EXPECT_TRUE(everyEdgeSharedByTwoTriangles(p.value()));
    EXPECT_TRUE(isWatertight(p.value()));
    // A box-shaped prism has the volume of the box.
    const auto asBox = makePrism(makeRectPolygon(Rect2::fromSize(2, 3)), 4.0);
    ASSERT_TRUE(asBox.ok());
    EXPECT_DOUBLE_EQ(volume(asBox.value()), 24.0);
}

TEST(Mesh, PrismErrors) {
    const auto bow = makePrism(Polygon2{{{0, 0}, {10, 10}, {10, 0}, {0, 10}}}, 5.0);
    ASSERT_FALSE(bow.ok());
    EXPECT_EQ(bow.error().code, fos::core::ErrorCode::InvalidArgument);
    EXPECT_FALSE(makePrism(lShape(), 0.0).ok());
    EXPECT_FALSE(makePrism(lShape(), -3.0).ok());
    EXPECT_FALSE(makePrism(Polygon2{}, 3.0).ok());
    EXPECT_FALSE(makePrismEdges(lShape(), 0.0).ok());
    EXPECT_FALSE(makePrismEdges(Polygon2{{{0, 0}, {10, 0}}}, 3.0).ok());
}

TEST(Mesh, PrismFeatureEdges) {
    const auto edges = makePrismEdges(lShape(), 18.0);
    ASSERT_TRUE(edges.ok());
    // 6 bottom + 6 top + 6 vertical; length = 2 * perimeter 400 + 6 * 18 = 908.
    ASSERT_EQ(edges.value().size(), 18u);
    EXPECT_DOUBLE_EQ(totalLength(edges.value()), 908.0);
    EXPECT_EQ(edges.value()[0], (Edge3{{0, 0, 0}, {100, 0, 0}}));
    EXPECT_EQ(edges.value()[6], (Edge3{{0, 0, 18}, {100, 0, 18}}));
    EXPECT_EQ(edges.value()[12], (Edge3{{0, 0, 0}, {0, 0, 18}}));
    // A collinear base vertex adds ring edges but no vertical edge: 7 + 7 + 6 = 20.
    const Polygon2 withMid{{{0, 0}, {50, 0}, {100, 0}, {100, 40}, {40, 40}, {40, 100}, {0, 100}}};
    const auto e2 = makePrismEdges(withMid, 18.0);
    ASSERT_TRUE(e2.ok());
    EXPECT_EQ(e2.value().size(), 20u);
    EXPECT_DOUBLE_EQ(totalLength(e2.value()), 908.0);
}

TEST(Mesh, TransformedKeepsVolumeAndRotatesNormals) {
    // Vertical side panel: length -> +Z, width -> +Y, thickness -> -X, at x = 18.
    const Transform side = Transform::fromAxes({0, 0, 1}, {0, 1, 0}, {18, 0, 0});
    const TriangleMesh box = makeBox({720, 560, 18});
    const TriangleMesh world = transformed(box, side);
    EXPECT_TRUE(approxEqual(bounds(world).min, Vec3{0, 0, 0}));
    EXPECT_TRUE(approxEqual(bounds(world).max, Vec3{18, 560, 720}));
    EXPECT_NEAR(volume(world), 720.0 * 560.0 * 18.0, 1e-6);
    EXPECT_TRUE(isWatertight(world));
    expectFlatNormals(world);
    // Face A (local +Z) now faces -X.
    EXPECT_EQ(world.normals[0], (Vec3{-1, 0, 0}));
    // Volume is invariant under an arbitrary rigid motion.
    const Transform t = Transform::translation({100, -50, 25}) * Transform::rotationZ(37) * Transform::rotationX(-112);
    EXPECT_NEAR(volume(transformed(box, t)), 720.0 * 560.0 * 18.0, 1e-4);
    EXPECT_TRUE(isWatertight(transformed(box, t)));
    const auto edges = transformed(makeBoxEdges({720, 560, 18}), side);
    ASSERT_EQ(edges.size(), 12u);
    EXPECT_EQ(edges[0], (Edge3{{18, 0, 0}, {18, 0, 720}})); // local X edge runs up
    EXPECT_NEAR(totalLength(edges), 4 * (720.0 + 560.0 + 18.0), 1e-9);
}
