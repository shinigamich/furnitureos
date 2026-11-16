// SPDX-License-Identifier: AGPL-3.0-only
#include "heightmap.hpp"

#include "arc_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace fos::cam::detail {

Heightmap::Heightmap(double x0, double y0, double lx, double ly, double h, double topZ)
    : x0_(x0), y0_(y0), lx_(lx), ly_(ly), h_(h), top_(topZ),
      // ceil: a partial last strip (e.g. 0.4 mm of a 600.4 mm panel with 0.5 mm cells) is a
      // clipped cell of its own instead of being dropped.
      nx_(std::max(1, static_cast<int>(std::ceil(lx / h - 1e-9)))),
      ny_(std::max(1, static_cast<int>(std::ceil(ly / h - 1e-9)))) {}

double Heightmap::centreCoord(int i, double origin, double extent) const noexcept {
    const double lo = i * h_;
    const double hi = std::min((i + 1) * h_, extent); // the last cell is clipped to the stock
    return origin + 0.5 * (lo + hi);
}

void Heightmap::range(double lo, double hi, bool xAxis, int& first, int& last) const noexcept {
    const double o = xAxis ? x0_ : y0_;
    const int n = xAxis ? nx_ : ny_;
    // Regular centres are o + (k + 0.5) h; the clipped last centre lies left of its regular
    // position, so the upper bound gets one extra cell (callers test every centre exactly).
    const double a = std::ceil((lo - o) / h_ - 0.5 - 1e-9), b = std::floor((hi - o) / h_ - 0.5 + 1e-9) + 1;
    first = static_cast<int>(std::max(0.0, std::min(a, static_cast<double>(n))));
    last = static_cast<int>(std::min(static_cast<double>(n - 1), std::max(b, -1.0)));
}

void Heightmap::cut(geometry::Vec3 a, geometry::Vec3 b, double r, std::size_t motion) {
    if (std::min(a.z, b.z) >= top_ - 1e-9 || r <= 0) return;
    int i0, i1, j0, j1;
    range(std::min(a.x, b.x) - r, std::max(a.x, b.x) + r, true, i0, i1);
    range(std::min(a.y, b.y) - r, std::max(a.y, b.y) + r, false, j0, j1);
    const geometry::Vec2 d{b.x - a.x, b.y - a.y};
    const double dd = geometry::dot(d, d);
    const double rr = (r + 1e-9) * (r + 1e-9);
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            const geometry::Vec2 c = centre(i, j);
            const geometry::Vec2 f{a.x - c.x, a.y - c.y};
            double t0, t1;
            if (dd < 1e-18) {
                if (geometry::dot(f, f) > rr) continue;
                t0 = 0;
                t1 = 1;
            } else {
                const double bq = geometry::dot(f, d), cq = geometry::dot(f, f) - rr;
                const double disc = bq * bq - dd * cq;
                if (disc < 0) continue;
                const double s = std::sqrt(disc);
                t0 = std::max(0.0, (-bq - s) / dd);
                t1 = std::min(1.0, (-bq + s) / dd);
                if (t0 > t1) continue;
            }
            lower(i, j, std::min(a.z + (b.z - a.z) * t0, a.z + (b.z - a.z) * t1), motion);
        }
}

void Heightmap::lower(int i, int j, double z, std::size_t motion) {
    if (z >= top_ - 1e-9) return;
    auto [it, inserted] = cells_.try_emplace(key(i, j), Cell{z, motion});
    if (!inserted && z < it->second.minZ) it->second = Cell{z, motion};
}

void Heightmap::cutArc(geometry::Vec3 a, geometry::Vec3 b, geometry::Vec2 c, double sweep, double r,
                       std::size_t motion) {
    if (std::min(a.z, b.z) >= top_ - 1e-9 || r <= 0 || sweep == 0) return;
    const ArcGeom arc{c, geometry::length(geometry::Vec2{a.x, a.y} - c), std::atan2(a.y - c.y, a.x - c.x), sweep};
    double bx0, by0, bx1, by1;
    arcBounds(arc, bx0, by0, bx1, by1);
    int i0, i1, j0, j1;
    range(bx0 - r, bx1 + r, true, i0, i1);
    range(by0 - r, by1 + r, false, j0, j1);
    const double R = arc.radius, S = std::fabs(sweep), rr = r + 1e-9;
    constexpr double kTwoPi = 2 * std::numbers::pi;
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            const geometry::Vec2 p = centre(i, j);
            const geometry::Vec2 v = p - c;
            const double d = geometry::length(v);
            double alpha; // half-width of the covered angular window around phi_p
            if (d < 1e-12) {
                if (R > rr) continue;
                alpha = std::numbers::pi;
            } else {
                const double k = (d * d + R * R - rr * rr) / (2 * d * R);
                if (k > 1) continue;
                alpha = k <= -1 ? std::numbers::pi : std::acos(k);
            }
            // Angle from the arc start to phi_p, measured in the direction of travel, in [0, 2 pi).
            double delta = sweep > 0 ? std::atan2(v.y, v.x) - arc.startAngle : arc.startAngle - std::atan2(v.y, v.x);
            delta = std::fmod(delta, kTwoPi);
            if (delta < 0) delta += kTwoPi;
            double best = std::numeric_limits<double>::infinity();
            if (alpha >= std::numbers::pi) {
                best = std::min(a.z, b.z);
            } else {
                for (double shift : {-kTwoPi, 0.0, kTwoPi}) {
                    const double u0 = std::max(0.0, delta - alpha + shift), u1 = std::min(S, delta + alpha + shift);
                    if (u0 > u1) continue;
                    // Z is linear in the angle: the lowest point of the window is at one of its ends.
                    best = std::min({best, a.z + (b.z - a.z) * (u0 / S), a.z + (b.z - a.z) * (u1 / S)});
                }
            }
            if (best < std::numeric_limits<double>::infinity()) lower(i, j, best, motion);
        }
}

const Heightmap::Cell* Heightmap::cell(int i, int j) const {
    auto it = cells_.find(key(i, j));
    return it == cells_.end() ? nullptr : &it->second;
}

double Heightmap::minZ(int i, int j) const {
    const Cell* c = cell(i, j);
    return c ? c->minZ : std::numeric_limits<double>::infinity();
}

std::vector<std::pair<int, int>> Heightmap::removedCells(double eps) const {
    std::vector<std::pair<int, int>> out;
    for (const auto& [k, c] : cells_)
        if (c.minZ < top_ - eps)
            out.emplace_back(static_cast<int>(k >> 32), static_cast<int>(static_cast<std::uint32_t>(k & 0xffffffff)));
    std::sort(out.begin(), out.end());
    for (auto& p : out) std::swap(p.first, p.second); // -> (i, j), ordered by j then i
    return out;
}

} // namespace fos::cam::detail
