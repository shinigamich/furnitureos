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
/// Linear wall rail, sold per metre (custom "unit" = "m"); no placement drilling.
HardwareDefinition wallRail() {
    using namespace keys;
    HardwareDefinition d = generic(ids::kWallRailGeneric, HardwareKind::Other,
                                   "Steel wall rail for cabinet hangers, per metre (generic)",
                                   {{kRailHeight, 60.0}, {kRailThickness, 12.0}});
    d.custom["unit"] = "m";
    return d;
}
/// Linear wardrobe (clothes) rail, sold per metre (custom "unit" = "m"); no drilling of its own.
HardwareDefinition clothesRail() {
    using namespace keys;
    HardwareDefinition d = generic(ids::kClothesRailGeneric, HardwareKind::Other,
                                   "Oval wardrobe rail 30x15, per metre (generic)",
                                   {{kRailHeight, 30.0},
                                    {kRailThickness, 15.0},
                                    {kHookClearance, 35.0},
                                    {kSectionDepthMin, 500.0},
                                    {kHangingSpaceMin, 800.0}});
    d.custom["unit"] = "m";
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
        generic(ids::kSlidingGeneric, HardwareKind::SlidingSystem,
                "Sliding door system, two-lane top and bottom track (generic)",
                {{kTopTrackHeight, 40.0},
                 {kBottomTrackHeight, 10.0},
                 {kTrackDepth, 80.0},
                 {kTrackSetback, 10.0},
                 {kPlaneFrontOffset, 10.0},
                 {kPlanePitch, 35.0},
                 {kDoorOverlap, 30.0},
                 {kDoorHeightDeduction, 40.0},
                 {kDoorBottomClearance, 15.0},
                 {kDoorWidthMin, 400.0},
                 {kDoorWidthMax, 1200.0},
                 {kRollerHoleFromSideEdge, 40.0},
                 {kRollerHoleFromBottomEdge, 30.0},
                 {kGuideHoleFromTopEdge, 30.0},
                 {kRollerHoleDiameter, 4.0},
                 {kRollerHoleDepth, 10.0},
                 {kTrackScrewEndDistance, 50.0},
                 {kTrackScrewPitch, 300.0},
                 {kTrackScrewDiameter, 4.0},
                 {kTrackScrewDepth, 10.0},
                 {kInteriorClearance, 5.0},
                 {kInteriorDepthMin, 250.0}}),
        generic(ids::kHangerGeneric, HardwareKind::Suspension, "Concealed cabinet hanger for a wall rail (generic)",
                {{kBodyHeight, 60.0},
                 {kBodyWidth, 45.0},
                 {kBodyDepth, 16.0},
                 {kBackSpaceMin, 18.0},
                 {kScrewFromRearEdge, 8.0},
                 {kScrewBelowTop, 15.0},
                 {kScrewSpacing, 32.0},
                 {kScrewDiameter, 3.0},
                 {kScrewDepth, 10.0},
                 {kRailTopBelowTop, 20.0}}),
        wallRail(),
        clothesRail(),
        generic(ids::kRailSupportGeneric, HardwareKind::Other,
                "End support for oval wardrobe rail, screw-fixed (generic)",
                {{kBodyHeight, 50.0}, {kBodyDepth, 30.0}, {kScrewSpacing, 32.0}, {kScrewDiameter, 3.0}, {kScrewDepth, 10.0}}),
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
    case HardwareKind::SlidingSystem:
        return {kTopTrackHeight, kBottomTrackHeight, kTrackDepth, kTrackSetback, kPlaneFrontOffset, kPlanePitch,
                kDoorOverlap, kDoorHeightDeduction, kDoorBottomClearance, kDoorWidthMin, kDoorWidthMax,
                kRollerHoleFromSideEdge, kRollerHoleFromBottomEdge, kGuideHoleFromTopEdge, kRollerHoleDiameter,
                kRollerHoleDepth, kTrackScrewEndDistance, kTrackScrewPitch, kTrackScrewDiameter, kTrackScrewDepth,
                kInteriorClearance, kInteriorDepthMin};
    case HardwareKind::Suspension:
        return {kBodyHeight,   kBodyWidth,     kBodyDepth,  kBackSpaceMin, kScrewFromRearEdge,
                kScrewBelowTop, kScrewSpacing, kScrewDiameter, kScrewDepth, kRailTopBelowTop};
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
