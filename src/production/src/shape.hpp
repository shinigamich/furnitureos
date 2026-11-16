// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// Contoured-part quantities for the reports (ADR-0008). Internal, not public API.
#include "fos/domain/model.hpp"

#include <optional>
#include <vector>

namespace fos::production::detail {

/// Exact material area of a shaped part's FINISHED outline in mm2: |signedArea(outer)| minus
/// the |signedArea| of every cut-out (arcs exact). nullopt when the part has no shape or the
/// outer contour / a cut-out fails geometry::validateContour.
std::optional<double> outlineAreaMm2(const domain::Part& part);

/// Finished area in m2 used by every report: the exact outline area for shaped parts (0 when
/// the shape is invalid), length * width otherwise (0 unless both are positive finite).
double finishedAreaM2(const domain::Part& part);

/// One continuous strip of edge band along the finished outline of a shaped part: maximal
/// runs of consecutive outline segments carrying the same band whose junctions are tangent
/// (|sin| of the turning angle <= 1e-6). A sharp corner ends a strip, as on a rectangle where
/// each banded edge is its own strip. A fully banded, everywhere-tangent outline is ONE strip.
/// `length` is the exact length (arc lengths for arcs) of the segments in the strip.
struct BandStrip {
    domain::EdgeBandId band;
    double length = 0; ///< mm
};

/// Strips in outline order, starting at the first strip that begins at or after segment 0.
/// Empty for parts without shape or without `outerEdges` (or a wrong entry count).
std::vector<BandStrip> bandStrips(const domain::Part& part);

/// True when the part is produced by routing its outline (shape that is not a plain rectangle).
bool isRouted(const domain::Part& part) noexcept;

} // namespace fos::production::detail
