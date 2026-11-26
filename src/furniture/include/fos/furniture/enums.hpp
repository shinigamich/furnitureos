// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Enumerations of the furniture module with STABLE text forms for serialisation:
/// `toString(e)` and `fromString<E>(text)` are exact inverses; the texts never change.
#include <optional>
#include <string_view>

namespace fos::furniture {

/// How a horizontal carcass panel (top / bottom) meets the sides.
enum class JointKind {
    BetweenSides, ///< Sides run through; the panel sits between them (length = inner width).
    OverSides,    ///< The panel runs the full width; the sides stand between top and bottom.
};
enum class TopKind {
    Panel, ///< One full-depth top panel.
    Rails, ///< Two flat stretcher rails (front and rear) between the sides (base cabinets).
};
enum class BackMount {
    None,
    Overlay, ///< Nailed onto the rear of the carcass, inset from the outer edges.
    Grooved, ///< Sits in grooves milled into sides, bottom and top (or rear rail).
    Inset,   ///< Between sides, top and bottom, flush with the rear edges.
};
enum class PlinthKind {
    None,         ///< Carcass stands on the floor.
    SidesToFloor, ///< Sides run to the floor; a recessed plinth panel closes the front.
    Feet,         ///< Carcass raised by `plinth_height` on four adjustable feet (no plinth panel).
};
enum class JointFastener { None, Confirmat, Dowel };
enum class SplitDirection {
    None,       ///< Leaf section.
    Vertical,   ///< Children side by side (left -> right), separated by vertical partitions.
    Horizontal, ///< Children stacked (bottom -> top), separated by fixed shelves.
};
enum class SizeKind {
    Fixed,      ///< `value` millimetres.
    Weight,     ///< Share of the remaining space proportional to `value`.
    Expression, ///< `expression` over the cabinet's parameter graph, in millimetres.
};
/// Flap: one front hinged at its TOP edge to the panel above (lift-up flap, see README).
/// SlidingDoors: 2..6 doors running in a two-lane top / bottom track inside the opening
/// (README "Sliding doors"; system HardwareSelection::slidingSystem).
/// ApplianceNiche: an OPEN niche for a built-in appliance (oven, microwave): no front part;
/// the panel below is the shelf the appliance stands on, with an optional ventilation
/// opening (FrontSpec::niche, README "Appliance housings").
/// ApplianceFront: a decor front fixed to the door of an integrated appliance (dishwasher):
/// a loose front part without hinges, marked "fixed to appliance door".
enum class FrontKind { Door, DoorPair, DrawerStack, Flap, SlidingDoors, ApplianceNiche, ApplianceFront };
enum class OverlayKind { Full, Half, Inset };
enum class HingeSide { Left, Right };
enum class HandlePosition { None, Center, Top, Bottom };
/// Drawer: drawer-box sides, inner front and back; DrawerBottom: drawer-box bottoms.
enum class MaterialRole { Carcass, Back, Front, Drawer, DrawerBottom };
/// How the bottom of a drawer box is fixed (definition.hpp DrawerBoxSpec).
enum class DrawerBottomMount {
    Grooved,     ///< In grooves milled into the sides, the inner front and the back.
    NailedUnder, ///< Full-size panel nailed / screwed under the sides, front and back.
};
enum class PanelOrientation {
    Horizontal,    ///< length -> +X, width -> +Y, thickness -> +Z (shelf, worktop).
    VerticalSide,  ///< length -> +Z, width -> +Y, thickness -> -X (like a cabinet side).
    VerticalFront, ///< length -> +X, width -> +Z, thickness -> -Y (like a back panel / filler strip).
};

/// Kind of a parametric part outline (contour.hpp).
enum class ContourKind {
    Rectangle,      ///< Plain rectangle (no shape unless there are cut-outs).
    RoundedCorners, ///< Rectangle with four corner radii (expressions).
    ArcEdge,        ///< One edge replaced by a circular arc of a given rise.
    Custom,         ///< Explicit vertices whose coordinates are expressions.
};
/// Direction of an ArcEdge arc relative to the part (contour.hpp).
enum class ArcDirection {
    Convex,  ///< Bulges outwards; the arc's apex lies on the original edge line.
    Concave, ///< Dips into the part; the arc's ends are the original corners.
};
enum class CutoutKind { Rectangle, Circle };
/// Corner construction of a cabinet (definition.hpp CornerSpec, README "Corner cabinets").
enum class CornerKind {
    None,   ///< Ordinary rectangular cabinet.
    Blind,  ///< Blind corner: a fixed blind panel in the front plane on one side, the door covers the rest.
    LShape, ///< 90-degree L-shaped corner base cabinet (two legs, contoured top / bottom / shelves).
};
/// Side of the blind panel of a blind corner cabinet (seen from the front).
enum class CornerSide { Left, Right };
/// Ventilation opening of an appliance niche (definition.hpp ApplianceNicheSpec).
enum class NicheVent {
    None,       ///< No opening.
    ShelfSlot,  ///< Rectangular slot cut-out at the rear of the panel the appliance stands on.
    BackCutout, ///< Rectangular cut-out in the back panel at the top of the niche.
};
/// End panels of an appliance gap (definition.hpp ApplianceGapSpec), seen from the front.
enum class GapPanels { None, Left, Right, Both };
/// Construction of a door / flap / drawer front (README "Frame-and-panel fronts").
/// Slab = one board (default); FramePanel = five-piece front: 2 stiles, 2 rails, 1 panel.
enum class FrontStyle { Slab, FramePanel };
/// Corner joint of a frame-and-panel front: StubTenon = the rails carry tenons as long as the
/// groove depth that sit in the stiles' panel groove; Dowel = rails butt the stiles, 2 dowels
/// (HardwareSelection::dowel) per joint.
enum class FrameJoint { StubTenon, Dowel };
/// Panel of a frame-and-panel front: Grooved = a board panel floating in a groove (default);
/// Glass = a glass pane in a rebate on the back (inner) edges of the frame.
enum class FramePanelKind { Grooved, Glass };
/// Retention of a glass pane: None = nothing generated (fixed by clips / silicone, not
/// modelled); Strip = four glazing-bead strips in the rebate behind the glass.
enum class GlassBeads { None, Strip };
/// Front of an inner drawer (definition.hpp InnerDrawerSpec, README "Inner drawers").
/// Box = no separate front: the drawer box's own front panel is what is seen when the door
/// opens. Facia = a thin internal front (drawer material) in front of the box front.
enum class InnerDrawerFront { Box, Facia };
/// Grip of an inner-drawer facia: None, or CutOut = a rectangular through cut-out near the
/// top edge of the facia, centred on its width.
enum class InnerFaciaHandle { None, CutOut };

const char* toString(JointKind) noexcept;
const char* toString(TopKind) noexcept;
const char* toString(BackMount) noexcept;
const char* toString(PlinthKind) noexcept;
const char* toString(JointFastener) noexcept;
const char* toString(SplitDirection) noexcept;
const char* toString(SizeKind) noexcept;
const char* toString(FrontKind) noexcept;
const char* toString(OverlayKind) noexcept;
const char* toString(HingeSide) noexcept;
const char* toString(HandlePosition) noexcept;
const char* toString(MaterialRole) noexcept;
const char* toString(PanelOrientation) noexcept;
const char* toString(ContourKind) noexcept;
const char* toString(ArcDirection) noexcept;
const char* toString(CutoutKind) noexcept;
const char* toString(DrawerBottomMount) noexcept;
const char* toString(CornerKind) noexcept;
const char* toString(CornerSide) noexcept;
const char* toString(NicheVent) noexcept;
const char* toString(GapPanels) noexcept;
const char* toString(FrontStyle) noexcept;
const char* toString(FrameJoint) noexcept;
const char* toString(FramePanelKind) noexcept;
const char* toString(GlassBeads) noexcept;
const char* toString(InnerDrawerFront) noexcept;
const char* toString(InnerFaciaHandle) noexcept;

/// Inverse of toString for every enum above; std::nullopt for unknown text (case-sensitive).
template <class E> std::optional<E> fromString(std::string_view text) noexcept;
template <> std::optional<JointKind> fromString<JointKind>(std::string_view) noexcept;
template <> std::optional<TopKind> fromString<TopKind>(std::string_view) noexcept;
template <> std::optional<BackMount> fromString<BackMount>(std::string_view) noexcept;
template <> std::optional<PlinthKind> fromString<PlinthKind>(std::string_view) noexcept;
template <> std::optional<JointFastener> fromString<JointFastener>(std::string_view) noexcept;
template <> std::optional<SplitDirection> fromString<SplitDirection>(std::string_view) noexcept;
template <> std::optional<SizeKind> fromString<SizeKind>(std::string_view) noexcept;
template <> std::optional<FrontKind> fromString<FrontKind>(std::string_view) noexcept;
template <> std::optional<OverlayKind> fromString<OverlayKind>(std::string_view) noexcept;
template <> std::optional<HingeSide> fromString<HingeSide>(std::string_view) noexcept;
template <> std::optional<HandlePosition> fromString<HandlePosition>(std::string_view) noexcept;
template <> std::optional<MaterialRole> fromString<MaterialRole>(std::string_view) noexcept;
template <> std::optional<PanelOrientation> fromString<PanelOrientation>(std::string_view) noexcept;
template <> std::optional<ContourKind> fromString<ContourKind>(std::string_view) noexcept;
template <> std::optional<ArcDirection> fromString<ArcDirection>(std::string_view) noexcept;
template <> std::optional<CutoutKind> fromString<CutoutKind>(std::string_view) noexcept;
template <> std::optional<DrawerBottomMount> fromString<DrawerBottomMount>(std::string_view) noexcept;
template <> std::optional<CornerKind> fromString<CornerKind>(std::string_view) noexcept;
template <> std::optional<CornerSide> fromString<CornerSide>(std::string_view) noexcept;
template <> std::optional<NicheVent> fromString<NicheVent>(std::string_view) noexcept;
template <> std::optional<FrontStyle> fromString<FrontStyle>(std::string_view) noexcept;
template <> std::optional<FrameJoint> fromString<FrameJoint>(std::string_view) noexcept;
template <> std::optional<FramePanelKind> fromString<FramePanelKind>(std::string_view) noexcept;
template <> std::optional<GlassBeads> fromString<GlassBeads>(std::string_view) noexcept;
template <> std::optional<GapPanels> fromString<GapPanels>(std::string_view) noexcept;
template <> std::optional<InnerDrawerFront> fromString<InnerDrawerFront>(std::string_view) noexcept;
template <> std::optional<InnerFaciaHandle> fromString<InnerFaciaHandle>(std::string_view) noexcept;

} // namespace fos::furniture
