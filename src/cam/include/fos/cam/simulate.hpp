// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cam/contour.hpp"
#include "fos/cam/machine.hpp"
#include "fos/cam/setup.hpp"
#include "fos/cam/tool.hpp"
#include "fos/cam/toolpath.hpp"
#include "fos/core/diagnostic.hpp"

#include <cstddef>

/// Manufacturing simulation of a Program, INDEPENDENT of the generators: expectations are
/// derived from the domain operations (setup.hpp targets), never from generator output.
/// NOT independent on face mapping: the targets use the same setup.hpp mapping
/// (holeTarget / grooveTarget / faceToMachineXY) as the generator; that mapping is tested
/// against first principles separately (see README).
///
/// Motion checks (error unless noted; `entityId` = part id):
///   cam.sim.setup_mismatch         program face-up differs from the setup / invalid setup
///   cam.sim.setup_mismatch         also: invalid cell size, negative stock origin Z (makeSetupFrame)
///   cam.sim.safe_z_below_clearance machine safe Z below stock top + clearance (checked once per run)
///   cam.sim.position_unknown       XY move before the position is known, or first retract below safe Z
///   cam.sim.envelope               target outside the machine envelope
///   cam.sim.rapid_below_clearance  rapid below the clearance plane over the stock (pure upward retracts
///                                  allowed); a move from an UNKNOWN X/Y counts as crossing the stock
///   cam.sim.tool_below_resolution  tool diameter < 2 x cellSize (its cuts could miss every cell centre)
///   cam.sim.spoilboard_limit       tool tip below stock bottom - maxSpoilboardPenetration
///   cam.sim.no_tool                linear move with no tool loaded
///   cam.sim.spindle_off_cut        linear move while the spindle is off
///   cam.sim.feed_limit             feed <= 0 or > machine maxFeed
///   cam.sim.rpm_limit              rpm <= 0 or > machine/tool limits
///   cam.sim.unknown_tool           tool number not in the library
///   cam.sim.tool_change_unsafe     tool change with spindle on or below safe Z
///   cam.sim.flute_length           cutting deeper below the stock top than the flute length
///   cam.sim.spindle_on_at_end      program ends with the spindle running
///   cam.sim.invalid_dwell          negative dwell
///   cam.sim.arc_invalid            arc whose start and end radii differ by > 0.001 mm, radius
///                                  < 0.001 mm, or start == end (full circles are not allowed)
/// Material checks (coarse heightmap of cell centres over the WHOLE stock, last row/column
/// clipped to the stock edge; flat-bottomed cylindrical tool):
///   cam.sim.hole_not_cut / cam.sim.groove_not_cut / cam.sim.pocket_not_cut   expected cells not cut
///                                  to the floor (pockets: the rectangle with inner corners rounded to
///                                  the effective radius, pocket.hpp precedence; removal is allowed
///                                  inside the sharp rectangle, open sides extended past the stock edge)
///   cam.sim.plunge_not_centre_cutting  vertical feed move of a non-centre-cutting tool below the
///                                  material surface at the cell nearest its centre (heuristic: one cell)
///   cam.sim.too_deep               cut inside an operation footprint below its floor
///   cam.sim.gouge                  material removed outside every operation footprint
///   cam.sim.unresolved (warning)   operation too small for the cell size
/// Contour routing (shaped parts, contour.hpp; only when SimulationOptions::expectContours). The
/// expectations come from domain::cutContour() / the cut-outs mapped with contourToMachineXY():
///   cam.sim.contour_not_cut        a contour has no cutting motion, or cells on its waste side within
///                                  one tool radius of the contour AND reachable by that tool (within r
///                                  of the ideal compensated path offsetContour(contour, +/-r)) are not
///                                  cut through to the stock bottom; unreachable sharp corners are not
///                                  required (the generator warns about them)
///   material on the waste side (outside the raw outline, inside a cut-out) may be removed down to the
///   spoilboard limit; anything removed INSIDE the finished part area (raw outline minus cut-outs,
///   beyond contourTolerance, and outside every hole/groove) is cam.sim.gouge.
namespace fos::cam {

struct SimulationOptions {
    double cellSize = 0.5;        ///< mm; heightmap resolution
    double depthTolerance = 0.01; ///< mm
    /// Expect contour routing for parts with needsContourRouting() (set false when the program was
    /// generated with CamOptions::routeContours = false).
    bool expectContours = true;
    double contourTolerance = 0.001; ///< mm; cell centres this close to a contour count as on it
    /// Model providing the edge bands for domain::cutContour(); may be null unless an outline
    /// segment is banded (cam.sim.setup_mismatch otherwise). Not owned.
    const domain::Model* model = nullptr;
    /// Fallback inner corner radius for sharp pockets, mm; must equal CamOptions::pocketCornerRadius
    /// (pocket.hpp; a pocket's own Pocket::cornerRadius > 0 takes precedence).
    double pocketCornerRadius = 0;
};

struct SimulationReport {
    core::Diagnostics diagnostics;
    double rapidLength = 0;       ///< mm (initial Z-only retract from an unknown position not counted)
    double cutLength = 0;         ///< mm of Linear moves
    /// Seconds: rapids at profile.rapidFeed, linears at their feed, dwells, tool changes.
    /// An ESTIMATE: acceleration and controller behaviour are ignored.
    double estimatedSeconds = 0;
    int toolChanges = 0;
    std::size_t cellsRemoved = 0;
    bool hasErrors() const noexcept { return core::hasErrors(diagnostics); }
};

SimulationReport simulate(const Program& program, const domain::Part& part, const Setup& setup,
                          const MachineProfile& machine, const ToolLibrary& tools,
                          const SimulationOptions& options = {});

} // namespace fos::cam
