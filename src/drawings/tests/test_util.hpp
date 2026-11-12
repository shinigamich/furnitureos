// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/fixtures.hpp"
#include "fos/drawings/drawing.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace fos::drawings::testing {

inline bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
inline bool samePoint(Vec2 a, Vec2 b, double eps = 1e-9) { return near(a.x, b.x, eps) && near(a.y, b.y, eps); }

/// Finds a segment with these end points (either direction); nullptr if absent.
inline const ViewSegment* findSegment(const View& v, Vec2 a, Vec2 b) {
    for (const ViewSegment& s : v.segments)
        if ((samePoint(s.a, a) && samePoint(s.b, b)) || (samePoint(s.a, b) && samePoint(s.b, a))) return &s;
    return nullptr;
}

inline std::size_t countStyle(const View& v, LineStyle style) {
    std::size_t n = 0;
    for (const ViewSegment& s : v.segments) n += s.style == style ? 1 : 0;
    return n;
}

inline const Dimension* findDimension(const std::vector<Dimension>& ds, const std::string& id) {
    for (const Dimension& d : ds)
        if (d.id == id) return &d;
    return nullptr;
}

inline const domain::Assembly& cabinet(const domain::Model& m) { return m.assemblies.front(); }

} // namespace fos::drawings::testing
