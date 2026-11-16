// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/drawings/annotation.hpp"
#include "fos/drawings/view.hpp"

#include <string>

/// Workshop drawing of a single part, seen perpendicular to face A (default) or B.
///
/// VIEW COORDINATES (part frame, see fos/domain/operation.hpp):
///   face A view (looking along -Z at z = T): u =  x, v =  y   (basis right +X, up +Y, toViewer +Z)
///   face B view (looking along +Z at z = 0): u =  x, v = -y   (basis right +X, up -Y, toViewer -Z)
/// so the part occupies [0, L] x [0, W] (face A) or [0, L] x [-W, 0] (face B); the
/// part origin is the view origin in both cases.
///
/// Drawing conventions:
///  * outline of the FINISHED part: visible lines;
///  * face holes: circle at the true position with a centre cross (Center style).
///    Holes on the viewed face are visible; BLIND holes on the opposite face are
///    hidden (dashed); THROUGH holes are always visible and additionally carry two
///    diagonal thin lines ("X") inside the circle;
///  * edge holes (faces L1/L2/W1/W2): hidden outline of the bore (diameter x depth)
///    entering from that edge, with a centre line;
///  * grooves on A/B: outline of the slot (visible on the viewed face, hidden on the
///    opposite one); grooves on an edge face: hidden band, `depth` deep, along that edge;
///  * pockets on A/B: closed contour (visible / hidden like holes; through = visible).
///    Pockets on edge faces are not drawn (diagnostic "drawings.part.unsupported_operation").
///
/// Contoured parts (Part::shape, ADR-0008): the exact FINISHED outline (domain::finishedContour)
/// is drawn with straight segments in View::segments and TRUE arcs in View::arcs (never
/// tessellated); cut-outs are through openings and are drawn with visible (solid) lines,
/// circular cut-outs as a circle with a centre cross. Edge-face operations of contoured parts
/// are still drawn against the bounding rectangle [0,L] x [0,W].
namespace fos::drawings {

enum class PartViewFace { A, B };

/// Words used in generated notes; replace for localisation. Default: English.
struct PartDrawingLabels {
    std::string material = "Material";
    std::string finishedSize = "Finished size";
    std::string cutSize = "Cut size";
    std::string quantity = "Qty";
    std::string grain = "Grain";
    std::string grainAlongLength = "along length";
    std::string grainAlongWidth = "along width";
    std::string edgeBand = "Edge band";
    std::string hole = "Hole";
    std::string through = "through";
    std::string deep = "deep";
    std::string face = "face";
    std::string unit = "mm";
    std::string missing = "not found in the model";
    std::string shapedPart = "shaped part (routed)"; ///< Appended to the cut-size note of contoured parts.
    std::string outlineSegments = "outline segments"; ///< Edge-band legend of contoured parts.
};

struct PartViewOptions {
    PartViewFace face = PartViewFace::A;
    /// Number of identical parts to make. Supplied by the caller (grouping identical
    /// parts is the job of the production module); printed in the title notes.
    int quantity = 1;
    bool holeDimensions = true; ///< Baseline dimensions of hole positions from the part origin.
    double titleHeight = 5.0;   ///< Paper mm.
    double noteHeight = 2.5;    ///< Paper mm.
    PartDrawingLabels labels;
};

/// Geometry of the part view. Id: "<part-id>/view.faceA" or ".faceB".
View generatePartView(const domain::Part& part, const PartViewOptions& options = {});

/// Annotations of a part view, laid out for paper scale `scale`:
///  * dimensions (horizontal below, vertical left; baseline dimensions from the part
///    origin for every distinct hole coordinate, smallest nearest to the part, then
///    the overall size):  "<view>/dim.hole.x[<operation-id>]", "<view>/dim.hole.y[<operation-id>]"
///    (first operation at that coordinate), "<view>/dim.overall.length", "<view>/dim.overall.width";
///  * edge-band marks "<view>/edge.<L1|L2|W1|W2>" with legend codes E1, E2, ... (one per edge band);
///  * grain arrow "<view>/grain" (when the part has a grain direction);
///  * hole labels "<view>/hole[<operation-id>].label" with codes H1, H2, ... (one per
///    distinct face + diameter + depth/through combination);
///  * notes stacked above the view, top to bottom: "<view>/title" (part name),
///    "<view>/note.material", "<view>/note.finished" (finished L x W x T and quantity),
///    "<view>/note.cutsize" (domain::cutSize), "<view>/note.grain",
///    "<view>/legend.edge[<edge-band-id>]", "<view>/legend.hole.H<n>".
/// Contoured parts additionally get:
///  * ordinate (baseline) dimensions of the cut-out positions, merged with the hole
///    coordinates (a coordinate already dimensioned keeps its first id):
///    circular cut-out k -> centre "<view>/dim.cutout.x[k]" / "<view>/dim.cutout.y[k]",
///    other cut-outs -> bounding box "<view>/dim.cutout.x[k.min]", "[k.max]" (same for y);
///  * RadiusDimension "R <r>" per outer arc "<view>/dim.radius[<segment>]", per arc of a
///    non-circular cut-out "<view>/dim.radius.cutout[<k>.<segment>]", and a diameter
///    "\u00D8 <d>" per circular cut-out "<view>/dim.diameter.cutout[<k>]" (values always from
///    the geometry);
///  * edge-band marks along the banded outer segments "<view>/edge.seg[<segment>]" (concentric
///    arc marks on arcs) instead of "<view>/edge.<L1|L2|W1|W2>"; the legend lists the number
///    of banded segments and their exact finished length;
///  * the cut-size note ends with ", " + labels.shapedPart when the outline is not a plain
///    rectangle (domain::isRectangularOutline).
ViewAnnotations annotatePartView(const domain::Model& model, const domain::Part& part, const View& view,
                                 double scale, const DimensionStyle& style, const PartViewOptions& options = {});

} // namespace fos::drawings
