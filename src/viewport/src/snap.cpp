// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/viewport/snap.hpp"

#include "fos/geometry/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace fos::viewport {
using geometry::Vec3;

namespace {
/// Start offset (mm) of the occlusion ray from the candidate toward the viewer.
constexpr double kOcclusionTolerance = 0.05;

bool sameXY(const geometry::Edge3& e) noexcept {
    return std::abs(e.a.x - e.b.x) <= core::kLengthEps && std::abs(e.a.y - e.b.y) <= core::kLengthEps &&
           std::abs(e.a.z - e.b.z) > core::kLengthEps;
}
} // namespace

std::vector<SnapPoint> partSnapPoints(const Scene& scene, std::size_t index) {
    std::vector<SnapPoint> out;
    if (!scene.isVisible(index)) return out;
    const ScenePart& part = scene.parts()[index];
    auto add = [&](Vec3 local, SnapKind kind) {
        const Vec3 w = part.world.applyPoint(local);
        if (scene.section() && !scene.section()->keeps(w)) return;
        out.push_back({w, kind, index});
    };
    if (part.contoured) {
        for (const auto& e : scene.localEdges(index)) {
            if (!sameXY(e)) continue;
            add(e.a, SnapKind::OutlineVertex);
            add(e.b, SnapKind::OutlineVertex);
            add((e.a + e.b) * 0.5, SnapKind::EdgeMidpoint);
        }
        return out;
    }
    const Vec3 s = part.size;
    for (int i = 0; i < 8; ++i) add({(i & 1) ? s.x : 0, (i & 2) ? s.y : 0, (i & 4) ? s.z : 0}, SnapKind::Corner);
    for (const auto& e : geometry::makeBoxEdges(s)) add((e.a + e.b) * 0.5, SnapKind::EdgeMidpoint);
    const Vec3 c = s * 0.5;
    // geometry::BoxFace order: ZMax, ZMin, YMin, YMax, XMin, XMax.
    for (Vec3 f : {Vec3{c.x, c.y, s.z}, Vec3{c.x, c.y, 0}, Vec3{c.x, 0, c.z}, Vec3{c.x, s.y, c.z},
                   Vec3{0, c.y, c.z}, Vec3{s.x, c.y, c.z}})
        add(f, SnapKind::FaceCenter);
    return out;
}

std::optional<SnapPoint> snapAt(const Scene& scene, const Camera& camera, double px, double py, double radiusPx) {
    struct Near {
        double dist;
        SnapKind kind;
        std::size_t part, order;
        Vec3 point;
    };
    std::vector<Near> near;
    for (std::size_t i = 0; i < scene.parts().size(); ++i) {
        const auto pts = partSnapPoints(scene, i);
        for (std::size_t k = 0; k < pts.size(); ++k) {
            const auto pix = camera.projectToPixel(pts[k].point);
            if (!pix) continue;
            const double d = std::hypot(pix->x - px, pix->y - py);
            if (d <= radiusPx) near.push_back({d, pts[k].kind, i, k, pts[k].point});
        }
    }
    std::sort(near.begin(), near.end(), [](const Near& a, const Near& b) {
        return std::tie(a.dist, a.kind, a.part, a.order) < std::tie(b.dist, b.kind, b.part, b.order);
    });
    const bool perspective = camera.projection() == CameraProjection::Perspective;
    for (const Near& n : near) {
        // Occlusion: a ray from the candidate (offset by the tolerance) toward the viewer.
        const Vec3 dir = perspective ? geometry::normalized(camera.eye() - n.point) : camera.basis().towardViewer;
        const auto hit = scene.pickHit(geometry::Ray3{n.point + dir * kOcclusionTolerance, dir});
        if (!hit || (perspective && hit->t >= geometry::length(camera.eye() - n.point)))
            return SnapPoint{n.point, n.kind, n.part};
    }
    const auto hit = scene.pickHit(camera.rayThroughPixel(px, py));
    if (!hit) return std::nullopt;
    return SnapPoint{hit->point, SnapKind::Surface, hit->index};
}

} // namespace fos::viewport
