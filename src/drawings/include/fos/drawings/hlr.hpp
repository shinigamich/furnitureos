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

} // namespace fos::drawings
