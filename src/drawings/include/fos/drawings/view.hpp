// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/domain/model.hpp"
#include "fos/drawings/primitives.hpp"

#include <optional>
#include <string>
#include <vector>

/// Orthographic views of an assembly in MODEL scale.
///
/// VIEW COORDINATES: a point p of the assembly frame maps to
///     u = dot(p, basis.right),  v = dot(p, basis.up),  depth = dot(p, basis.toViewer)
/// in millimetres at 1:1. A larger depth is nearer to the viewer. The bases are
/// right-handed (right x up = toViewer):
///
///   view    looks along   right   up    toViewer   (u, v)
///   Front   +Y            +X      +Z    -Y         ( x,  z)
///   Back    -Y            -X      +Z    +Y         (-x,  z)
///   Left    +X            -Y      +Z    -X         (-y,  z)
///   Right   -X            +Y      +Z    +X         ( y,  z)
///   Top     -Z            +X      +Y    +Z         ( x,  y)   front of the furniture at the bottom
///   Bottom  +Z            +X      -Y    -Z         ( x, -y)
///
/// View coordinates are NOT shifted to start at zero: e.g. the Left view of a 563 mm
/// deep cabinet spans u in [-563, 0].
namespace fos::drawings {

enum class ViewKind { Front, Back, Left, Right, Top, Bottom };

struct ViewBasis {
    Vec3 right, up, toViewer;
};

/// The table above.
ViewBasis viewBasis(ViewKind k) noexcept;
/// Lower-case name used in ids: "front", "back", "left", "right", "top", "bottom".
const char* toString(ViewKind k) noexcept;

inline Vec2 projectPoint(const ViewBasis& b, Vec3 p) noexcept { return {dot(p, b.right), dot(p, b.up)}; }
inline double depthOf(const ViewBasis& b, Vec3 p) noexcept { return dot(p, b.toViewer); }

/// Placement of a view on paper: paper = origin + viewPoint * scale.
struct ViewPlacement {
    Vec2 origin{};      ///< Paper position (mm) of the view point (0, 0).
    double scale = 1.0; ///< Paper mm per model mm (0.1 for 1:10).
    Vec2 toPaper(Vec2 viewPoint) const noexcept { return origin + viewPoint * scale; }
    Vec2 toView(Vec2 paperPoint) const noexcept { return (paperPoint - origin) * (1.0 / scale); }
};

/// Straight piece of view geometry (view coordinates, model mm).
struct ViewSegment {
    Vec2 a, b;
    LineStyle style = LineStyle::Visible;
    std::string sourceId; ///< Part id (assembly views) or operation/part id (part views).
};

struct ViewCircle {
    Vec2 center;
    double radius = 0;
    LineStyle style = LineStyle::Visible;
    std::string sourceId;
};

/// Cross-section of a part in the cutting plane of a section view. Hatch lines are
/// generated when the view is rendered (spacing is a paper quantity).
struct CutRegion {
    Rect2 rect;
    double hatchAngleDeg = 45.0; ///< Alternates 45 / 135 between consecutive cut parts.
    std::string sourceId;
};

/// How a panel appears in a view.
enum class PanelAspect {
    FaceOn,           ///< Thickness along the view direction: seen as a full rectangle.
    EdgeOnVertical,   ///< Thickness along view `right`: seen as a thin vertical strip.
    EdgeOnHorizontal, ///< Thickness along view `up`: seen as a thin horizontal strip.
    Oblique           ///< Not axis-aligned.
};

/// Projected extent of one part; input of automatic dimensioning and of UI picking.
struct PartFootprint {
    std::string partId;
    domain::PartRole role = domain::PartRole::Generic;
    Rect2 rect;           ///< Extent in view coordinates (after section clipping).
    double depthNear = 0; ///< Largest depth (nearest to the viewer; the cutting plane for cut parts).
    double depthFar = 0;
    PanelAspect aspect = PanelAspect::FaceOn;
    bool cut = false; ///< Intersected by the section plane.
};

enum class HiddenLineMode { Dashed, Omit };
enum class ViewSubject { Assembly, Part };

struct ViewOptions {
    HiddenLineMode hiddenLines = HiddenLineMode::Dashed;
    /// Section view: coordinate of the cutting plane along the assembly axis the view
    /// looks along (Front/Back: y, Left/Right: x, Top/Bottom: z). Everything on the
    /// viewer's side of the plane is removed. Vertical plane = Front/Back/Left/Right,
    /// horizontal plane = Top/Bottom.
    std::optional<double> sectionPosition;
    /// View id; empty = "<assembly-id>/view.<kind>" or "<assembly-id>/section.<kind>".
    std::string id;
};

struct View {
    std::string id;
    ViewSubject subject = ViewSubject::Assembly;
    ViewKind kind = ViewKind::Front; ///< For part views: Top (looking at the drawn face).
    ViewBasis basis;                 ///< In the assembly frame (assembly views) or part frame (part views).
    std::optional<double> sectionPosition;
    std::vector<ViewSegment> segments; ///< Deterministic order; coincident pieces already merged.
    std::vector<ViewCircle> circles;
    std::vector<CutRegion> cuts;
    std::vector<PartFootprint> footprints; ///< In assembly part order (removed parts omitted).
    Rect2 bounds;                          ///< Extent of all remaining parts in view coordinates.
    core::Diagnostics diagnostics;
};

/// True when every axis of the placement is parallel to an assembly axis (1e-9).
bool isAxisAligned(const geometry::Transform& placement) noexcept;

/// Generates a view (or, with `sectionPosition`, a section view) with hidden-line
/// removal. Parts whose placement is not axis-aligned are drawn with all 12 edges
/// visible, do not hide anything, are not cut by a section plane, and are reported
/// with code "drawings.hlr.not_axis_aligned" in View::diagnostics.
View generateAssemblyView(const domain::Assembly& assembly, ViewKind kind, const ViewOptions& options = {});

} // namespace fos::drawings
