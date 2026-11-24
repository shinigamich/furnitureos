// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Plan workspace view-model: the widget-free geometry of the 2D top-down furniture plan.
///
/// Frame: the PROJECT frame seen from above (+Z towards the viewer): X to the right, Y = depth
/// away from the viewer of the front (ARCHITECTURE.md: front at small y). All values are mm in
/// the project XY plane. Everything is derived from the resolved model (generated parts and the
/// item placements); moving / rotating only ever produces commands::setPlacement, ONE undo step
/// per gesture.
///
/// Footprints: each generated part of an item is projected to XY. A part lying flat (its local
/// Z parallel to the project Z) contributes its true outline (the contour of a shaped part,
/// tessellated with 0.5 mm chord tolerance, else its rectangle); any other part contributes the
/// XY rectangle of its box. The union of these polygons is the item's footprint (an L-shaped
/// corner cabinet shows as an L); `bounds` is the XY box of the generated assembly bounds.
/// Hardware (handles, feet) is not part of the footprint.
///
/// The room is a plain rectangle the user types in. The project file has no place for room or
/// plan settings, so it is NOT stored anywhere (session only, see PlanRoom).
#include "fos/core/result.hpp"
#include "fos/geometry/polygon.hpp"
#include "fos/geometry/rect.hpp"
#include "fos/geometry/transform.hpp"
#include "fos/project/commands.hpp"
#include "fos/project/resolve.hpp"

#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace fos::app {

class Session;

/// Top-down footprint of one generated item.
struct PlanFootprint {
    std::string itemId;
    std::string name;                          ///< Item name.
    geometry::Rect2 bounds;                    ///< XY of the assembly bounds in the project frame, mm.
    std::vector<geometry::Polygon2> outlines;  ///< One per part (project XY); their union = footprint.
    bool shaped = false;                       ///< At least one flat part contributed a contour outline.
};

/// Footprints of every item that was generated, in project order (items that failed to
/// generate are absent).
std::vector<PlanFootprint> planFootprints(const project::Project& project, const project::Resolved& resolved);
/// Same for the session; empty when the session has no resolved model.
std::vector<PlanFootprint> planFootprints(const Session& session);

/// Smallest rectangle containing every footprint's bounds (empty rect for no footprints).
geometry::Rect2 planExtents(const std::vector<PlanFootprint>& footprints);

/// Simple rectangular room: inner floor [x, x + width] x [y, y + depth] (project frame, mm).
/// wallThickness is only used for drawing (walls are drawn outside the inner rectangle).
struct PlanRoom {
    double x = 0, y = 0;
    double width = 0, depth = 0;
    double wallThickness = 100;
    geometry::Rect2 inner() const noexcept { return geometry::Rect2::fromOriginSize({x, y}, width, depth); }
};

/// InvalidArgument (naming the value) unless width and depth are finite and > 0, the wall
/// thickness finite and >= 0 and x / y finite.
core::Result<void> checkPlanRoom(const PlanRoom& room);

/// Free distance along X between an item and its right-hand neighbour (or a room wall).
struct PlanGap {
    std::string leftId;   ///< Empty = the left room wall.
    std::string rightId;  ///< Empty = the right room wall.
    double fromX = 0;     ///< Right edge of the left item (or the left wall), mm.
    double toX = 0;       ///< Left edge of the right item (or the right wall), mm.
    double y = 0;         ///< Middle of the Y range both share: where the gap is drawn, mm.
    double gap() const noexcept { return toX - fromX; }
};

/// Gaps along X between neighbouring footprints (by bounds). The right neighbour of item A is
/// the item B whose Y range overlaps A's (by more than 1e-6 mm) and whose left edge is the
/// smallest one at or right of A's right edge. With a room (checked by checkPlanRoom; an
/// invalid room is ignored), an item with no left / right neighbour inside the room also gets
/// the gap to that wall when it is inside the room along X and overlaps it along Y. Gaps of
/// <= 0.05 mm (touching) are not listed. Order: by fromX, then y, then ids.
std::vector<PlanGap> planGapsX(const std::vector<PlanFootprint>& footprints, const std::optional<PlanRoom>& room);

/// Result of snapping a drag.
struct PlanSnap {
    geometry::Vec2 delta;          ///< Translation to apply, mm.
    std::optional<double> guideX;  ///< X of the edge snapped to (vertical guide line), if any.
    std::optional<double> guideY;  ///< Y of the edge snapped to (horizontal guide line), if any.
};

/// Snaps the translation `delta` of item `movingId` (by its bounds): along each axis the
/// moving item's min / max edge snaps to the closest min / max edge of every other footprint
/// and to the room's inner walls when that distance is <= `tolerance` (mm; the GUI passes
/// snap pixels / pixels per mm). Ties: the earlier target (footprint order, then room) wins.
/// An axis that does not snap is rounded to a multiple of `grid` (mm; <= 0 = no rounding).
/// Unknown `movingId`: delta returned unchanged (rounded), no guides.
PlanSnap snapPlanMove(const std::vector<PlanFootprint>& footprints, const std::string& movingId,
                      geometry::Vec2 delta, double tolerance, const std::optional<PlanRoom>& room,
                      double grid = 1.0);

/// `placement` translated by (delta.x, delta.y, 0) in the project frame.
geometry::Transform movedPlacement(const geometry::Transform& placement, geometry::Vec2 delta);
/// `placement` rotated by +90 degrees (counter-clockwise seen from above) about the vertical
/// axis through `pivot` (project XY). The rotation matrix is exact (no cos/sin round-off).
geometry::Transform rotatedPlacement(const geometry::Transform& placement, geometry::Vec2 pivot);

/// commands::setPlacement of the moved item. NotFound for an unknown item, InvalidArgument for
/// a non-finite delta.
core::Result<project::Command> planMoveCommand(const project::Project& project, const std::string& itemId,
                                               geometry::Vec2 delta);
/// commands::setPlacement rotating the item by +90 degrees about `pivot`. NotFound / InvalidArgument as above.
core::Result<project::Command> planRotateCommand(const project::Project& project, const std::string& itemId,
                                                 geometry::Vec2 pivot);
/// Executes planMoveCommand (one undo step). A zero delta does nothing and succeeds.
core::Result<void> applyPlanMove(Session& session, const std::string& itemId, geometry::Vec2 delta);
/// Rotates the item +90 degrees about the centre of its footprint bounds (one undo step).
/// NotFound when the item has no footprint (not generated).
core::Result<void> applyPlanRotate(Session& session, const std::string& itemId);

/// Id of the item under `point` (project XY): the LAST footprint in order with an outline
/// containing the point (within `tolerance` mm of its bounds when no outline contains it).
/// Empty when none.
std::string planItemAt(const std::vector<PlanFootprint>& footprints, geometry::Vec2 point, double tolerance = 0.0);

/// "600 × 560 mm" (overall X × Y size of a footprint; numbers rounded to 0.1 mm, trailing ".0" dropped).
QString planSizeText(double sizeX, double sizeY);
/// "35 mm" (a gap or a single dimension, same rounding).
QString planLengthText(double length);

} // namespace fos::app
