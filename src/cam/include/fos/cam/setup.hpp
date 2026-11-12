// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/domain/part.hpp"
#include "fos/geometry/aabb.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

/// One clamping of a part on a 3-axis vertical-spindle machine, and the mapping from the
/// part frame to the machine frame.
///
/// FRAME MAPPING (L, W, T = part length, width, thickness; o = Setup::stockOrigin):
///  * Face A up (part lies as modelled):  machine = o + (x, y, z).
///    Face A point (u, v) -> machine XY (o.x + u, o.y + v), surface at Z = o.z + T.
///  * Face B up: the panel is turned over about its LENGTH axis (rotation of 180 degrees
///    about local X, a proper rotation, det = +1):  machine = o + (x, W - y, T - z).
///    Face B point (u, v) -> machine XY (o.x + u, o.y + W - v), surface at Z = o.z + T.
///    This is exactly machining::flippedFaceCoordinates(part, B, (u, v), FlipAxis::Length).
/// In both cases the part's length runs along machine +X and the stock occupies
/// [o.x, o.x + L] x [o.y, o.y + W] x [o.z, o.z + T]. Edge faces cannot face the spindle.
namespace fos::cam {

struct Setup {
    domain::Face faceUp = domain::Face::A; ///< Only A or B are valid.
    /// Machine coordinates of the clamped stock's minimum corner (front-left-bottom as
    /// seen on the table): the fixture offset. o.z is the stock bottom (normally 0).
    geometry::Vec3 stockOrigin;
};

/// Derived, validated geometry of a setup.
struct SetupFrame {
    domain::Face faceUp = domain::Face::A;
    geometry::Vec3 origin;
    double length = 0, width = 0, thickness = 0;
    double topZ = 0;     ///< machine Z of the upward face
    double bottomZ = 0;  ///< machine Z of the stock bottom
    geometry::Aabb stock;///< machine frame

    /// Part-frame point -> machine frame (formulas above).
    geometry::Vec3 partToMachine(geometry::Vec3 partPoint) const noexcept;
};

/// Errors: faceUp not A/B (Unsupported), non-positive part dimensions (InvalidArgument).
core::Result<SetupFrame> makeSetupFrame(const domain::Part& part, const Setup& setup);

/// Machine XY of a point (u, v) on the UPWARD face, computed with the machining flip
/// helper for face B. Precondition: frame was made from `part`.
geometry::Vec2 faceToMachineXY(const domain::Part& part, const SetupFrame& frame, geometry::Vec2 uv) noexcept;

enum class Disposition { MachinableNow, NeedsOtherSetup, Unsupported };
const char* toString(Disposition d) noexcept;

struct PlannedOperation {
    std::size_t index = 0;          ///< into Part::operations
    domain::OperationId id;
    Disposition disposition = Disposition::Unsupported;
    std::optional<domain::Face> requiredFaceUp; ///< NeedsOtherSetup: the face that must be up.
    std::string reason;             ///< English explanation (empty for MachinableNow).
};

/// Explicit partition of ALL operations of the part; every operation is in exactly one list,
/// each list in Part::operations order. Unsupported: edge-face operations (no horizontal
/// spindle) and all pockets (no verified pocket strategy exists).
struct SetupPlan {
    std::vector<PlannedOperation> machinableNow;
    std::vector<PlannedOperation> needsOtherSetup;
    std::vector<PlannedOperation> unsupported;
};

core::Result<SetupPlan> planSetup(const domain::Part& part, const Setup& setup);

/// Nominal hole volume in the machine frame (from the domain operation only).
struct HoleTarget {
    domain::OperationId id;
    geometry::Vec2 centre;  ///< machine XY
    double radius = 0;
    double topZ = 0;
    double floorZ = 0;      ///< topZ - depth; stock bottom for through holes
    bool through = false;
};
/// Nominal groove volume: rectangle around the centre line start-end, |lateral| <= halfWidth.
/// An end is OPEN when it lies on the panel boundary and the groove runs perpendicular to
/// that boundary edge (the tool may run out past it, giving a square end); otherwise the end
/// is CLOSED and an end mill leaves it rounded.
struct GrooveTarget {
    domain::OperationId id;
    geometry::Vec2 start, end; ///< machine XY of the centre-line ends
    double halfWidth = 0;
    double topZ = 0;
    double floorZ = 0;
    bool startOpen = false, endOpen = false;
};
/// Preconditions: the operation lies on frame.faceUp and has the respective shape.
HoleTarget holeTarget(const domain::Part& part, const SetupFrame& frame, const domain::Operation& op);
GrooveTarget grooveTarget(const domain::Part& part, const SetupFrame& frame, const domain::Operation& op);

} // namespace fos::cam
