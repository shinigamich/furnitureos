// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/furniture/parameters.hpp"

#include <algorithm>

namespace fos::furniture {
namespace {
using parametric::ConstraintDef;
using parametric::ConstraintSeverity;
using parametric::ParameterDef;
using parametric::ParameterKind;

StandardParameter input(const char* name, double value, const char* description, std::optional<double> lower = 0.0) {
    return {name, false, value, {}, description, lower};
}
StandardParameter derived(const char* name, std::string formula, const char* description) {
    return {name, true, 0.0, std::move(formula), description, std::nullopt};
}

const std::vector<StandardParameter>& defaultList() {
    static const std::vector<StandardParameter> list = standardParameters(ConstructionOptions{});
    return list;
}
const StandardParameter* findStandard(std::string_view name) {
    for (const auto& p : defaultList())
        if (p.name == name) return &p;
    return nullptr;
}
} // namespace

std::vector<StandardParameter> standardParameters(const ConstructionOptions& o) {
    namespace p = param;
    const bool topOver = o.topJoint == JointKind::OverSides;
    const bool bottomOver = o.bottomJoint == JointKind::OverSides;
    std::vector<StandardParameter> out{
        input(p::kWidth, 600, "Overall width", 1.0),
        input(p::kHeight, 720, "Overall height including the plinth", 1.0),
        input(p::kDepth, 560, "Carcass depth (front edge to rear edge of the sides)", 1.0),
        input(p::kThickness, 18, "Carcass board thickness (synchronised from the carcass material)", 1.0),
        input(p::kBackThickness, 3, "Back panel thickness (synchronised from the back material)", 0.1),
        input(p::kFrontThickness, 18, "Front thickness (synchronised from the front material)", 1.0),
        input(p::kPlinthHeight, 100, "Height of the plinth / feet space below the carcass"),
        input(p::kPlinthSetback, 50, "Plinth panel front face behind the carcass front"),
        input(p::kBackInset, 2, "Overlay back: inset from the outer carcass edges"),
        input(p::kGrooveDepth, 8, "Grooved back: groove depth"),
        input(p::kGrooveDistance, 10, "Grooved back: rear face of the back panel in front of the rear carcass edge"),
        input(p::kGrooveClearance, 1, "Grooved back: clearance between each back edge and the groove bottom"),
        input(p::kGrooveFit, 0, "Grooved back: groove width in excess of the back thickness"),
        input(p::kRailWidth, 100, "Width of the stretcher rails"),
        input(p::kShelfSetback, 20, "Adjustable shelf front edge behind the carcass front"),
        input(p::kShelfClearance, 1, "Clearance between an adjustable shelf and each supporting panel"),
        input(p::kShelfPinFront, 50, "Front shelf pin behind the shelf front edge"),
        input(p::kShelfPinBack, 50, "Rear shelf pin in front of the shelf rear edge"),
        input(p::kFrontReveal, 2, "Full overlay: front edge inside the outer cabinet envelope"),
        input(p::kFrontGap, 3, "Gap between two adjacent fronts"),
        input(p::kFrontOffset, 2, "Gap between the carcass front and the inner face of overlay fronts"),
        input(p::kInsetGap, 2, "Inset fronts: gap to the surrounding panels"),
        input(p::kHingeOffset, 100, "Top / bottom hinge centre from the door end"),
        input(p::kHandleEdgeOffset, 50, "Door handle centre from the free door edge"),
        input(p::kHandleEndOffset, 100, "Door handle centre from the door top / bottom (HandlePosition Top / Bottom)"),
        input(p::kJointEndOffset, 50, "First / last joint fastener from the joint ends (at most a quarter of the joint)"),
        input(p::kJointMaxSpacing, 250, "Largest joint fastener spacing", 1.0),
    };
    out.push_back(derived(p::kCarcassBottom, o.plinth == PlinthKind::None ? "0" : "plinth_height",
                          "Height of the underside of the bottom panel"));
    out.push_back(derived(p::kCarcassHeight, "height - carcass_bottom", "Height of the carcass box"));
    out.push_back(derived(p::kInnerWidth, "width - 2 * thickness", "Clear width between the sides"));
    out.push_back(derived(p::kSideBottom,
                          o.plinth == PlinthKind::SidesToFloor ? "0"
                          : bottomOver ? "carcass_bottom + thickness" : "carcass_bottom",
                          "Height of the lower end of the sides"));
    out.push_back(derived(p::kSideTop, topOver ? "height - thickness" : "height", "Height of the upper end of the sides"));
    out.push_back(derived(p::kSideHeight, "side_top - side_bottom", "Length of the sides"));
    out.push_back(derived(p::kTopLength, topOver ? "width" : "inner_width", "Length of the top panel / rails"));
    out.push_back(derived(p::kTopX, topOver ? "0" : "thickness", "Left end of the top panel / rails"));
    out.push_back(derived(p::kBottomLength, bottomOver ? "width" : "inner_width", "Length of the bottom panel"));
    out.push_back(derived(p::kBottomX, bottomOver ? "0" : "thickness", "Left end of the bottom panel"));
    out.push_back(derived(p::kInteriorBottom, "carcass_bottom + thickness", "Height of the upper face of the bottom panel"));
    out.push_back(derived(p::kInteriorTop, "height - thickness", "Height of the underside of the top panel / rails"));
    out.push_back(derived(p::kInnerHeight, "interior_top - interior_bottom", "Clear height between bottom and top"));
    out.push_back(derived(p::kBackEngagement, "groove_depth - groove_clearance", "Depth by which a grooved back enters each groove"));
    const char *bw = "0", *bh = "0", *bx = "0", *bz = "0", *by = "depth", *idepth = "depth";
    switch (o.back) {
    case BackMount::None: break;
    case BackMount::Overlay:
        bw = "width - 2 * back_inset"; bh = "carcass_height - 2 * back_inset";
        bx = "back_inset"; bz = "carcass_bottom + back_inset"; by = "depth + back_thickness";
        break;
    case BackMount::Grooved:
        bw = "inner_width + 2 * back_engagement"; bh = "inner_height + 2 * back_engagement";
        bx = "thickness - back_engagement"; bz = "interior_bottom - back_engagement"; by = "depth - groove_distance";
        idepth = "depth - groove_distance - back_thickness";
        break;
    case BackMount::Inset:
        bw = "inner_width"; bh = "inner_height"; bx = "thickness"; bz = "interior_bottom";
        idepth = "depth - back_thickness";
        break;
    }
    out.push_back(derived(p::kBackWidth, bw, "Back panel size along the cabinet width"));
    out.push_back(derived(p::kBackHeight, bh, "Back panel size along the cabinet height"));
    out.push_back(derived(p::kBackX, bx, "Left edge of the back panel"));
    out.push_back(derived(p::kBackZ, bz, "Lower edge of the back panel"));
    out.push_back(derived(p::kBackRearY, by, "Depth coordinate of the rear face of the back panel"));
    out.push_back(derived(p::kInteriorDepth, idepth, "Usable depth in front of the back panel"));
    out.push_back(derived(p::kShelfDepth, "interior_depth - shelf_setback", "Depth (part width) of adjustable shelves"));
    out.push_back(derived(p::kGrooveCenter, "depth - groove_distance - back_thickness / 2",
                          "Depth coordinate of the back groove centre line"));
    out.push_back(derived(p::kGrooveWidth, "back_thickness + groove_fit", "Width of the back groove"));
    out.push_back(derived(p::kOverlayOuter, "thickness - front_reveal",
                          "Full overlay: how far a front covers an outer carcass panel"));
    out.push_back(derived(p::kOverlayShared, "thickness / 2 - front_gap / 2",
                          "How far a front covers a panel shared with a neighbouring front"));
    return out;
}

std::vector<ConstraintDef> standardConstraints(const ConstructionOptions& o) {
    const auto err = ConstraintSeverity::Error;
    std::vector<ConstraintDef> out{
        {"std.inner_width", "inner_width > 0",
         "Inner width must be positive: width {width} mm minus two {thickness} mm sides leaves {inner_width} mm", err},
        {"std.inner_height", "inner_height > 0",
         "Inner height must be positive: height {height} mm leaves {inner_height} mm between the bottom and top panels "
         "({thickness} mm each, carcass starting at {carcass_bottom} mm)", err},
        {"std.interior_depth", "interior_depth > 0",
         "Interior depth must be positive: depth {depth} mm leaves {interior_depth} mm in front of the back panel", err},
        {"std.shelf_depth", "shelf_depth > 0",
         "Shelf setback {shelf_setback} mm must be smaller than the interior depth {interior_depth} mm "
         "(shelf depth would be {shelf_depth} mm)", err},
        {"std.front_overlay", "overlay_outer >= 0 && overlay_shared >= 0",
         "Front reveal {front_reveal} mm and half the front gap {front_gap} mm must not exceed what a {thickness} mm "
         "panel can carry", err},
    };
    if (o.back == BackMount::Overlay)
        out.push_back({"std.back_size", "back_width > 0 && back_height > 0",
                       "Back inset {back_inset} mm leaves a back panel of {back_width} x {back_height} mm", err});
    if (o.back == BackMount::Grooved) {
        out.push_back({"std.groove_depth", "groove_depth < thickness && back_engagement > 0",
                       "Groove depth {groove_depth} mm must be less than the board thickness {thickness} mm and more "
                       "than the groove clearance {groove_clearance} mm", err});
        if (o.top == TopKind::Rails)
            out.push_back({"std.rail_groove", "rail_width > groove_distance + back_thickness + groove_fit",
                           "Rail width {rail_width} mm must exceed groove distance {groove_distance} mm plus groove "
                           "width {groove_width} mm so that the rear rail can carry the back groove", err});
    }
    if (o.top == TopKind::Rails)
        out.push_back({"std.rail_width", "rail_width > 0 && 2 * rail_width <= depth",
                       "Two stretcher rails of {rail_width} mm do not fit into the depth {depth} mm", err});
    if (o.plinth == PlinthKind::SidesToFloor)
        out.push_back({"std.plinth", "plinth_height > 0 && plinth_setback + thickness <= depth",
                       "Plinth panel needs a height above 0 (is {plinth_height} mm) and setback {plinth_setback} mm plus "
                       "{thickness} mm within the depth {depth} mm", err});
    if (o.plinth == PlinthKind::Feet)
        out.push_back({"std.plinth", "plinth_height > 0", "Feet need a plinth height above 0 (is {plinth_height} mm)", err});
    return out;
}

bool isStandardParameter(std::string_view name) { return findStandard(name) != nullptr; }
bool isDerivedParameter(std::string_view name) {
    const auto* p = findStandard(name);
    return p && p->derived;
}
bool isMaterialSyncedParameter(std::string_view name) {
    return name == param::kThickness || name == param::kBackThickness || name == param::kFrontThickness;
}

core::Result<void> validateConstruction(const ConstructionOptions& o) {
    if (o.top == TopKind::Rails && o.topJoint != JointKind::BetweenSides)
        return core::makeError(core::ErrorCode::Unsupported,
                               "Stretcher rails (top 'Rails') sit between the sides: set the top joint to 'BetweenSides' "
                               "or use a full top panel.");
    if (o.plinth == PlinthKind::SidesToFloor && o.bottomJoint != JointKind::BetweenSides)
        return core::makeError(core::ErrorCode::Unsupported,
                               "Plinth 'SidesToFloor' needs the bottom between the sides: set the bottom joint to "
                               "'BetweenSides' or choose plinth 'Feet' or 'None'.");
    return {};
}

core::Result<parametric::ChangeSet> syncStandardParameters(CabinetDefinition& def) {
    if (auto ok = validateConstruction(def.construction); !ok) return ok.error();
    parametric::Transaction tx;
    for (const auto& sp : standardParameters(def.construction)) {
        const ParameterDef* existing = def.parameters.find(sp.name);
        if (!existing) {
            ParameterDef d = sp.derived ? ParameterDef::formula(sp.name, sp.formula, ParameterKind::Length)
                                        : ParameterDef::constant(sp.name, sp.defaultValue, ParameterKind::Length);
            d.description = sp.description;
            d.lowerBound = sp.lowerBound;
            tx.define(std::move(d));
        } else if (sp.derived && !def.pinnedParameters.count(sp.name) && existing->expression != sp.formula) {
            tx.setExpression(sp.name, sp.formula);
        }
    }
    for (const auto& c : def.parameters.constraints())
        if (c.name.rfind("std.", 0) == 0) tx.removeConstraint(c.name);
    for (auto& c : standardConstraints(def.construction)) tx.addConstraint(std::move(c));
    parametric::ParameterGraph graph = def.parameters; // cheap copy-on-write; commit only on success
    auto changes = graph.apply(tx);
    if (!changes) return changes.error();
    def.parameters = std::move(graph);
    return changes;
}

core::Result<CabinetDefinition> makeCabinet(domain::AssemblyId id, std::string name, const ConstructionOptions& options) {
    CabinetDefinition def;
    def.id = std::move(id);
    def.name = std::move(name);
    def.construction = options;
    if (auto r = syncStandardParameters(def); !r) return r.error();
    return def;
}

} // namespace fos::furniture
