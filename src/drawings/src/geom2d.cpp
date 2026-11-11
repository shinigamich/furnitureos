// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/geom2d.hpp"

#include <cmath>

namespace fos::drawings {

bool intersects(const Rect2& a, const Rect2& b, double eps) noexcept {
    if (a.empty() || b.empty()) return false;
    return a.min.x < b.max.x - eps && b.min.x < a.max.x - eps && a.min.y < b.max.y - eps &&
           b.min.y < a.max.y - eps;
}

std::optional<std::pair<double, double>> clipSegmentParams(const Segment2& s, const Rect2& r) noexcept {
    if (r.empty()) return std::nullopt;
    double t0 = 0.0, t1 = 1.0;
    const double dx = s.b.x - s.a.x, dy = s.b.y - s.a.y;
    const auto clip = [&](double p, double q) {
        if (p == 0.0) return q >= 0.0; // parallel to this boundary: inside or outside as a whole
        const double t = q / p;
        if (p < 0.0) {
            if (t > t1) return false;
            if (t > t0) t0 = t;
        } else {
            if (t < t0) return false;
            if (t < t1) t1 = t;
        }
        return true;
    };
    if (!clip(-dx, s.a.x - r.min.x) || !clip(dx, r.max.x - s.a.x) || !clip(-dy, s.a.y - r.min.y) ||
        !clip(dy, r.max.y - s.a.y))
        return std::nullopt;
    return std::make_pair(t0, t1);
}

std::optional<Segment2> clipSegmentToRect(const Segment2& s, const Rect2& r) noexcept {
    const auto t = clipSegmentParams(s, r);
    if (!t) return std::nullopt;
    const Vec2 d = s.b - s.a;
    if ((t->second - t->first) * length(d) <= core::kLengthEps) return std::nullopt;
    return Segment2{s.a + d * t->first, s.a + d * t->second};
}

bool segmentCrossesRect(const Segment2& s, const Rect2& r, double eps) noexcept {
    const auto t = clipSegmentParams(s, r);
    if (!t) return false;
    const Vec2 d = s.b - s.a;
    if ((t->second - t->first) * length(d) <= eps) return false;
    const Vec2 mid = s.a + d * ((t->first + t->second) * 0.5);
    return mid.x > r.min.x + eps && mid.x < r.max.x - eps && mid.y > r.min.y + eps && mid.y < r.max.y - eps;
}

std::vector<Segment2> hatchRect(const Rect2& r, double angleDeg, double spacing) {
    std::vector<Segment2> out;
    if (r.empty() || !(spacing > 0.0) || r.width() <= 0.0 || r.height() <= 0.0) return out;
    const double a = core::degToRad(angleDeg);
    const Vec2 d{std::cos(a), std::sin(a)};
    const Vec2 n{-d.y, d.x};
    const Vec2 corners[4] = {r.min, {r.max.x, r.min.y}, r.max, {r.min.x, r.max.y}};
    double nMin = dot(n, corners[0]), nMax = nMin, tMin = dot(d, corners[0]), tMax = tMin;
    for (const Vec2& c : corners) {
        nMin = std::min(nMin, dot(n, c));
        nMax = std::max(nMax, dot(n, c));
        tMin = std::min(tMin, dot(d, c));
        tMax = std::max(tMax, dot(d, c));
    }
    if ((nMax - nMin) / spacing > 20000.0) return out;
    const double eps = 1e-9;
    const auto kFirst = static_cast<long long>(std::ceil((nMin + eps) / spacing));
    const auto kLast = static_cast<long long>(std::floor((nMax - eps) / spacing));
    for (long long k = kFirst; k <= kLast; ++k) {
        const Vec2 base = n * (static_cast<double>(k) * spacing);
        const Segment2 full{base + d * tMin, base + d * tMax};
        if (auto seg = clipSegmentToRect(full, r)) out.push_back(*seg);
    }
    return out;
}

} // namespace fos::drawings
