// SPDX-License-Identifier: AGPL-3.0-only
/// Runs the deferred hardware placements through the rules of fos::hardware.
#include "fos/hardware/corner_joint.hpp"
#include "fos/hardware/hinges.hpp"
#include "fos/hardware/lift.hpp"
#include "fos/hardware/shelf_supports.hpp"
#include "fos/hardware/sliding_doors.hpp"
#include "fos/machining/validation.hpp"
#include "gen_context.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <variant>

namespace fos::furniture::detail {
namespace {

/// Shared-panel rule (gen_context.hpp): `key` (a blind face-hole depth) of `def` limited to
/// sharedHoleDepthLimit(panelThickness).
domain::HardwareDefinition limitDepth(domain::HardwareDefinition def, const char* key, double panelThickness) {
    const auto it = def.params.find(key);
    if (it != def.params.end()) it->second = std::min(it->second, sharedHoleDepthLimit(panelThickness));
    return def;
}

/// Intermediate hinge heights (from the door bottom) moved out of the zones occupied by
/// shelves that rest against the carrying side panel `carrier`. The shelves are taken from
/// the generator's own record (Gen::shelfCarriers: every adjustable shelf with the two panels
/// it is pinned to / lies between, every fixed shelf with the two panels it is joined to), so
/// a shelf counts whatever its clearance to the panel is. The top and bottom hinges keep their
/// rule positions. Each forbidden zone is [shelf bottom - clearance, shelf top + clearance]
/// relative to the door bottom; a hinge inside one is moved to the nearer zone boundary
/// that is free and keeps the hinges ascending and at least `clearance` from the end hinges
/// (on a tie the lower boundary is taken).
/// Returns an empty vector when no hinge had to move (the rule positions are used as is).
std::vector<double> hingesClearOfShelves(const std::vector<double>& rule, const domain::Part& door, std::size_t carrier,
                                         const Gen& g, double clearance, bool& unresolved) {
    unresolved = false;
    if (rule.size() < 3 || clearance <= 0) return {};
    const geometry::Aabb d = door.bounds();
    std::vector<std::pair<double, double>> zones;
    for (const auto& [shelf, panel] : g.shelfCarriers) {
        if (panel != carrier) continue;
        const geometry::Aabb b = g.assembly.parts[shelf].bounds();
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
        // Shared-panel rule: a joint into the face of a divider is a blind dowel joint (a
        // confirmat needs a through hole, which would meet a joint drilled at the same spot
        // from the divider's other face). The face hole is limited to the shared-panel depth
        // and the edge hole is deepened so that the two holes still take the whole dowel.
        const bool dowel = j.sharedPanel || j.fastener == JointFastener::Dowel;
        auto def = lookup(dowel ? sel.dowel : sel.confirmat, "joint fastener");
        if (!def) return def.error();
        auto joint = hardware::detectButtJoint(parts[j.a], parts[j.b]);
        if (!joint) return joint.error();
        hardware::CornerJointConfig c;
        c.instanceId = id;
        // End offsets: joint_end_offset, at most a quarter of the fastener span (the contact,
        // or its part inside the zone of a drawer-box joint).
        const machining::Range along = joint.value().along;
        const double span = j.zone ? std::min(along.max, j.zone->max) - std::max(along.min, j.zone->min) : along.length();
        c.minEndOffset = std::min(g.v(p::kJointEndOffset), std::max(span, 0.0) / 4);
        c.zone = j.zone;
        c.maxSpacing = g.v(p::kJointMaxSpacing);
        if (j.sharedPanel) {
            const auto& prm = def.value().params;
            const auto face = prm.find(hardware::keys::kFaceDepth), edge = prm.find(hardware::keys::kEdgeDepth),
                       length = prm.find(hardware::keys::kLength);
            if (face != prm.end()) {
                const double faceDepth = std::min(face->second, sharedHoleDepthLimit(parts[j.a].thickness));
                c.dowelFaceDepth = faceDepth;
                if (edge != prm.end() && length != prm.end())
                    c.dowelEdgeDepth = std::max(edge->second, length->second - faceDepth);
            }
        }
        return hardware::connectCorner(parts[j.a], parts[j.b], def.value(), c);
    }
    case JobKind::ShelfPins: {
        auto def = lookup(sel.shelfPin, "shelf support");
        if (!def) return def.error();
        hardware::ShelfSupportConfig c;
        c.instanceId = id;
        c.setbackFront = g.v(p::kShelfPinFront);
        c.setbackBack = g.v(p::kShelfPinBack);
        c.maxGap = g.v(p::kShelfClearance) + kShelfPinGapSlack;
        const domain::HardwareDefinition& pin = def.value();
        if (!j.sharedPanel && !j.sharedC) return hardware::placeShelfSupports(parts[j.a], parts[j.b], parts[j.c], pin, c);
        // A divider support takes holes of at most the shared-panel depth; an outer side keeps
        // the catalogue depth. The rule takes one definition for both supports, so a mixed pair
        // is placed twice and the outer side's operations are taken from the unlimited run
        // (both runs number the operations identically: first side op.1-2, second op.3-4).
        const auto limited = limitDepth(pin, hardware::keys::kHoleDepth, g.v(p::kThickness));
        auto placed = hardware::placeShelfSupports(parts[j.a], parts[j.b], parts[j.c], limited, c);
        if (!placed || (j.sharedPanel && j.sharedC)) return placed;
        auto full = hardware::placeShelfSupports(parts[j.a], parts[j.b], parts[j.c], pin, c);
        if (!full) return full.error();
        const domain::PartId& outer = parts[j.sharedPanel ? j.c : j.b].id;
        const auto* outerOps = full.value().operationsFor(outer);
        for (auto& po : placed.value().operations)
            if (po.part == outer && outerOps) po.operations = *outerOps;
        return placed;
    }
    case JobKind::Hinges: {
        auto def = lookup(j.hinge.empty() ? sel.hinge : j.hinge, "hinge");
        if (!def) return def.error();
        hardware::HingeConfig c;
        c.instanceId = id;
        c.offsetFromTop = c.offsetFromBottom = g.v(p::kHingeOffset);
        if (j.flap) {
            // Top-hinged flap: axis X, positions from the flap's left end, count from the
            // table applied to the flap width (heuristic), no shelf avoidance (panel above).
            c.axis = {1, 0, 0};
            c.maxFrontGap = std::max(c.maxFrontGap, g.v(p::kFrontOffset));
            const auto hinge = j.sharedPanel
                                   ? limitDepth(def.value(), hardware::keys::kPlateHoleDepth, g.v(p::kThickness))
                                   : def.value();
            return hardware::placeHinges(parts[j.a], parts[j.b], hinge, c);
        }
        c.maxFrontGap = std::max(c.maxFrontGap, g.v(p::kFrontOffset));
        const auto hinge =
            j.sharedPanel ? limitDepth(def.value(), hardware::keys::kPlateHoleDepth, g.v(p::kThickness)) : def.value();
        // Keep intermediate hinges clear of shelves on the carrying panel.
        const domain::Part& door = parts[j.a];
        const double doorHeight = door.bounds().size().z;
        if (auto n = hardware::hingeCount(doorHeight, c.weightClass, c.rules); n) {
            if (auto rule = hardware::hingePositions(doorHeight, n.value(), c.offsetFromTop, c.offsetFromBottom); rule) {
                bool unresolved = false;
                c.positions =
                    hingesClearOfShelves(rule.value(), door, j.b, g, g.v(p::kHingeShelfClearance), unresolved);
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
    case JobKind::Slide: {
        auto def = lookup(j.slide, "drawer slide");
        if (!def) return def.error();
        hardware::DrawerSlideConfig c;
        c.instanceId = id;
        c.slideLength = j.slideLength;
        if (j.sharedPanel) c.maxCabinetHoleDepth = sharedHoleDepthLimit(parts[j.b].thickness);
        return hardware::placeDrawerSlides(parts[j.a], parts[j.b], def.value(), c);
    }
    case JobKind::Lift: {
        auto def = lookup(sel.lift, "lift mechanism");
        if (!def) return def.error();
        hardware::LiftConfig c;
        c.instanceId = id;
        if (j.sharedPanel) c.maxSideHoleDepth = sharedHoleDepthLimit(parts[j.b].thickness);
        return hardware::placeLiftMechanism(parts[j.a], parts[j.b], parts[j.c], def.value(), c);
    }
    case JobKind::SlidingTrack: {
        auto def = lookup(sel.slidingSystem, "sliding door system");
        if (!def) return def.error();
        hardware::SlidingTrackConfig c = j.track;
        c.instanceId = id;
        if (j.sharedPanel) c.maxHoleDepth = sharedHoleDepthLimit(parts[j.a].thickness);
        return hardware::placeSlidingTrack(parts[j.a], def.value(), c);
    }
    case JobKind::SlidingRollers: {
        auto def = lookup(sel.slidingSystem, "sliding door system");
        if (!def) return def.error();
        hardware::SlidingRollerConfig c;
        c.instanceId = id;
        return hardware::placeSlidingDoorRollers(parts[j.a], def.value(), c);
    }
    case JobKind::Hanger: {
        auto def = lookup(sel.hanger, "cabinet hanger");
        if (!def) return def.error();
        hardware::CabinetHangerConfig c;
        c.instanceId = id;
        return hardware::placeCabinetHanger(parts[j.a], parts[j.b], def.value(), c);
    }
    case JobKind::WallRail: {
        auto hanger = lookup(sel.hanger, "cabinet hanger");
        if (!hanger) return hanger.error();
        auto rail = lookup(sel.wallRail, "wall rail");
        if (!rail) return rail.error();
        hardware::WallRailConfig c = j.rail;
        c.instanceId = id;
        return hardware::placeWallRail(rail.value(), hanger.value(), c);
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

/// Index of the part with id `id` (an id "<cabinet>/<slot>" made by addPart), or
/// parts.size(). O(1) through Gen::slotIndex.
std::size_t indexOfId(const Gen& g, const domain::PartId& id) {
    const std::string prefix = g.partId("");
    const std::string& s = id.str();
    if (s.size() <= prefix.size() || s.compare(0, prefix.size(), prefix) != 0) return g.assembly.parts.size();
    const std::size_t i = g.index(s.substr(prefix.size()));
    return i < g.assembly.parts.size() && g.assembly.parts[i].id == id ? i : g.assembly.parts.size();
}

/// SHELF-PIN COLLISION RULE. Shelf pins are placed after the joints of the panels they are
/// drilled into (carcass and divider joints are scheduled first, see buildInterior), and
/// joints take precedence: when a pin hole of `placement` would intersect a hole already
/// drilled into the same part (machining::holesCollide, e.g. the confirmat or dowel of a
/// partition end in a low cabinet), the description of the first such pair is returned and
/// the whole pin set of that shelf is dropped (furniture.shelf_pins.dropped). Empty = clear.
/// Also used for drawer-box joints and slides (Job::refuseCollisions), which fail instead
/// (furniture.drawer_box.hole_collision).
std::string pinCollision(const Gen& g, const hardware::Placement& placement) {
    for (const auto& po : placement.operations) {
        const std::size_t i = indexOfId(g, po.part);
        if (i >= g.assembly.parts.size()) continue; // reported by storePlacement
        const domain::Part& part = g.assembly.parts[i];
        for (const auto& added : po.operations) {
            const auto* h = std::get_if<domain::Hole>(&added.shape);
            if (!h) continue;
            for (const auto& existing : part.operations) {
                const auto* e = std::get_if<domain::Hole>(&existing.shape);
                if (e && machining::holesCollide(part, *e, *h))
                    return "its hole on face " + std::string(domain::toString(h->face)) + " at (" + num(h->u) + ", " +
                           num(h->v) + ") of '" + part.name + "' (" + part.id.str() + ") would intersect '" +
                           existing.id.str() + "' on face " + std::string(domain::toString(e->face)) + " at (" +
                           num(e->u) + ", " + num(e->v) + ")";
            }
        }
    }
    return {};
}

/// Same result as hardware::applyPlacement for a placement whose instance ids are new to the
/// assembly (always the case for the generator: one job per instance id), but O(size of the
/// placement) instead of a scan of every operation and instance of the assembly. Falls back
/// to applyPlacement when an instance id was stored before.
core::Result<void> storePlacement(Gen& g, std::set<std::string>& stored, const hardware::Placement& placement) {
    std::vector<std::size_t> targets;
    for (const auto& po : placement.operations) {
        const std::size_t i = indexOfId(g, po.part);
        if (i >= g.assembly.parts.size()) return hardware::applyPlacement(g.assembly, placement); // NotFound
        targets.push_back(i);
    }
    for (const auto& inst : placement.instances)
        if (stored.count(inst.id.str())) return hardware::applyPlacement(g.assembly, placement);
    for (const auto& inst : placement.instances) {
        stored.insert(inst.id.str());
        g.assembly.hardware.push_back(inst);
    }
    for (std::size_t k = 0; k < placement.operations.size(); ++k) {
        auto& ops = g.assembly.parts[targets[k]].operations;
        ops.insert(ops.end(), placement.operations[k].operations.begin(), placement.operations[k].operations.end());
    }
    return {};
}

} // namespace

void runHardware(Gen& g) {
    std::set<std::string> stored;
    for (const auto& h : g.assembly.hardware) stored.insert(h.id.str());
    for (const Job& j : g.jobs) {
        const domain::HardwareInstanceId id(g.partId(j.instance));
        auto placement = place(g, j, id);
        if (placement && j.refuseCollisions) {
            if (const std::string why = pinCollision(g, placement.value()); !why.empty()) {
                g.error("furniture.drawer_box.hole_collision",
                        "Hardware '" + id.str() + "' was not placed: " + why +
                            ". Change the drawer-box joint fastener or the drawer height.",
                        id.str());
                continue;
            }
        }
        if (placement && j.kind == JobKind::ShelfPins) {
            if (const std::string why = pinCollision(g, placement.value()); !why.empty()) {
                const domain::Part& shelf = g.assembly.parts[j.a];
                g.warn("furniture.shelf_pins.dropped",
                       "The shelf pins of '" + shelf.name + "' (" + shelf.id.str() + ") were not drilled: " + why +
                           ". The shelf has no supports; move it (fewer shelves or a taller section) or use another "
                           "joint fastener.",
                       shelf.id.str());
                continue;
            }
        }
        if (placement) {
            if (auto r = storePlacement(g, stored, placement.value()); !r) placement = r.error();
        }
        if (!placement)
            g.error(placement.error().code == core::ErrorCode::NotFound ? "furniture.hardware.missing_definition"
                                                                        : "furniture.hardware.placement_failed",
                    "Hardware '" + id.str() + "' was not placed: " + placement.error().message, id.str());
    }
}

} // namespace fos::furniture::detail
