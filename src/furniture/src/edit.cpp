// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/furniture/edit.hpp"

#include "fos/furniture/parameters.hpp"

#include <algorithm>
#include <map>

namespace fos::furniture {
namespace {

std::map<std::string, double> valuesOf(const parametric::ParameterGraph& g) {
    std::map<std::string, double> out;
    for (const auto& n : g.names()) out[n] = g.value(n).valueOr(0.0);
    return out;
}

core::Error noParameter(const CabinetDefinition& def, const std::string& name) {
    return core::makeError(core::ErrorCode::NotFound,
                           "Cabinet '" + def.id.str() + "' has no parameter '" + name + "'.", name);
}

void pinIfMaintained(CabinetDefinition& def, const std::string& name) {
    if (isDerivedParameter(name) || isMaterialSyncedParameter(name)) def.pinnedParameters.insert(name);
}

using Mutation = std::function<core::Result<void>(Section&)>;
core::Result<EditResult> editSection(CabinetDefinition& definition, const Libraries& libs, const std::string& path,
                                     const Mutation& mutation) {
    return applyEdit(definition, libs, [&](CabinetDefinition& d) -> core::Result<void> {
        auto s = findSection(d.interior, path);
        if (!s) return s.error();
        return mutation(*s.value());
    });
}

/// Pinned derived parameters whose standard formula differs between the construction options
/// of `before` and `after`: their pinned expression was chosen for the old construction and
/// is NOT rewritten, so the user must review it (furniture.parameter.pinned_formula_changed).
core::Diagnostics pinnedFormulaWarnings(const CabinetDefinition& before, const CabinetDefinition& after) {
    core::Diagnostics out;
    if (before.construction == after.construction) return out;
    const auto oldList = standardParameters(before.construction), newList = standardParameters(after.construction);
    for (const auto& np : newList) {
        if (!np.derived || !after.pinnedParameters.count(np.name)) continue;
        const auto op = std::find_if(oldList.begin(), oldList.end(), [&](const auto& x) { return x.name == np.name; });
        if (op == oldList.end() || op->formula == np.formula) continue;
        const parametric::ParameterDef* pd = after.parameters.find(np.name);
        const std::string pinned = pd ? (pd->isExpression() ? "'" + pd->expression + "'" : core::formatLength(pd->value, 3))
                                      : std::string("?");
        std::string values;
        const auto current = after.parameters.value(np.name);
        const auto standard = after.parameters.evaluate(np.formula);
        if (current && standard)
            values = " (pinned value " + core::formatLength(current.value(), 3) + " mm, standard value " +
                     core::formatLength(standard.value(), 3) + " mm)";
        out.push_back({core::Severity::Warning, "furniture.parameter.pinned_formula_changed",
                       "Parameter '" + np.name + "' is pinned to " + pinned +
                           ", but with the new construction options its standard formula is '" + np.formula +
                           "' (was '" + op->formula + "')" + values +
                           ". The pinned value is kept; reset the parameter to follow the new construction.",
                       after.id.str()});
    }
    return out;
}

} // namespace

core::Result<EditResult> applyEdit(CabinetDefinition& definition, const Libraries& libraries,
                                   const std::function<core::Result<void>(CabinetDefinition&)>& mutate) {
    CabinetDefinition copy = definition;
    if (auto r = mutate(copy); !r) return r.error();
    if (auto r = resolve(copy, libraries); !r) return r.error();
    auto generated = generate(copy, libraries);
    if (!generated) return generated.error();

    EditResult out;
    const auto before = valuesOf(definition.parameters), after = valuesOf(copy.parameters);
    for (const auto& [name, value] : after) {
        const auto it = before.find(name);
        if (it == before.end() || it->second != value) out.changedParameters.push_back(name);
    }
    for (const auto& [name, value] : before)
        if (!after.count(name)) out.changedParameters.push_back(name);
    std::sort(out.changedParameters.begin(), out.changedParameters.end());
    out.generated = std::move(generated.value());
    const auto pinned = pinnedFormulaWarnings(definition, copy);
    out.generated.diagnostics.insert(out.generated.diagnostics.end(), pinned.begin(), pinned.end());
    definition = std::move(copy);
    return out;
}

core::Result<EditResult> setParameter(CabinetDefinition& definition, const Libraries& libs, const std::string& name,
                                      double value) {
    return applyEdit(definition, libs, [&](CabinetDefinition& d) -> core::Result<void> {
        if (!d.parameters.contains(name)) return noParameter(d, name);
        if (auto r = d.parameters.setValue(name, value); !r) return r.error();
        pinIfMaintained(d, name);
        return {};
    });
}

core::Result<EditResult> setParameterExpression(CabinetDefinition& definition, const Libraries& libs,
                                                const std::string& name, const std::string& expression) {
    return applyEdit(definition, libs, [&](CabinetDefinition& d) -> core::Result<void> {
        if (!d.parameters.contains(name)) return noParameter(d, name);
        if (expression.empty())
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "The expression for parameter '" + name + "' is empty; use setParameter for a constant.",
                                   name);
        if (auto r = d.parameters.setExpression(name, expression); !r) return r.error();
        pinIfMaintained(d, name);
        return {};
    });
}

core::Result<EditResult> resetParameter(CabinetDefinition& definition, const Libraries& libs, const std::string& name) {
    return applyEdit(definition, libs, [&](CabinetDefinition& d) -> core::Result<void> {
        if (!isStandardParameter(name))
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "'" + name + "' is not a standard cabinet parameter; only standard parameters have a "
                                   "default to return to.", name);
        d.pinnedParameters.erase(name);
        for (const auto& sp : standardParameters(d.construction)) {
            if (sp.name != name) continue;
            // Derived formulas are restored by resolve(); thicknesses by the material sync.
            if (!sp.derived && !isMaterialSyncedParameter(name))
                if (auto r = d.parameters.setValue(name, sp.defaultValue); !r) return r.error();
        }
        return {};
    });
}

core::Result<EditResult> addParameter(CabinetDefinition& definition, const Libraries& libs,
                                      const parametric::ParameterDef& parameter) {
    return applyEdit(definition, libs, [&](CabinetDefinition& d) -> core::Result<void> {
        if (isStandardParameter(parameter.name))
            return core::makeError(core::ErrorCode::AlreadyExists,
                                   "'" + parameter.name + "' is a standard cabinet parameter; choose another name for a "
                                   "custom parameter.", parameter.name);
        if (auto r = d.parameters.define(parameter); !r) return r.error();
        return {};
    });
}

core::Result<EditResult> removeParameter(CabinetDefinition& definition, const Libraries& libs, const std::string& name) {
    return applyEdit(definition, libs, [&](CabinetDefinition& d) -> core::Result<void> {
        if (isStandardParameter(name))
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "'" + name + "' is a standard cabinet parameter and cannot be removed.", name);
        if (!d.parameters.contains(name)) return noParameter(d, name);
        if (auto r = d.parameters.remove(name); !r) return r.error();
        return {};
    });
}

core::Result<EditResult> setConstruction(CabinetDefinition& definition, const Libraries& libs,
                                         const ConstructionOptions& options) {
    return applyEdit(definition, libs, [&](CabinetDefinition& d) -> core::Result<void> {
        d.construction = options;
        return {};
    });
}

core::Result<EditResult> setMaterials(CabinetDefinition& definition, const Libraries& libs,
                                      const MaterialAssignment& materials) {
    return applyEdit(definition, libs, [&](CabinetDefinition& d) -> core::Result<void> {
        d.materials = materials;
        return {};
    });
}

core::Result<EditResult> setHardware(CabinetDefinition& definition, const Libraries& libs, const HardwareSelection& hw) {
    return applyEdit(definition, libs, [&](CabinetDefinition& d) -> core::Result<void> {
        d.hardware = hw;
        return {};
    });
}

core::Result<EditResult> setPartOverride(CabinetDefinition& definition, const Libraries& libs, const std::string& slot,
                                         const std::optional<PartOverride>& override) {
    auto r = applyEdit(definition, libs, [&](CabinetDefinition& d) -> core::Result<void> {
        if (override) d.materials.overrides[slot] = *override;
        else d.materials.overrides.erase(slot);
        return {};
    });
    return r;
}

core::Result<EditResult> setInterior(CabinetDefinition& definition, const Libraries& libs, const Section& root) {
    return applyEdit(definition, libs, [&](CabinetDefinition& d) -> core::Result<void> {
        d.interior = root;
        return {};
    });
}

core::Result<EditResult> replaceSection(CabinetDefinition& definition, const Libraries& libs, const std::string& path,
                                        const Section& section) {
    return editSection(definition, libs, path, [&](Section& s) -> core::Result<void> {
        const SectionSize size = s.size;
        s = section;
        s.size = size;
        return {};
    });
}

core::Result<EditResult> setShelfCount(CabinetDefinition& definition, const Libraries& libs, const std::string& path,
                                       int count) {
    return editSection(definition, libs, path, [&](Section& s) -> core::Result<void> {
        s.shelfCount = count;
        return {};
    });
}

core::Result<EditResult> setFront(CabinetDefinition& definition, const Libraries& libs, const std::string& path,
                                  const std::optional<FrontSpec>& front) {
    return editSection(definition, libs, path, [&](Section& s) -> core::Result<void> {
        s.front = front;
        return {};
    });
}

core::Result<EditResult> splitSection(CabinetDefinition& definition, const Libraries& libs, const std::string& path,
                                      SplitDirection direction, const std::vector<SectionSize>& sizes) {
    return editSection(definition, libs, path, [&](Section& s) -> core::Result<void> {
        const std::string label = "Section '" + (path.empty() ? std::string("root") : path) + "'";
        if (s.split != SplitDirection::None)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   label + " is already split (" + toString(s.split) + "); split one of its children instead.",
                                   path);
        if (direction == SplitDirection::None || sizes.size() < 2)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   label + ": a split needs a direction (Vertical or Horizontal) and at least 2 child "
                                           "sizes (got " + std::to_string(sizes.size()) + ").", path);
        s.split = direction;
        for (const auto& size : sizes) {
            Section child;
            child.size = size;
            s.children.push_back(std::move(child));
        }
        s.children.front().shelfCount = s.shelfCount;
        s.shelfCount = 0;
        return {};
    });
}

core::Result<EditResult> setSectionSize(CabinetDefinition& definition, const Libraries& libs, const std::string& path,
                                        const SectionSize& size) {
    if (path.empty())
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The root section always fills the cabinet interior; only child sections have a size.");
    return editSection(definition, libs, path, [&](Section& s) -> core::Result<void> {
        s.size = size;
        return {};
    });
}

} // namespace fos::furniture
