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
enum class FrontKind { Door, DoorPair, DrawerStack, Flap };
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

} // namespace fos::furniture
