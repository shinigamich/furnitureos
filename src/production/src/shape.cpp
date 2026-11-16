// SPDX-License-Identifier: AGPL-3.0-only
#include "shape.hpp"

#include "collect.hpp"

#include <cmath>

namespace fos::production::detail {
namespace {

using geometry::ContourSegmentInfo;
using geometry::Vec2;

/// Unit tangent of a segment at its start (`atEnd` = false) or end, in travel direction.
Vec2 tangent(const ContourSegmentInfo& s, bool atEnd) {
    Vec2 d;
    if (!s.isArc) {
        d = s.end - s.start;
    } else {
        const Vec2 r = (atEnd ? s.end : s.start) - s.center;
        d = s.sweep > 0 ? Vec2{-r.y, r.x} : Vec2{r.y, -r.x};
    }
    const double len = std::hypot(d.x, d.y);
    return len > 0 ? Vec2{d.x / len, d.y / len} : Vec2{};
}

/// The junction at the END of segment i (vertex i + 1) is tangent.
bool tangentJunction(const geometry::Contour& c, std::size_t i) {
    const Vec2 a = tangent(geometry::segmentInfo(c, i), true);
    const Vec2 b = tangent(geometry::segmentInfo(c, (i + 1) % c.size()), false);
    return std::fabs(a.x * b.y - a.y * b.x) <= 1e-6 && a.x * b.x + a.y * b.y > 0;
}

} // namespace

std::optional<double> outlineAreaMm2(const domain::Part& part) {
    if (!part.shape || !geometry::validateContour(part.shape->outer)) return std::nullopt;
    double area = std::fabs(geometry::signedArea(part.shape->outer));
    for (const auto& c : part.shape->cutouts) {
        if (!geometry::validateContour(c)) return std::nullopt;
        area -= std::fabs(geometry::signedArea(c));
    }
    return area;
}

double finishedAreaM2(const domain::Part& part) {
    if (!part.shape) return areaM2(part.length, part.width);
    const auto a = outlineAreaMm2(part);
    return a && positiveFinite(*a) ? *a / 1.0e6 : 0.0;
}

std::vector<BandStrip> bandStrips(const domain::Part& part) {
    std::vector<BandStrip> out;
    if (!part.shape) return out;
    const auto& c = part.shape->outer;
    const auto& bands = part.shape->outerEdges;
    const std::size_t n = c.size();
    if (n == 0 || bands.size() != n) return out;
    // Does a strip continue from segment i into segment i + 1?
    auto continues = [&](std::size_t i) {
        const auto& next = bands[(i + 1) % n];
        return bands[i] && next && *bands[i] == *next && tangentJunction(c, i);
    };
    std::size_t start = n; // first segment that starts a strip
    for (std::size_t i = 0; i < n && start == n; ++i)
        if (bands[i] && !continues((i + n - 1) % n)) start = i;
    if (start == n) { // no strip start: either nothing banded or one closed strip
        if (!bands[0]) return out;
        BandStrip s{*bands[0], 0.0};
        for (std::size_t i = 0; i < n; ++i) s.length += geometry::segmentInfo(c, i).length;
        out.push_back(s);
        return out;
    }
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t i = (start + k) % n;
        if (!bands[i]) continue;
        const double len = geometry::segmentInfo(c, i).length;
        if (k > 0 && continues((i + n - 1) % n)) out.back().length += len;
        else out.push_back({*bands[i], len});
    }
    return out;
}

bool isRouted(const domain::Part& part) noexcept {
    return domain::hasShape(part) && !domain::isRectangularOutline(part);
}

} // namespace fos::production::detail
