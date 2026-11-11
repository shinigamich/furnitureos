// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/enums.hpp"

namespace fos::formats {
namespace {
using namespace fos::domain;

constexpr EnumToken<MaterialKind> kMaterialKinds[] = {
    {MaterialKind::Particleboard, "particleboard"}, {MaterialKind::Mdf, "mdf"},
    {MaterialKind::Hdf, "hdf"},                     {MaterialKind::Plywood, "plywood"},
    {MaterialKind::SolidWood, "solid_wood"},        {MaterialKind::Other, "other"}};

constexpr EnumToken<EdgeBandKind> kEdgeBandKinds[] = {
    {EdgeBandKind::Abs, "abs"},       {EdgeBandKind::Pvc, "pvc"},
    {EdgeBandKind::Melamine, "melamine"}, {EdgeBandKind::Veneer, "veneer"},
    {EdgeBandKind::SolidWood, "solid_wood"}, {EdgeBandKind::Other, "other"}};

constexpr EnumToken<HardwareKind> kHardwareKinds[] = {
    {HardwareKind::Hinge, "hinge"},
    {HardwareKind::MountingPlate, "mounting_plate"},
    {HardwareKind::DrawerSlide, "drawer_slide"},
    {HardwareKind::Handle, "handle"},
    {HardwareKind::ShelfSupport, "shelf_support"},
    {HardwareKind::Confirmat, "confirmat"},
    {HardwareKind::Dowel, "dowel"},
    {HardwareKind::CamConnector, "cam_connector"},
    {HardwareKind::Screw, "screw"},
    {HardwareKind::Foot, "foot"},
    {HardwareKind::Leg, "leg"},
    {HardwareKind::Suspension, "suspension"},
    {HardwareKind::SlidingSystem, "sliding_system"},
    {HardwareKind::LiftMechanism, "lift_mechanism"},
    {HardwareKind::Other, "other"}};

constexpr EnumToken<PartRole> kPartRoles[] = {
    {PartRole::Side, "side"},
    {PartRole::Top, "top"},
    {PartRole::Bottom, "bottom"},
    {PartRole::Shelf, "shelf"},
    {PartRole::Partition, "partition"},
    {PartRole::Back, "back"},
    {PartRole::Door, "door"},
    {PartRole::DrawerFront, "drawer_front"},
    {PartRole::DrawerSide, "drawer_side"},
    {PartRole::DrawerBack, "drawer_back"},
    {PartRole::DrawerBottom, "drawer_bottom"},
    {PartRole::Plinth, "plinth"},
    {PartRole::Worktop, "worktop"},
    {PartRole::Rail, "rail"},
    {PartRole::Generic, "generic"}};

constexpr EnumToken<EdgeSide> kEdgeSides[] = {
    {EdgeSide::L1, "l1"}, {EdgeSide::L2, "l2"}, {EdgeSide::W1, "w1"}, {EdgeSide::W2, "w2"}};

constexpr EnumToken<GrainDirection> kGrainDirections[] = {{GrainDirection::None, "none"},
                                                          {GrainDirection::AlongLength, "along_length"},
                                                          {GrainDirection::AlongWidth, "along_width"}};

constexpr EnumToken<Face> kFaces[] = {{Face::A, "a"},   {Face::B, "b"},   {Face::L1, "l1"},
                                      {Face::L2, "l2"}, {Face::W1, "w1"}, {Face::W2, "w2"}};
} // namespace

template <> std::span<const EnumToken<MaterialKind>> enumTokens<MaterialKind>() noexcept { return kMaterialKinds; }
template <> std::span<const EnumToken<EdgeBandKind>> enumTokens<EdgeBandKind>() noexcept { return kEdgeBandKinds; }
template <> std::span<const EnumToken<HardwareKind>> enumTokens<HardwareKind>() noexcept { return kHardwareKinds; }
template <> std::span<const EnumToken<PartRole>> enumTokens<PartRole>() noexcept { return kPartRoles; }
template <> std::span<const EnumToken<EdgeSide>> enumTokens<EdgeSide>() noexcept { return kEdgeSides; }
template <> std::span<const EnumToken<GrainDirection>> enumTokens<GrainDirection>() noexcept {
    return kGrainDirections;
}
template <> std::span<const EnumToken<Face>> enumTokens<Face>() noexcept { return kFaces; }

} // namespace fos::formats
