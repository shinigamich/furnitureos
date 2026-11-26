// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/domain/model.hpp"
#include "fos/drawings/sheet.hpp"
#include "fos/drawings/view.hpp"

#include <optional>
#include <string>
#include <vector>

/// Pictorial (isometric and exploded isometric) assembly views.
///
/// PROJECTION: isometric DRAWING per ISO 5456-3 - the three assembly axes are drawn at
/// 120 degrees to each other with TRUE length along each axis (the orthographic isometric
/// projection enlarged by sqrt(3/2) ~ 1.2247). View coordinates, model mm at 1:1:
///
///   FrontLeft  (viewer front-left-above):  u = (x - y) cos30,   v = (x + y) sin30 + z
///              X runs up-right at +30 deg, Y (depth) up-left at 150 deg, Z up.
///   FrontRight (viewer front-right-above): u = (x + y) cos30,   v = (y - x) sin30 + z
///              X runs down-right at -30 deg, Y up-right at +30 deg, Z up.
///
/// The front (y = 0), the top and the left (FrontLeft) or right (FrontRight) faces are seen.
///
/// HIDDEN LINES are omitted (pictorial convention); only visible pieces are produced. No
/// dimensions are generated for pictorial views: lengths along the axes are true, but any
/// other measurement on the picture is distorted, so dimensions belong on the orthographic
/// views. Hardware and machining are not drawn.
///
/// VISIBILITY METHOD (painter's ordering applied in object space):
///  * Every part is treated, for occlusion, as its bounding box [0,L]x[0,W]x[0,T] in the part
///    frame (a convex box, also when rotated). For every pair of parts whose projections can
///    overlap, the front-to-back order is the painter's order given by a separating plane
///    (separating-axis test over the 3 + 3 face normals and 9 edge cross products): the box on
///    the viewer's side of the plane is in front. For disjoint convex boxes this is exact, so
///    there are no cyclic-overlap failures (unlike sorting by a single depth value).
///  * Instead of filling polygons, every edge of a part is CLIPPED against the projected convex
///    silhouette (hexagon) of each part in front of it; pieces strictly inside are dropped.
///  * Self-occlusion: a part's own far-face edges and side-wall edges are kept only where an
///    adjacent face looks towards the viewer (exact for convex outlines).
///  * Interpenetrating boxes (no separating plane, e.g. a back panel modelled inside a groove)
///    are ordered by the axis of least penetration - APPROXIMATION, reported once per view with
///    the Info "drawings.pictorial.overlap".
///  * Contoured parts (ADR-0008) are drawn as their EXTRUDED finished outline (arcs tessellated
///    to `chordTolerance`, so they appear as polylines of short Lines, not as ellipses), with
///    connecting edges at outline corners and at the silhouette points of arcs; cut-outs are
///    drawn on the face towards the viewer only. As OCCLUDERS they count as their bounding box
///    (APPROXIMATION: nothing is seen through cut-outs or past rounded corners), and for
///    concave outlines a far-face edge seen through a notch of the same part may be drawn.
namespace fos::drawings {

enum class PictorialKind { Isometric, Exploded };
enum class IsoDirection { FrontLeft, FrontRight };

/// "iso" / "exploded".
const char* toString(PictorialKind k) noexcept;

/// The isometric drawing projection above (view coordinates, model mm).
Vec2 isoProject(IsoDirection d, Vec3 p) noexcept;
/// Unit vector from the model towards the viewer; larger dot(p, toViewer) = nearer.
Vec3 isoToViewer(IsoDirection d) noexcept;

/// Explode direction of a part from its role (unit assembly axis, or zero = stays):
///   Side            -X when its bounding-box centre is left of the centre of `assemblyBounds`,
///                   +X when right of it, zero when centred (within 1e-6 mm);
///   Top, Worktop    +Z;            Bottom, Plinth   -Z;
///   Back            +Y (backwards, away from the carcass: the front is at y = 0);
///   Door, DrawerFront, DrawerSide, DrawerBack, DrawerBottom   -Y (forwards; drawer boxes
///                   travel with their fronts);
///   Shelf, Partition, Rail, Generic   zero.
Vec3 explodeDirection(const domain::Part& part, const geometry::Aabb& assemblyBounds) noexcept;

struct PictorialViewOptions {
    PictorialKind kind = PictorialKind::Isometric;
    IsoDirection direction = IsoDirection::FrontLeft;
    /// Exploded views: a moving part is translated along explodeDirection() by
    /// (explodeFactor - 1) x the assembly bounding-box size along that axis
    /// (1 = not exploded; 1.5 = sides move out by half the assembly width each).
    /// Must be finite and within [1, 10]. Ignored for Isometric.
    double explodeFactor = 1.5;
    /// Part-list number of the first part of the assembly (balloons number the parts
    /// consecutively in Assembly::parts order). See firstPartListNumber().
    int firstPartNumber = 1;
    /// Max. distance (mm) between a tessellated arc and the true arc. Must be > 0.
    double chordTolerance = 0.5;
    /// View id; empty = "<assembly-id>/view.iso" or "<assembly-id>/view.exploded".
    std::string id;
};

/// One part as it appears in a pictorial view.
struct PictorialPart {
    domain::PartId partId;
    std::string name;
    domain::PartRole role = domain::PartRole::Generic;
    int number = 0;     ///< Part-list number (balloon text).
    Vec3 offset{};      ///< Explode translation in the assembly frame (zero for Isometric).
    std::vector<Vec2> silhouette; ///< Convex hull (counter-clockwise) of the projected bounding box.
    /// Midpoint of the longest visible segment of the part; empty when nothing of it is visible.
    std::optional<Vec2> balloonAnchor;
};

struct PictorialView {
    std::string id;
    PictorialKind kind = PictorialKind::Isometric;
    IsoDirection direction = IsoDirection::FrontLeft;
    double explodeFactor = 1.0; ///< 1 for Isometric.
    std::vector<ViewSegment> segments; ///< Visible pieces only; part order, then edge order.
    std::vector<PictorialPart> parts;  ///< Assembly part order (parts with a non-positive size omitted).
    Rect2 bounds;                      ///< Union of the part silhouettes (view coordinates).
    core::Diagnostics diagnostics;
};

/// Part-list number of the first part of `assemblyId`: the same rule as
/// production::PartRow::number (1-based over the WHOLE model, assemblies in Model order,
/// parts in Assembly::parts order, bought-in parts included). 0 when the assembly is missing.
int firstPartListNumber(const domain::Model& model, const domain::AssemblyId& assemblyId) noexcept;

/// Generates the view. Fails (InvalidArgument) for an explode factor outside [1, 10] or a
/// non-positive chord tolerance, (NotFound) when no part has a positive size.
/// Parts with a non-positive size are skipped with the Warning "drawings.pictorial.empty_part".
core::Result<PictorialView> generatePictorialView(const domain::Assembly& assembly,
                                                  const PictorialViewOptions& options = {});

/// Paper sizes (mm) of part balloons.
struct BalloonStyle {
    double radius = 4.0;     ///< Circle radius.
    double textHeight = 3.5; ///< Number text height.
    double clearance = 8.0;  ///< Distance from the geometry box to the nearest balloon edge.
    double gap = 1.0;        ///< Minimum free space between two balloons.
    double dotSize = 1.0;    ///< Diameter of the dot at the leader's start (on the part).
};

struct PictorialRenderOptions {
    bool balloons = false;
    BalloonStyle balloon;
};

/// Paper-space primitives: visible part edges (LineStyle::Visible, sourceId = part id), then,
/// with `balloons`, per part in number order: leader Line + Dot marker (Dimension style),
/// Circle and the number Text, all with elementId "<view-id>/balloon[<part-id>]".
/// Balloon layout (HEURISTIC, deterministic): each balloon lies on the ray from the centre of
/// the geometry box through its anchor, `clearance` outside the box; when it would come closer
/// than `gap` to an earlier balloon it moves outwards along the ray in steps of 2 r + gap
/// (at most 64 steps). Leaders may cross part edges; balloons are not checked against text.
/// Parts without a visible anchor get no balloon (Warning "drawings.pictorial.balloon_hidden"
/// is added by generatePictorialDrawing).
std::vector<Primitive> renderPictorialView(const PictorialView& view, const ViewPlacement& placement,
                                           const PictorialRenderOptions& options = {});

/// Model-space primitives at 1:1 (placement origin (0, 0), scale 1) with balloon sizes multiplied
/// by `annotationScale`; write them with toDxf(..., DxfOptions{.linetypeScale = annotationScale}).
std::vector<Primitive> pictorialModelSpacePrimitives(const PictorialView& view,
                                                     const PictorialRenderOptions& options = {},
                                                     double annotationScale = 10.0);

struct PictorialDrawingOptions {
    SheetOptions sheet; ///< sheet.title.title empty -> the assembly name is used.
    PictorialViewOptions view; ///< view.firstPartNumber is replaced by firstPartListNumber().
    PictorialRenderOptions render;
};

struct PictorialDrawingResult {
    Sheet sheet;
    PictorialView view;
    core::Diagnostics diagnostics; ///< View diagnostics, then balloon warnings.
};

/// One sheet with the pictorial view centred in the drawing area. Scale selection as
/// generateAssemblyDrawing(): explicit (fails with ConstraintViolation when it does not fit)
/// or fit-to-page over standardScales(). Fails with NotFound for a missing assembly.
core::Result<PictorialDrawingResult> generatePictorialDrawing(const domain::Model& model,
                                                              const domain::AssemblyId& assemblyId,
                                                              const PictorialDrawingOptions& options = {});

} // namespace fos::drawings
