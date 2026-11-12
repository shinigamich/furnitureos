// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/operation.hpp"
#include "fos/geometry/vec.hpp"

#include <string>
#include <vector>

/// Machine-independent toolpath intermediate representation (IR).
/// All coordinates are ABSOLUTE tool-tip positions in the machine frame (machine.hpp), mm.
/// Radius compensation is already applied: targets are tool-centre positions.
namespace fos::cam {

enum class MotionKind { Rapid, Linear, ToolChange, SpindleOn, SpindleOff, Dwell };
const char* toString(MotionKind k) noexcept;

struct Motion {
    MotionKind kind = MotionKind::Rapid;
    geometry::Vec3 target;   ///< Rapid/Linear: end point.
    /// Rapid only: only Z is commanded, X/Y stay where they are (used for the initial
    /// retract when the start position is unknown). target.x/y are then meaningless.
    bool zOnly = false;
    double feed = 0;         ///< Linear: mm/min.
    int toolNumber = 0;      ///< ToolChange.
    double rpm = 0;          ///< SpindleOn (clockwise).
    double seconds = 0;      ///< Dwell.
    domain::OperationId source; ///< Operation this motion belongs to (empty for set-up moves).

    static Motion rapid(geometry::Vec3 p, domain::OperationId src = {});
    static Motion rapidZ(double z);
    static Motion linear(geometry::Vec3 p, double feed, domain::OperationId src = {});
    static Motion toolChange(int number);
    static Motion spindleOn(double rpm);
    static Motion spindleOff();
    static Motion dwell(double seconds, domain::OperationId src = {});
};

/// One-line English description, e.g. "Linear to (346, 37, 5) F600".
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
