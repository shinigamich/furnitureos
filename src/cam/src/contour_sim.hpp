// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: material checks of contour routing in the simulator (see simulate.hpp).
#include "heightmap.hpp"

#include "fos/cam/contour.hpp"
#include "fos/cam/simulate.hpp"

#include <map>
#include <string>
#include <vector>

namespace fos::cam::detail {

class ContourChecker {
public:
    /// Derives the targets from the domain (contourTargets). Problems are reported as
    /// cam.sim.setup_mismatch errors; then no contour is expected.
    ContourChecker(const domain::Part& part, const SetupFrame& frame, const SimulationOptions& options,
                   core::Diagnostics& diags);
    const std::vector<ContourTarget>& targets() const noexcept { return targets_; }
    /// The contour whose WASTE side contains p (inside a cut-out, or outside the raw outline;
    /// points within contourTolerance of a contour count as waste), or nullptr when p lies in the
    /// finished part area.
    const ContourTarget* wasteAt(geometry::Vec2 p) const;
    /// Verifies that every contour is cut through along its whole length. `toolDiameter` maps an
    /// operation id to the diameter of the tool its cutting motions used (missing = never cut).
    void checkCut(const Heightmap& hm, const std::map<std::string, double>& toolDiameter, double bottomZ,
                  core::Diagnostics& diags) const;

private:
    const domain::Part& part_;
    SimulationOptions opt_;
    std::vector<ContourTarget> targets_;
};

} // namespace fos::cam::detail
