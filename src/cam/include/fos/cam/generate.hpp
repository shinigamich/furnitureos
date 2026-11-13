// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cam/machine.hpp"
#include "fos/cam/setup.hpp"
#include "fos/cam/tool.hpp"
#include "fos/cam/toolpath.hpp"
#include "fos/core/diagnostic.hpp"

#include <vector>

/// Toolpath generation for one setup. Operations are sequenced with
/// machining::orderOperations (groups by face/tool, nearest-neighbour heuristic).
///
/// Drilling (per hole): XY rapid at the current height (>= clearance plane), rapid down to
/// the clearance plane, feed (tool plunge rate) to the floor, optional dwell, rapid straight
/// up to the clearance plane. Floor = top - depth; through holes: stock bottom minus
/// `breakthroughAllowance`, which must not exceed the profile's maxSpoilboardPenetration.
/// Pecking (peckDepth > 0): feed to peckDepth, 2*peckDepth, ..., floor, retracting to the
/// clearance plane after every peck and FEEDING (not rapid) back down from the clearance plane.
///
/// Grooves: step-down levels maxDepthOfCut, 2*maxDepthOfCut, ..., depth; at each level
/// lateral passes at offsets evenly spread over [-(w-d)/2, +(w-d)/2] (n = 1 if w == d, else
/// ceil((w-d)/(stepoverFraction*d)) + 1). Each pass: plunge at the start at plunge rate, cut
/// to the end at feed rate, rapid straight up. The centre line is shortened by d/2 at closed
/// ends and extended by d/2 + grooveLeadOut at open ends (see GrooveTarget).
///
/// Pockets are NOT generated: they are reported as unsupported by planSetup.
namespace fos::cam {

struct CamOptions {
    double breakthroughAllowance = 0.5; ///< mm below the stock bottom for through holes
    double peckDepth = 0;               ///< mm; 0 = no pecking
    double dwellAtHoleBottom = 0;       ///< s; 0 = none
    double spindleSpinUpDwell = 0;      ///< s after every SpindleOn; 0 = none
    double grooveLeadOut = 2.0;         ///< mm beyond an open groove end (plus tool radius)
    double stepoverFraction = 0.5;      ///< lateral stepover <= fraction * tool diameter, (0, 1]
};

struct CamResult {
    Program program;
    SetupPlan plan;
    /// Warnings: cam.setup.needs_other_setup, cam.setup.unsupported, cam.groove.rounded_end,
    /// cam.profile.unverified.
    core::Diagnostics diagnostics;
    std::vector<int> toolsUsed; ///< in order of first use
};

/// Errors (Result): invalid setup or profile, machining validation errors on the part,
/// missing/unsuitable tool (message from ToolLibrary), tool rpm/feed above machine limits,
/// safe Z below the clearance plane, breakthrough allowance beyond the spoilboard limit,
/// invalid options, closed groove shorter than the tool diameter, stock origin Z < 0
/// (makeSetupFrame), and any generated position outside the machine envelope (ConstraintViolation
/// naming the operation, the position and the envelope; e.g. a groove lead-out past an open end
/// with the stock clamped close to the envelope limit).
/// Apart from the envelope the program is NOT checked here; run simulate() (postprocessors do so
/// themselves).
core::Result<CamResult> generateProgram(const domain::Part& part, const Setup& setup, const MachineProfile& machine,
                                        const ToolLibrary& tools, const CamOptions& options = {});

} // namespace fos::cam
