// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/furniture/enums.hpp"

#include <array>
#include <utility>

namespace fos::furniture {
namespace {
template <class E, std::size_t N> using Table = std::array<std::pair<E, const char*>, N>;

template <class E, std::size_t N> const char* nameOf(const Table<E, N>& table, E e) noexcept {
    for (const auto& [value, text] : table)
        if (value == e) return text;
    return "?";
}
template <class E, std::size_t N> std::optional<E> valueOf(const Table<E, N>& table, std::string_view s) noexcept {
    for (const auto& [value, text] : table)
        if (s == text) return value;
    return std::nullopt;
}

constexpr Table<JointKind, 2> kJointKind{{{JointKind::BetweenSides, "BetweenSides"}, {JointKind::OverSides, "OverSides"}}};
constexpr Table<TopKind, 2> kTopKind{{{TopKind::Panel, "Panel"}, {TopKind::Rails, "Rails"}}};
constexpr Table<BackMount, 4> kBackMount{{{BackMount::None, "None"}, {BackMount::Overlay, "Overlay"},
                                         {BackMount::Grooved, "Grooved"}, {BackMount::Inset, "Inset"}}};
constexpr Table<PlinthKind, 3> kPlinthKind{{{PlinthKind::None, "None"}, {PlinthKind::SidesToFloor, "SidesToFloor"},
                                           {PlinthKind::Feet, "Feet"}}};
constexpr Table<JointFastener, 3> kJointFastener{{{JointFastener::None, "None"}, {JointFastener::Confirmat, "Confirmat"},
                                                 {JointFastener::Dowel, "Dowel"}}};
constexpr Table<SplitDirection, 3> kSplitDirection{{{SplitDirection::None, "None"}, {SplitDirection::Vertical, "Vertical"},
                                                   {SplitDirection::Horizontal, "Horizontal"}}};
constexpr Table<SizeKind, 3> kSizeKind{{{SizeKind::Fixed, "Fixed"}, {SizeKind::Weight, "Weight"},
                                       {SizeKind::Expression, "Expression"}}};
constexpr Table<FrontKind, 7> kFrontKind{{{FrontKind::Door, "Door"}, {FrontKind::DoorPair, "DoorPair"},
                                         {FrontKind::DrawerStack, "DrawerStack"}, {FrontKind::Flap, "Flap"},
                                         {FrontKind::SlidingDoors, "SlidingDoors"},
                                         {FrontKind::ApplianceNiche, "ApplianceNiche"},
                                         {FrontKind::ApplianceFront, "ApplianceFront"}}};
constexpr Table<OverlayKind, 3> kOverlayKind{{{OverlayKind::Full, "Full"}, {OverlayKind::Half, "Half"},
                                             {OverlayKind::Inset, "Inset"}}};
constexpr Table<HingeSide, 2> kHingeSide{{{HingeSide::Left, "Left"}, {HingeSide::Right, "Right"}}};
constexpr Table<HandlePosition, 4> kHandlePosition{{{HandlePosition::None, "None"}, {HandlePosition::Center, "Center"},
                                                   {HandlePosition::Top, "Top"}, {HandlePosition::Bottom, "Bottom"}}};
constexpr Table<MaterialRole, 5> kMaterialRole{{{MaterialRole::Carcass, "Carcass"}, {MaterialRole::Back, "Back"},
                                               {MaterialRole::Front, "Front"}, {MaterialRole::Drawer, "Drawer"},
                                               {MaterialRole::DrawerBottom, "DrawerBottom"}}};
constexpr Table<DrawerBottomMount, 2> kDrawerBottomMount{{{DrawerBottomMount::Grooved, "Grooved"},
                                                         {DrawerBottomMount::NailedUnder, "NailedUnder"}}};
constexpr Table<PanelOrientation, 3> kPanelOrientation{{{PanelOrientation::Horizontal, "Horizontal"},
                                                       {PanelOrientation::VerticalSide, "VerticalSide"},
                                                       {PanelOrientation::VerticalFront, "VerticalFront"}}};
constexpr Table<ContourKind, 4> kContourKind{{{ContourKind::Rectangle, "Rectangle"},
                                             {ContourKind::RoundedCorners, "RoundedCorners"},
                                             {ContourKind::ArcEdge, "ArcEdge"},
                                             {ContourKind::Custom, "Custom"}}};
constexpr Table<ArcDirection, 2> kArcDirection{{{ArcDirection::Convex, "Convex"}, {ArcDirection::Concave, "Concave"}}};
constexpr Table<CutoutKind, 2> kCutoutKind{{{CutoutKind::Rectangle, "Rectangle"}, {CutoutKind::Circle, "Circle"}}};
constexpr Table<CornerKind, 3> kCornerKind{{{CornerKind::None, "None"}, {CornerKind::Blind, "Blind"},
                                           {CornerKind::LShape, "LShape"}}};
constexpr Table<CornerSide, 2> kCornerSide{{{CornerSide::Left, "Left"}, {CornerSide::Right, "Right"}}};
constexpr Table<NicheVent, 3> kNicheVent{{{NicheVent::None, "None"}, {NicheVent::ShelfSlot, "ShelfSlot"},
                                         {NicheVent::BackCutout, "BackCutout"}}};
constexpr Table<GapPanels, 4> kGapPanels{{{GapPanels::None, "None"}, {GapPanels::Left, "Left"},
                                         {GapPanels::Right, "Right"}, {GapPanels::Both, "Both"}}};
constexpr Table<FrontStyle, 2> kFrontStyle{{{FrontStyle::Slab, "Slab"}, {FrontStyle::FramePanel, "FramePanel"}}};
constexpr Table<FrameJoint, 2> kFrameJoint{{{FrameJoint::StubTenon, "StubTenon"}, {FrameJoint::Dowel, "Dowel"}}};
} // namespace

#define FOS_FURNITURE_ENUM(E, table)                                                                \
    const char* toString(E e) noexcept { return nameOf(table, e); }                                 \
    template <> std::optional<E> fromString<E>(std::string_view s) noexcept { return valueOf(table, s); }

FOS_FURNITURE_ENUM(JointKind, kJointKind)
FOS_FURNITURE_ENUM(TopKind, kTopKind)
FOS_FURNITURE_ENUM(BackMount, kBackMount)
FOS_FURNITURE_ENUM(PlinthKind, kPlinthKind)
FOS_FURNITURE_ENUM(JointFastener, kJointFastener)
FOS_FURNITURE_ENUM(SplitDirection, kSplitDirection)
FOS_FURNITURE_ENUM(SizeKind, kSizeKind)
FOS_FURNITURE_ENUM(FrontKind, kFrontKind)
FOS_FURNITURE_ENUM(OverlayKind, kOverlayKind)
FOS_FURNITURE_ENUM(HingeSide, kHingeSide)
FOS_FURNITURE_ENUM(HandlePosition, kHandlePosition)
FOS_FURNITURE_ENUM(MaterialRole, kMaterialRole)
FOS_FURNITURE_ENUM(PanelOrientation, kPanelOrientation)
FOS_FURNITURE_ENUM(ContourKind, kContourKind)
FOS_FURNITURE_ENUM(ArcDirection, kArcDirection)
FOS_FURNITURE_ENUM(CutoutKind, kCutoutKind)
FOS_FURNITURE_ENUM(DrawerBottomMount, kDrawerBottomMount)
FOS_FURNITURE_ENUM(CornerKind, kCornerKind)
FOS_FURNITURE_ENUM(CornerSide, kCornerSide)
FOS_FURNITURE_ENUM(NicheVent, kNicheVent)
FOS_FURNITURE_ENUM(GapPanels, kGapPanels)
FOS_FURNITURE_ENUM(FrontStyle, kFrontStyle)
FOS_FURNITURE_ENUM(FrameJoint, kFrameJoint)
#undef FOS_FURNITURE_ENUM

} // namespace fos::furniture
