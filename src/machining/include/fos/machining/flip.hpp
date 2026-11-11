// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/part.hpp"

#include <vector>

/// Presentation helpers for working on the second side of a panel.
///
/// In the model, faces A and B share the same (u, v) = (local x, local y) — B is NOT
/// mirrored (see operation.hpp). An operator who turns the panel over to work on face B
/// sees a mirrored picture, and which coordinate is mirrored depends on how the panel
/// was turned:
///
///  * FlipAxis::Length — the panel is rotated 180 degrees about its LENGTH axis (local X,
///    through the panel centre), like turning the page of a calendar hung on its long
///    side. A point at part (x, y) ends up at (x, W - y). Seen from above, with the
///    length still running left to right from the same end:  u' = u,  v' = W - v.
///  * FlipAxis::Width — rotated 180 degrees about the WIDTH axis (local Y):
///    u' = L - u,  v' = v.
///
/// The returned coordinates are a PRESENTATION for drawings/operator lists only. They
/// must never be stored back into a Part.
namespace fos::machining {

enum class FlipAxis { Length, Width };

/// Operator-view coordinates of a face point after the flip:
/// Length: (u, vMax - v);  Width: (uMax - u, v), with uMax/vMax the extents of `face`.
geometry::Vec2 flippedFaceCoordinates(const domain::Part& part, domain::Face face, geometry::Vec2 uv,
                                      FlipAxis axis = FlipAxis::Length) noexcept;

/// Copies of all operations of `part` lying on `face`, with their coordinates converted
/// by flippedFaceCoordinates (ids, sizes, depths, source unchanged; original order kept).
/// Intended for face B (default flip: about the length axis). Pocket contours are
/// re-ordered so that they stay counter-clockwise in the mirrored view (the first point
/// stays first).
std::vector<domain::Operation> mirrorOperationsForFace(const domain::Part& part, domain::Face face = domain::Face::B,
                                                       FlipAxis axis = FlipAxis::Length);

} // namespace fos::machining
