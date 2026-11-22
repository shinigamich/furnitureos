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
/// Cup hinge with a cranked arm. Same cup and plate drilling as hw.hinge_cup_35; `crank`
/// documents the arm offset and [overlay_min, overlay_max] the door overlay it supports
/// (generic: about 16 mm nominal overlay at 0 mm crank, minus the crank, plus adjustment).
HardwareDefinition crankHinge(const char* id, const char* name, double crank, double ovMin, double ovMax) {
    using namespace keys;
    return generic(id, HardwareKind::Hinge, name,
                   {{kCupDiameter, 35.0},
                    {kCupDepth, 13.0},
                    {kCupCenterFromDoorEdge, 21.5},
                    {kPlateLineFromFrontEdge, 37.0},
                    {kPlateHoleSpacing, 32.0},
                    {kPlateHoleDiameter, 5.0},
                    {kPlateHoleDepth, 13.0},
                    {kCrank, crank},
                    {kOverlayMin, ovMin},
                    {kOverlayMax, ovMax}});
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
        generic(ids::kSlideBallBearing, HardwareKind::DrawerSlide, "Ball-bearing drawer slide, side-mounted (generic)",
                {{kUndermount, 0.0},
                 {kLengthMin, 250.0},
                 {kLengthMax, 650.0},
                 {kLengthStep, 50.0},
                 {kDepthAllowance, 0.0},
                 {kBoxLengthDeduction, 0.0},
                 {kBottomRecess, 0.0},
                 {kMinBoxHeight, 50.0},
                 {kSideClearance, 12.7},
                 {kHoleLineHeight, 22.5},
                 {kCabinetHoleFirst, 37.0},
                 {kCabinetHolePitch, 128.0},
                 {kDrawerHoleFirst, 32.0},
                 {kDrawerHolePitch, 128.0},
                 {kHoleEndMargin, 20.0},
                 {kCabinetHoleDiameter, 5.0},
                 {kCabinetHoleDepth, 12.0},
                 {kDrawerHoleDiameter, 2.5},
                 {kDrawerHoleDepth, 8.0}}),
        generic(ids::kSlideUndermount, HardwareKind::DrawerSlide, "Concealed undermount runner (generic)",
                {{kUndermount, 1.0},
                 {kLengthMin, 250.0},
                 {kLengthMax, 600.0},
                 {kLengthStep, 50.0},
                 {kDepthAllowance, 3.0},
                 {kBoxLengthDeduction, 10.0},
                 {kBottomRecess, 13.0},
                 {kMinBoxHeight, 70.0},
                 {kInnerWidthDeduction, 42.0},
                 {kMaxSideThickness, 16.0},
                 {kHoleLineHeight, 7.0},
                 {kCabinetHoleFirst, 37.0},
                 {kCabinetHolePitch, 128.0},
                 {kDrawerHoleFirst, 20.0},
                 {kDrawerHolePitch, 224.0},
                 {kHoleEndMargin, 20.0},
                 {kCabinetHoleDiameter, 5.0},
                 {kCabinetHoleDepth, 12.0},
                 {kDrawerHoleDiameter, 2.5},
                 {kDrawerHoleDepth, 10.0}}),
        crankHinge(ids::kHingeCup35Half, "Cup hinge 35 mm, half overlay, 9.5 mm crank (generic)", 9.5, 4.0, 10.0),
        crankHinge(ids::kHingeCup35Inset, "Cup hinge 35 mm, inset, 16 mm crank (generic)", 16.0, -4.0, 0.0),
        generic(ids::kLiftGeneric, HardwareKind::LiftMechanism, "Lift mechanism for top-hinged flaps (generic)",
                {{kSideHoleFromFront, 37.0},
                 {kSideHoleSpacing, 32.0},
                 {kSideHoleBelowTop, 40.0},
                 {kSideHoleDiameter, 5.0},
                 {kSideHoleDepth, 12.0},
                 {kFlapHoleFromSideEdge, 50.0},
                 {kFlapHoleFromTopEdge, 100.0},
                 {kFlapHoleSpacing, 32.0},
                 {kFlapHoleDiameter, 4.0},
                 {kFlapHoleDepth, 10.0},
                 {kCabinetHeightMin, 240.0},
                 {kCabinetHeightMax, 600.0},
                 {kCabinetDepthMin, 250.0},
                 {kPowerFactorMin, 400.0},
                 {kPowerFactorMax, 1800.0}}),
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
    case HardwareKind::DrawerSlide:
        return {kUndermount,       kLengthMin,       kLengthMax,        kLengthStep,          kDepthAllowance,
                kBoxLengthDeduction, kBottomRecess,  kMinBoxHeight,     kHoleLineHeight,      kCabinetHoleFirst,
                kCabinetHolePitch, kDrawerHoleFirst, kDrawerHolePitch,  kHoleEndMargin,       kCabinetHoleDiameter,
                kCabinetHoleDepth, kDrawerHoleDiameter, kDrawerHoleDepth};
    case HardwareKind::LiftMechanism:
        return {kSideHoleFromFront, kSideHoleSpacing,  kSideHoleBelowTop, kSideHoleDiameter, kSideHoleDepth,
                kFlapHoleFromSideEdge, kFlapHoleFromTopEdge, kFlapHoleSpacing, kFlapHoleDiameter, kFlapHoleDepth,
                kCabinetHeightMin, kCabinetHeightMax, kCabinetDepthMin, kPowerFactorMin, kPowerFactorMax};
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
