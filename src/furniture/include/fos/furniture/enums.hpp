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
enum class FrontKind { Door, DoorPair, DrawerStack };
enum class OverlayKind { Full, Half, Inset };
enum class HingeSide { Left, Right };
enum class HandlePosition { None, Center, Top, Bottom };
enum class MaterialRole { Carcass, Back, Front };
enum class PanelOrientation {
    Horizontal,    ///< length -> +X, width -> +Y, thickness -> +Z (shelf, worktop).
    VerticalSide,  ///< length -> +Z, width -> +Y, thickness -> -X (like a cabinet side).
    VerticalFront, ///< length -> +X, width -> +Z, thickness -> -Y (like a back panel / filler strip).
};

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

} // namespace fos::furniture
