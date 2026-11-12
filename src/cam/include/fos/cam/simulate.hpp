// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cam/machine.hpp"
#include "fos/cam/setup.hpp"
#include "fos/cam/tool.hpp"
#include "fos/cam/toolpath.hpp"
#include "fos/core/diagnostic.hpp"

#include <cstddef>

/// Manufacturing simulation of a Program, INDEPENDENT of the generators: expectations are
/// derived from the domain operations (setup.hpp targets), never from generator output.
///
/// Motion checks (error unless noted; `entityId` = part id):
///   cam.sim.setup_mismatch         program face-up differs from the setup / invalid setup
///   cam.sim.position_unknown       XY move before the position is known, or first retract below safe Z
///   cam.sim.envelope               target outside the machine envelope
///   cam.sim.rapid_below_clearance  rapid below the clearance plane over the stock (pure upward retracts allowed)
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
/// Material checks (coarse heightmap of cell centres over the stock; flat-bottomed
/// cylindrical tool):
///   cam.sim.hole_not_cut / cam.sim.groove_not_cut   expected cells not cut to the floor
///   cam.sim.too_deep               cut inside an operation footprint below its floor
///   cam.sim.gouge                  material removed outside every operation footprint
///   cam.sim.unresolved (warning)   operation too small for the cell size
namespace fos::cam {

struct SimulationOptions {
    double cellSize = 0.5;        ///< mm; heightmap resolution
    double depthTolerance = 0.01; ///< mm
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
