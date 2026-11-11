// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/compatibility.hpp"

#include "fos/hardware/catalog.hpp"
#include "util.hpp"

namespace fos::hardware {
namespace {
using core::Severity;
using namespace detail;

/// Parameter errors only (no "unverified" info).
core::Diagnostics paramProblems(const domain::HardwareDefinition& def) {
    core::Diagnostics out;
    for (const auto& key : requiredParams(def.kind)) {
        const bool allowZero = key == keys::kPilotExtraDepth;
        const auto r = requireParam(def, key, allowZero);
        if (r.ok()) continue;
        const bool missing = def.params.find(key) == def.params.end();
        out.push_back({Severity::Error, missing ? "hardware.definition.missing_param" : "hardware.definition.invalid_param",
                       r.error().message, def.id.str()});
    }
    return out;
}

core::Diagnostics wrongKind(const domain::HardwareDefinition& def, domain::HardwareKind expected) {
    return {{Severity::Error, "hardware.definition.wrong_kind",
             "Hardware definition '" + def.id.str() + "' (" + def.name + ") is of kind " + domain::toString(def.kind) +
                 " but a definition of kind " + domain::toString(expected) + " is required here.",
             def.id.str()}};
}

void add(core::Diagnostics& out, const char* code, std::string msg, const domain::Part& part) {
    out.push_back({Severity::Error, code, std::move(msg), part.id.str()});
}

std::string defName(const domain::HardwareDefinition& d) { return d.name.empty() ? d.id.str() : d.name; }
} // namespace

core::Diagnostics checkDefinition(const domain::HardwareDefinition& def) {
    core::Diagnostics out = paramProblems(def);
    if (!def.dimensionsVerified)
        out.push_back({Severity::Info, "hardware.definition.unverified",
                       "The dimensions of '" + defName(def) + "' (" + def.id.str() +
                           ") are generic and have not been verified against a manufacturer data sheet; check the "
                           "drilling data of the actual fitting.",
                       def.id.str()});
    return out;
}

core::Diagnostics checkConfirmatFit(const domain::HardwareDefinition& def, const domain::Part& facePart,
                                    const domain::Part& edgePart, domain::Face edgeFace, const FitOptions& o) {
    if (def.kind != domain::HardwareKind::Confirmat) return wrongKind(def, domain::HardwareKind::Confirmat);
    core::Diagnostics out = paramProblems(def);
    if (!out.empty()) return out;
    const double length = def.params.at(keys::kLength);
    const double pilotD = def.params.at(keys::kPilotDiameter);
    const double extra = def.params.at(keys::kPilotExtraDepth);
    const double behind = machining::faceExtents(edgePart, edgeFace).materialDepth;
    if (length <= facePart.thickness + core::kLengthEps) {
        add(out, "hardware.confirmat.too_short",
            defName(def) + " is " + num(length) + " mm long but " + quoted(facePart) + " is " +
                num(facePart.thickness) + " mm thick; the screw would not reach " + quoted(edgePart) + ".",
            facePart);
        return out;
    }
    const double pilotDepth = length - facePart.thickness + extra;
    if (pilotDepth >= behind - core::kLengthEps)
        add(out, "hardware.confirmat.pilot_too_deep",
            defName(def) + " needs a " + num(pilotDepth) + " mm deep pilot hole (" + num(length) + " - " +
                num(facePart.thickness) + " + " + num(extra) + ") but " + quoted(edgePart) + " measures only " +
                num(behind) + " mm behind edge " + domain::toString(edgeFace) + ".",
            edgePart);
    if (pilotD + 2 * o.minEdgeWall > edgePart.thickness + core::kLengthEps)
        add(out, "hardware.confirmat.panel_too_thin",
            defName(def) + " needs a " + num(pilotD) + " mm pilot hole with at least " + num(o.minEdgeWall) +
                " mm of material on each side, but " + quoted(edgePart) + " is only " + num(edgePart.thickness) +
                " mm thick.",
            edgePart);
    return out;
}

core::Diagnostics checkDowelFit(const domain::HardwareDefinition& def, const domain::Part& facePart,
                                const domain::Part& edgePart, domain::Face edgeFace, const FitOptions& o) {
    if (def.kind != domain::HardwareKind::Dowel) return wrongKind(def, domain::HardwareKind::Dowel);
    core::Diagnostics out = paramProblems(def);
    if (!out.empty()) return out;
    const double d = def.params.at(keys::kDiameter), length = def.params.at(keys::kLength);
    const double faceDepth = def.params.at(keys::kFaceDepth), edgeDepth = def.params.at(keys::kEdgeDepth);
    const double behind = machining::faceExtents(edgePart, edgeFace).materialDepth;
    if (faceDepth >= facePart.thickness - o.minRemainingMaterial - core::kLengthEps)
        add(out, "hardware.dowel.face_hole_too_deep",
            "The " + num(faceDepth) + " mm deep dowel hole for " + defName(def) + " leaves " +
                num(facePart.thickness - faceDepth) + " mm of the " + num(facePart.thickness) + " mm thick " +
                quoted(facePart) + "; more than " + num(o.minRemainingMaterial) + " mm must remain.",
            facePart);
    if (edgeDepth >= behind - core::kLengthEps)
        add(out, "hardware.dowel.edge_hole_too_deep",
            "The " + num(edgeDepth) + " mm deep dowel hole for " + defName(def) + " does not fit into " +
                quoted(edgePart) + ", which measures only " + num(behind) + " mm behind edge " +
                domain::toString(edgeFace) + ".",
            edgePart);
    if (faceDepth + edgeDepth < length - core::kLengthEps)
        add(out, "hardware.dowel.holes_too_shallow",
            defName(def) + " is " + num(length) + " mm long but the holes in " + quoted(facePart) + " (" +
                num(faceDepth) + " mm) and " + quoted(edgePart) + " (" + num(edgeDepth) + " mm) give only " +
                num(faceDepth + edgeDepth) + " mm; the joint would not close.",
            edgePart);
    if (d + 2 * o.minEdgeWall > edgePart.thickness + core::kLengthEps)
        add(out, "hardware.dowel.panel_too_thin",
            defName(def) + " needs a " + num(d) + " mm hole with at least " + num(o.minEdgeWall) +
                " mm of material on each side, but " + quoted(edgePart) + " is only " + num(edgePart.thickness) +
                " mm thick.",
            edgePart);
    return out;
}

core::Diagnostics checkHingeFit(const domain::HardwareDefinition& def, const domain::Part& door,
                                const domain::Part& side, const FitOptions& o) {
    if (def.kind != domain::HardwareKind::Hinge) return wrongKind(def, domain::HardwareKind::Hinge);
    core::Diagnostics out = paramProblems(def);
    if (!out.empty()) return out;
    const double cupDepth = def.params.at(keys::kCupDepth), plateDepth = def.params.at(keys::kPlateHoleDepth);
    if (cupDepth >= door.thickness - o.minRemainingMaterial - core::kLengthEps)
        add(out, "hardware.hinge.cup_too_deep",
            "The " + num(cupDepth) + " mm deep hinge cup of " + defName(def) + " would leave " +
                num(door.thickness - cupDepth) + " mm of the " + num(door.thickness) + " mm thick door " +
                quoted(door) + "; more than " + num(o.minRemainingMaterial) + " mm must remain.",
            door);
    if (plateDepth >= side.thickness - o.minRemainingMaterial - core::kLengthEps)
        add(out, "hardware.hinge.plate_hole_too_deep",
            "The " + num(plateDepth) + " mm deep mounting-plate holes of " + defName(def) + " would leave " +
                num(side.thickness - plateDepth) + " mm of the " + num(side.thickness) + " mm thick " + quoted(side) +
                "; more than " + num(o.minRemainingMaterial) + " mm must remain.",
            side);
    return out;
}

core::Diagnostics checkShelfPinFit(const domain::HardwareDefinition& def, const domain::Part& side,
                                   const FitOptions& o) {
    if (def.kind != domain::HardwareKind::ShelfSupport) return wrongKind(def, domain::HardwareKind::ShelfSupport);
    core::Diagnostics out = paramProblems(def);
    if (!out.empty()) return out;
    const double depth = def.params.at(keys::kHoleDepth);
    if (depth >= side.thickness - o.minRemainingMaterial - core::kLengthEps)
        add(out, "hardware.shelf_pin.hole_too_deep",
            "The " + num(depth) + " mm deep hole for " + defName(def) + " would leave " +
                num(side.thickness - depth) + " mm of the " + num(side.thickness) + " mm thick " + quoted(side) +
                "; more than " + num(o.minRemainingMaterial) + " mm must remain.",
            side);
    return out;
}

} // namespace fos::hardware
