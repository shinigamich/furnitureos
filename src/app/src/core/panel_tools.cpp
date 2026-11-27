// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/panel_tools.hpp"

#include "edit_tools_detail.hpp"
#include "fos/app/session.hpp"
#include "fos/app/templates.hpp"
#include "fos/core/units.hpp"
#include "fos/furniture/panel.hpp"

#include <QCoreApplication>

#include <cmath>

namespace fos::app {
namespace {

[[maybe_unused]] const char* const kPanelKeys[] = {
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Add panel {0}"),
};

using detail::axisValue;
using furniture::PanelOrientation;

core::Error invalid(std::string message) { return core::makeError(core::ErrorCode::InvalidArgument, std::move(message)); }

std::string mm(double v) { return core::formatLength(v) + " mm"; }

/// Assembly axes of the panel's length, width and thickness.
struct PanelAxes {
    Axis length, width, thickness;
};
PanelAxes panelAxes(PanelOrientation o) {
    switch (o) {
    case PanelOrientation::Horizontal: return {Axis::X, Axis::Y, Axis::Z};
    case PanelOrientation::VerticalSide: return {Axis::Z, Axis::Y, Axis::X};
    case PanelOrientation::VerticalFront: return {Axis::X, Axis::Z, Axis::Y};
    }
    return {Axis::X, Axis::Y, Axis::Z};
}

void setAxis(geometry::Vec3& v, Axis a, double value) {
    (a == Axis::X ? v.x : a == Axis::Y ? v.y : v.z) = value;
}

const char* axisName(Axis a) { return a == Axis::X ? "X" : a == Axis::Y ? "Y" : "Z"; }

} // namespace

geometry::Vec3 panelBoxSize(PanelOrientation orientation, double length, double width, double thickness) {
    geometry::Vec3 s;
    const auto axes = panelAxes(orientation);
    setAxis(s, axes.length, length);
    setAxis(s, axes.width, width);
    setAxis(s, axes.thickness, thickness);
    return s;
}

core::Result<AddPanelPlan> planAddPanel(const project::Project& project, const project::Resolved* resolved,
                                        const AddPanelRequest& request) {
    const domain::Material* material = project.libraries.findMaterial(domain::MaterialId(request.materialId));
    if (!material)
        return core::makeError(core::ErrorCode::NotFound,
                               "Material '" + request.materialId + "' is not in the project libraries.",
                               request.materialId);
    for (const auto& band : request.edges)
        if (band && !project.libraries.findEdgeBand(domain::EdgeBandId(*band)))
            return core::makeError(core::ErrorCode::NotFound, "Edge band '" + *band + "' is not in the project libraries.",
                                   *band);
    AddPanelPlan plan;
    plan.thickness = material->thickness;
    plan.length = request.length;
    plan.width = request.width;
    if (!(plan.thickness > 0) || !std::isfinite(plan.thickness))
        return invalid("Material '" + request.materialId + "' has no usable thickness.");

    const bool needsRefs = request.placement != PanelPlacement::Origin;
    std::vector<geometry::Aabb> refs;
    if (needsRefs) {
        if (!resolved) return invalid("The project could not be resolved, so the selection has no position.");
        if (auto r = detail::checkItemIds(project, request.referenceIds); !r)
            return invalid("Select the item(s) to place the panel against: " + r.error().message);
        for (const auto& id : request.referenceIds) {
            const auto box = itemBounds(*resolved, id);
            if (!box)
                return core::makeError(core::ErrorCode::NotFound,
                                       "Item '" + id + "' has no generated geometry (see Diagnostics).", id);
            refs.push_back(*box);
        }
    }
    const auto axes = panelAxes(request.orientation);

    if (request.placement == PanelPlacement::FillGap) {
        if (refs.size() != 2)
            return invalid("Fill gap needs exactly two selected items; " + std::to_string(refs.size()) + " are selected.");
        // Separation axis: the largest positive clearance between the two boxes.
        std::optional<Axis> gapAxis;
        double clearance = 0;
        geometry::Aabb region;
        for (Axis a : {Axis::X, Axis::Y, Axis::Z}) {
            const auto& lo = axisValue(refs[0].min, a) <= axisValue(refs[1].min, a) ? refs[0] : refs[1];
            const auto& hi = &lo == &refs[0] ? refs[1] : refs[0];
            const double c = axisValue(hi.min, a) - axisValue(lo.max, a);
            if (c > clearance + 1e-9) {
                clearance = c;
                gapAxis = a;
                setAxis(region.min, a, axisValue(lo.max, a));
                setAxis(region.max, a, axisValue(hi.min, a));
            }
        }
        if (!gapAxis)
            return invalid("The two selected items touch or overlap, so there is no gap between them to fill.");
        for (Axis a : {Axis::X, Axis::Y, Axis::Z}) {
            if (a == *gapAxis) continue;
            const double lo = std::max(axisValue(refs[0].min, a), axisValue(refs[1].min, a));
            const double hi = std::min(axisValue(refs[0].max, a), axisValue(refs[1].max, a));
            if (hi - lo <= 1e-9)
                return invalid(std::string("The two selected items do not overlap along ") + axisName(a) +
                               ", so the gap between them has no extent there.");
            setAxis(region.min, a, lo);
            setAxis(region.max, a, hi);
        }
        const geometry::Vec3 extent = region.max - region.min;
        plan.length = axisValue(extent, axes.length);
        plan.width = axisValue(extent, axes.width);
        const double room = axisValue(extent, axes.thickness);
        if (request.thicknessOffset < -1e-9 || request.thicknessOffset + plan.thickness > room + 1e-9)
            return invalid("The gap is " + mm(room) + " along " + axisName(axes.thickness) + " but the panel needs " +
                           mm(plan.thickness) + " at offset " + mm(request.thicknessOffset) + ".");
        plan.minCorner = region.min;
        setAxis(plan.minCorner, axes.thickness, axisValue(region.min, axes.thickness) + request.thicknessOffset);
    }

    const auto maxDim = 100000.0;
    for (const auto& [name, v] : {std::pair{"length", plan.length}, std::pair{"width", plan.width}})
        if (!std::isfinite(v) || v <= 0 || v > maxDim)
            return invalid(std::string("The panel ") + name + " must be more than 0 and at most 100 m; " + mm(v) +
                           " was given.");
    plan.size = panelBoxSize(request.orientation, plan.length, plan.width, plan.thickness);

    if (request.placement != PanelPlacement::FillGap && request.placement != PanelPlacement::Origin) {
        geometry::Aabb box;
        for (const auto& r : refs) box.expand(r);
        geometry::Vec3 c = box.min;
        switch (request.placement) {
        case PanelPlacement::OnTop: c.z = box.max.z; break;
        case PanelPlacement::Below: c.z = box.min.z - plan.size.z; break;
        case PanelPlacement::LeftOf: c.x = box.min.x - plan.size.x; break;
        case PanelPlacement::RightOf: c.x = box.max.x; break;
        case PanelPlacement::InFront: c.y = box.min.y - plan.size.y; break;
        case PanelPlacement::Behind: c.y = box.max.y; break;
        default: break;
        }
        plan.minCorner = c;
    }
    return plan;
}

core::Result<project::Item> makePanelItem(const project::Project& project, const std::string& itemId,
                                          const AddPanelRequest& request, const AddPanelPlan& plan) {
    const std::string base = request.name.empty()
                                 ? QCoreApplication::translate("fos::app::PanelTools", "Panel").toStdString()
                                 : request.name;
    auto def = furniture::makePanel(domain::PartId(itemId + "/panel"), uniqueItemName(project, base),
                                    domain::MaterialId(request.materialId));
    def.orientation = request.orientation;
    for (std::size_t i = 0; i < 4; ++i)
        if (request.edges[i]) def.edges[i] = domain::EdgeBandId(*request.edges[i]);
    parametric::Transaction tx;
    tx.setValue(furniture::panel_param::kLength, plan.length);
    tx.setValue(furniture::panel_param::kWidth, plan.width);
    if (auto r = def.parameters.apply(tx); !r) return r.error();
    project::PanelItem panel;
    panel.id = domain::AssemblyId(itemId);
    panel.placement = geometry::Transform::translation(plan.minCorner);
    panel.panel = std::move(def);
    project::Item item;
    item.definition = std::move(panel);
    return item;
}

core::Result<std::string> applyAddPanel(Session& session, const AddPanelRequest& request) {
    const auto plan = planAddPanel(session.project(), session.resolved(), request);
    if (!plan) return plan.error();
    const std::string id = nextItemId(session.project(), "panel");
    auto item = makePanelItem(session.project(), id, request, plan.value());
    if (!item) return item.error();
    auto command = project::commands::addItem(std::move(item).value());
    command.label = {"Add panel {0}", {id}};
    if (auto r = session.execute(command); !r) return r.error();
    session.selectItem(id);
    return id;
}

} // namespace fos::app
