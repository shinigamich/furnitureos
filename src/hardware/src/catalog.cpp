// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/catalog.hpp"

#include <cmath>

namespace fos::hardware {
namespace {
using domain::HardwareDefinition;
using domain::HardwareKind;

HardwareDefinition generic(const char* id, HardwareKind kind, const char* name, std::map<std::string, double> params) {
    HardwareDefinition d;
    d.id = domain::HardwareId(id);
    d.kind = kind;
    d.name = name;
    d.dimensionsVerified = false; // generic data is never "verified"
    d.params = std::move(params);
    d.custom["origin"] = "FurnitureOS generic catalogue; typical dimensions, not from a manufacturer data sheet";
    return d;
}
} // namespace

std::vector<HardwareDefinition> builtinCatalog() {
    using namespace keys;
    return {
        generic(ids::kHingeCup35, HardwareKind::Hinge, "Cup hinge 35 mm with mounting plate (generic)",
                {{kCupDiameter, 35.0},
                 {kCupDepth, 13.0},
                 {kCupCenterFromDoorEdge, 21.5},
                 {kPlateLineFromFrontEdge, 37.0},
                 {kPlateHoleSpacing, 32.0},
                 {kPlateHoleDiameter, 5.0},
                 {kPlateHoleDepth, 13.0}}),
        generic(ids::kConfirmat7x50, HardwareKind::Confirmat, "Confirmat screw 7x50 (generic)",
                {{kLength, 50.0}, {kThroughDiameter, 7.0}, {kPilotDiameter, 5.0}, {kPilotExtraDepth, 5.0}}),
        generic(ids::kDowel8x30, HardwareKind::Dowel, "Wooden dowel 8x30 (generic)",
                {{kDiameter, 8.0}, {kLength, 30.0}, {kFaceDepth, 12.0}, {kEdgeDepth, 20.0}}),
        generic(ids::kShelfPin5, HardwareKind::ShelfSupport, "Shelf support pin 5 mm (generic)",
                {{kHoleDiameter, 5.0}, {kHoleDepth, 13.0}}),
        generic(ids::kCam15, HardwareKind::CamConnector, "Cam connector 15 mm with 8 mm bolt hole (generic)",
                {{kCamDiameter, 15.0},
                 {kCamDepth, 12.5},
                 {kCamCenterFromEdge, 34.0},
                 {kBoltHoleDiameter, 8.0},
                 {kBoltHoleDepth, 34.0},
                 {kBoltAnchorDiameter, 5.0},
                 {kBoltAnchorDepth, 11.5}}),
        generic(ids::kHandleBar128, HardwareKind::Handle, "Bar handle, 128 mm hole spacing (generic)",
                {{kHoleSpacing, 128.0}, {kHoleDiameter, 5.0}}),
        generic(ids::kFootAdjustable, HardwareKind::Foot, "Adjustable cabinet foot 100 mm (generic)",
                {{kHeightNominal, 100.0}, {kHeightMin, 95.0}, {kHeightMax, 120.0}}),
    };
}

core::Result<HardwareDefinition> builtinDefinition(const std::string& id) {
    for (auto& d : builtinCatalog())
        if (d.id.str() == id) return std::move(d);
    return core::makeError(core::ErrorCode::NotFound,
                           "The generic hardware catalogue has no definition with id '" + id + "'.", id);
}

std::vector<std::string> requiredParams(HardwareKind kind) {
    using namespace keys;
    switch (kind) {
    case HardwareKind::Hinge:
        return {kCupDiameter,      kCupDepth,          kCupCenterFromDoorEdge, kPlateLineFromFrontEdge,
                kPlateHoleSpacing, kPlateHoleDiameter, kPlateHoleDepth};
    case HardwareKind::Confirmat: return {kLength, kThroughDiameter, kPilotDiameter, kPilotExtraDepth};
    case HardwareKind::Dowel: return {kDiameter, kLength, kFaceDepth, kEdgeDepth};
    case HardwareKind::ShelfSupport: return {kHoleDiameter, kHoleDepth};
    case HardwareKind::Handle: return {kHoleSpacing, kHoleDiameter};
    default: return {};
    }
}

core::Result<double> requireParam(const HardwareDefinition& def, const std::string& key, bool allowZero) {
    const auto it = def.params.find(key);
    if (it == def.params.end())
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Hardware definition '" + def.id.str() + "' (" + def.name +
                                   ") has no parameter '" + key + "'.",
                               def.id.str());
    const double v = it->second;
    if (!std::isfinite(v) || v < 0 || (v == 0 && !allowZero))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Parameter '" + key + "' of hardware definition '" + def.id.str() + "' is " +
                                   std::to_string(v) + " mm but must be " + (allowZero ? "zero or positive." : "positive."),
                               def.id.str());
    return v;
}

} // namespace fos::hardware
