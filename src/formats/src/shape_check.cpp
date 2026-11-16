// SPDX-License-Identifier: AGPL-3.0-only
// Semantic validation of domain::PartShape (ADR-0008), shared by the codec and the OBJ export.
#include "codec_reader.hpp"
#include "fos/core/units.hpp"
#include "fos/formats/codec.hpp"
#include "fos/geometry/curve_intersect.hpp"
#include "shape_codec.hpp"

#include <algorithm>
#include <cmath>

namespace fos::formats::detail {
namespace {

using geometry::Contour;
using geometry::ContourSegmentInfo;
using geometry::Rect2;
using geometry::Vec2;

constexpr double kEps = core::kLengthEps; // contact distance for all exact tests, mm

std::string mm(double value) { return core::formatLength(value, 7); }
std::string point(Vec2 p) { return "(" + mm(p.x) + ", " + mm(p.y) + ")"; }

/// Conservative axis-aligned box of one segment (an arc gets its full circle's box), grown by eps.
Rect2 segmentBox(const ContourSegmentInfo& s) {
    Rect2 box;
    if (s.isArc) {
        box.min = {s.center.x - s.radius, s.center.y - s.radius};
        box.max = {s.center.x + s.radius, s.center.y + s.radius};
    } else {
        box.min = {std::min(s.start.x, s.end.x), std::min(s.start.y, s.end.y)};
        box.max = {std::max(s.start.x, s.end.x), std::max(s.start.y, s.end.y)};
    }
    box.min = {box.min.x - kEps, box.min.y - kEps};
    box.max = {box.max.x + kEps, box.max.y + kEps};
    return box;
}

bool overlaps(const Rect2& a, const Rect2& b) {
    return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y;
}

struct Segments {
    std::vector<ContourSegmentInfo> info;
    std::vector<Rect2> box;
    Rect2 all; ///< Union of the segment boxes.
};

Segments segmentsOf(const Contour& c) {
    Segments s;
    s.info.reserve(c.size());
    s.box.reserve(c.size());
    for (std::size_t i = 0; i < c.size(); ++i) {
        s.info.push_back(geometry::segmentInfo(c, i));
        s.box.push_back(segmentBox(s.info.back()));
        s.all.min = {std::min(s.all.min.x, s.box.back().min.x), std::min(s.all.min.y, s.box.back().min.y)};
        s.all.max = {std::max(s.all.max.x, s.box.back().max.x), std::max(s.all.max.y, s.box.back().max.y)};
    }
    return s;
}

/// First contact between a segment of `a` and a segment of `b`: "segment i of <a> touches or
/// crosses segment j of <b>[ at (x, y)]", or "" when they are farther apart than eps.
std::string firstContact(const Segments& a, const std::string& aName, const Segments& b, const std::string& bName) {
    if (!overlaps(a.all, b.all)) return {};
    for (std::size_t i = 0; i < a.info.size(); ++i) {
        if (!overlaps(a.box[i], b.all)) continue;
        for (std::size_t j = 0; j < b.info.size(); ++j) {
            if (!overlaps(a.box[i], b.box[j])) continue;
            const auto hit = geometry::intersectContourSegments(a.info[i], b.info[j], kEps);
            if (hit.kind == geometry::CurveIntersectionKind::None) continue;
            std::string text = "segment " + std::to_string(i) + " of " + aName + " touches or crosses segment " +
                               std::to_string(j) + " of " + bName;
            if (!hit.points.empty()) text += " at " + point(hit.points.front());
            return text;
        }
    }
    return {};
}

std::string cutoutName(std::size_t k) { return "cut-out " + std::to_string(k); }

} // namespace

core::Result<void> checkPartShape(const domain::Part& part, std::string_view partPath) {
    if (!part.shape) return {};
    const domain::PartShape& shape = *part.shape;
    const std::string base = joinPath(partPath, "shape");
    const std::string outerPath = joinPath(base, "outer");
    const std::string cutoutsPath = joinPath(base, "cutouts");

    for (const domain::EdgeSide side : domain::kAllEdges)
        if (const auto& band = part.edge(side))
            return schemaError(joinPath(joinPath(partPath, "edges"), enumToToken(side)),
                               "the part has a shape, so its edge bands are given per outer segment in "
                               "\"shape\".\"outerEdges\" and \"edges\" must be empty, but edge " +
                                   std::string(enumToToken(side)) + " has the band \"" + band->str() + "\"");

    // Limits (also checked while reading JSON; repeated here for in-memory models).
    if (shape.outer.size() > kMaxContourVertices)
        return schemaError(outerPath, "the outer contour has " + std::to_string(shape.outer.size()) +
                                          " vertices; at most " + std::to_string(kMaxContourVertices) +
                                          " are allowed per contour");
    if (shape.cutouts.size() > kMaxShapeCutouts)
        return schemaError(cutoutsPath, "the part has " + std::to_string(shape.cutouts.size()) +
                                            " cut-outs; at most " + std::to_string(kMaxShapeCutouts) + " are allowed");
    std::size_t total = shape.outer.size();
    for (std::size_t k = 0; k < shape.cutouts.size(); ++k) {
        total += shape.cutouts[k].size();
        if (total > kMaxShapeVertices) return schemaError(joinPath(cutoutsPath, k), totalVerticesMessage(total));
    }
    for (std::size_t k = 0; k < shape.cutouts.size(); ++k)
        if (shape.cutouts[k].size() > kMaxContourVertices)
            return schemaError(joinPath(cutoutsPath, k), cutoutName(k) + " has " +
                                                             std::to_string(shape.cutouts[k].size()) +
                                                             " vertices; at most " +
                                                             std::to_string(kMaxContourVertices) +
                                                             " are allowed per contour");

    // Outer contour.
    if (auto valid = geometry::validateContour(shape.outer, kEps); !valid)
        return schemaError(outerPath, "invalid outer contour: " + valid.error().message);
    const double outerArea = geometry::signedArea(shape.outer);
    if (!(outerArea > 0))
        return schemaError(outerPath, "the outer contour is clockwise (signed area " + core::formatLength(outerArea, 3) +
                                          " mm^2); it must be counter-clockwise");
    const Rect2 box = geometry::bounds(shape.outer);
    const double L = part.length, W = part.width, tol = kShapeBoundsTolerance;
    if (!(std::abs(box.min.x) <= tol && std::abs(box.min.y) <= tol && std::abs(box.max.x - L) <= tol &&
          std::abs(box.max.y - W) <= tol))
        return schemaError(outerPath, "the bounding box of the outer contour is [" + mm(box.min.x) + ", " +
                                          mm(box.max.x) + "] x [" + mm(box.min.y) + ", " + mm(box.max.y) +
                                          "] mm but the part's length x width " + mm(L) + " x " + mm(W) +
                                          " mm requires [0, " + mm(L) + "] x [0, " + mm(W) +
                                          "] (tolerance " + mm(tol) + " mm)");

    // Cut-outs.
    const Segments outer = segmentsOf(shape.outer);
    std::vector<Segments> cutouts;
    cutouts.reserve(shape.cutouts.size());
    for (std::size_t k = 0; k < shape.cutouts.size(); ++k) {
        const Contour& c = shape.cutouts[k];
        const std::string path = joinPath(cutoutsPath, k);
        if (auto valid = geometry::validateContour(c, kEps); !valid)
            return schemaError(path, "invalid " + cutoutName(k) + ": " + valid.error().message);
        for (std::size_t i = 0; i < c.size(); ++i) {
            const auto where = geometry::locatePoint(shape.outer, c.vertices[i].point, kEps);
            if (where != geometry::PointLocation::Inside)
                return schemaError(joinPath(path, i),
                                   "vertex " + std::to_string(i) + " " + point(c.vertices[i].point) + " of " +
                                       cutoutName(k) +
                                       (where == geometry::PointLocation::Outside
                                            ? " lies outside the outer contour"
                                            : " lies on the outer contour (closer than " + mm(kEps) + " mm)") +
                                       "; cut-outs must lie strictly inside the outer contour");
        }
        cutouts.push_back(segmentsOf(c));
        // All vertices inside + no contact with the outer boundary => the whole (connected)
        // cut-out boundary is inside, arcs included.
        if (const std::string contact = firstContact(cutouts.back(), cutoutName(k), outer, "the outer contour");
            !contact.empty())
            return schemaError(path, contact + "; cut-outs must lie strictly inside the outer contour");
    }
    for (std::size_t m = 1; m < cutouts.size(); ++m) {
        const std::string path = joinPath(cutoutsPath, m);
        for (std::size_t k = 0; k < m; ++k) {
            if (const std::string contact = firstContact(cutouts[m], cutoutName(m), cutouts[k], cutoutName(k));
                !contact.empty())
                return schemaError(path, contact + "; cut-outs must not touch or overlap each other");
            // No contact: either disjoint or one strictly inside the other.
            const Vec2 pm = shape.cutouts[m].vertices.front().point;
            const Vec2 pk = shape.cutouts[k].vertices.front().point;
            if (geometry::locatePoint(shape.cutouts[k], pm, kEps) != geometry::PointLocation::Outside)
                return schemaError(path, cutoutName(m) + " lies inside " + cutoutName(k) + " (its vertex 0 " +
                                             point(pm) + " is enclosed); nested cut-outs are not allowed");
            if (geometry::locatePoint(shape.cutouts[m], pk, kEps) != geometry::PointLocation::Outside)
                return schemaError(path, cutoutName(m) + " encloses " + cutoutName(k) + " (vertex 0 " + point(pk) +
                                             " of " + cutoutName(k) + " is inside it); nested cut-outs are not allowed");
        }
    }

    // Per-segment edge bands.
    const std::string edgesPath = joinPath(base, "outerEdges");
    if (!shape.outerEdges.empty() && shape.outerEdges.size() != shape.outer.size())
        return schemaError(edgesPath, "has " + std::to_string(shape.outerEdges.size()) +
                                          " entries but the outer contour has " + std::to_string(shape.outer.size()) +
                                          " segments; give one entry (edge band id or null) per segment, or an "
                                          "empty array when no segment is banded");
    for (std::size_t i = 0; i < shape.outerEdges.size(); ++i)
        if (shape.outerEdges[i] && shape.outerEdges[i]->str().empty())
            return schemaError(joinPath(edgesPath, i), "expected an edge band id (non-empty) or null, got \"\"");
    return {};
}

} // namespace fos::formats::detail
