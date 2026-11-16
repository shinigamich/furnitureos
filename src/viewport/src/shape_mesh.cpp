// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/viewport/shape_mesh.hpp"

#include "fos/geometry/triangulate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace fos::viewport {
using geometry::Vec2;
using geometry::Vec3;

namespace {

core::Error invalid(std::string msg) { return core::makeError(core::ErrorCode::InvalidArgument, std::move(msg)); }

Vec2 unit(Vec2 d) noexcept {
    const double l = length(d);
    return l > 0 ? d * (1.0 / l) : Vec2{};
}

/// Unit tangent of segment s at its start (atEnd = false) or end, in traversal direction.
Vec2 tangent(const geometry::ContourSegmentInfo& s, bool atEnd) noexcept {
    if (!s.isArc) return unit(s.end - s.start);
    const Vec2 r = unit((atEnd ? s.end : s.start) - s.center);
    return s.sweep > 0 ? Vec2{-r.y, r.x} : Vec2{r.y, -r.x};
}

/// Contour vertices where the tangent turns by more than 1 degree.
std::vector<Vec2> corners(const geometry::Contour& c) {
    std::vector<Vec2> out;
    const std::size_t n = c.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 tin = tangent(geometry::segmentInfo(c, (i + n - 1) % n), true);
        const Vec2 tout = tangent(geometry::segmentInfo(c, i), false);
        if (dot(tin, tout) < std::cos(core::degToRad(1.0))) out.push_back(c.vertices[i].point);
    }
    return out;
}

} // namespace

double shapeChordTolerance(double length, double width) noexcept {
    const double s = std::max(length, width) / 3000.0;
    if (!std::isfinite(s)) return kMaxShapeChordTolerance;
    return std::clamp(s, 0.01, kMaxShapeChordTolerance);
}

core::Result<ShapedPartGeometry> makeShapedPartGeometry(const geometry::Contour& outer,
                                                        const std::vector<geometry::Contour>& cutouts,
                                                        double thickness, double chordTolerance) {
    if (!(std::isfinite(thickness) && thickness > 0)) return invalid("The thickness must be a finite number > 0.");
    if (!(std::isfinite(chordTolerance) && chordTolerance > 0))
        return invalid("The chord tolerance must be a finite number > 0.");
    const geometry::Polygon2 op = geometry::tessellate(outer, chordTolerance);
    std::vector<geometry::Polygon2> hp;
    for (const geometry::Contour& c : cutouts) hp.push_back(geometry::tessellate(c, chordTolerance));
    // Shared triangulator: points are the outer ring (CCW) then every hole (CW), each input
    // vertex exactly once, so the side walls below close the mesh.
    auto tri = geometry::triangulateWithHoles(op, hp);
    if (!tri.ok()) return tri.error();
    const std::vector<Vec2>& v = tri.value().points;
    const auto& tris = tri.value().triangles;

    ShapedPartGeometry g;
    geometry::TriangleMesh& m = g.mesh;
    const auto vertex = [&](Vec2 p, double z, Vec3 nrm) {
        m.positions.push_back({p.x, p.y, z});
        m.normals.push_back(nrm);
        return static_cast<std::uint32_t>(m.positions.size() - 1);
    };
    // Caps: bottom reversed (normal -Z), top as triangulated (normal +Z).
    for (const double z : {0.0, thickness}) {
        const bool top = z > 0;
        const auto base = static_cast<std::uint32_t>(m.positions.size());
        for (const Vec2 p : v) vertex(p, z, {0, 0, top ? 1.0 : -1.0});
        for (const auto& t : tris) {
            m.indices.push_back(base + t[0]);
            m.indices.push_back(base + (top ? t[1] : t[2]));
            m.indices.push_back(base + (top ? t[2] : t[1]));
        }
    }
    // Rings in the order of `v`: outer (CCW) then holes (CW); outward = right of the traversal.
    const std::vector<std::size_t>& rs = tri.value().ringStart;
    for (std::size_t r = 0; r + 1 < rs.size(); ++r) {
        const std::size_t s = rs[r], n = rs[r + 1] - rs[r];
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2 a = v[s + i], b = v[s + (i + 1) % n];
            const Vec2 d = unit(b - a);
            const Vec3 nrm{d.y, -d.x, 0};
            const std::uint32_t a0 = vertex(a, 0, nrm), b0 = vertex(b, 0, nrm), b1 = vertex(b, thickness, nrm),
                                a1 = vertex(a, thickness, nrm);
            m.indices.insert(m.indices.end(), {a0, b0, b1, a0, b1, a1});
            g.edges.push_back({{a.x, a.y, 0}, {b.x, b.y, 0}});
            g.edges.push_back({{a.x, a.y, thickness}, {b.x, b.y, thickness}});
        }
    }
    const auto vertical = [&](const geometry::Contour& c) {
        for (const Vec2 p : corners(c)) g.edges.push_back({{p.x, p.y, 0}, {p.x, p.y, thickness}});
    };
    vertical(outer);
    for (const auto& c : cutouts) vertical(c);
    return g;
}

} // namespace fos::viewport
