// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/machining/flip.hpp"

#include "fos/machining/face.hpp"

#include <algorithm>

namespace fos::machining {
using geometry::Vec2;

Vec2 flippedFaceCoordinates(const domain::Part& part, domain::Face face, Vec2 uv, FlipAxis axis) noexcept {
    const FaceExtents e = faceExtents(part, face);
    return axis == FlipAxis::Length ? Vec2{uv.x, e.v.max - uv.y} : Vec2{e.u.max - uv.x, uv.y};
}

std::vector<domain::Operation> mirrorOperationsForFace(const domain::Part& part, domain::Face face, FlipAxis axis) {
    std::vector<domain::Operation> out;
    auto flip = [&](double u, double v) { return flippedFaceCoordinates(part, face, {u, v}, axis); };
    for (const auto& src : part.operations) {
        if (domain::faceOf(src) != face) continue;
        domain::Operation op = src;
        if (auto* h = std::get_if<domain::Hole>(&op.shape)) {
            const Vec2 p = flip(h->u, h->v);
            h->u = p.x;
            h->v = p.y;
        } else if (auto* g = std::get_if<domain::Groove>(&op.shape)) {
            const Vec2 a = flip(g->u1, g->v1), b = flip(g->u2, g->v2);
            g->u1 = a.x;
            g->v1 = a.y;
            g->u2 = b.x;
            g->v2 = b.y;
        } else if (auto* p = std::get_if<domain::Pocket>(&op.shape)) {
            for (auto& q : p->contour) q = flip(q.x, q.y);
            // Mirroring reverses the winding; restore it, keeping the first point first.
            if (p->contour.size() > 1) std::reverse(p->contour.begin() + 1, p->contour.end());
        }
        out.push_back(std::move(op));
    }
    return out;
}

} // namespace fos::machining
