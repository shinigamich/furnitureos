// SPDX-License-Identifier: AGPL-3.0-only
#include "contour_sim.hpp"

#include "arc_math.hpp"

#include "fos/core/units.hpp"

#include <algorithm>
#include <cmath>

namespace fos::cam::detail {

using geometry::Vec2;

namespace {
std::string mm(double v) { return core::formatLength(v, 3); }
} // namespace

ContourChecker::ContourChecker(const domain::Part& part, const SetupFrame& frame, const SimulationOptions& options,
                               core::Diagnostics& diags)
    : part_(part), opt_(options) {
    if (!options.expectContours) return;
    auto t = contourTargets(part, frame, options.model);
    if (!t) {
        diags.push_back({core::Severity::Error, "cam.sim.setup_mismatch",
                         "Contour routing of '" + part.name + "' cannot be verified: " + t.error().message,
                         part.id.str()});
        return;
    }
    targets_ = std::move(t).value();
}

const ContourTarget* ContourChecker::wasteAt(Vec2 p) const {
    for (const auto& t : targets_) {
        const geometry::PointLocation loc = geometry::locatePoint(t.contour, p, opt_.contourTolerance);
        if (t.outer ? loc != geometry::PointLocation::Inside : loc != geometry::PointLocation::Outside) return &t;
    }
    return nullptr;
}

void ContourChecker::checkCut(const Heightmap& hm, const std::map<std::string, double>& toolDiameter, double bottomZ,
                              core::Diagnostics& diags) const {
    const std::string pid = part_.id.str();
    const int nx = hm.columns(), ny = hm.rows();
    for (const auto& t : targets_) {
        const std::string what = (t.outer ? std::string("The outline") : "Cut-out " + std::to_string(t.cutoutIndex)) +
                                 " ('" + t.id.str() + "') of '" + part_.name + "'";
        const auto it = toolDiameter.find(t.id.str());
        if (it == toolDiameter.end()) {
            diags.push_back({core::Severity::Error, "cam.sim.contour_not_cut",
                             what + " is not routed: the program has no cutting motion for it.", pid});
            continue;
        }
        const double r = it->second / 2;
        // Reach of a tool of this radius: within r of the ideal compensated path.
        auto ideal = geometry::offsetContour(t.contour, t.outer ? r : -r);
        if (!ideal) {
            diags.push_back({core::Severity::Error, "cam.sim.contour_not_cut",
                             what + " cannot be routed with a " + mm(2 * r) + " mm tool: " + ideal.error().message,
                             pid});
            continue;
        }
        const double tol = opt_.contourTolerance;
        std::vector<char> seen(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny), 0);
        std::size_t n = 0, shallow = 0;
        double shallowest = -1e300;
        Vec2 first{};
        for (std::size_t s = 0; s < t.contour.size(); ++s) {
            const geometry::ContourSegmentInfo si = geometry::segmentInfo(t.contour, s);
            double x0, y0, x1, y1;
            if (si.isArc) {
                arcBounds(segmentArc(t.contour, s), x0, y0, x1, y1);
            } else {
                x0 = std::min(si.start.x, si.end.x);
                x1 = std::max(si.start.x, si.end.x);
                y0 = std::min(si.start.y, si.end.y);
                y1 = std::max(si.start.y, si.end.y);
            }
            int i0, i1, j0, j1;
            hm.range(x0 - r, x1 + r, true, i0, i1);
            hm.range(y0 - r, y1 + r, false, j0, j1);
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i) {
                    char& v = seen[static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) +
                                   static_cast<std::size_t>(i)];
                    if (v) continue;
                    v = 1;
                    const Vec2 c = hm.centre(i, j);
                    const geometry::PointLocation loc = geometry::locatePoint(t.contour, c, tol);
                    const bool waste = t.outer ? loc == geometry::PointLocation::Outside
                                               : loc == geometry::PointLocation::Inside;
                    if (!waste || distanceToContour(c, t.contour) > r - tol ||
                        distanceToContour(c, ideal.value()) > r - tol)
                        continue;
                    ++n;
                    const double z = std::min(hm.minZ(i, j), hm.topZ());
                    if (z > bottomZ + opt_.depthTolerance) {
                        if (shallow++ == 0) first = c;
                        shallowest = std::max(shallowest, z);
                    }
                }
        }
        if (n == 0) {
            diags.push_back({core::Severity::Warning, "cam.sim.unresolved",
                             what + " is too small for the simulation cell size " + mm(opt_.cellSize) +
                                 " mm and was not verified.", pid});
        } else if (shallow > 0) {
            diags.push_back({core::Severity::Error, "cam.sim.contour_not_cut",
                             what + " is not cut through to the stock bottom Z " + mm(bottomZ) + " mm along its whole "
                             "length: " + std::to_string(shallow) + " of " + std::to_string(n) +
                                 " cells within one tool radius on its waste side are shallower (first at machine X " +
                                 mm(first.x) + ", Y " + mm(first.y) + "; shallowest at Z " + mm(shallowest) + " mm" +
                                 (shallowest >= hm.topZ() - 1e-6 ? ", i.e. untouched" : "") + ").",
                             pid});
        }
    }
}

} // namespace fos::cam::detail
