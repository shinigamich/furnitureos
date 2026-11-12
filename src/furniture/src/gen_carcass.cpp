// SPDX-License-Identifier: AGPL-3.0-only
/// Carcass: sides, bottom, top panel or stretcher rails, plinth panel / feet, corner joints.
#include "gen_context.hpp"

namespace fos::furniture::detail {
namespace {
void corner(Gen& g, std::size_t facePart, std::size_t edgePart, JointFastener fastener, const std::string& faceSlot,
            const std::string& edgeSlot) {
    if (fastener == JointFastener::None) return;
    Job j;
    j.kind = JobKind::Corner;
    j.instance = "hw.joint." + edgeSlot + "." + faceSlot;
    j.a = facePart;
    j.b = edgePart;
    j.fastener = fastener;
    g.jobs.push_back(std::move(j));
}
} // namespace

core::Result<Bounds> buildCarcass(Gen& g) {
    namespace p = param;
    const ConstructionOptions& o = g.def.construction;
    const double T = g.v(p::kThickness), W = g.v(p::kWidth), D = g.v(p::kDepth);
    const bool topOver = o.topJoint == JointKind::OverSides, bottomOver = o.bottomJoint == JointKind::OverSides;
    const auto role = MaterialRole::Carcass;
    using domain::PartRole;

    // Sides: length up, width to the back, thickness towards -X (face B of the left side
    // and face A of the right side are the inner faces). L1 = front edge, W1 = lower end,
    // W2 = upper end; the ends are banded (hidden band) where they are not covered.
    const Edges sideEdges{EdgeUse::Visible, EdgeUse::Raw, bottomOver ? EdgeUse::Raw : EdgeUse::Hidden,
                          topOver ? EdgeUse::Raw : EdgeUse::Hidden};
    const double sideZ = g.v(p::kSideBottom), sideL = g.v(p::kSideHeight);
    const std::size_t left = g.addPart("side.left", "Left side", PartRole::Side, role, sideL, D, T,
                                       Transform::fromAxes(kUp, kBack, {T, 0, sideZ}), sideEdges);
    const std::size_t right = g.addPart("side.right", "Right side", PartRole::Side, role, sideL, D, T,
                                        Transform::fromAxes(kUp, kBack, {W, 0, sideZ}), sideEdges);

    // Horizontal panels: length -> +X, width -> +Y, thickness -> +Z. L1 = front edge.
    Bounds b;
    b.left = left;
    b.right = right;
    const Edges bottomEdges{EdgeUse::Visible, EdgeUse::Raw, bottomOver ? EdgeUse::Hidden : EdgeUse::Raw,
                            bottomOver ? EdgeUse::Hidden : EdgeUse::Raw};
    const std::size_t bottom =
        g.addPart("bottom", "Bottom", PartRole::Bottom, role, g.v(p::kBottomLength), D, T,
                  Transform::translation({g.v(p::kBottomX), 0, g.v(p::kCarcassBottom)}), bottomEdges);
    b.bottom = {bottom};
    for (const auto& [side, slot] : {std::pair{left, "side.left"}, std::pair{right, "side.right"}}) {
        if (bottomOver) corner(g, bottom, side, o.bottomFastener, "bottom", slot);
        else corner(g, side, bottom, o.bottomFastener, slot, "bottom");
    }

    const double topZ = g.v(p::kInteriorTop);
    if (o.top == TopKind::Panel) {
        const Edges topEdges{EdgeUse::Visible, EdgeUse::Raw, topOver ? EdgeUse::Hidden : EdgeUse::Raw,
                             topOver ? EdgeUse::Hidden : EdgeUse::Raw};
        const std::size_t top = g.addPart("top", "Top", PartRole::Top, role, g.v(p::kTopLength), D, T,
                                          Transform::translation({g.v(p::kTopX), 0, topZ}), topEdges);
        b.top = {top};
        for (const auto& [side, slot] : {std::pair{left, "side.left"}, std::pair{right, "side.right"}}) {
            if (topOver) corner(g, top, side, o.topFastener, "top", slot);
            else corner(g, side, top, o.topFastener, slot, "top");
        }
    } else {
        const double rw = g.v(p::kRailWidth), len = g.v(p::kTopLength), x = g.v(p::kTopX);
        const Edges frontEdges{EdgeUse::Visible, EdgeUse::Raw, EdgeUse::Raw, EdgeUse::Raw};
        const Edges rearEdges{EdgeUse::Raw, EdgeUse::Raw, EdgeUse::Raw, EdgeUse::Raw};
        const std::size_t front = g.addPart("rail.front", "Front rail", PartRole::Rail, role, len, rw, T,
                                            Transform::translation({x, 0, topZ}), frontEdges);
        const std::size_t rear = g.addPart("rail.back", "Rear rail", PartRole::Rail, role, len, rw, T,
                                           Transform::translation({x, D - rw, topZ}), rearEdges);
        b.top = {front, rear};
        for (const auto& [rail, railSlot] : {std::pair{front, "rail.front"}, std::pair{rear, "rail.back"}})
            for (const auto& [side, slot] : {std::pair{left, "side.left"}, std::pair{right, "side.right"}})
                corner(g, side, rail, o.topFastener, slot, railSlot);
    }

    if (o.plinth == PlinthKind::SidesToFloor) {
        // Plinth panel between the sides: length -> +X, width -> +Z, thickness -> -Y.
        const double setback = g.v(p::kPlinthSetback);
        const std::size_t plinth =
            g.addPart("plinth", "Plinth", PartRole::Plinth, role, g.v(p::kInnerWidth), g.v(p::kPlinthHeight), T,
                      Transform::fromAxes(kRight, kUp, {T, setback + T, 0}), Edges{});
        corner(g, left, plinth, o.dividerFastener, "side.left", "plinth");
        corner(g, right, plinth, o.dividerFastener, "side.right", "plinth");
    } else if (o.plinth == PlinthKind::Feet) {
        const domain::HardwareDefinition* foot = g.libs.findHardware(g.def.hardware.foot);
        const std::string instance = g.partId("hw.feet");
        if (!foot) {
            g.error("furniture.hardware.missing_definition",
                    "The foot '" + g.def.hardware.foot.str() + "' selected for cabinet '" + g.def.id.str() +
                        "' is not in the hardware library; no feet were added.",
                    instance);
        } else {
            const double ph = g.v(p::kPlinthHeight);
            const auto lo = foot->params.find(hardware::keys::kHeightMin), hi = foot->params.find(hardware::keys::kHeightMax);
            if (lo != foot->params.end() && hi != foot->params.end() && (ph < lo->second || ph > hi->second))
                g.warn("furniture.plinth.feet_height",
                       "Plinth height " + num(ph) + " mm is outside the adjustment range " + num(lo->second) + ".." +
                           num(hi->second) + " mm of foot '" + foot->name + "'.",
                       instance);
            domain::HardwareInstance inst;
            inst.id = domain::HardwareInstanceId(instance);
            inst.definition = foot->id;
            inst.quantity = 4;
            inst.parts = {g.assembly.parts[bottom].id};
            inst.position = {g.v(p::kBottomX), 0, g.v(p::kCarcassBottom)};
            inst.note = "Four feet under the bottom panel (screw-on; no drilling generated)";
            g.assembly.hardware.push_back(std::move(inst));
        }
    }
    return b;
}

} // namespace fos::furniture::detail
