// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/geometry/contour.hpp"
#include "fos/geometry/mesh.hpp"

#include <vector>

/// Display meshes of CONTOURED parts (ADR-0008): the finished outline extruded along the part
/// thickness with its through cut-outs. Presentation geometry only, in the PART frame (mm):
/// outline in the XY plane, z from 0 (face B) to thickness (face A).
namespace fos::viewport {

/// Upper bound of the chord tolerance used for display meshes (mm).
inline constexpr double kMaxShapeChordTolerance = 0.2;

/// Chord tolerance used by the Scene for a part of finished size L x W:
///   clamp(max(L, W) / 3000, 0.01, 0.2) mm
/// i.e. at most 0.2 mm, and at most 1/3000 of the part size, so the deviation of every arc
/// from its chords stays below one pixel even when the part fills a 3000-pixel-wide view.
double shapeChordTolerance(double length, double width) noexcept;

struct ShapedPartGeometry {
    /// Closed, outward-oriented, flat-shaded mesh: bottom cap (normal -Z), top cap (+Z),
    /// then one quad (2 triangles, outward normal) per polygon edge of the outline and of
    /// every cut-out. Watertight (geometry::isWatertight).
    geometry::TriangleMesh mesh;
    /// Feature edges: the tessellated outline and cut-outs at z = 0 and z = thickness, plus a
    /// vertical edge at every contour vertex where the tangent turns by more than 1 degree
    /// (no lines along smooth arcs or tangent line/arc junctions).
    std::vector<geometry::Edge3> edges;
};

/// Extrusion of `outer` minus `cutouts` (exact contours, tessellated with geometry::tessellate
/// at `chordTolerance`, so the polygon lies inside convex arcs by at most the tolerance).
/// Errors: invalid thickness/tolerance, or triangulation failure (message names the cause).
core::Result<ShapedPartGeometry> makeShapedPartGeometry(const geometry::Contour& outer,
                                                        const std::vector<geometry::Contour>& cutouts,
                                                        double thickness, double chordTolerance);

} // namespace fos::viewport
