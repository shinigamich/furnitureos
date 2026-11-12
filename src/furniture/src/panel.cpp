// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/furniture/panel.hpp"

namespace fos::furniture {

PanelDefinition makePanel(domain::PartId id, std::string name, domain::MaterialId material) {
    PanelDefinition def;
    def.id = std::move(id);
    def.name = std::move(name);
    def.material = std::move(material);
    using parametric::ParameterDef;
    using parametric::ParameterKind;
    parametric::Transaction tx;
    tx.define(ParameterDef::constant(panel_param::kLength, 600, ParameterKind::Length));
    tx.define(ParameterDef::constant(panel_param::kWidth, 300, ParameterKind::Length));
    for (const char* n : {panel_param::kX, panel_param::kY, panel_param::kZ})
        tx.define(ParameterDef::constant(n, 0, ParameterKind::Length));
    [[maybe_unused]] const auto r = def.parameters.apply(tx); // constants only: cannot fail
    return def;
}

core::Result<PanelResult> generatePanel(const PanelDefinition& def, const Libraries& libs) {
    const auto fail = [&](core::ErrorCode code, std::string message) {
        return core::makeError(code, "Panel '" + def.id.str() + "': " + std::move(message), def.id.str());
    };
    double vals[5] = {};
    const char* names[5] = {panel_param::kLength, panel_param::kWidth, panel_param::kX, panel_param::kY, panel_param::kZ};
    for (int i = 0; i < 5; ++i) {
        auto v = def.parameters.value(names[i]);
        if (!v) return fail(core::ErrorCode::NotFound, std::string("the standard parameter '") + names[i] + "' is missing.");
        vals[i] = v.value();
    }
    const double length = vals[0], width = vals[1];
    const geometry::Vec3 o{vals[2], vals[3], vals[4]};
    if (!(length > 0) || !(width > 0))
        return fail(core::ErrorCode::ConstraintViolation,
                    "length and width must be greater than 0 (are " + core::formatLength(length, 3) + " x " +
                        core::formatLength(width, 3) + " mm).");
    const domain::Material* m = libs.findMaterial(def.material);
    if (!m) return fail(core::ErrorCode::NotFound, "material '" + def.material.str() + "' is not in the material library.");

    PanelResult out;
    domain::Part& p = out.part;
    p.id = def.id;
    p.name = def.name;
    p.role = def.role;
    p.length = length;
    p.width = width;
    p.thickness = m->thickness;
    p.material = def.material;
    p.grain = m->hasGrain ? domain::GrainDirection::AlongLength : domain::GrainDirection::None;
    using geometry::Transform;
    switch (def.orientation) {
    case PanelOrientation::Horizontal: p.placement = Transform::translation(o); break;
    case PanelOrientation::VerticalSide: // thickness -> -X, so the origin is on the +X face
        p.placement = Transform::fromAxes({0, 0, 1}, {0, 1, 0}, {o.x + m->thickness, o.y, o.z});
        break;
    case PanelOrientation::VerticalFront: // thickness -> -Y, so the origin is on the rear face
        p.placement = Transform::fromAxes({1, 0, 0}, {0, 0, 1}, {o.x, o.y + m->thickness, o.z});
        break;
    }
    for (const auto side : domain::kAllEdges) {
        const auto& band = def.edges[static_cast<std::size_t>(side)];
        if (band && !libs.findEdgeBand(*band)) {
            out.diagnostics.push_back({core::Severity::Warning, "furniture.edge.missing_band",
                                       "Edge band '" + band->str() + "' for edge " + domain::toString(side) + " of panel '" +
                                           def.id.str() + "' is not in the edge band library; the edge is left raw.",
                                       def.id.str()});
            continue;
        }
        p.edge(side) = band;
    }
    return out;
}

} // namespace fos::furniture
