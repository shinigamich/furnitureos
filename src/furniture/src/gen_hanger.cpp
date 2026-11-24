// SPDX-License-Identifier: AGPL-3.0-only
/// Wall mounting: concealed cabinet hangers on both sides and the wall rail (README "Wall mounting").
#include "gen_context.hpp"

namespace fos::furniture::detail {

core::Result<void> buildHangers(Gen& g) {
    namespace p = param;
    const HardwareSelection& sel = g.def.hardware;
    if (sel.hanger.empty()) return {};
    const std::string cab = "cabinet '" + g.def.id.str() + "'";
    const auto* hanger = g.libs.findHardware(sel.hanger);
    if (!hanger)
        return core::makeError(core::ErrorCode::NotFound,
                               "The cabinet hanger '" + sel.hanger.str() + "' selected for " + cab +
                                   " is not in the hardware library.",
                               g.def.id.str());
    if (!g.libs.findHardware(sel.wallRail))
        return core::makeError(core::ErrorCode::NotFound,
                               "The wall rail '" + sel.wallRail.str() + "' selected for " + cab +
                                   " is not in the hardware library.",
                               g.def.id.str());
    auto space = hardware::hangerBackSpace(*hanger);
    if (!space) return space.error();
    const double need = space.value();
    const std::string what = "cabinet hanger '" + (hanger->name.empty() ? hanger->id.str() : hanger->name) + "'";

    // BACK RULE: the hanger body and the wall rail live in the space between the rear face of
    // the back panel and the wall (= the rear edges of the sides), which must be at least
    // back_space_min deep. A grooved back with groove_distance >= back_space_min (or no back)
    // provides it; the back panel then lies completely in front of the hangers, so it needs
    // no notch. An Overlay or Inset back closes that space and is refused.
    const auto refuse = [&](const std::string& why) {
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               "Cabinet '" + g.def.id.str() + "' cannot be hung on " + what + ": the hanger needs " +
                                   num(need) + " mm of free space between the back panel and the wall, but " + why,
                               g.def.id.str());
    };
    switch (g.def.construction.back) {
    case BackMount::None: break;
    case BackMount::Overlay:
        return refuse("an Overlay back is nailed onto the rear edges of the carcass. Use a Grooved back with "
                      "groove_distance of at least " + num(need) + " mm.");
    case BackMount::Inset:
        return refuse("an Inset back is flush with the rear edges of the sides. Use a Grooved back with "
                      "groove_distance of at least " + num(need) + " mm.");
    case BackMount::Grooved:
        if (g.v(p::kGrooveDistance) < need - core::kLengthEps)
            return refuse("groove_distance is " + num(g.v(p::kGrooveDistance)) + " mm. Set groove_distance to at least " +
                          num(need) + " mm.");
        break;
    }

    const std::size_t left = g.index("side.left"), right = g.index("side.right");
    std::size_t above = g.index("top");
    if (above >= g.assembly.parts.size()) above = g.index("rail.back");
    if (left >= g.assembly.parts.size() || right >= g.assembly.parts.size() || above >= g.assembly.parts.size())
        return core::makeError(core::ErrorCode::Unsupported,
                               "Cabinet hangers need the two carcass sides and a top panel or rear rail; " + cab +
                                   " has no such carcass.",
                               g.def.id.str());
    for (const auto& [side, slot] : {std::pair{left, "hanger.left"}, std::pair{right, "hanger.right"}}) {
        Job j;
        j.kind = JobKind::Hanger;
        j.instance = slot;
        j.a = side;
        j.b = above;
        g.jobs.push_back(std::move(j));
    }
    Job rail;
    rail.kind = JobKind::WallRail;
    rail.instance = "wall_rail";
    rail.rail.x0 = 0;
    rail.rail.x1 = g.v(p::kWidth);
    rail.rail.wallY = g.v(p::kDepth);
    rail.rail.referenceZ = g.v(p::kInteriorTop);
    rail.rail.parts = {g.assembly.parts[left].id, g.assembly.parts[right].id};
    g.jobs.push_back(std::move(rail));
    return {};
}

} // namespace fos::furniture::detail
