// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/edge_band_rolls.hpp"

#include "collect.hpp"
#include "shape.hpp"

#include <map>

namespace fos::production {

std::vector<EdgeBandStripJob> edgeBandStripJobs(const domain::Model& model, const EdgeBandRollOptions& options) {
    std::vector<EdgeBandStripJob> out;
    if (!detail::positiveFinite(options.rollLength)) return out;
    const double overhang =
        detail::positiveFinite(options.edgeBanding.overhangPerEnd) ? options.edgeBanding.overhangPerEnd : 0.0;
    const double kerf = detail::positiveFinite(options.kerf) ? options.kerf : 0.0;
    const double minOffcut = detail::positiveFinite(options.minReusableOffcut) ? options.minReusableOffcut : 0.0;

    std::map<domain::EdgeBandId, EdgeBandStripJob> acc;
    auto addStrip = [&](const domain::EdgeBandId& band, std::string id, const std::string& label, double length) {
        if (!detail::positiveFinite(length)) return;
        auto& j = acc[band];
        j.edgeBandId = band;
        ++j.stripCount;
        j.appliedLength += length;
        j.job.pieces.push_back(linear::Piece{std::move(id), label, length + 2.0 * overhang, 1});
    };
    for (const auto& ref : detail::collect(model, options.scope, detail::PartSet::Workshop).parts) {
        const auto& part = *ref.part;
        int n = 0;
        for (const auto& strip : detail::bandStrips(part))
            addStrip(strip.band, part.id.str() + "/strip" + std::to_string(++n), part.name, strip.length);
        for (auto s : domain::kAllEdges)
            if (const auto& e = part.edge(s))
                addStrip(*e, part.id.str() + "/" + domain::toString(s), part.name, domain::edgeLength(part, s));
    }
    auto finish = [&](EdgeBandStripJob& j, double costPerMeter) {
        j.job.kerf = kerf;
        j.job.minReusableOffcut = minOffcut;
        j.job.stock = {linear::Stock{"roll", options.rollLength, 0, true,
                                     detail::positiveFinite(costPerMeter) ? costPerMeter * options.rollLength / 1000.0
                                                                          : 0.0,
                                     false, 0}};
    };
    for (const auto& eb : model.edgeBands) {
        auto it = acc.find(eb.id);
        if (it == acc.end()) continue;
        EdgeBandStripJob j = std::move(it->second);
        acc.erase(it);
        j.name = eb.name;
        finish(j, eb.costPerMeter);
        out.push_back(std::move(j));
    }
    for (auto& [id, j] : acc) {
        j.missing = true;
        finish(j, 0.0);
        out.push_back(std::move(j));
    }
    return out;
}

} // namespace fos::production
