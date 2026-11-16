// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/drawings/view.hpp"

#include <string>
#include <vector>

/// Exact hidden-line removal for boxes whose faces are parallel/perpendicular to the
/// view direction (view coordinates, model mm; larger depth = nearer to the viewer).
namespace fos::drawings {

struct HlrBox {
    Rect2 rect;           ///< Projection of the box.
    double depthNear = 0; ///< Depth of the face nearest to the viewer.
    double depthFar = 0;
    std::string sourceId;
    /// false: the box only OCCLUDES (contributes no edges). Used for contoured face-on parts,
    /// whose exact outline is supplied as HlrEdge/HlrArc and whose bounding box is the occluder.
    bool drawEdges = true;
};

/// A straight edge at constant depth that is hidden by boxes like box edges and merged with
/// them (e.g. a straight piece of a contoured outline seen face-on).
struct HlrEdge {
    Vec2 a, b;
    double depth = 0;
    std::string sourceId;
};

/// A circular arc at constant depth (view coordinates; counter-clockwise from startDeg to
/// endDeg as ViewArc) that is hidden by boxes.
struct HlrArc {
    Vec2 center;
    double radius = 0;
    double startDeg = 0, endDeg = 90;
    double depth = 0;
    std::string sourceId;
};

/// An extra edge that is always drawn visible and hides nothing (fallback geometry).
struct HlrLooseEdge {
    Vec2 a, b;
    double depth = 0; ///< Used only to pick the source of coincident pieces.
    std::string sourceId;
};

/// Rule: a point of an edge at depth d is HIDDEN when it lies strictly inside
/// (deeper than core::kLengthEps) the projected rectangle of a box whose near face is
/// nearer than d by more than core::kLengthEps. Each box contributes the 4 edges of
/// its near face and the 4 edges of its far face (the 4 edges parallel to the view
/// direction project to points). Edges are split into visible and hidden intervals.
///
/// Merging: pieces on the same line are cut at all piece end points; on each
/// elementary interval a visible piece wins over hidden ones and the nearest piece
/// (then the earliest box) provides the sourceId; neighbouring intervals with the same
/// style and source are joined. Output is ordered by line, then along the line.
std::vector<ViewSegment> removeHiddenLines(const std::vector<HlrBox>& boxes,
                                           const std::vector<HlrLooseEdge>& looseEdges = {},
                                           HiddenLineMode mode = HiddenLineMode::Dashed);

/// Same, with additional hideable straight edges (classified with the same rule as box edges
/// and merged with them; their order for source ties is after all boxes, before loose edges).
std::vector<ViewSegment> removeHiddenLines(const std::vector<HlrBox>& boxes, const std::vector<HlrEdge>& edges,
                                           const std::vector<HlrLooseEdge>& looseEdges, HiddenLineMode mode);

/// Splits arcs into visible and hidden sub-arcs with the same rule (a point is hidden when it
/// lies strictly inside, by more than core::kLengthEps, the rectangle of a box whose near face
/// is nearer by more than core::kLengthEps). Exact: the split angles are the intersections of
/// the circle with the rectangle sides. Hidden pieces are dashed or omitted per `mode`.
/// Coincident arcs are not merged. Output in input order, along each arc.
std::vector<ViewArc> removeHiddenArcs(const std::vector<HlrBox>& boxes, const std::vector<HlrArc>& arcs,
                                      HiddenLineMode mode = HiddenLineMode::Dashed);

} // namespace fos::drawings
