// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/mesh.hpp"

#include "fos/geometry/triangulate.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace fos::geometry {

namespace {

bool validSize(Vec3 s) noexcept {
    return std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.z) && s.x > 0 && s.y > 0 && s.z > 0;
}

std::uint32_t u32(std::size_t v) noexcept { return static_cast<std::uint32_t>(v); }

void addQuad(TriangleMesh& m, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 normal) {
    const std::uint32_t base = u32(m.positions.size());
    for (Vec3 p : {p0, p1, p2, p3}) {
        m.positions.push_back(p);
        m.normals.push_back(normal);
    }
    for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m.indices.push_back(base + i);
}

/// Validates prism input and returns the base as a CCW ring.
core::Result<Polygon2> prismBase(const Polygon2& base, double height) {
    if (!std::isfinite(height) || !(height > 0.0))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Prism height must be greater than 0 mm (got " + std::to_string(height) + ").");
    if (!isSimple(base))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Prism base with " + std::to_string(base.size()) +
                                   " vertices is not a simple polygon (repeated vertices, self-intersection or "
                                   "zero area).");
    return normalizedCcw(base);
}

} // namespace

Vec3 boxFaceNormal(BoxFace face) noexcept {
    switch (face) {
    case BoxFace::ZMax: return {0, 0, 1};
    case BoxFace::ZMin: return {0, 0, -1};
    case BoxFace::YMin: return {0, -1, 0};
    case BoxFace::YMax: return {0, 1, 0};
    case BoxFace::XMin: return {-1, 0, 0};
    case BoxFace::XMax: return {1, 0, 0};
    }
    return {};
}

TriangleMesh makeBox(Vec3 size) {
    TriangleMesh m;
    if (!validSize(size)) return m;
    const double L = size.x, W = size.y, T = size.z;
    m.positions.reserve(24);
    m.normals.reserve(24);
    m.indices.reserve(36);
    // Each quad is counter-clockwise seen from outside (cross(p1-p0, p2-p0) points outwards).
    addQuad(m, {0, 0, T}, {L, 0, T}, {L, W, T}, {0, W, T}, {0, 0, 1});  // ZMax
    addQuad(m, {0, 0, 0}, {0, W, 0}, {L, W, 0}, {L, 0, 0}, {0, 0, -1}); // ZMin
    addQuad(m, {0, 0, 0}, {L, 0, 0}, {L, 0, T}, {0, 0, T}, {0, -1, 0}); // YMin
    addQuad(m, {0, W, 0}, {0, W, T}, {L, W, T}, {L, W, 0}, {0, 1, 0});  // YMax
    addQuad(m, {0, 0, 0}, {0, 0, T}, {0, W, T}, {0, W, 0}, {-1, 0, 0}); // XMin
    addQuad(m, {L, 0, 0}, {L, W, 0}, {L, W, T}, {L, 0, T}, {1, 0, 0});  // XMax
    return m;
}

std::vector<Edge3> makeBoxEdges(Vec3 size) {
    if (!validSize(size)) return {};
    const double L = size.x, W = size.y, T = size.z;
    return {
        {{0, 0, 0}, {L, 0, 0}}, {{0, W, 0}, {L, W, 0}}, {{0, 0, T}, {L, 0, T}}, {{0, W, T}, {L, W, T}}, // along X
        {{0, 0, 0}, {0, W, 0}}, {{L, 0, 0}, {L, W, 0}}, {{0, 0, T}, {0, W, T}}, {{L, 0, T}, {L, W, T}}, // along Y
        {{0, 0, 0}, {0, 0, T}}, {{L, 0, 0}, {L, 0, T}}, {{0, W, 0}, {0, W, T}}, {{L, W, 0}, {L, W, T}}, // along Z
    };
}

core::Result<TriangleMesh> makePrism(const Polygon2& base, double height) {
    auto ring = prismBase(base, height);
    if (!ring) return ring.error();
    const Polygon2& p = ring.value();
    auto tris = triangulate(p);
    if (!tris) return tris.error();

    const std::size_t n = p.size();
    TriangleMesh m;
    m.positions.reserve(6 * n);
    m.normals.reserve(6 * n);
    m.indices.reserve(3 * (2 * (n - 2) + 2 * n));
    for (Vec2 v : p.vertices) { // bottom cap vertices 0..n-1
        m.positions.push_back({v.x, v.y, 0.0});
        m.normals.push_back({0, 0, -1});
    }
    for (Vec2 v : p.vertices) { // top cap vertices n..2n-1
        m.positions.push_back({v.x, v.y, height});
        m.normals.push_back({0, 0, 1});
    }
    for (const TriangleIndices& t : tris.value()) // bottom: reversed to face -Z
        for (std::uint32_t i : {t[0], t[2], t[1]}) m.indices.push_back(i);
    for (const TriangleIndices& t : tris.value())
        for (std::uint32_t i : {t[0], t[1], t[2]}) m.indices.push_back(u32(n) + i);
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = p[i], b = p[(i + 1) % n];
        const Vec2 d = b - a;
        const double len = length(d);
        addQuad(m, {a.x, a.y, 0.0}, {b.x, b.y, 0.0}, {b.x, b.y, height}, {a.x, a.y, height},
                {d.y / len, -d.x / len, 0.0});
    }
    return m;
}

core::Result<std::vector<Edge3>> makePrismEdges(const Polygon2& base, double height) {
    auto ring = prismBase(base, height);
    if (!ring) return ring.error();
    const Polygon2& p = ring.value();
    const std::size_t n = p.size();
    std::vector<Edge3> edges;
    edges.reserve(3 * n);
    for (double z : {0.0, height})
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2 a = p[i], b = p[(i + 1) % n];
            edges.push_back({{a.x, a.y, z}, {b.x, b.y, z}});
        }
    for (std::size_t i = 0; i < n; ++i) {
        if (orientation(p[(i + n - 1) % n], p[i], p[(i + 1) % n]) == Orientation::Collinear) continue;
        edges.push_back({{p[i].x, p[i].y, 0.0}, {p[i].x, p[i].y, height}});
    }
    return edges;
}

TriangleMesh transformed(const TriangleMesh& mesh, const Transform& t) {
    TriangleMesh out = mesh;
    for (Vec3& p : out.positions) p = t.applyPoint(p);
    for (Vec3& nrm : out.normals) nrm = t.applyVector(nrm);
    return out;
}

std::vector<Edge3> transformed(const std::vector<Edge3>& edges, const Transform& t) {
    std::vector<Edge3> out = edges;
    for (Edge3& e : out) e = {t.applyPoint(e.a), t.applyPoint(e.b)};
    return out;
}

Aabb bounds(const TriangleMesh& mesh) noexcept {
    Aabb box;
    for (Vec3 p : mesh.positions) box.expand(p);
    return box;
}

double volume(const TriangleMesh& mesh) noexcept {
    double six = 0.0;
    const std::size_t count = mesh.positions.size();
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
        if (ia >= count || ib >= count || ic >= count) continue;
        six += dot(mesh.positions[ia], cross(mesh.positions[ib], mesh.positions[ic]));
    }
    return six / 6.0;
}

bool isWatertight(const TriangleMesh& mesh) {
    if (mesh.indices.empty() || mesh.indices.size() % 3 != 0) return false;
    const std::size_t count = mesh.positions.size();
    for (std::uint32_t i : mesh.indices)
        if (i >= count) return false;

    // Weld vertices with identical coordinates: weld[i] = smallest rank among equals.
    const auto less = [](Vec3 a, Vec3 b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    };
    std::vector<std::uint32_t> order(count);
    for (std::size_t i = 0; i < count; ++i) order[i] = u32(i);
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        return less(mesh.positions[a], mesh.positions[b]) || (mesh.positions[a] == mesh.positions[b] && a < b);
    });
    std::vector<std::uint32_t> weld(count);
    for (std::size_t k = 0; k < count; ++k)
        weld[order[k]] = (k > 0 && mesh.positions[order[k]] == mesh.positions[order[k - 1]]) ? weld[order[k - 1]]
                                                                                             : order[k];

    std::vector<std::pair<std::uint32_t, std::uint32_t>> directed;
    directed.reserve(mesh.indices.size());
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const std::uint32_t a = weld[mesh.indices[i]], b = weld[mesh.indices[i + 1]], c = weld[mesh.indices[i + 2]];
        if (a == b || b == c || c == a) return false;
        directed.push_back({a, b});
        directed.push_back({b, c});
        directed.push_back({c, a});
    }
    std::sort(directed.begin(), directed.end());
    if (std::adjacent_find(directed.begin(), directed.end()) != directed.end()) return false; // used twice
    for (const auto& e : directed)
        if (!std::binary_search(directed.begin(), directed.end(), std::make_pair(e.second, e.first))) return false;
    return true;
}

} // namespace fos::geometry
