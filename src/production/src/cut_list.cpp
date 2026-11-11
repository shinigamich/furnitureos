// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/cut_list.hpp"

#include "collect.hpp"
#include "fos/core/units.hpp"

#include <algorithm>
#include <tuple>

namespace fos::production {
namespace {

std::string num(double v) { return core::formatLength(v, 3); }

struct TokenVisitor {
    std::string operator()(const domain::Hole& h) const {
        return std::string("H|") + domain::toString(h.face) + "|" + num(h.u) + "|" + num(h.v) + "|" +
               num(h.diameter) + "|" + (h.through ? std::string("T") : num(h.depth));
    }
    std::string operator()(const domain::Groove& g) const {
        auto a = std::make_pair(g.u1, g.v1), b = std::make_pair(g.u2, g.v2);
        if (b < a) std::swap(a, b);
        return std::string("G|") + domain::toString(g.face) + "|" + num(a.first) + "|" + num(a.second) + "|" +
               num(b.first) + "|" + num(b.second) + "|" + num(g.width) + "|" + num(g.depth);
    }
    std::string operator()(const domain::Pocket& p) const {
        std::string s = std::string("P|") + domain::toString(p.face) + "|" +
                        (p.through ? std::string("T") : num(p.depth)) + "|";
        for (std::size_t i = 0; i < p.contour.size(); ++i) {
            if (i) s += ' ';
            s += num(p.contour[i].x) + "," + num(p.contour[i].y);
        }
        return s;
    }
};

bool same(double a, double b, double tol) { return std::fabs(a - b) <= tol; } // false for NaN
double key(double v) { return std::isfinite(v) ? v : 0.0; }

template <class T> void addDistinct(std::vector<T>& v, const T& x) {
    if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x);
}

} // namespace

std::string operationSignature(const domain::Part& part) {
    std::vector<std::string> tokens;
    tokens.reserve(part.operations.size());
    for (const auto& op : part.operations) tokens.push_back(std::visit(TokenVisitor{}, op.shape));
    std::sort(tokens.begin(), tokens.end());
    std::string out;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i) out += ';';
        out += tokens[i];
    }
    return out;
}

std::string CutListRow::name() const {
    std::string out;
    for (std::size_t i = 0; i < partNames.size(); ++i) {
        if (i) out += " / ";
        out += partNames[i];
    }
    return out;
}

CutList cutList(const domain::Model& model, const CutListOptions& options, const Scope& scope) {
    const auto c = detail::collect(model, scope);
    CutList out;
    out.diagnostics = partList(model, scope).diagnostics;
    const double tol = std::isfinite(options.sizeTolerance) ? std::max(0.0, options.sizeTolerance) : 0.0;

    for (const auto& ref : c.parts) {
        const PartRow r = detail::makeRow(model, ref);
        const std::string sig = operationSignature(*ref.part);
        auto matches = [&](const CutListRow& g) {
            if (g.materialId != r.materialId || g.grain != r.grain || g.operationSignature != sig) return false;
            if (!same(g.thickness, r.thickness, tol) || !same(g.cutLength, r.cutLength, tol) ||
                !same(g.cutWidth, r.cutWidth, tol) || !same(g.length, r.length, tol) ||
                !same(g.width, r.width, tol))
                return false;
            for (std::size_t i = 0; i < 4; ++i)
                if (g.edges[i].id != r.edges[i].id) return false;
            return options.groupAcrossAssemblies || g.assemblyIds.front() == r.assemblyId;
        };
        auto it = std::find_if(out.rows.begin(), out.rows.end(), matches);
        if (it == out.rows.end()) {
            CutListRow g;
            g.materialId = r.materialId;
            g.materialName = r.materialName;
            g.materialMissing = r.materialMissing;
            g.thickness = r.thickness;
            g.cutLength = r.cutLength;
            g.cutWidth = r.cutWidth;
            g.length = r.length;
            g.width = r.width;
            g.grain = r.grain;
            const auto* mat = model.findMaterial(r.materialId);
            g.rotationAllowed = r.grain == domain::GrainDirection::None || (mat && !mat->hasGrain);
            g.edges = r.edges;
            g.operationCount = r.operationCount;
            g.operationSignature = sig;
            out.rows.push_back(std::move(g));
            it = std::prev(out.rows.end());
        }
        ++it->quantity;
        it->partIds.push_back(r.partId);
        it->partNumbers.push_back(r.number);
        addDistinct(it->partNames, r.name);
        addDistinct(it->assemblyIds, r.assemblyId);
    }

    std::stable_sort(out.rows.begin(), out.rows.end(), [](const CutListRow& a, const CutListRow& b) {
        // Descending sizes: compare b against a for the numeric members.
        return std::make_tuple(std::cref(a.materialName), std::cref(a.materialId), key(b.cutLength),
                               key(b.cutWidth), a.partNumbers.front()) <
               std::make_tuple(std::cref(b.materialName), std::cref(b.materialId), key(a.cutLength),
                               key(a.cutWidth), b.partNumbers.front());
    });
    return out;
}

} // namespace fos::production
