// SPDX-License-Identifier: AGPL-3.0-only
/// Corner constructions: structural checks and the blind corner panel (README "Corner cabinets").
#include "gen_context.hpp"

namespace fos::furniture::detail {
namespace {

core::Error invalid(const Gen& g, const std::string& message) {
    return core::makeError(core::ErrorCode::InvalidArgument, message, g.def.id.str());
}

bool hasValue(const Gen& g, const char* name) { return g.values.count(name) != 0; }

} // namespace

core::Result<void> checkCorner(const Gen& g) {
    const CornerSpec& c = g.def.corner;
    const std::string id = g.def.id.str();
    if (c.kind == CornerKind::None) return {};
    if (c.kind == CornerKind::Blind) {
        const std::string who = "Blind corner cabinet '" + id + "'";
        if (!hasValue(g, corner_param::kBlindWidth))
            return invalid(g, who + " needs the parameter '" + std::string(corner_param::kBlindWidth) +
                                  "' (front width of the blind part, mm); add it to the parameters.");
        const auto& front = g.def.interior.front;
        if (!front || front->kind != FrontKind::Door)
            return invalid(g, who + " needs a single door (front kind Door) on the root section; the blind panel "
                                  "takes the rest of the front.");
        const bool blindLeft = c.blindSide == CornerSide::Left;
        if ((front->hingeSide == HingeSide::Left) == blindLeft)
            return invalid(g, who + " has its blind panel on the " + toString(c.blindSide) +
                                  " but the door is hinged on the same side, where no carcass side carries the hinges; "
                                  "hinge the door on the " + (blindLeft ? "Right." : "Left."));
        return {};
    }
    // LShape.
    const std::string who = "L-shaped corner cabinet '" + id + "'";
    for (const char* name : {corner_param::kWidthB, corner_param::kDepthB})
        if (!hasValue(g, name))
            return invalid(g, who + " needs the parameter '" + std::string(name) + "' (mm); add it to the parameters.");
    const ConstructionOptions& o = g.def.construction;
    if (o.topJoint != JointKind::BetweenSides || o.bottomJoint != JointKind::BetweenSides)
        return invalid(g, who + " supports only BetweenSides top and bottom joints (the L-shaped panels sit between "
                                "the two end sides).");
    if (o.top != TopKind::Panel)
        return invalid(g, who + " needs a full top panel (TopKind Panel); stretcher rails are not supported.");
    if (o.back != BackMount::Overlay && o.back != BackMount::None)
        return invalid(g, who + " supports only an Overlay back (two panels on the wall sides) or no back; '" +
                              toString(o.back) + "' is not supported.");
    if (o.plinth == PlinthKind::Feet)
        return invalid(g, who + " supports no plinth or SidesToFloor (two plinth panels); feet are not supported.");
    const Section& root = g.def.interior;
    if (root.split != SplitDirection::None || !root.children.empty())
        return invalid(g, who + " cannot take dividers; its root section must be a leaf (shelf count only).");
    if (root.shelfCount < 0)
        return invalid(g, who + " has shelf count " + std::to_string(root.shelfCount) + "; it must be 0 or more.");
    if (root.front && (root.front->kind != FrontKind::DoorPair || root.front->overlay != OverlayKind::Full))
        return invalid(g, who + " takes either no front or a Full-overlay DoorPair (one door per leg); front kind '" +
                              toString(root.front->kind) + "' with overlay '" + toString(root.front->overlay) +
                              "' is not supported.");
    return {};
}

core::Result<std::pair<double, double>> buildBlindPanel(Gen& g, double x0, double x1, double z0, double z1,
                                                        double yInner) {
    namespace p = param;
    const double W = g.v(p::kWidth), bw = g.v(corner_param::kBlindWidth), gap = g.v(p::kFrontGap);
    const double ft = g.v(p::kFrontThickness);
    const bool left = g.def.corner.blindSide == CornerSide::Left;
    // The split line (centre of the gap) lies blind_width from the blind side's outer face.
    const double split = left ? bw : W - bw;
    const double px0 = left ? x0 : split + gap / 2, px1 = left ? split - gap / 2 : x1;
    const double dx0 = left ? split + gap / 2 : x0, dx1 = left ? x1 : split - gap / 2;
    const std::string who = "Blind corner cabinet '" + g.def.id.str() + "' (width " + num(W) + " mm, blind_width " +
                            num(bw) + " mm, front gap " + num(gap) + " mm)";
    if (px1 - px0 < kMinBlindPanelWidth - core::kLengthEps)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               who + ": the blind panel would be " + num(px1 - px0) + " mm wide; it must be at least " +
                                   num(kMinBlindPanelWidth) + " mm. Increase blind_width.",
                               g.def.id.str());
    const double door = dx1 - dx0;
    if (door < kMinCornerDoorWidth - core::kLengthEps || door > kMaxCornerDoorWidth + core::kLengthEps)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               who + ": the door would be " + num(door) + " mm wide; a blind corner door must be " +
                                   num(kMinCornerDoorWidth) + " to " + num(kMaxCornerDoorWidth) +
                                   " mm wide. Change width or blind_width.",
                               g.def.id.str());
    // Same frame as a door: length -> +Z, width -> +X, thickness -> +Y (face A inner, B visible).
    const Edges all{EdgeUse::Front, EdgeUse::Front, EdgeUse::Front, EdgeUse::Front};
    g.addPart("blind", "Blind panel", domain::PartRole::Generic, MaterialRole::Front, z1 - z0, px1 - px0, ft,
              Transform::fromAxes(kUp, kRight, {px0, yInner - ft, z0}), all);
    return std::pair{dx0, dx1};
}

} // namespace fos::furniture::detail
