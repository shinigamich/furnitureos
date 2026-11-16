// SPDX-License-Identifier: AGPL-3.0-only
/// Contoured-part checks (ADR-0008), run by check 1 (parts) and check 2 (edge banding).
/// Segment and cut-out numbers in messages are 0-based indices, exactly as in PartShape.
#include "detail.hpp"

#include "fos/geometry/curve_intersect.hpp"
#include "fos/machining/material_region.hpp"

#include <cmath>

namespace fos::validation::detail {
namespace {
using core::Severity;

/// Bounding-box tolerance of the [0,L]x[0,W] invariant, mm.
constexpr double kBoundsEps = core::kLengthEps;

std::string seg(std::size_t i) { return "segment " + std::to_string(i); }
std::string cut(std::size_t i) { return "cut-out " + std::to_string(i); }

bool inside(const geometry::Contour& outer, const geometry::Contour& c) {
    return geometry::locatePoint(outer, c.vertices.front().point) == geometry::PointLocation::Inside;
}
} // namespace

void checkPartShape(Context& c, const domain::Assembly& a, const domain::Part& p) {
    if (!p.shape) return;
    const domain::PartShape& s = *p.shape;
    const std::string pid = p.id.str(), aid = a.id.str();
    auto err = [&](const char* code, std::string msg, std::vector<std::string> related = {}) {
        c.add(Severity::Error, code, std::move(msg), pid, aid, pid, std::move(related));
    };

    bool edgesOk = true;
    for (const domain::EdgeSide side : domain::kAllEdges)
        if (p.edge(side)) {
            err("validation.shape.edges_with_shape",
                label(p) + " has a shape but also an edge band on rectangular edge " + domain::toString(side) +
                    "; contoured parts band their outline segments (PartShape::outerEdges) and Part::edges must be "
                    "empty.");
            edgesOk = false;
            break;
        }
    const std::size_t n = s.outer.size();
    const bool countOk = s.outerEdges.empty() || s.outerEdges.size() == n;
    if (!countOk)
        err("validation.shape.outer_edges_count",
            label(p) + " has " + std::to_string(s.outerEdges.size()) + " outline edge band entries but its outline has " +
                std::to_string(n) + " segments; give one entry per segment or none.");

    const auto valid = geometry::validateContour(s.outer);
    if (!valid) {
        err("validation.shape.invalid_outline", label(p) + " has an invalid outline: " + valid.error().message);
        return; // nothing below is meaningful
    }
    bool outerOk = true;
    if (geometry::signedArea(s.outer) < 0) {
        err("validation.shape.clockwise_outline",
            label(p) + " has a clockwise outline (signed area " + num(geometry::signedArea(s.outer)) +
                " mm2); the outline must run counter-clockwise.");
        outerOk = false;
    }
    const auto b = geometry::bounds(s.outer);
    if (std::fabs(b.min.x) > kBoundsEps || std::fabs(b.min.y) > kBoundsEps ||
        std::fabs(b.max.x - p.length) > kBoundsEps || std::fabs(b.max.y - p.width) > kBoundsEps) {
        err("validation.shape.bounds_mismatch",
            label(p) + " has an outline whose bounding box is [" + num(b.min.x) + ", " + num(b.max.x) + "] x [" +
                num(b.min.y) + ", " + num(b.max.y) + "] mm, but it must be exactly [0, " + num(p.length) + "] x [0, " +
                num(p.width) + "] mm (the finished length and width).");
        outerOk = false;
    }

    std::vector<std::size_t> goodCutouts;
    for (std::size_t k = 0; k < s.cutouts.size(); ++k) {
        const auto& co = s.cutouts[k];
        const auto v = geometry::validateContour(co);
        if (!v) {
            err("validation.shape.invalid_cutout", label(p) + " " + cut(k) + " is invalid: " + v.error().message);
            continue;
        }
        if (machining::contoursTouch(co, s.outer) || !inside(s.outer, co)) {
            err("validation.shape.cutout_outside",
                label(p) + " " + cut(k) + " is not strictly inside the outline (it touches, crosses or lies outside "
                           "it); cut-outs are through openings surrounded by material.");
            continue;
        }
        goodCutouts.push_back(k);
    }
    for (std::size_t x = 0; x < goodCutouts.size(); ++x)
        for (std::size_t y = x + 1; y < goodCutouts.size(); ++y) {
            const auto& cx = s.cutouts[goodCutouts[x]];
            const auto& cy = s.cutouts[goodCutouts[y]];
            if (machining::contoursTouch(cx, cy) || inside(cx, cy) || inside(cy, cx))
                err("validation.shape.cutouts_overlap",
                    label(p) + " " + cut(goodCutouts[x]) + " and " + cut(goodCutouts[y]) +
                        " touch or overlap; merge them into one cut-out.");
        }

    if (outerOk && countOk && edgesOk) {
        const auto raw = domain::cutContour(p, c.model);
        if (!raw)
            err("validation.shape.cut_contour",
                label(p) + ": the raw outline before edge banding (finished outline offset inwards by the band "
                           "thicknesses) cannot be computed: " + raw.error().message);
    }
}

void checkShapeBands(Context& c, const domain::Assembly& a, const domain::Part& p) {
    if (!p.shape) return;
    const domain::PartShape& s = *p.shape;
    if (s.outerEdges.empty() || s.outerEdges.size() != s.outer.size()) return; // reported by checkPartShape
    const std::string pid = p.id.str(), aid = a.id.str();
    const double area = geometry::signedArea(s.outer);
    for (std::size_t i = 0; i < s.outerEdges.size(); ++i) {
        const auto& ref = s.outerEdges[i];
        if (!ref) continue;
        const std::string where = label(p) + " outline " + seg(i);
        const domain::EdgeBand* band = c.model.findEdgeBand(*ref);
        if (!band) {
            c.add(Severity::Error, "validation.edge.missing_band",
                  where + " refers to edge band '" + ref->str() +
                      "', which is not in the model's edge band library; the edge would stay raw.",
                  pid, aid, pid, {ref->str()});
            continue;
        }
        if (finite(p.thickness) && finite(band->width) && band->width < p.thickness - core::kLengthEps)
            c.add(Severity::Error, "validation.edge.band_too_narrow",
                  where + ": edge band '" + band->id.str() + "' is " + num(band->width) + " mm wide but the board is " +
                      num(p.thickness) + " mm thick.",
                  pid, aid, pid, {band->id.str()});
        const double factor = c.options.edgeBandMinBendRadiusFactor;
        if (!(factor > 0) || !finite(band->thickness) || !(band->thickness > 0) || area == 0) continue;
        const geometry::ContourSegmentInfo info = geometry::segmentInfo(s.outer, i);
        if (!info.isArc) continue;
        // Convex arc: turns the same way as the outline; the band's inner face (on the raw
        // substrate) then has radius R - t, on a concave arc R + t.
        const bool convex = (info.sweep > 0) == (area > 0);
        const double bend = convex ? info.radius - band->thickness : info.radius + band->thickness;
        const double minimum = factor * band->thickness;
        if (bend < minimum - core::kLengthEps)
            c.add(Severity::Warning, "validation.edge.radius_too_small",
                  where + " is a " + (convex ? "convex" : "concave") + " arc of radius " + num(info.radius) +
                      " mm banded with '" + band->id.str() + "' (" + num(band->thickness) +
                      " mm thick): the band would bend around " + num(bend) + " mm, below the minimum of " +
                      num(minimum) + " mm (" + num(factor) +
                      " x band thickness; generic, unverified rule of thumb - check the band supplier's data).",
                  pid, aid, pid, {band->id.str()});
    }
}

} // namespace fos::validation::detail
