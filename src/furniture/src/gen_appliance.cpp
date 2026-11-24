// SPDX-License-Identifier: AGPL-3.0-only
/// Appliance housings (README "Appliance housings"): open appliance niches inside a carcass,
/// decor fronts fixed to an appliance door, and the appliance gap (end panels + decor front).
/// All sizes are GENERIC; the appliance's installation sheet is authoritative.
#include "gen_context.hpp"

#include <cmath>

namespace fos::furniture::detail {
namespace {

core::Error invalid(const Gen& g, const std::string& message) {
    return core::makeError(core::ErrorCode::InvalidArgument, message, g.def.id.str());
}
core::Error tooSmall(const Gen& g, const std::string& message) {
    return core::makeError(core::ErrorCode::ConstraintViolation, message, g.def.id.str());
}

bool positive(double v) { return std::isfinite(v) && v > 0; }

} // namespace

core::Result<void> buildApplianceNiche(Gen& g, const FrontSpec& spec, const std::string& path, const Box& box,
                                       const Bounds& b) {
    namespace p = param;
    const ApplianceNicheSpec& n = spec.niche;
    const std::string who = "Appliance niche '" + sectionLabel(path) + "' of cabinet '" + g.def.id.str() + "'";
    if (n.vent == NicheVent::None) return {};
    const std::pair<const char*, double> fields[] = {
        {"ventilation size", n.ventSize}, {"ventilation edge distance", n.ventEdgeDistance},
        {"ventilation end margin", n.ventEndMargin}};
    for (const auto& [name, value] : fields)
        if (!positive(value))
            return invalid(g, who + ": the " + name + " must be greater than 0 mm (is " + num(value) + " mm).");

    // Across the niche: the opening keeps ventEndMargin from the panels on both sides.
    const double xa = box.x0 + n.ventEndMargin, xb = box.x1 - n.ventEndMargin;
    if (xb - xa <= core::kLengthEps)
        return tooSmall(g, who + " is " + num(box.x1 - box.x0) + " mm wide; with a ventilation end margin of " +
                               num(n.ventEndMargin) + " mm on each side no opening remains. Reduce the margin.");
    PendingCutout c;
    c.owner = "appliance niche '" + sectionLabel(path) + "' of cabinet '" + g.def.id.str() + "'";
    if (n.vent == NicheVent::ShelfSlot) {
        // Slot at the rear of the panel the appliance stands on, in front of the back panel.
        const double yRear = g.v(p::kInteriorDepth) - n.ventEdgeDistance, yFront = yRear - n.ventSize;
        if (yFront <= box.y0 + core::kLengthEps)
            return tooSmall(g, who + ": a ventilation slot " + num(n.ventSize) + " mm deep at " +
                                   num(n.ventEdgeDistance) + " mm from the back does not fit into the interior depth of " +
                                   num(g.v(p::kInteriorDepth) - box.y0) + " mm.");
        const std::size_t panel = b.bottom.front();
        c.slot = g.assembly.parts[panel].custom.at("furniture.slot");
        c.lo = {xa, yFront, box.z0 - g.v(p::kThickness)};
        c.hi = {xb, yRear, box.z0};
        g.cutouts.push_back(std::move(c));
        return {};
    }
    // BackCutout: at the top of the niche, in the back panel.
    if (g.def.construction.back == BackMount::None) {
        g.warn("furniture.appliance.no_back",
               who + " asks for a ventilation cut-out in the back panel, but the cabinet has no back panel; the rear "
                     "of the niche is open and no cut-out is made.",
               g.def.id.str());
        return {};
    }
    const double zt = box.z1 - n.ventEdgeDistance, zb = zt - n.ventSize;
    if (zb <= box.z0 + core::kLengthEps)
        return tooSmall(g, who + " is " + num(box.z1 - box.z0) + " mm high; a back cut-out of " + num(n.ventSize) +
                               " mm at " + num(n.ventEdgeDistance) + " mm below the top does not fit.");
    c.slot = "back";
    c.lo = {xa, g.v(p::kInteriorDepth), zb};
    c.hi = {xb, g.v(p::kDepth) + g.v(p::kBackThickness), zt};
    g.cutouts.push_back(std::move(c));
    return {};
}

std::size_t addApplianceFront(Gen& g, const FrontSpec& spec, const std::string& slot, double x0, double x1, double z0,
                              double z1, double yInner) {
    const double ft = g.v(param::kFrontThickness);
    // Like a door: length -> +Z, width -> +X, thickness -> +Y (face A inner, face B visible).
    const std::size_t part =
        g.addPart(slot, "Appliance decor front", domain::PartRole::Door, MaterialRole::Front, z1 - z0, x1 - x0, ft,
                  Transform::fromAxes(kUp, kRight, {x0, yInner - ft, z0}),
                  Edges{EdgeUse::Front, EdgeUse::Front, EdgeUse::Front, EdgeUse::Front});
    g.assembly.parts[part].custom[kMountingKey] = kMountingApplianceDoor;
    handleJob(g, part, slot, spec, false, false); // horizontal handle; never hinges
    return part;
}

core::Result<void> buildApplianceGap(Gen& g) {
    namespace p = param;
    const std::string who = "Appliance gap '" + g.def.id.str() + "'";
    const Section& root = g.def.interior;
    if (g.def.corner.kind != CornerKind::None)
        return invalid(g, who + " cannot also be a corner cabinet; set the corner kind to None.");
    if (root.split != SplitDirection::None || root.shelfCount != 0)
        return invalid(g, who + " has an interior layout (split or shelves); an appliance gap holds only the appliance. "
                               "Remove the child sections and shelves.");
    if (root.front && root.front->kind != FrontKind::ApplianceFront)
        return invalid(g, who + " has a front of kind " + toString(root.front->kind) +
                               "; an appliance gap takes only an ApplianceFront (decor front fixed to the appliance "
                               "door) or no front.");

    if (root.front)
        if (auto r = checkFrontStyle(g, *root.front, who); !r) return r.error();
    const GapPanels panels = g.def.applianceGap->panels;
    const bool left = panels == GapPanels::Left || panels == GapPanels::Both;
    const bool right = panels == GapPanels::Right || panels == GapPanels::Both;
    const double T = g.v(p::kThickness), W = g.v(p::kWidth), H = g.v(p::kHeight), D = g.v(p::kDepth);
    const double ft = g.v(p::kFrontThickness), fo = g.v(p::kFrontOffset);
    const double xL = left ? T : 0.0, xR = right ? W - T : W, gap = xR - xL;
    if (gap <= core::kLengthEps)
        return tooSmall(g, who + " is " + num(W) + " mm wide; its end panels of " + num(T) +
                               " mm leave no gap. Enlarge the width.");
    if (const auto it = g.values.find(appliance_param::kGapWidth);
        it != g.values.end() && !core::approxEqual(it->second, gap))
        g.warn("furniture.appliance.gap_width",
               who + " leaves a gap of " + num(gap) + " mm between its end panels / neighbours, but the parameter '" +
                   std::string(appliance_param::kGapWidth) + "' is " + num(it->second) +
                   " mm. Adjust the width (or the end panels) so that the gap matches.",
               g.def.id.str());

    // End panels: like carcass sides (length up, width to the back, thickness -X), full height
    // from the floor and flush with the visible face of the fronts.
    const double yFront = -(fo + ft);
    const Edges panelEdges{EdgeUse::Visible, EdgeUse::Raw, EdgeUse::Raw, EdgeUse::Hidden};
    if (left)
        g.addPart("end_panel.left", "Left end panel", domain::PartRole::Side, MaterialRole::Carcass, H, D - yFront, T,
                  Transform::fromAxes(kUp, kBack, {T, yFront, 0}), panelEdges);
    if (right)
        g.addPart("end_panel.right", "Right end panel", domain::PartRole::Side, MaterialRole::Carcass, H, D - yFront, T,
                  Transform::fromAxes(kUp, kBack, {W, yFront, 0}), panelEdges);

    if (!root.front) return {};
    const double reveal = g.v(p::kFrontReveal), fgap = g.v(p::kFrontGap);
    const double x0 = xL + (left ? fgap : reveal), x1 = xR - (right ? fgap : reveal);
    const double z0 = g.v(p::kCarcassBottom) + reveal, z1 = H - reveal;
    if (x1 - x0 <= core::kLengthEps || z1 - z0 <= core::kLengthEps)
        return tooSmall(g, who + ": the decor front would measure " + num(x1 - x0) + " x " + num(z1 - z0) +
                               " mm; the gap is too small for the configured front gaps.");
    addApplianceFront(g, *root.front, "appliance_front", x0, x1, z0, z1, -fo);
    return {};
}

} // namespace fos::furniture::detail
