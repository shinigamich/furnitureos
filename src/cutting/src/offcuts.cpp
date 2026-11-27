// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cutting/offcuts.hpp"

#include "fos/core/units.hpp"

#include <algorithm>
#include <cmath>

namespace fos::cutting {

std::vector<UsableOffcut> usableOffcuts(const SheetLayout& layout, double minLength, double minWidth) {
    std::vector<UsableOffcut> out;
    if (!std::isfinite(minLength) || !std::isfinite(minWidth))
        return out;
    minLength = std::max(0.0, minLength);
    minWidth = std::max(0.0, minWidth);
    for (std::size_t i = 0; i < layout.offcuts.size(); ++i) {
        const Rect& r = layout.offcuts[i].rect;
        if (!(r.length > core::kLengthEps) || !(r.width > core::kLengthEps))
            continue;
        const double longer = std::max(r.length, r.width);
        const double shorter = std::min(r.length, r.width);
        if (longer + core::kLengthEps < minLength || shorter + core::kLengthEps < minWidth)
            continue;
        out.push_back({-1, static_cast<int>(i), r});
    }
    std::stable_sort(out.begin(), out.end(), [](const UsableOffcut& a, const UsableOffcut& b) {
        if (std::abs(a.rect.area() - b.rect.area()) > core::kLengthEps)
            return a.rect.area() > b.rect.area();
        if (std::abs(a.rect.y - b.rect.y) > core::kLengthEps)
            return a.rect.y < b.rect.y;
        return a.rect.x < b.rect.x - core::kLengthEps;
    });
    return out;
}

std::vector<UsableOffcut> usableOffcuts(const CuttingResult& result, double minLength, double minWidth) {
    std::vector<UsableOffcut> out;
    for (std::size_t s = 0; s < result.sheets.size(); ++s)
        for (UsableOffcut o : usableOffcuts(result.sheets[s], minLength, minWidth)) {
            o.sheetIndex = static_cast<int>(s);
            out.push_back(o);
        }
    return out;
}

} // namespace fos::cutting
