// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/labels.hpp"

#include <map>

namespace fos::production {

std::string Label::quantityText() const { return std::to_string(indexInGroup) + "/" + std::to_string(groupQuantity); }

LabelSet labels(const domain::Model& model, const LabelOptions& options, const Scope& scope) {
    const PartList parts = partList(model, scope);
    const CutList cuts = cutList(model, options.cutList, scope);

    // part number -> (1-based index in group, group size)
    std::map<int, std::pair<int, int>> position;
    for (const auto& g : cuts.rows)
        for (std::size_t i = 0; i < g.partNumbers.size(); ++i)
            position[g.partNumbers[i]] = {static_cast<int>(i) + 1, g.quantity};

    LabelSet out;
    out.diagnostics = parts.diagnostics;
    out.labels.reserve(parts.rows.size());
    for (const auto& r : parts.rows) {
        Label l;
        l.projectName = options.projectName;
        l.assemblyName = r.assemblyName;
        l.partName = r.name;
        l.partId = r.partId;
        l.partNumber = r.number;
        l.length = r.length;
        l.width = r.width;
        l.thickness = r.thickness;
        l.cutLength = r.cutLength;
        l.cutWidth = r.cutWidth;
        l.materialMissing = r.materialMissing;
        l.materialName = r.materialMissing ? r.materialId.str() : r.materialName;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& e = r.edges[i];
            l.edgeCode += e.missing ? '?' : (e.banded() ? '1' : '0');
            l.edgeThickness[i] = e.thickness;
        }
        const auto pos = position.at(r.number); // every part is in exactly one group
        l.indexInGroup = pos.first;
        l.groupQuantity = pos.second;
        out.labels.push_back(std::move(l));
    }
    return out;
}

} // namespace fos::production
