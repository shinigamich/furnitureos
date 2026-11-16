// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cam/contour.hpp"
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
///
/// CONTOUR ROUTING (shaped parts, `needsContourRouting(part)`, contour.hpp; after all holes and
/// grooves, cut-outs first, then the outline):
///  * Tool: `contourToolNumber` if set (must be a centre-cutting end mill), otherwise the LARGEST
///    centre-cutting end mill (ties: lowest number) whose flute length covers thickness +
///    breakthroughAllowance and for which every compensated path below exists.
///  * Radius compensation in the IR (no G41/G42): tool-centre path =
///    geometry::offsetContour(raw outline, +r) (outside) and offsetContour(cut-out, -r) (inside),
///    r = tool radius. Lines stay lines, arcs stay concentric arcs (R50 outline corner -> path R54
///    for an 8 mm cutter; R50 cut-out corner -> R46) and are emitted as exact ArcCW/ArcCCW.
///    Sharp corners whose waste side is a wedge (inside corners of the outline, corners of a
///    cut-out) are left with the tool radius: warning cam.contour.rounded_corner.
///  * Direction (spindle clockwise, M3): Climb (default) keeps the part on the RIGHT of the tool
///    (outline clockwise, cut-outs counter-clockwise, seen from +Z); Conventional is the reverse.
///  * Step-downs maxDepthOfCut, 2*maxDepthOfCut, ..., through to stock bottom -
///    breakthroughAllowance (<= profile spoilboard limit). Each level is a closed loop.
///  * Lead-in / lead-out: tangential quarter-circle ARCS of radius contourLeadRadius (0 = tool
///    radius) on the waste side, at the midpoint of the longest straight path segment (longest
///    segment if none is straight). Per level: rapid above the lead-in start, plunge at the plunge
///    rate there (in the waste), lead-in arc, full loop, lead-out arc, rapid retract. Tabs/bridges
///    are NOT generated: the part is free once the last level is cut (hold it by vacuum/clamps).
namespace fos::cam {

enum class ContourDirection { Climb, Conventional };

struct CamOptions {
    double breakthroughAllowance = 0.5; ///< mm below the stock bottom for through holes
    double peckDepth = 0;               ///< mm; 0 = no pecking
    double dwellAtHoleBottom = 0;       ///< s; 0 = none
    double spindleSpinUpDwell = 0;      ///< s after every SpindleOn; 0 = none
    double grooveLeadOut = 2.0;         ///< mm beyond an open groove end (plus tool radius)
    double stepoverFraction = 0.5;      ///< lateral stepover <= fraction * tool diameter, (0, 1]
    bool routeContours = true;          ///< route shaped parts (see CONTOUR ROUTING); false = skip
    ContourDirection contourDirection = ContourDirection::Climb;
    int contourToolNumber = 0;          ///< 0 = automatic selection
    double contourLeadRadius = 0;       ///< mm; 0 = tool radius
    /// Model providing the edge bands for domain::cutContour(). May be null unless an outline
    /// segment of a routed part is banded. Not owned.
    const domain::Model* model = nullptr;
};

struct CamResult {
    Program program;
    SetupPlan plan;
    /// Warnings: cam.setup.needs_other_setup, cam.setup.unsupported, cam.groove.rounded_end,
    /// cam.contour.rounded_corner, cam.profile.unverified.
    core::Diagnostics diagnostics;
    std::vector<int> toolsUsed; ///< in order of first use
};

/// Errors (Result): invalid setup or profile, machining validation errors on the part,
/// missing/unsuitable tool (message from ToolLibrary), tool rpm/feed above machine limits,
/// safe Z below the clearance plane, breakthrough allowance beyond the spoilboard limit,
/// invalid options, closed groove shorter than the tool diameter, no end mill able to route a
/// contour (message names the contour and the tools tried), invalid shape / missing model for a
/// banded outline (contourTargets), stock origin Z < 0
/// (makeSetupFrame), and any generated position outside the machine envelope (ConstraintViolation
/// naming the operation, the position and the envelope; e.g. a groove lead-out past an open end
/// with the stock clamped close to the envelope limit).
/// Apart from the envelope the program is NOT checked here; run simulate() (postprocessors do so
/// themselves).
core::Result<CamResult> generateProgram(const domain::Part& part, const Setup& setup, const MachineProfile& machine,
                                        const ToolLibrary& tools, const CamOptions& options = {});

} // namespace fos::cam
