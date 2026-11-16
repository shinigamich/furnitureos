// SPDX-License-Identifier: AGPL-3.0-only
#include "part_shape.hpp"

#include "contour_view.hpp"

#include <cmath>

namespace fos::drawings::detail {
namespace {

PlaneMap partMap(double ySign) noexcept { return PlaneMap{{0, 0}, {1, 0}, {0, ySign}}; }

const std::optional<domain::EdgeBandId>* bandOf(const domain::PartShape& shape, std::size_t i) noexcept {
    if (i >= shape.outerEdges.size() || !shape.outerEdges[i]) return nullptr;
    return &shape.outerEdges[i];
}

void warnInvalid(const domain::Part& part, const std::string& what, const core::Error& e, View& view) {
    view.diagnostics.push_back({core::Severity::Warning, "drawings.part.invalid_shape",
                                "The " + what + " of '" + part.name +
                                    "' is not a valid contour; it is drawn as stored and must be fixed before "
                                    "production. " + e.message,
                                part.id.str()});
}

} // namespace

bool drawsContour(const domain::Part& part) noexcept { return domain::hasShape(part); }

void drawShape(const domain::Part& part, double ySign, View& view) {
    const PlaneMap m = partMap(ySign);
    const std::string& pid = part.id.str();
    const geometry::Contour outer = domain::finishedContour(part);
    if (auto v = geometry::validateContour(outer); !v.ok()) warnInvalid(part, "outline", v.error(), view);
    appendContour(outer, m, LineStyle::Visible, pid, view.segments, view.arcs);
    const auto& cutouts = part.shape->cutouts;
    for (std::size_t k = 0; k < cutouts.size(); ++k) {
        const geometry::Contour& c = cutouts[k];
        if (auto v = geometry::validateContour(c); !v.ok())
            warnInvalid(part, "cut-out " + std::to_string(k + 1), v.error(), view);
        Vec2 center;
        double r = 0;
        if (isCircle(c, center, r)) {
            const Vec2 cv = m.apply(center);
            view.circles.push_back({cv, r, LineStyle::Visible, pid});
            const double e = r * 1.2;
            view.segments.push_back({cv - Vec2{e, 0}, cv + Vec2{e, 0}, LineStyle::Center, pid});
            view.segments.push_back({cv - Vec2{0, e}, cv + Vec2{0, e}, LineStyle::Center, pid});
            continue;
        }
        appendContour(c, m, LineStyle::Visible, pid, view.segments, view.arcs);
    }
}

void cutoutCoordinates(const domain::Part& part, const std::string& viewId, std::vector<CutoutCoord>& xs,
                       std::vector<CutoutCoord>& ys) {
    if (!part.shape) return;
    const auto& cutouts = part.shape->cutouts;
    for (std::size_t k = 0; k < cutouts.size(); ++k) {
        const std::string ks = std::to_string(k);
        Vec2 center;
        double r = 0;
        if (isCircle(cutouts[k], center, r)) {
            xs.push_back({center.x, viewId + "/dim.cutout.x[" + ks + "]"});
            ys.push_back({center.y, viewId + "/dim.cutout.y[" + ks + "]"});
            continue;
        }
        const geometry::Rect2 b = geometry::bounds(cutouts[k]);
        if (b.empty()) continue;
        xs.push_back({b.min.x, viewId + "/dim.cutout.x[" + ks + ".min]"});
        xs.push_back({b.max.x, viewId + "/dim.cutout.x[" + ks + ".max]"});
        ys.push_back({b.min.y, viewId + "/dim.cutout.y[" + ks + ".min]"});
        ys.push_back({b.max.y, viewId + "/dim.cutout.y[" + ks + ".max]"});
    }
}

void addRadiusDimensions(const domain::Part& part, const View& view, double ySign, ViewAnnotations& ann) {
    const PlaneMap m = partMap(ySign);
    const std::string& pid = part.id.str();
    const auto arcDims = [&](const geometry::Contour& c, const std::string& idPrefix) {
        for (std::size_t i = 0; i < c.size(); ++i) {
            const geometry::ContourSegmentInfo s = geometry::segmentInfo(c, i);
            if (!s.isArc) continue;
            const double mid = core::degToRad(s.startAngle + s.sweep * 0.5);
            RadiusDimension d;
            d.id = idPrefix + std::to_string(i) + "]";
            d.center = m.apply(s.center);
            d.arcPoint = m.apply(s.center + Vec2{std::cos(mid), std::sin(mid)} * s.radius);
            d.sourceId = pid;
            ann.radiusDimensions.push_back(std::move(d));
        }
    };
    arcDims(domain::finishedContour(part), view.id + "/dim.radius[");
    if (!part.shape) return;
    const auto& cutouts = part.shape->cutouts;
    for (std::size_t k = 0; k < cutouts.size(); ++k) {
        Vec2 center;
        double r = 0;
        if (isCircle(cutouts[k], center, r)) {
            RadiusDimension d;
            d.id = view.id + "/dim.diameter.cutout[" + std::to_string(k) + "]";
            d.diameter = true;
            d.center = m.apply(center);
            d.arcPoint = d.center + Vec2{1, 1} * (r * std::sqrt(0.5));
            d.sourceId = pid;
            ann.radiusDimensions.push_back(std::move(d));
            continue;
        }
        arcDims(cutouts[k], view.id + "/dim.radius.cutout[" + std::to_string(k) + ".");
    }
}

void addShapeEdgeMarks(const domain::Part& part, const View& view, double ySign,
                       std::vector<domain::EdgeBandId>& bands, ViewAnnotations& ann) {
    if (!part.shape) return;
    const PlaneMap m = partMap(ySign);
    const geometry::Contour outer = domain::finishedContour(part);
    for (std::size_t i = 0; i < outer.size(); ++i) {
        const auto* band = bandOf(*part.shape, i);
        if (!band) continue;
        std::size_t bi = 0;
        while (bi < bands.size() && bands[bi] != **band) ++bi;
        if (bi == bands.size()) bands.push_back(**band);
        const geometry::ContourSegmentInfo s = geometry::segmentInfo(outer, i);
        EdgeBandMark mark;
        mark.id = view.id + "/edge.seg[" + std::to_string(i) + "]";
        mark.a = m.apply(s.start);
        mark.b = m.apply(s.end);
        mark.code = "E" + std::to_string(bi + 1);
        mark.sourceId = (*band)->str();
        if (s.isArc) {
            const ViewArc a = mapArc(s, m, LineStyle::Visible, {});
            // The outline is counter-clockwise, so a counter-clockwise arc is convex: its
            // centre lies on the material side.
            mark.arc = EdgeMarkArc{a.center, a.radius, a.startDeg, a.endDeg, s.sweep > 0};
        } else {
            const Vec2 d = s.end - s.start;
            const double len = length(d);
            mark.inward = len > 0 ? m.applyVector(Vec2{-d.y, d.x} * (1.0 / len)) : Vec2{};
        }
        ann.edgeBandMarks.push_back(std::move(mark));
    }
}

std::string shapeBandUsage(const domain::Part& part, const domain::EdgeBandId& band, const PartDrawingLabels& labels) {
    if (!part.shape) return {};
    const geometry::Contour outer = domain::finishedContour(part);
    int count = 0;
    double total = 0;
    for (std::size_t i = 0; i < outer.size(); ++i) {
        const auto* b = bandOf(*part.shape, i);
        if (!b || **b != band) continue;
        ++count;
        total += geometry::segmentInfo(outer, i).length;
    }
    return std::to_string(count) + " " + labels.outlineSegments + ", " + core::formatLength(total) + " " + labels.unit;
}

} // namespace fos::drawings::detail
