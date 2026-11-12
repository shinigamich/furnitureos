// SPDX-License-Identifier: AGPL-3.0-only
#include "heightmap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fos::cam::detail {

Heightmap::Heightmap(double x0, double y0, double lx, double ly, double h, double topZ)
    : x0_(x0), y0_(y0), h_(h), top_(topZ), nx_(static_cast<int>(std::floor(lx / h + 1e-9))),
      ny_(static_cast<int>(std::floor(ly / h + 1e-9))) {}

void Heightmap::range(double lo, double hi, bool xAxis, int& first, int& last) const noexcept {
    const double o = xAxis ? x0_ : y0_;
    const int n = xAxis ? nx_ : ny_;
    const double a = std::ceil((lo - o) / h_ - 0.5 - 1e-9), b = std::floor((hi - o) / h_ - 0.5 + 1e-9);
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
            const double z = std::min(a.z + (b.z - a.z) * t0, a.z + (b.z - a.z) * t1);
            if (z >= top_ - 1e-9) continue;
            auto [it, inserted] = cells_.try_emplace(key(i, j), Cell{z, motion});
            if (!inserted && z < it->second.minZ) it->second = Cell{z, motion};
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
