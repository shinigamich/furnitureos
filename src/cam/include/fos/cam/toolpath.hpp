// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/operation.hpp"
#include "fos/geometry/vec.hpp"

#include <string>
#include <vector>

/// Machine-independent toolpath intermediate representation (IR).
/// All coordinates are ABSOLUTE tool-tip positions in the machine frame (machine.hpp), mm.
/// Radius compensation is already applied: targets are tool-centre positions.
///
/// ARCS (ArcCW / ArcCCW): circular interpolation in the machine XY plane (G17), seen from +Z
/// looking down: ArcCCW turns counter-clockwise, ArcCW clockwise. The arc starts at the current
/// position, ends at `target` and has its EXACT centre at `centre` (absolute machine XY, never
/// rounded in the IR). Z moves linearly with the swept angle (a helix when target.z differs).
/// The swept angle is in (0, 360) degrees: full circles are not allowed (split them), and the
/// start and end must lie on the same circle (the simulator reports cam.sim.arc_invalid).
namespace fos::cam {

enum class MotionKind { Rapid, Linear, ToolChange, SpindleOn, SpindleOff, Dwell, ArcCW, ArcCCW };
const char* toString(MotionKind k) noexcept;

struct Motion {
    MotionKind kind = MotionKind::Rapid;
    geometry::Vec3 target;   ///< Rapid/Linear/Arc: end point.
    geometry::Vec2 centre;   ///< ArcCW/ArcCCW: absolute machine XY of the arc centre (exact).
    /// Rapid only: only Z is commanded, X/Y stay where they are (used for the initial
    /// retract when the start position is unknown). target.x/y are then meaningless.
    bool zOnly = false;
    double feed = 0;         ///< Linear/Arc: mm/min.
    int toolNumber = 0;      ///< ToolChange.
    double rpm = 0;          ///< SpindleOn (clockwise).
    double seconds = 0;      ///< Dwell.
    domain::OperationId source; ///< Operation this motion belongs to (empty for set-up moves).

    static Motion rapid(geometry::Vec3 p, domain::OperationId src = {});
    static Motion rapidZ(double z);
    static Motion linear(geometry::Vec3 p, double feed, domain::OperationId src = {});
    /// Circular move from the current position to `p` around `centre`; ccw selects ArcCCW.
    static Motion arc(geometry::Vec3 p, geometry::Vec2 centre, bool ccw, double feed, domain::OperationId src = {});
    static Motion toolChange(int number);
    static Motion spindleOn(double rpm);
    static Motion spindleOff();
    static Motion dwell(double seconds, domain::OperationId src = {});
};

/// True for ArcCW / ArcCCW.
inline bool isArc(MotionKind k) noexcept { return k == MotionKind::ArcCW || k == MotionKind::ArcCCW; }
/// True for motions that remove material at feed (Linear and arcs).
inline bool isFeedMove(MotionKind k) noexcept { return k == MotionKind::Linear || isArc(k); }

/// One-line English description, e.g. "Linear to (346, 37, 5) F600" or
/// "Arc CCW to (10, 0, 5) centre (5, 0) F3000".
std::string describe(const Motion& m);

struct Program {
    std::string partId;
    std::string partName;
    domain::Face faceUp = domain::Face::A;
    double safeZ = 0;       ///< machine Z
    double clearanceZ = 0;  ///< stock top + clearance, machine Z
    std::vector<Motion> motions;
};

} // namespace fos::cam
