// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/viewport/manipulator.hpp"

#include <QString>

#include <cstddef>
#include <optional>

class QPainter;

/// QPainter overlay of the resize handles (private to the viewport module): an arrow plus a
/// sphere-like disc of constant screen size at each face centre, and during a drag the ghost
/// box (dashed edges) with the dimension label ("W 912 mm") next to the dragged handle.
namespace fos::viewport::detail {

/// Radius of a handle disc in logical pixels (also the base of the hit radius).
inline constexpr double kHandleRadiusPx = 7.0;

struct HandleDragState {
    std::size_t handle = 0;
    double value = 0; ///< Current (snapped) dimension, mm.
};

void paintHandleOverlay(QPainter& painter, const Camera& camera, const ResizeTarget& target,
                        std::optional<std::size_t> hovered, const std::optional<HandleDragState>& drag,
                        const QString& lengthSuffix);

/// Label text, e.g. "W 912 mm" (no decimals for whole millimetres, else one decimal).
QString handleLabel(const std::string& letter, double mm, const QString& suffix);

} // namespace fos::viewport::detail
