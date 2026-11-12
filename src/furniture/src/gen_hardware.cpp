// SPDX-License-Identifier: AGPL-3.0-only
/// Runs the deferred hardware placements through the rules of fos::hardware.
#include "fos/hardware/corner_joint.hpp"
#include "fos/hardware/hinges.hpp"
#include "fos/hardware/shelf_supports.hpp"
#include "gen_context.hpp"

#include <algorithm>
#include <cmath>

namespace fos::furniture::detail {
namespace {

/// A hole drilled into a panel that may be drilled at the same spot from its other face
/// (interior partition) must stop before the mid-plane: at most (T - 2) / 2 deep.
domain::HardwareDefinition limitDepth(domain::HardwareDefinition def, const char* key, double panelThickness) {
    const auto it = def.params.find(key);
    if (it != def.params.end()) it->second = std::min(it->second, (panelThickness - 2.0) / 2.0);
    return def;
}

/// Intermediate hinge heights (from the door bottom) moved out of the zones occupied by
/// shelves that rest against the carrying side panel. The top and bottom hinges keep their
/// rule positions. Each forbidden zone is [shelf bottom - clearance, shelf top + clearance]
/// relative to the door bottom; a hinge inside one is moved to the nearer zone boundary
/// that is free and keeps the hinges ascending and at least `clearance` from the end hinges.
/// Returns an empty vector when no hinge had to move (the rule positions are used as is).
std::vector<double> hingesClearOfShelves(const std::vector<double>& rule, const domain::Part& door,
                                         const domain::Part& side, const std::vector<domain::Part>& parts,
                                         double clearance, bool& unresolved) {
    unresolved = false;
    if (rule.size() < 3 || clearance <= 0) return {};
    const geometry::Aabb d = door.bounds(), s = side.bounds();
    std::vector<std::pair<double, double>> zones;
    for (const domain::Part& p : parts) {
        if (p.role != domain::PartRole::Shelf) continue;
        const geometry::Aabb b = p.bounds();
        const bool touchesSide = b.min.x <= s.max.x + 0.5 && b.max.x >= s.min.x - 0.5 && b.min.y < s.max.y &&
                                 b.max.y > s.min.y;
        if (!touchesSide) continue;
        zones.emplace_back(b.min.z - d.min.z - clearance, b.max.z - d.min.z + clearance);
    }
    if (zones.empty()) return {};
    const auto blocked = [&](double h) {
        for (const auto& [lo, hi] : zones)
            if (h > lo + core::kLengthEps && h < hi - core::kLengthEps) return true;
        return false;
    };
    std::vector<double> out = rule;
    bool moved = false;
    for (std::size_t i = 1; i + 1 < out.size(); ++i) {
        if (!blocked(out[i])) continue;
        const double lowLimit = out[i - 1] + clearance, highLimit = rule.back() - clearance;
        double best = 0;
        bool found = false;
        for (const auto& [lo, hi] : zones) {
            for (double cand : {lo, hi}) {
                if (cand < lowLimit - core::kLengthEps || cand > highLimit + core::kLengthEps || blocked(cand)) continue;
                if (!found || std::abs(cand - rule[i]) < std::abs(best - rule[i])) best = cand, found = true;
            }
        }
        if (!found) {
            unresolved = true;
            return {};
        }
        out[i] = best;
        moved = true;
    }
    return moved ? out : std::vector<double>{};
}

core::Result<hardware::Placement> place(Gen& g, const Job& j, const domain::HardwareInstanceId& id) {
    namespace p = param;
    const auto& parts = g.assembly.parts;
    const HardwareSelection& sel = g.def.hardware;
    const auto lookup = [&](const domain::HardwareId& hid, const char* what) -> core::Result<domain::HardwareDefinition> {
        if (const auto* d = g.libs.findHardware(hid)) return *d;
        return core::makeError(core::ErrorCode::NotFound,
                               std::string("The ") + what + " '" + hid.str() + "' selected for cabinet '" + g.def.id.str() +
                                   "' is not in the hardware library.");
    };
    switch (j.kind) {
    case JobKind::Corner: {
        auto def = lookup(j.fastener == JointFastener::Confirmat ? sel.confirmat : sel.dowel, "joint fastener");
        if (!def) return def.error();
        auto joint = hardware::detectButtJoint(parts[j.a], parts[j.b]);
        if (!joint) return joint.error();
        hardware::CornerJointConfig c;
        c.instanceId = id;
        c.minEndOffset = std::min(g.v(p::kJointEndOffset), joint.value().along.length() / 4);
        c.maxSpacing = g.v(p::kJointMaxSpacing);
        return hardware::connectCorner(parts[j.a], parts[j.b], def.value(), c);
    }
    case JobKind::ShelfPins: {
        auto def = lookup(sel.shelfPin, "shelf support");
        if (!def) return def.error();
        hardware::ShelfSupportConfig c;
        c.instanceId = id;
        c.setbackFront = g.v(p::kShelfPinFront);
        c.setbackBack = g.v(p::kShelfPinBack);
        c.maxGap = std::max(2.0, g.v(p::kShelfClearance) + 0.5);
        const auto pin = j.sharedPanel ? limitDepth(def.value(), hardware::keys::kHoleDepth, g.v(p::kThickness)) : def.value();
        return hardware::placeShelfSupports(parts[j.a], parts[j.b], parts[j.c], pin, c);
    }
    case JobKind::Hinges: {
        auto def = lookup(sel.hinge, "hinge");
        if (!def) return def.error();
        hardware::HingeConfig c;
        c.instanceId = id;
        c.offsetFromTop = c.offsetFromBottom = g.v(p::kHingeOffset);
        c.maxFrontGap = std::max(c.maxFrontGap, g.v(p::kFrontOffset));
        const auto hinge =
            j.sharedPanel ? limitDepth(def.value(), hardware::keys::kPlateHoleDepth, g.v(p::kThickness)) : def.value();
        // Keep intermediate hinges clear of shelves on the carrying panel.
        const domain::Part& door = parts[j.a];
        const double doorHeight = door.bounds().size().z;
        if (auto n = hardware::hingeCount(doorHeight, c.weightClass, c.rules); n) {
            if (auto rule = hardware::hingePositions(doorHeight, n.value(), c.offsetFromTop, c.offsetFromBottom); rule) {
                bool unresolved = false;
                c.positions = hingesClearOfShelves(rule.value(), door, parts[j.b], parts, g.v(p::kHingeShelfClearance),
                                                   unresolved);
                if (unresolved)
                    g.warn("furniture.hinge.near_shelf",
                           "An intermediate hinge of '" + door.name + "' could not be moved at least " +
                               core::formatLength(g.v(p::kHingeShelfClearance)) +
                               " mm away from a shelf; check the hinge and shelf positions.",
                           door.id.str());
            }
        }
        return hardware::placeHinges(door, parts[j.b], hinge, c);
    }
    case JobKind::Handle: {
        auto def = lookup(sel.handle, "handle");
        if (!def) return def.error();
        hardware::HandleConfig c = j.handle;
        c.instanceId = id;
        return hardware::placeHandle(parts[j.a], def.value(), c);
    }
    }
    return core::makeError(core::ErrorCode::Internal, "Unknown hardware job.");
}

} // namespace

void runHardware(Gen& g) {
    for (const Job& j : g.jobs) {
        const domain::HardwareInstanceId id(g.partId(j.instance));
        auto placement = place(g, j, id);
        if (placement) {
            if (auto stored = hardware::applyPlacement(g.assembly, placement.value()); !stored)
                placement = stored.error();
        }
        if (!placement)
            g.error(placement.error().code == core::ErrorCode::NotFound ? "furniture.hardware.missing_definition"
                                                                        : "furniture.hardware.placement_failed",
                    "Hardware '" + id.str() + "' was not placed: " + placement.error().message, id.str());
    }
}

} // namespace fos::furniture::detail
