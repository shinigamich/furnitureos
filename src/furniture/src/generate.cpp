// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/furniture/generate.hpp"

#include "fos/machining/validation.hpp"
#include "gen_context.hpp"

#include <cmath>

namespace fos::furniture {
namespace detail {

std::string num(double value) { return core::formatLength(value, 3); }

std::string joinSlot(std::initializer_list<std::string_view> segments) {
    std::string out;
    for (const auto s : segments) {
        if (s.empty()) continue;
        if (!out.empty()) out += '.';
        out += s;
    }
    return out;
}

std::string sectionLabel(const std::string& path) { return path.empty() ? "root" : path; }

std::size_t Gen::index(const std::string& slot) const {
    const std::string id = partId(slot);
    for (std::size_t i = 0; i < assembly.parts.size(); ++i)
        if (assembly.parts[i].id.str() == id) return i;
    return assembly.parts.size();
}
void Gen::warn(std::string code, std::string message, std::string entity) {
    diagnostics.push_back({core::Severity::Warning, std::move(code), std::move(message), std::move(entity)});
}
void Gen::error(std::string code, std::string message, std::string entity) {
    diagnostics.push_back({core::Severity::Error, std::move(code), std::move(message), std::move(entity)});
}

namespace {
domain::MaterialId roleMaterial(const MaterialAssignment& m, MaterialRole role) {
    switch (role) {
    case MaterialRole::Carcass: return m.carcass;
    case MaterialRole::Back: return m.back;
    case MaterialRole::Front: return m.front.empty() ? m.carcass : m.front;
    }
    return m.carcass;
}
} // namespace

std::size_t Gen::addPart(const std::string& slot, std::string name, domain::PartRole role, MaterialRole material,
                         double length, double width, double thickness, const Transform& placement, const Edges& edges) {
    domain::Part p;
    p.id = domain::PartId(partId(slot));
    p.name = std::move(name);
    p.role = role;
    p.length = length;
    p.width = width;
    p.thickness = thickness;
    p.placement = placement;
    p.custom["furniture.slot"] = slot;

    const MaterialAssignment& ma = def.materials;
    const auto ovIt = ma.overrides.find(slot);
    const PartOverride* ov = ovIt == ma.overrides.end() ? nullptr : &ovIt->second;
    p.material = roleMaterial(ma, material);
    if (ov && ov->material) {
        // resolve() verified that override materials exist.
        const domain::Material* m = libs.findMaterial(*ov->material);
        p.material = *ov->material;
        if (m && !core::approxEqual(m->thickness, thickness))
            warn("furniture.material.override_thickness",
                 "Part '" + p.name + "' (" + slot + ") is overridden with material '" + m->name + "' of " +
                     num(m->thickness) + " mm, but the cabinet is constructed for " + num(thickness) +
                     " mm at this position; the part keeps " + num(thickness) + " mm.",
                 p.id.str());
    }
    const domain::Material* mat = libs.findMaterial(p.material);
    p.grain = mat && mat->hasGrain ? domain::GrainDirection::AlongLength : domain::GrainDirection::None;

    for (const auto side : domain::kAllEdges) {
        std::optional<domain::EdgeBandId> band;
        switch (edges[static_cast<std::size_t>(side)]) {
        case EdgeUse::Raw: break;
        case EdgeUse::Visible:
            if (!ma.visibleEdge.empty()) band = ma.visibleEdge;
            break;
        case EdgeUse::Hidden: band = ma.hiddenEdge; break;
        case EdgeUse::Front:
            if (ma.frontEdge) band = ma.frontEdge;
            else if (!ma.visibleEdge.empty()) band = ma.visibleEdge;
            break;
        }
        if (ov) {
            if (const auto it = ov->edges.find(side); it != ov->edges.end()) band = it->second;
        }
        if (band && !libs.findEdgeBand(*band)) {
            warn("furniture.edge.missing_band",
                 "Edge band '" + band->str() + "' for edge " + domain::toString(side) + " of part '" + p.name + "' (" +
                     slot + ") is not in the edge band library; the edge is left raw.",
                 p.id.str());
            band.reset();
        }
        p.edge(side) = band;
    }
    assembly.parts.push_back(std::move(p));
    return assembly.parts.size() - 1;
}

core::Result<std::vector<double>> distribute(const Gen& g, double available, const std::vector<SectionSize>& sizes,
                                             const std::string& what) {
    std::vector<double> out(sizes.size(), 0.0);
    double fixedSum = 0, weightSum = 0;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const SectionSize& s = sizes[i];
        const std::string item = what + ", entry " + std::to_string(i + 1);
        if (s.kind == SizeKind::Weight) {
            if (!(s.value > 0) || !std::isfinite(s.value))
                return core::makeError(core::ErrorCode::InvalidArgument,
                                       item + ": the weight must be greater than 0 (is " + num(s.value) + ").");
            weightSum += s.value;
            continue;
        }
        double mm = s.value;
        if (s.kind == SizeKind::Expression) {
            auto r = g.def.parameters.evaluate(s.expression);
            if (!r)
                return core::makeError(r.error().code,
                                       item + ": size expression '" + s.expression + "' cannot be evaluated: " +
                                           r.error().message);
            mm = r.value();
        }
        if (!(mm > 0) || !std::isfinite(mm))
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   item + ": the size must be greater than 0 mm (is " + num(mm) + " mm).");
        out[i] = mm;
        fixedSum += mm;
    }
    const double rest = available - fixedSum;
    if (weightSum > 0) {
        if (rest <= core::kLengthEps)
            return core::makeError(core::ErrorCode::ConstraintViolation,
                                   what + ": the fixed sizes add up to " + num(fixedSum) + " mm but only " +
                                       num(available) + " mm are available, leaving " + num(rest) +
                                       " mm for the proportional entries. Reduce the fixed sizes or enlarge the cabinet.");
        for (std::size_t i = 0; i < sizes.size(); ++i)
            if (sizes[i].kind == SizeKind::Weight) out[i] = rest * sizes[i].value / weightSum;
    } else if (rest < -core::kLengthEps) {
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               what + ": the fixed sizes add up to " + num(fixedSum) + " mm but only " + num(available) +
                                   " mm are available (" + num(-rest) + " mm too much).");
    } else if (rest > core::kLengthEps) {
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               what + ": the fixed sizes add up to " + num(fixedSum) + " mm, leaving " + num(rest) +
                                   " mm of the available " + num(available) +
                                   " mm unassigned. Make one entry proportional (Weight) to absorb the rest.");
    }
    return out;
}

} // namespace detail

namespace {
struct Sync {
    const char* parameter;
    MaterialRole role;
    const char* label;
};

core::Error missingMaterial(const CabinetDefinition& def, const char* label, const domain::MaterialId& id) {
    return core::makeError(core::ErrorCode::NotFound,
                           id.empty() ? std::string("Cabinet '") + def.id.str() + "' has no " + label +
                                            " material assigned."
                                      : std::string("The ") + label + " material '" + id.str() + "' of cabinet '" +
                                            def.id.str() + "' is not in the material library.",
                           def.id.str());
}
} // namespace

core::Result<void> resolve(CabinetDefinition& definition, const Libraries& libs, core::Diagnostics* diagnostics) {
    CabinetDefinition def = definition;
    core::Diagnostics diags;
    auto synced = syncStandardParameters(def);
    if (!synced) return synced.error();

    const MaterialAssignment& ma = def.materials;
    const domain::MaterialId frontId = ma.front.empty() ? ma.carcass : ma.front;
    const Sync syncs[] = {{param::kThickness, MaterialRole::Carcass, "carcass"},
                          {param::kBackThickness, MaterialRole::Back, "back"},
                          {param::kFrontThickness, MaterialRole::Front, "front"}};
    parametric::Transaction tx;
    for (const Sync& s : syncs) {
        if (s.role == MaterialRole::Back && def.construction.back == BackMount::None) continue;
        const domain::MaterialId& id =
            s.role == MaterialRole::Carcass ? ma.carcass : s.role == MaterialRole::Back ? ma.back : frontId;
        const domain::Material* m = libs.findMaterial(id);
        if (!m) return missingMaterial(def, s.label, id);
        const double current = def.parameters.value(s.parameter).valueOr(0.0);
        if (def.pinnedParameters.count(s.parameter)) {
            if (!core::approxEqual(current, m->thickness))
                diags.push_back({core::Severity::Warning, "furniture.material.thickness_mismatch",
                                 std::string("Parameter '") + s.parameter + "' is fixed by the user at " +
                                     detail::num(current) + " mm but the " + s.label + " material '" + m->name +
                                     "' is " + detail::num(m->thickness) +
                                     " mm thick. Reset the parameter to follow the material, or assign a matching material.",
                                 def.id.str()});
        } else {
            const parametric::ParameterDef* pd = def.parameters.find(s.parameter);
            if (pd->isExpression() || !core::approxEqual(current, m->thickness)) tx.setValue(s.parameter, m->thickness);
        }
    }
    for (const auto& [slot, ov] : ma.overrides)
        if (ov.material && !libs.findMaterial(*ov.material))
            return core::makeError(core::ErrorCode::NotFound,
                                   "The override material '" + ov.material->str() + "' for part '" + slot +
                                       "' of cabinet '" + def.id.str() + "' is not in the material library.",
                                   def.id.str());
    if (!tx.empty()) {
        auto r = def.parameters.apply(tx);
        if (!r)
            return core::makeError(r.error().code,
                                   "Cabinet '" + def.id.str() + "' cannot take the board thicknesses of its materials: " +
                                       r.error().message,
                                   def.id.str());
    }
    for (const auto& d : def.parameters.checkConstraints())
        if (d.severity == core::Severity::Warning)
            diags.push_back({core::Severity::Warning, "furniture.parameter.constraint", d.message, def.id.str()});
    definition = std::move(def);
    if (diagnostics) diagnostics->insert(diagnostics->end(), diags.begin(), diags.end());
    return {};
}

core::Result<GenerationResult> generate(const CabinetDefinition& definition, const Libraries& libraries) {
    CabinetDefinition def = definition;
    core::Diagnostics diags;
    if (auto r = resolve(def, libraries, &diags); !r) return r.error();

    detail::Gen g{def, libraries, {}, {}, std::move(diags), {}};
    for (const auto& name : def.parameters.names()) g.values[name] = def.parameters.value(name).valueOr(0.0);
    g.assembly.id = def.id;
    g.assembly.name = def.name;
    g.assembly.placement = def.placement;

    auto bounds = detail::buildCarcass(g);
    if (!bounds) return bounds.error();
    if (auto r = detail::buildInterior(g, bounds.value()); !r) return r.error();
    if (auto r = detail::buildBack(g); !r) return r.error();
    detail::runHardware(g);

    for (const auto& part : g.assembly.parts) {
        auto found = machining::validateOperations(part);
        g.diagnostics.insert(g.diagnostics.end(), found.begin(), found.end());
    }
    return GenerationResult{std::move(g.assembly), std::move(g.diagnostics), std::move(g.values)};
}

} // namespace fos::furniture
