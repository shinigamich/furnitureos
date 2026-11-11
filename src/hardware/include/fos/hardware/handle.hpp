// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/hardware/placement.hpp"

namespace fos::hardware {

enum class HandleOrientation { Horizontal, Vertical };
enum class HorizontalAnchor { Center, FromLeft, FromRight };
enum class VerticalAnchor { Center, FromTop, FromBottom };

/// Position of a two-hole handle on a VERTICAL front (door, drawer front).
///
/// Directions are defined in the ASSEMBLY frame: "vertical" is the assembly Z axis;
/// "horizontal" is the direction in the front's plane perpendicular to Z. LEFT is the end
/// of the front with the smaller assembly X (for a front lying in a YZ plane: the smaller
/// assembly Y) — for a front at the front of the furniture this is the viewer's left.
struct HandleConfig {
    domain::HardwareInstanceId instanceId; ///< Required.
    HandleOrientation orientation = HandleOrientation::Horizontal;
    HorizontalAnchor horizontalAnchor = HorizontalAnchor::Center;
    /// Handle CENTRE to the left/right edge of the front, mm (ignored for Center).
    double horizontalOffset = 0.0;
    VerticalAnchor verticalAnchor = VerticalAnchor::Center;
    /// Handle CENTRE to the top/bottom edge of the front, mm (ignored for Center).
    double verticalOffset = 0.0;
    /// Minimum material between a hole wall and any edge of the front.
    double minEdgeMaterial = 5.0;
    /// Face the through holes are recorded on (they pass through the whole front; A and B
    /// share the same coordinates, so this only affects from which side they are drilled).
    domain::Face face = domain::Face::A;
};

/// Two through holes (hole_diameter, hole_spacing apart) for a handle of kind Handle.
/// Result: one instance (quantity 1, parts {front}, position = handle centre on
/// `config.face`, assembly frame); two operations `<instance>/op.1` (left or lower hole)
/// and `/op.2` (right or upper hole).
/// Errors: front not vertical or rotated in its plane, `face` not A/B, a hole would be
/// closer than minEdgeMaterial to an edge or off the front.
core::Result<Placement> placeHandle(const domain::Part& front, const domain::HardwareDefinition& handle,
                                    const HandleConfig& config);

} // namespace fos::hardware
