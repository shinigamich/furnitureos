// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/bought_in.hpp"

#include "collect.hpp"
#include "fos/core/units.hpp"

#include <algorithm>
#include <tuple>

namespace fos::production {
namespace {

bool same(double a, double b) { return std::fabs(a - b) <= core::kLengthEps; } // false for NaN
double key(double v) { return std::isfinite(v) ? v : 0.0; }

std::string joined(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? sep : "") + v[i];
    return out;
}

} // namespace

BoughtInList boughtInList(const domain::Model& model, const Scope& scope) {
    auto c = detail::collect(model, scope);
    BoughtInList out;
    out.diagnostics = std::move(c.diagnostics);
    for (const auto& ref : c.parts) {
        const domain::Part& p = *ref.part;
        if (!domain::isBoughtIn(p)) continue;
        const domain::Material* m = model.findMaterial(p.material);
        if (m == nullptr)
            detail::warn(out.diagnostics, "production.material.missing",
                         "Bought-in part " + detail::describe(p) + " references material '" + p.material.str() +
                             "' which is not in the model; it is listed without a material name or price.",
                         p.id.str());
        if (!detail::positiveFinite(p.length) || !detail::positiveFinite(p.width) ||
            !detail::positiveFinite(p.thickness))
            detail::warn(out.diagnostics, "production.part.invalid_size",
                         "Bought-in part " + detail::describe(p) + " has a non-positive or non-finite size " +
                             core::formatLength(p.length) + " x " + core::formatLength(p.width) + " x " +
                             core::formatLength(p.thickness) + " mm; its area counts as 0.",
                         p.id.str());
        auto matches = [&](const BoughtInRow& g) {
            if (g.materialId != p.material || !same(g.thickness, p.thickness)) return false;
            return (same(g.width, p.length) && same(g.height, p.width)) ||
                   (same(g.width, p.width) && same(g.height, p.length));
        };
        auto it = std::find_if(out.rows.begin(), out.rows.end(), matches);
        if (it == out.rows.end()) {
            BoughtInRow g;
            g.materialId = p.material;
            g.materialMissing = m == nullptr;
            if (m != nullptr) g.materialName = m->name;
            g.width = p.length;
            g.height = p.width;
            g.thickness = p.thickness;
            g.areaEachM2 = detail::areaM2(p.length, p.width);
            out.rows.push_back(std::move(g));
            it = std::prev(out.rows.end());
        }
        ++it->quantity;
        it->totalAreaM2 = it->quantity * it->areaEachM2;
        it->partIds.push_back(p.id);
        it->partNumbers.push_back(ref.number);
        if (std::find(it->partNames.begin(), it->partNames.end(), p.name) == it->partNames.end())
            it->partNames.push_back(p.name);
    }
    std::stable_sort(out.rows.begin(), out.rows.end(), [](const BoughtInRow& a, const BoughtInRow& b) {
        const double aMax = std::max(key(a.width), key(a.height)), aMin = std::min(key(a.width), key(a.height));
        const double bMax = std::max(key(b.width), key(b.height)), bMin = std::min(key(b.width), key(b.height));
        // Descending sizes: compare b against a for those members.
        return std::make_tuple(std::cref(a.materialName), std::cref(a.materialId), key(a.thickness), bMax, bMin,
                               a.partNumbers.front()) <
               std::make_tuple(std::cref(b.materialName), std::cref(b.materialId), key(b.thickness), aMax, aMin,
                               b.partNumbers.front());
    });
    for (const auto& r : out.rows) {
        out.itemCount += r.quantity;
        out.totalAreaM2 += r.totalAreaM2;
    }
    return out;
}

Table boughtInTable(const BoughtInList& list) {
    using core::formatLength;
    Table t;
    t.header = {"Item", "Material", "Width", "Height", "Thickness", "Qty", "Area each (m2)", "Total area (m2)",
                "Name", "Part numbers"};
    for (const auto& r : list.rows) {
        std::vector<std::string> numbers;
        for (int n : r.partNumbers) numbers.push_back(std::to_string(n));
        t.rows.push_back({r.kind,
                          r.materialMissing ? (r.materialId.empty() ? std::string("[MISSING]")
                                                                    : r.materialId.str() + " [MISSING]")
                                            : r.materialName,
                          formatLength(r.width), formatLength(r.height), formatLength(r.thickness),
                          std::to_string(r.quantity), formatLength(r.areaEachM2, 4), formatLength(r.totalAreaM2, 4),
                          joined(r.partNames, " / "), joined(numbers, " ")});
    }
    return t;
}

} // namespace fos::production
