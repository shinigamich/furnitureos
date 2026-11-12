// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/machining/face.hpp"

#include <string>

/// Machine profile: a description of a 3-axis machine with ONE vertical spindle that
/// points down (tool along -Z). Nothing else is modelled: no horizontal spindles,
/// aggregates, drilling heads, 4th/5th axes or tool-length compensation.
///
/// MACHINE FRAME (program coordinates): right-handed, X/Y on the table, Z up, in mm.
/// Its origin is the work zero the operator sets on the control; Z = 0 is normally the
/// spoilboard top. All envelope values, safe heights and toolpath coordinates are
/// expressed in this frame. The mapping from the part frame is defined in setup.hpp.
///
/// HONESTY: a profile is user data. `verified` is false unless someone has proven the
/// profile on the real machine; FurnitureOS itself never sets it to true.
namespace fos::cam {

struct MachineProfile {
    std::string name;
    std::string controllerLabel; ///< Free text, e.g. "unknown controller". Not interpreted.
    bool verified = false;
    std::string verificationNote = "Not verified against any physical machine or controller.";
    machining::Range envelopeX;  ///< Reachable tool-tip positions, machine frame, mm.
    machining::Range envelopeY;
    machining::Range envelopeZ;
    double safeZ = 0;            ///< Absolute machine Z for travel and tool changes, mm.
    double clearance = 5;        ///< Height above the stock top below which no rapid may move over the stock, mm.
    double maxFeed = 0;          ///< mm/min
    double maxRpm = 0;           ///< 1/min
    double rapidFeed = 0;        ///< mm/min, used ONLY for time estimates.
    double toolChangeSeconds = 0;///< Used ONLY for time estimates.
    double maxSpoilboardPenetration = 1.0; ///< Deepest allowed tool tip below the stock bottom, mm.
};

/// Codes: cam.profile.invalid_envelope, cam.profile.safe_z_outside_envelope,
/// cam.profile.invalid_limit (non-positive feed/rpm/rapid, negative clearance/penetration/
/// tool-change time), cam.profile.unverified (warning, always raised while verified == false).
core::Diagnostics validateProfile(const MachineProfile& profile);

} // namespace fos::cam
