// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cam/setup.hpp"
#include "fos/geometry/vec.hpp"

#include <optional>
#include <string>

/// Face pockets (domain::Pocket on the upward face A/B) as CAM targets, derived from the DOMAIN only.
///
/// SUPPORTED SHAPE: a blind pocket whose contour is an axis-parallel rectangle in face coordinates
/// (4 distinct corners, consecutive vertices sharing u or v). Face u/v are axis-parallel to machine
/// X/Y for both A and B up, so the target is an axis-parallel machine rectangle.
///
/// CORNER RADIUS (precedence). A rotating end mill cannot cut sharp inner corners. The radius used
/// for a pocket's INNER corners is, in this order:
///  1. the operation's own domain::Pocket::cornerRadius when > 0 (the model wins; the caller's option
///     is ignored for that pocket), e.g. the sliding-door grip (6 mm);
///  2. otherwise (sharp pocket, cornerRadius == 0) the radius the caller accepts:
///     CamOptions::pocketCornerRadius / SimulationOptions::pocketCornerRadius (mm) — a fallback that
///     applies only to sharp pockets. With 0 there too, a pocket that has an inner corner is
///     unsupported (the reason names Pocket::cornerRadius and the option).
/// The cut pocket is the rectangle with its INNER corners rounded to that radius; tools are limited to
/// diameter <= 2 x radius. See effectivePocketCornerRadius().
///
/// OPEN SIDES (rebates / edge-open pockets): a rectangle side lying on the part boundary is OPEN when
/// the stock is exactly the part (Setup::blankAllowance == 0): the tool runs past it. A corner is an
/// INNER corner only when both adjacent sides are closed. Open sides on a blank with allowance, a
/// pocket open on all four sides, through pockets and every other contour are unsupported.
namespace fos::cam {

struct PocketTarget {
    domain::OperationId id;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0; ///< machine XY of the (sharp) domain rectangle, x0 < x1, y0 < y1
    /// Open sides: [0] x = x0, [1] x = x1, [2] y = y0, [3] y = y1.
    bool open[4] = {false, false, false, false};
    double cornerRadius = 0; ///< radius of the INNER corners, mm (machine frame); 0 if none
    bool radiusFromModel = false; ///< cornerRadius is the operation's Pocket::cornerRadius (not the option)
    double topZ = 0;
    double floorZ = 0;       ///< topZ - depth

    /// True when at least one corner has both adjacent sides closed.
    bool hasInnerCorner() const noexcept;
    /// Width of the narrowest closed span (X when both X sides are closed, likewise Y); +inf if none.
    double closedSpan() const noexcept;
    /// p lies in the sharp rectangle, open sides extended to infinity, grown by tol (material there
    /// may be removed).
    bool allows(geometry::Vec2 p, double tol) const noexcept;
    /// p lies in the rectangle with inner corners rounded to cornerRadius, shrunk by tol (material
    /// there must be removed down to the floor).
    bool expects(geometry::Vec2 p, double tol) const noexcept;
};

/// Inner corner radius used for `pocket` (precedence above): pocket.cornerRadius when > 0, else
/// `acceptedRadius` (the CamOptions / SimulationOptions::pocketCornerRadius fallback), mm.
double effectivePocketCornerRadius(const domain::Pocket& pocket, double acceptedRadius) noexcept;

/// Why a pocket operation cannot be milled in this setup, or nullopt when it can (shape rules above).
/// Precondition: `op` holds a domain::Pocket on frame.faceUp. `cornerRadius` (the caller's accepted
/// fallback radius for sharp pockets) must be >= 0.
std::optional<std::string> pocketUnsupportedReason(const domain::Part& part, const SetupFrame& frame,
                                                   const domain::Operation& op, double cornerRadius);

/// Machine-frame target. Precondition: pocketUnsupportedReason(...) returned nullopt.
PocketTarget pocketTarget(const domain::Part& part, const SetupFrame& frame, const domain::Operation& op,
                          double cornerRadius);

} // namespace fos::cam
