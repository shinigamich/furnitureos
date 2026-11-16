// SPDX-License-Identifier: AGPL-3.0-only
#include "view_shape.hpp"

#include "contour_view.hpp"
#include "fos/geometry/curve_intersect.hpp"

#include <algorithm>
#include <cmath>

namespace fos::drawings::detail {
namespace {

constexpr double kEps = core::kLengthEps;
/// Chord tolerance (mm) of the polygon used to check arcs for the approximation warning only.
constexpr double kCheckChordTolerance = 0.05;

struct Candidate {
    Vec2 a, b;
    double depth = 0;
    const std::string* source = nullptr;
};

bool reallyVisibleAt(const FaceOnShape& s, Vec2 p) {
    const geometry::PointLocation outer = geometry::locatePoint(s.outer, p);
    if (outer == geometry::PointLocation::Outside) return true;
    if (outer == geometry::PointLocation::OnBoundary) return false;
    for (const geometry::Contour& c : s.cutouts)
        if (geometry::locatePoint(c, p) == geometry::PointLocation::Inside) return true;
    return false;
}

/// True when some stretch of the candidate is hidden only because shape `s` is treated as
/// its bounding rectangle.
bool approximatedStretch(const Candidate& c, const FaceOnShape& s, const std::vector<HlrBox>& boxes) {
    const Vec2 d = c.b - c.a;
    const double len2 = dot(d, d);
    if (len2 <= kEps * kEps) return false;
    std::vector<double> ts{0.0, 1.0};
    for (const HlrBox& box : boxes) {
        if (box.depthNear <= c.depth + kEps) continue;
        if (const auto t = clipSegmentParams({c.a, c.b}, box.rect)) {
            ts.push_back(t->first);
            ts.push_back(t->second);
        }
    }
    geometry::ContourSegmentInfo line;
    line.start = c.a;
    line.end = c.b;
    line.length = std::sqrt(len2);
    const auto curveCuts = [&](const geometry::Contour& contour) {
        for (std::size_t i = 0; i < contour.size(); ++i) {
            const auto hit = geometry::intersectContourSegments(line, geometry::segmentInfo(contour, i));
            for (const Vec2& p : hit.points) ts.push_back(std::clamp(dot(p - c.a, d) / len2, 0.0, 1.0));
        }
    };
    curveCuts(s.outer);
    for (const geometry::Contour& h : s.cutouts) curveCuts(h);
    std::sort(ts.begin(), ts.end());
    const HlrBox& own = boxes[s.boxIndex];
    for (std::size_t i = 0; i + 1 < ts.size(); ++i) {
        if ((ts[i + 1] - ts[i]) * line.length <= kEps) continue;
        const Vec2 m = c.a + d * ((ts[i] + ts[i + 1]) * 0.5);
        if (!own.rect.contains(m, -kEps) || !reallyVisibleAt(s, m)) continue;
        bool hiddenByOther = false;
        for (std::size_t j = 0; j < boxes.size() && !hiddenByOther; ++j)
            hiddenByOther = j != s.boxIndex && boxes[j].depthNear > c.depth + kEps && boxes[j].rect.contains(m, -kEps);
        if (!hiddenByOther) return true;
    }
    return false;
}

} // namespace

bool isContoured(const domain::Part& part) noexcept {
    return domain::hasShape(part) && !domain::isRectangularOutline(part);
}

FaceOnShape makeFaceOnShape(const domain::Part& part, const ViewBasis& b, double depth, std::size_t boxIndex) {
    const auto proj = [&](Vec3 v) { return Vec2{dot(v, b.right), dot(v, b.up)}; };
    const PlaneMap m{projectPoint(b, part.placement.applyPoint({0, 0, 0})), proj(part.placement.xAxis),
                     proj(part.placement.yAxis)};
    FaceOnShape s;
    s.boxIndex = boxIndex;
    s.partId = part.id.str();
    s.partName = part.name;
    s.depth = depth;
    s.outer = mapContour(domain::finishedContour(part), m);
    for (const geometry::Contour& c : part.shape->cutouts) s.cutouts.push_back(mapContour(c, m));
    return s;
}

void appendHlrGeometry(const FaceOnShape& s, std::vector<HlrEdge>& edges, std::vector<HlrArc>& arcs) {
    const auto add = [&](const geometry::Contour& c) {
        for (std::size_t i = 0; i < c.size(); ++i) {
            const geometry::ContourSegmentInfo info = geometry::segmentInfo(c, i);
            if (!info.isArc) {
                edges.push_back({info.start, info.end, s.depth, s.partId});
                continue;
            }
            const ViewArc a = viewArcOf(info, LineStyle::Visible, s.partId);
            arcs.push_back({a.center, a.radius, a.startDeg, a.endDeg, s.depth, s.partId});
        }
    };
    add(s.outer);
    for (const geometry::Contour& c : s.cutouts) add(c);
}

core::Diagnostics shapeApproximationDiagnostics(const std::string& viewId, const domain::Assembly& assembly,
                                                const std::vector<HlrBox>& boxes,
                                                const std::vector<FaceOnShape>& shapes,
                                                const std::vector<HlrEdge>& edges, const std::vector<HlrArc>& arcs) {
    core::Diagnostics out;
    if (shapes.empty()) return out;
    std::vector<Candidate> cand;
    for (const HlrBox& box : boxes) {
        if (!box.drawEdges) continue;
        const Vec2 c[4] = {box.rect.min, {box.rect.max.x, box.rect.min.y}, box.rect.max, {box.rect.min.x, box.rect.max.y}};
        for (const double depth : {box.depthNear, box.depthFar})
            for (int i = 0; i < 4; ++i) cand.push_back({c[i], c[(i + 1) % 4], depth, &box.sourceId});
    }
    for (const HlrEdge& e : edges) cand.push_back({e.a, e.b, e.depth, &e.sourceId});
    for (const HlrArc& a : arcs) {
        double sweep = std::fmod(a.endDeg - a.startDeg, 360.0);
        if (sweep <= 0) sweep += 360.0;
        if (!(a.radius > kEps)) continue;
        const double step = core::radToDeg(2.0 * std::acos(std::max(-1.0, 1.0 - kCheckChordTolerance / a.radius)));
        const int n = std::max(1, static_cast<int>(std::ceil(sweep / std::max(step, 1e-3))));
        const auto at = [&](int k) {
            const double t = core::degToRad(a.startDeg + sweep * k / n);
            return a.center + Vec2{std::cos(t), std::sin(t)} * a.radius;
        };
        for (int k = 0; k < n; ++k) cand.push_back({at(k), at(k + 1), a.depth, &a.sourceId});
    }
    for (const FaceOnShape& s : shapes) {
        std::vector<std::string> reported;
        for (const Candidate& c : cand) {
            if (*c.source == s.partId || boxes[s.boxIndex].depthNear <= c.depth + kEps) continue;
            if (std::find(reported.begin(), reported.end(), *c.source) != reported.end()) continue;
            if (!approximatedStretch(c, s, boxes)) continue;
            reported.push_back(*c.source);
            const domain::Part* other = assembly.findPart(domain::PartId(*c.source));
            const std::string otherName = other ? other->name : *c.source;
            out.push_back({core::Severity::Warning, "drawings.hlr.shape_approximated",
                           "In view '" + viewId + "' the contoured part '" + s.partName +
                               "' hides other parts as its bounding rectangle (hidden-line approximation), so "
                               "edges of '" + otherName +
                               "' that are really visible through its cut-outs or past its curved outline are "
                               "drawn hidden.",
                           s.partId});
        }
    }
    return out;
}

} // namespace fos::drawings::detail
