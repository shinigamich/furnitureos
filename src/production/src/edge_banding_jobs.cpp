// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/edge_banding_jobs.hpp"

#include "collect.hpp"
#include "shape.hpp"

#include "fos/core/units.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace fos::production {
namespace {

struct BandInfo {
    std::string name;
    bool missing = true;
    double thickness = 0, width = 0;
};

BandInfo bandInfo(const domain::Model& model, const domain::EdgeBandId& id) {
    BandInfo b;
    if (const auto* eb = model.findEdgeBand(id)) {
        b.name = eb->name;
        b.missing = false;
        b.thickness = eb->thickness;
        b.width = eb->width;
    }
    return b;
}

/// Band order of the job list: model order, then missing ids sorted.
std::vector<domain::EdgeBandId> orderedBands(const domain::Model& model, const std::set<domain::EdgeBandId>& used) {
    std::vector<domain::EdgeBandId> out;
    std::set<domain::EdgeBandId> seen;
    for (const auto& eb : model.edgeBands)
        if (used.count(eb.id) != 0 && seen.insert(eb.id).second) out.push_back(eb.id);
    for (const auto& id : used)
        if (seen.count(id) == 0) out.push_back(id);
    return out;
}

/// Contour job of a shaped part, or nullopt when no outline segment is banded.
std::optional<ContourBandingJob> contourJob(const domain::Model& model, const detail::PartRef& ref, double overhang,
                                            core::Diagnostics& diags) {
    const auto& p = *ref.part;
    const auto& shape = *p.shape;
    ContourBandingJob j;
    j.partNumber = ref.number;
    j.partId = p.id;
    j.partName = p.name;
    j.assemblyId = ref.assembly->id;
    const auto cut = domain::cutSize(p, model);
    j.cutLength = cut.length;
    j.cutWidth = cut.width;
    j.thickness = p.thickness;
    j.routed = detail::isRouted(p);
    const std::size_t n = shape.outer.size();
    const bool countOk = shape.outerEdges.size() == n;
    const bool valid = countOk && n > 0 && static_cast<bool>(geometry::validateContour(shape.outer));
    for (std::size_t i = 0; i < shape.outerEdges.size(); ++i) {
        if (!shape.outerEdges[i]) continue;
        ContourSegmentBand s;
        s.segment = static_cast<int>(i);
        s.edgeBandId = *shape.outerEdges[i];
        if (valid) s.length = geometry::segmentInfo(shape.outer, i).length;
        j.segments.push_back(std::move(s));
    }
    if (j.segments.empty()) return std::nullopt;
    if (valid) {
        for (const auto& st : detail::bandStrips(p))
            if (detail::positiveFinite(st.length))
                j.strips.push_back({st.band, st.length, st.length + 2.0 * overhang});
    } else {
        j.lengthsAvailable = false;
        const std::string why =
            countOk ? "its outline is invalid"
                    : "it has " + std::to_string(shape.outerEdges.size()) + " outline band entries for " +
                          std::to_string(n) + " outline segments";
        detail::warn(diags, "production.edgeband_jobs.shape_lengths_unavailable",
                     "Shaped part " + detail::describe(p) + " has banded outline segments but " + why +
                         ", so no banding lengths can be given; it is listed for contour banding without lengths.",
                     p.id.str());
    }
    return j;
}

} // namespace

EdgeBandingJobs edgeBandingJobs(const domain::Model& model, const EdgeBandingOptions& settings, const Scope& scope) {
    EdgeBandingJobs out;
    out.overhangPerEnd = detail::positiveFinite(settings.overhangPerEnd) ? settings.overhangPerEnd : 0.0;
    const double overhang = out.overhangPerEnd;
    auto collected = detail::collect(model, scope);
    out.diagnostics = std::move(collected.diagnostics);

    std::map<domain::EdgeBandId, std::vector<PartBandingJob>> byBand;
    std::set<domain::EdgeBandId> used;
    std::set<domain::EdgeBandId> contourUsed;

    for (const auto& ref : collected.parts) {
        const auto& p = *ref.part;
        if (p.shape) {
            for (auto s : domain::kAllEdges)
                if (p.edge(s))
                    detail::warn(out.diagnostics, "production.edgeband_jobs.edges_on_shaped_part",
                                 "Shaped part " + detail::describe(p) + " has a band on edge " +
                                     domain::toString(s) +
                                     "; shaped parts band their outline segments, so this edge entry is ignored.",
                                 p.id.str());
            if (auto j = contourJob(model, ref, overhang, out.diagnostics)) {
                for (const auto& s : j->segments) contourUsed.insert(s.edgeBandId);
                out.contour.push_back(std::move(*j));
            }
            continue;
        }
        // Banded edges with a usable length, ordered: longer first, then L1, L2, W1, W2.
        struct Edge {
            domain::EdgeSide side;
            domain::EdgeBandId band;
            double length;
        };
        std::vector<Edge> edges;
        for (auto s : domain::kAllEdges) {
            const auto& e = p.edge(s);
            if (!e) continue;
            const double len = domain::edgeLength(p, s);
            if (!detail::positiveFinite(len)) {
                detail::warn(out.diagnostics, "production.edgeband_jobs.invalid_edge",
                             "Edge " + std::string(domain::toString(s)) + " of " + detail::describe(p) +
                                 " is banded but its length is " + core::formatLength(len) +
                                 " mm; it is left out of the banding jobs.",
                             p.id.str());
                continue;
            }
            edges.push_back({s, *e, len});
        }
        if (edges.empty()) continue;
        std::stable_sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
            return core::definitelyGreater(a.length, b.length);
        });
        const auto cut = domain::cutSize(p, model);
        std::map<domain::EdgeBandId, PartBandingJob> perBand;
        for (std::size_t k = 0; k < edges.size(); ++k) {
            const auto& e = edges[k];
            auto [it, inserted] = perBand.try_emplace(e.band);
            PartBandingJob& job = it->second;
            if (inserted) {
                job.partNumber = ref.number;
                job.partId = p.id;
                job.partName = p.name;
                job.assemblyId = ref.assembly->id;
                job.length = p.length;
                job.width = p.width;
                job.cutLength = cut.length;
                job.cutWidth = cut.width;
                job.thickness = p.thickness;
                job.stepsTotal = static_cast<int>(edges.size());
            }
            const bool alongLength = e.side == domain::EdgeSide::L1 || e.side == domain::EdgeSide::L2;
            job.edges.push_back(
                {e.side, alongLength ? cut.length : cut.width, e.length, e.length + 2.0 * overhang, static_cast<int>(k) + 1});
        }
        const bool several = perBand.size() > 1;
        for (auto& [band, job] : perBand) {
            job.otherBands = several;
            used.insert(band);
            byBand[band].push_back(std::move(job));
        }
    }

    std::set<domain::EdgeBandId> all = used;
    all.insert(contourUsed.begin(), contourUsed.end());
    for (const auto& id : orderedBands(model, all)) {
        const BandInfo info = bandInfo(model, id);
        if (info.missing)
            detail::warn(out.diagnostics, "production.edgeband.missing",
                         "Edge band '" + id.str() +
                             "' is used by parts but is not in the model; its thickness and width are unknown.",
                         id.str());
        if (auto it = byBand.find(id); it != byBand.end()) {
            EdgeBandJobGroup g;
            g.edgeBandId = id;
            g.name = info.name;
            g.missing = info.missing;
            g.thickness = info.thickness;
            g.width = info.width;
            g.parts = std::move(it->second);
            for (const auto& pj : g.parts)
                for (const auto& e : pj.edges) {
                    ++g.edgeCount;
                    g.totalEdgeLength += e.edgeLength;
                    g.totalStripLength += e.stripLength;
                }
            out.groups.push_back(std::move(g));
        }
        if (contourUsed.count(id) != 0) {
            ContourBandTotal t;
            t.edgeBandId = id;
            t.name = info.name;
            t.missing = info.missing;
            for (const auto& cj : out.contour)
                for (const auto& s : cj.strips)
                    if (s.edgeBandId == id) {
                        ++t.stripCount;
                        t.totalLength += s.length;
                        t.totalStripLength += s.stripLength;
                    }
            out.contourTotals.push_back(std::move(t));
        }
    }
    return out;
}

} // namespace fos::production
