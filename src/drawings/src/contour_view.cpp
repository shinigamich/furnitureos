// SPDX-License-Identifier: AGPL-3.0-only
#include "contour_view.hpp"

#include <cmath>

namespace fos::drawings::detail {

double angleDeg(Vec2 v) noexcept {
    double a = core::radToDeg(std::atan2(v.y, v.x));
    if (a < 0) a += 360.0;
    if (a >= 360.0) a -= 360.0;
    return a;
}

ViewArc mapArc(const geometry::ContourSegmentInfo& s, const PlaneMap& m, LineStyle style, const std::string& source) {
    ViewArc a;
    a.center = m.apply(s.center);
    a.radius = s.radius;
    a.style = style;
    a.sourceId = source;
    const double as = angleDeg(m.apply(s.start) - a.center);
    const double ae = angleDeg(m.apply(s.end) - a.center);
    // Counter-clockwise in the part frame stays counter-clockwise unless the map mirrors.
    const bool ccwInView = (s.sweep > 0) != m.mirrors();
    a.startDeg = ccwInView ? as : ae;
    a.endDeg = ccwInView ? ae : as;
    return a;
}

geometry::Contour mapContour(const geometry::Contour& c, const PlaneMap& m) {
    geometry::Contour out;
    out.vertices.reserve(c.size());
    const double sign = m.mirrors() ? -1.0 : 1.0;
    for (const geometry::ContourVertex& v : c.vertices) out.vertices.push_back({m.apply(v.point), sign * v.bulge});
    return out;
}

ViewArc viewArcOf(const geometry::ContourSegmentInfo& s, LineStyle style, const std::string& source) {
    ViewArc a;
    a.center = s.center;
    a.radius = s.radius;
    a.style = style;
    a.sourceId = source;
    const double as = angleDeg(s.start - s.center), ae = angleDeg(s.end - s.center);
    a.startDeg = s.sweep > 0 ? as : ae;
    a.endDeg = s.sweep > 0 ? ae : as;
    return a;
}

bool isCircle(const geometry::Contour& c, Vec2& center, double& radius) noexcept {
    if (c.size() < 2) return false;
    for (std::size_t i = 0; i < c.size(); ++i) {
        const geometry::ContourSegmentInfo s = geometry::segmentInfo(c, i);
        if (!s.isArc) return false;
        if (i == 0) {
            center = s.center;
            radius = s.radius;
        } else if (length(s.center - center) > core::kLengthEps || std::fabs(s.radius - radius) > core::kLengthEps) {
            return false;
        }
    }
    return true;
}

void appendContour(const geometry::Contour& c, const PlaneMap& m, LineStyle style, const std::string& source,
                   std::vector<ViewSegment>& segments, std::vector<ViewArc>& arcs) {
    for (std::size_t i = 0; i < c.size(); ++i) {
        const geometry::ContourSegmentInfo s = geometry::segmentInfo(c, i);
        if (s.isArc)
            arcs.push_back(mapArc(s, m, style, source));
        else
            segments.push_back({m.apply(s.start), m.apply(s.end), style, source});
    }
}

} // namespace fos::drawings::detail
