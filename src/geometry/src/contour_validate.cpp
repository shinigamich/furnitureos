// SPDX-License-Identifier: AGPL-3.0-only
// Structural validation of contours: counts, finiteness, zero-length segments and exact
// self-intersection tests between lines and arcs (ADR-0008).
#include "contour_internal.hpp"
#include "fos/geometry/contour.hpp"
#include "fos/geometry/curve_intersect.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace fos::geometry {
namespace {

core::Error invalid(std::string message) {
    return core::makeError(core::ErrorCode::InvalidArgument, std::move(message), "contour");
}

std::string segName(std::size_t i) { return "Contour segment " + std::to_string(i); }

} // namespace

core::Result<void> validateContour(const Contour& c, double eps) {
    const std::size_t n = c.size();
    bool anyArc = false;
    for (std::size_t i = 0; i < n; ++i) {
        const ContourVertex& v = c.vertices[i];
        if (!std::isfinite(v.point.x) || !std::isfinite(v.point.y))
            return invalid("Contour vertex " + std::to_string(i) + " has a non-finite coordinate.");
        if (!std::isfinite(v.bulge) || std::fabs(v.bulge) >= 1e6)
            return invalid(segName(i) + " has bulge " + std::to_string(v.bulge) +
                           "; a bulge must be finite with |bulge| < 1e6 (use two segments for a full circle).");
        anyArc = anyArc || v.bulge != 0.0;
    }
    if (n < 2 || (n == 2 && !anyArc))
        return invalid("A contour needs at least 3 vertices, or 2 vertices when a segment is an arc (got " +
                       std::to_string(n) + " vertices" + (n == 2 ? ", both segments straight" : "") + ").");

    std::vector<ContourSegmentInfo> seg(n);
    for (std::size_t i = 0; i < n; ++i) {
        seg[i] = segmentInfo(c, i);
        if (length(seg[i].end - seg[i].start) <= eps)
            return invalid(segName(i) + " starting at " + detail::formatPoint(seg[i].start) +
                           " has zero length (its end vertex repeats its start vertex).");
    }

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            // Vertices shared by the two segments (adjacent: one; a 2-segment ring: both).
            std::vector<Vec2> shared;
            if (j == i + 1) shared.push_back(c.vertices[j].point);
            if (i == 0 && j == n - 1) shared.push_back(c.vertices[0].point);
            const CurveIntersection x = intersectContourSegments(seg[i], seg[j], eps);
            if (x.kind == CurveIntersectionKind::None) continue;
            if (x.kind == CurveIntersectionKind::Overlap)
                return invalid(segName(i) + " and segment " + std::to_string(j) +
                               " overlap (the contour folds back onto itself).");
            for (Vec2 p : x.points) {
                bool atShared = false;
                for (Vec2 s : shared) atShared = atShared || length(p - s) <= eps;
                if (atShared) continue;
                return invalid(segName(i) + " and segment " + std::to_string(j) + (shared.empty() ? " touch" : " meet again") +
                               " at " + detail::formatPoint(p) + "; the contour must not self-intersect.");
            }
        }
    }
    return {};
}

} // namespace fos::geometry
