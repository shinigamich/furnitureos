// SPDX-License-Identifier: AGPL-3.0-only
/// Checks 1 (parts), 2 (edge banding) and 7 (completeness).
#include "detail.hpp"

#include <map>
#include <set>

namespace fos::validation::detail {
namespace {
using core::Severity;
using domain::GrainDirection;

constexpr double kEps = core::kLengthEps;

bool validSheet(const domain::Material& m) noexcept {
    return finite(m.sheetLength) && finite(m.sheetWidth) && m.sheetLength > 0 && m.sheetWidth > 0;
}

void checkSheetFit(Context& c, const domain::Assembly& a, const domain::Part& p, const domain::Material& m) {
    if (!validSheet(m)) return; // reported by the completeness check
    const domain::CutSize cut = domain::cutSize(p, c.model);
    if (!finite(cut.length) || !finite(cut.width) || cut.length <= 0 || cut.width <= 0) return; // edge check
    const double L = m.sheetLength, W = m.sheetWidth;
    const bool straight = cut.length <= L + kEps && cut.width <= W + kEps; // part length along sheet length
    const bool rotated = cut.width <= L + kEps && cut.length <= W + kEps;  // part width along sheet length
    bool fits = straight || rotated;
    std::string why;
    if (m.hasGrain && p.grain == GrainDirection::AlongLength) {
        fits = straight;
        why = " with the grain running along the part length (no rotation allowed)";
    } else if (m.hasGrain && p.grain == GrainDirection::AlongWidth) {
        fits = rotated;
        why = " with the grain running along the part width (part must be rotated by 90 degrees)";
    } else {
        why = " in either orientation";
    }
    if (fits) return;
    c.add(Severity::Error, "validation.part.exceeds_sheet",
          label(p) + " has a cut size of " + num(cut.length) + " x " + num(cut.width) + " mm, which does not fit the " +
              num(L) + " x " + num(W) + " mm sheet of material '" + m.id.str() + "'" + why + ".",
          p.id.str(), a.id.str(), p.id.str(), {m.id.str()});
}

void checkPart(Context& c, const domain::Assembly& a, const domain::Part& p) {
    const std::string pid = p.id.str(), aid = a.id.str();
    const bool sizeOk = validSize(p);
    if (!sizeOk)
        c.add(Severity::Error, "validation.part.invalid_size",
              label(p) + " has an invalid size of " + num(p.length) + " x " + num(p.width) + " x " + num(p.thickness) +
                  " mm (length x width x thickness); all three must be finite and greater than 0.",
              pid, aid, pid);
    if (!validPlacement(p.placement))
        c.add(Severity::Error, "validation.part.non_rigid_placement",
              label(p) + " has a placement that is not a finite proper rotation plus translation (axes not "
                         "orthonormal and right-handed); it is excluded from the interference checks.",
              pid, aid, pid);

    const domain::Material* m = p.material.empty() ? nullptr : c.model.findMaterial(p.material);
    if (p.material.empty())
        c.add(Severity::Error, "validation.part.missing_material", label(p) + " has no material assigned.", pid, aid,
              pid);
    else if (!m)
        c.add(Severity::Error, "validation.part.missing_material",
              label(p) + " refers to material '" + p.material.str() + "', which is not in the model's material library.",
              pid, aid, pid, {p.material.str()});

    if (m && finite(p.thickness) && finite(m->thickness) && m->thickness > 0 &&
        !core::approxEqual(p.thickness, m->thickness))
        c.add(Severity::Error, "validation.part.thickness_mismatch",
              label(p) + " is " + num(p.thickness) + " mm thick but its material '" + m->id.str() + "' is " +
                  num(m->thickness) + " mm thick.",
              pid, aid, pid, {m->id.str()});

    if (!sizeOk) return;
    if (p.length < c.options.minPartSize || p.width < c.options.minPartSize)
        c.add(Severity::Warning, "validation.part.too_small",
              label(p) + " measures only " + num(p.length) + " x " + num(p.width) + " mm; parts below " +
                  num(c.options.minPartSize) + " mm in length or width cannot be machined safely.",
              pid, aid, pid);
    if (m) checkSheetFit(c, a, p, *m);
}
} // namespace

void checkParts(Context& c) {
    std::map<std::string, std::vector<const domain::Assembly*>> byId;
    for (const auto& a : c.model.assemblies) byId[a.id.str()].push_back(&a);
    for (const auto& [id, list] : byId) {
        if (list.size() < 2) continue;
        std::string names;
        for (const auto* a : list) names += (names.empty() ? "" : ", ") + label(*a);
        c.add(Severity::Error, "validation.assembly.duplicate_id",
              "Assembly id '" + id + "' is used by " + std::to_string(list.size()) + " assemblies (" + names + ").", id,
              id);
    }

    for (const auto& a : c.model.assemblies) {
        if (!validPlacement(a.placement))
            c.add(Severity::Error, "validation.assembly.non_rigid_placement",
                  label(a) + " has a placement that is not a finite proper rotation plus translation; it is excluded "
                             "from the interference check between assemblies.",
                  a.id.str(), a.id.str());
        std::map<std::string, std::size_t> counts;
        for (const auto& p : a.parts) ++counts[p.id.str()];
        for (const auto& [id, n] : counts)
            if (n > 1)
                c.add(Severity::Error, "validation.part.duplicate_id",
                      "Part id '" + id + "' is used by " + std::to_string(n) + " parts in " + label(a) + ".", id,
                      a.id.str(), id);
        for (const auto& p : a.parts) checkPart(c, a, p);
    }
}

void checkEdgeBanding(Context& c) {
    for (const auto& b : c.model.edgeBands)
        if (!finite(b.thickness) || b.thickness < 0 || !finite(b.width) || b.width <= 0)
            c.add(Severity::Error, "validation.edge.invalid_band",
                  "Edge band '" + b.id.str() + "' has thickness " + num(b.thickness) + " mm and width " + num(b.width) +
                      " mm; the thickness must be >= 0 and the width > 0.",
                  b.id.str());

    for (const auto& a : c.model.assemblies) {
        for (const auto& p : a.parts) {
            const std::string pid = p.id.str(), aid = a.id.str();
            bool anyBand = false;
            for (const domain::EdgeSide s : domain::kAllEdges) {
                const auto& ref = p.edge(s);
                if (!ref) continue;
                anyBand = true;
                const std::string edge = std::string("edge ") + domain::toString(s);
                const domain::EdgeBand* b = c.model.findEdgeBand(*ref);
                if (!b) {
                    c.add(Severity::Error, "validation.edge.missing_band",
                          label(p) + " " + edge + " refers to edge band '" + ref->str() +
                              "', which is not in the model's edge band library; the edge would stay raw.",
                          pid, aid, pid, {ref->str()});
                    continue;
                }
                if (finite(p.thickness) && finite(b->width) && b->width < p.thickness - kEps)
                    c.add(Severity::Error, "validation.edge.band_too_narrow",
                          label(p) + " " + edge + ": edge band '" + b->id.str() + "' is " + num(b->width) +
                              " mm wide but the board is " + num(p.thickness) + " mm thick.",
                          pid, aid, pid, {b->id.str()});
                const bool alongLength = s == domain::EdgeSide::L1 || s == domain::EdgeSide::L2;
                const double perp = alongLength ? p.width : p.length;
                if (finite(perp) && perp > 0 && finite(b->thickness) &&
                    b->thickness > c.options.maxBandThicknessShare * perp + kEps)
                    c.add(Severity::Warning, "validation.edge.band_too_thick",
                          label(p) + " " + edge + ": edge band '" + b->id.str() + "' is " + num(b->thickness) +
                              " mm thick, more than " + num(c.options.maxBandThicknessShare * 100) + " % of the " +
                              (alongLength ? "part width " : "part length ") + num(perp) + " mm.",
                          pid, aid, pid, {b->id.str()});
            }
            if (!anyBand || !validSize(p)) continue;
            const domain::CutSize cut = domain::cutSize(p, c.model);
            if (!(cut.length > 0) || !(cut.width > 0))
                c.add(Severity::Error, "validation.edge.non_positive_cut_size",
                      label(p) + " has a cut size of " + num(cut.length) + " x " + num(cut.width) + " mm after "
                          "subtracting its edge bands from the finished size " + num(p.length) + " x " + num(p.width) +
                          " mm.",
                      pid, aid, pid);
        }
    }
}

void checkCompleteness(Context& c) {
    std::set<std::string> usedMaterials;
    for (const auto& a : c.model.assemblies) {
        if (a.parts.empty())
            c.add(Severity::Warning, "validation.completeness.empty_assembly", "Assembly '" + (a.name.empty() ? a.id.str() : a.name) + "' contains no parts.",
                  a.id.str(), a.id.str());
        for (const auto& p : a.parts) {
            if (!p.material.empty()) usedMaterials.insert(p.material.str());
            if (p.name.empty())
                c.add(Severity::Warning, "validation.completeness.unnamed_part",
                      "Part '" + p.id.str() + "' in " + label(a) + " has no name; labels and reports will show its id.",
                      p.id.str(), a.id.str(), p.id.str());
        }
    }
    for (const auto& m : c.model.materials) {
        const std::string mid = m.id.str();
        if (!validSheet(m))
            c.add(Severity::Error, "validation.completeness.material_sheet_size",
                  "Material '" + mid + "' has no usable sheet size (" + num(m.sheetLength) + " x " + num(m.sheetWidth) +
                      " mm); cutting plans and sheet-fit checks need a positive length and width.",
                  mid);
        if (!finite(m.thickness) || m.thickness <= 0)
            c.add(Severity::Error, "validation.completeness.material_thickness",
                  "Material '" + mid + "' has a thickness of " + num(m.thickness) + " mm; it must be greater than 0.",
                  mid);
        if (usedMaterials.count(mid) && !(finite(m.costPerSheet) && m.costPerSheet > 0))
            c.add(Severity::Info, "validation.completeness.unknown_cost",
                  "Material '" + mid + "' has no cost per sheet; cost reports will exclude it.", mid);
    }
}

} // namespace fos::validation::detail
