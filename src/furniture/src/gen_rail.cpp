// SPDX-License-Identifier: AGPL-3.0-only
/// Hanging (clothes) rails in leaf sections (README "Hanging rails").
#include "gen_context.hpp"

namespace fos::furniture::detail {

core::Result<void> buildHangingRail(Gen& g, const HangingRailSpec& spec, const std::string& path, const Box& box,
                                    const Bounds& bounds) {
    namespace p = param;
    const std::string label = "Section '" + sectionLabel(path) + "' of cabinet '" + g.def.id.str() + "'";
    const auto* rail = g.libs.findHardware(spec.rail);
    if (!rail)
        return core::makeError(core::ErrorCode::NotFound,
                               "The clothes rail '" + spec.rail.str() + "' of the hanging rail in " + label +
                                   " is not in the hardware library.",
                               g.def.id.str());
    const auto* support = g.libs.findHardware(spec.supports);
    if (!support)
        return core::makeError(core::ErrorCode::NotFound,
                               "The rail support '" + spec.supports.str() + "' of the hanging rail in " + label +
                                   " is not in the hardware library.",
                               g.def.id.str());
    // Clear space: between the panels below / above, from the usable front (behind sliding-door
    // tracks) to the front of the back panel.
    hardware::ClothesRailSpace space;
    space.yFront = box.y0;
    space.yBack = g.v(p::kInteriorDepth);
    space.zBottom = box.z0;
    space.zTop = box.z1;
    space.heightFromTop = spec.heightFromTop;
    if (auto axis = hardware::planClothesRail(*rail, *support, space); !axis)
        return core::makeError(axis.error().code, label + " cannot take its hanging rail: " + axis.error().message,
                               g.def.id.str());

    Job j;
    j.kind = JobKind::ClothesRail;
    j.instance = joinSlot({"hw.rail", path});
    j.clothesSupportInstance = joinSlot({"hw.rail_supports", path});
    j.a = bounds.left;
    j.b = bounds.right;
    j.sharedPanel = !bounds.leftOuter;
    j.sharedC = !bounds.rightOuter;
    j.railId = spec.rail;
    j.supportId = spec.supports;
    j.clothesRail.space = space;
    g.jobs.push_back(std::move(j));
    return {};
}

} // namespace fos::furniture::detail
