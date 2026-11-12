// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

#include <limits>

namespace fos::nesting::test {

geometry::Polygon2 poly(std::initializer_list<geometry::Vec2> pts) { return geometry::Polygon2{std::vector<geometry::Vec2>(pts)}; }

geometry::Polygon2 rectPoly(double w, double h) { return poly({{0, 0}, {w, 0}, {w, h}, {0, h}}); }

Part makePart(std::string id, geometry::Polygon2 outline, int quantity, RotationRule rule) {
    Part p;
    p.label = id;
    p.id = std::move(id);
    p.outline = std::move(outline);
    p.quantity = quantity;
    p.rotation = rule;
    return p;
}

Sheet makeSheet(std::string id, double length, double width, int quantity) {
    Sheet s;
    s.label = id;
    s.id = std::move(id);
    s.length = length;
    s.width = width;
    s.quantity = quantity;
    return s;
}

std::string codes(const core::Diagnostics& ds) {
    std::string s;
    for (const auto& d : ds) s += d.code + ": " + d.message + "\n";
    return s;
}

NestResult nestValid(const Job& job, const NestOptions& options) {
    auto r = nest(job, options);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    if (!r.ok()) return {};
    const auto diags = validateNest(job, r.value(), options);
    EXPECT_TRUE(diags.empty()) << codes(diags);
    EXPECT_TRUE(r.value().isHeuristic);
    return r.value();
}

double minPairDistance(const SheetLayout& sheet) {
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < sheet.placements.size(); ++i)
        for (std::size_t j = i + 1; j < sheet.placements.size(); ++j)
            best = std::min(best, geometry::polygonDistance(sheet.placements[i].outline, sheet.placements[j].outline));
    return best;
}

} // namespace fos::nesting::test
