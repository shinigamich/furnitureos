// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Parametric outline of a generated part (ADR-0008), as plain serialisable data. A
/// ContourSpec is attached to a cabinet part slot (PartOverride::contour) or to a free panel
/// (PanelDefinition::contour); generation turns it into the part's domain::PartShape
/// (contour.hpp, README "Contoured parts").
///
/// FRAME: every coordinate is in the PART frame (mm): x along the part's length (0..L), y along
/// its width (0..W), the outline is the FINISHED outline (edge banding included).
///
/// EXPRESSIONS: every length is an expression text over the owner's parameter graph (the
/// cabinet's, or the panel's own) plus three read-only names of the part being shaped:
/// `contour.length` (L), `contour.width` (W) and `contour.thickness` (T). These names take
/// precedence over graph parameters of the same name. An empty text means 0 where noted.
#include "fos/domain/part.hpp"
#include "fos/furniture/enums.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace fos::furniture {

/// One vertex of a Custom outline. Segment i runs from vertex i to vertex i + 1 (closed).
struct ContourVertexSpec {
    std::string x; ///< Expression, mm (part frame).
    std::string y; ///< Expression, mm (part frame).
    /// Bulge of the segment starting here (geometry/contour.hpp convention: tan(theta / 4),
    /// 0 = straight, > 0 = counter-clockwise arc). A plain number, finite, |bulge| <= 1.
    double bulge = 0.0;
    /// Edge band of the segment starting here: taken from this part edge. When unset, a
    /// straight segment lying on a side of the box [0,L]x[0,W] takes that side's band and
    /// every other segment is raw.
    std::optional<domain::EdgeSide> bandFrom;
    bool operator==(const ContourVertexSpec&) const = default;
};

/// A through opening inside the outline (never edge-banded).
struct CutoutSpec {
    CutoutKind kind = CutoutKind::Rectangle;
    /// Rectangle: minimum corner; Circle: centre. Expressions, mm (part frame).
    std::string x, y;
    std::string length; ///< Rectangle: size along x (expression, mm, > 0).
    std::string width;  ///< Rectangle: size along y (expression, mm, > 0).
    /// Rectangle: corner radius (expression, mm, >= 0; empty = 0, sharp corners).
    /// Circle: radius (expression, mm, > 0).
    std::string radius;
    bool operator==(const CutoutSpec&) const = default;
};

/// Parametric outline of a part. Only the fields of the selected `kind` are used.
struct ContourSpec {
    ContourKind kind = ContourKind::Rectangle;
    /// RoundedCorners: radius expressions (mm, >= 0; empty = 0 = sharp) of the corners
    /// (0,0), (L,0), (L,W), (0,W), i.e. L1/W1, L1/W2, L2/W2, L2/W1.
    std::array<std::string, 4> radii{};
    /// ArcEdge: the edge replaced by an arc, its rise (expression, mm, > 0, at most half the
    /// edge length and less than the part's other dimension) and its direction. The part keeps
    /// its size: a Convex arc is cut INTO the box (its apex lies on the original edge line, its
    /// ends are moved inwards by the rise), a Concave arc runs between the original corners.
    domain::EdgeSide arcEdge = domain::EdgeSide::L2;
    std::string arcRise;
    ArcDirection arcDirection = ArcDirection::Convex;
    /// Custom: counter-clockwise vertices; the outline's exact bounding box must be [0,L]x[0,W].
    std::vector<ContourVertexSpec> vertices;
    /// Through cut-outs (any kind, also Rectangle).
    std::vector<CutoutSpec> cutouts;
    bool operator==(const ContourSpec&) const = default;
};

} // namespace fos::furniture
