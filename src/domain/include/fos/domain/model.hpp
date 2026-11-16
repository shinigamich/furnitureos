// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/hardware.hpp"
#include "fos/domain/material.hpp"
#include "fos/domain/part.hpp"

#include <string>
#include <vector>

namespace fos::domain {

/// A resolved furniture item: concrete parts and fittings in one assembly frame.
///
/// ASSEMBLY FRAME: right-handed, Z up. X = width (left -> right seen from the front),
/// Y = depth (front -> back, the front of the furniture is at y = 0), Z = height.
/// Origin: left-front-bottom corner of the nominal envelope.
struct Assembly {
    AssemblyId id;
    std::string name;
    geometry::Transform placement; ///< Assembly frame -> project (room) frame.
    std::vector<Part> parts;
    std::vector<HardwareInstance> hardware;

    /// Bounding box of all parts in the assembly frame.
    geometry::Aabb bounds() const;
    const Part* findPart(const PartId& id) const;
    Part* findPart(const PartId& id);
};

/// The resolved engineering model of a project: the single input for drawings,
/// production documents, cutting, machining and validation. It is DERIVED data:
/// it is regenerated from the parametric definitions (module `furniture`) and is
/// never edited directly by downstream modules.
struct Model {
    std::vector<Material> materials;
    std::vector<EdgeBand> edgeBands;
    std::vector<HardwareDefinition> hardwareCatalog;
    std::vector<Assembly> assemblies;

    const Material* findMaterial(const MaterialId& id) const;
    const EdgeBand* findEdgeBand(const EdgeBandId& id) const;
    const HardwareDefinition* findHardware(const HardwareId& id) const;
    const Assembly* findAssembly(const AssemblyId& id) const;
};

/// Saw size of a part before edge banding is applied.
struct CutSize {
    double length = 0;
    double width = 0;
};

/// Thickness of the edge band on edge `s`, 0 when the edge is raw. An edge that
/// references an edge band missing from the model also yields 0 (validation reports it).
double edgeBandThickness(const Part& part, EdgeSide s, const Model& model) noexcept;

/// THE definition of cut size (ADR-0004, generalised by ADR-0008):
///   rectangular part: cut.length = length - thickness(W1) - thickness(W2)
///                     cut.width  = width  - thickness(L1) - thickness(L2)
///   contoured part:   size of the exact bounding box of cutContour()
/// For a contoured part whose raw contour cannot be computed (invalid shape) the finished size
/// is returned; validation reports the shape problem.
CutSize cutSize(const Part& part, const Model& model) noexcept;

/// True when the part has a shape (ADR-0008). Such a part is cut from a rectangular blank and
/// its outline is routed; it goes to nesting rather than to guillotine cutting when the outline
/// is not a plain rectangle (see isRectangularOutline()).
bool hasShape(const Part& part) noexcept;
/// True for parts without a shape, and for shaped parts whose outer contour is an axis-aligned
/// rectangle covering [0,L]x[0,W] with straight segments only and no cut-outs.
bool isRectangularOutline(const Part& part) noexcept;

/// The finished outline in the part frame: the shape's outer contour, or the rectangle
/// [0,L]x[0,W] (counter-clockwise from (0,0): segments L1, W2, L2, W1).
geometry::Contour finishedContour(const Part& part);

/// Edge band thickness on outer segment `i` of finishedContour() (0 = raw or unknown band).
/// For rectangular parts segment 0..3 map to L1, W2, L2, W1.
double contourBandThickness(const Part& part, std::size_t segment, const Model& model) noexcept;

/// THE definition of the raw (pre-edge-banding) outline: finishedContour() offset inwards by
/// each segment's band thickness (lines stay lines, arcs keep their centre). For rectangular
/// parts this is exactly the rectangle [t(W1), L - t(W2)] x [t(L1), W - t(L2)].
core::Result<geometry::Contour> cutContour(const Part& part, const Model& model);

/// THE grain/rotation rule for laying a part on a stock sheet (used by `cutting` and
/// `production`; nothing else may re-derive it). The material grain, when present, runs
/// along Material::sheetLength.
///
/// Rotation by 90 degrees on the sheet is allowed iff
///   part.grain == GrainDirection::None, or
///   `material` is known (non-null) and material->hasGrain == false.
/// A grained part whose material is MISSING (nullptr) is NOT rotatable: without the
/// material the grain cannot be ruled out, so the part's own grain is respected.
bool canRotateOnSheet(const Part& part, const Material* material) noexcept;

/// How the part's LENGTH (local X) must lie relative to the sheet length (the grain).
enum class SheetAlignment {
    Free,             ///< canRotateOnSheet(): either orientation.
    LengthAlongSheet, ///< Part grain AlongLength on a grained (or missing) material.
    LengthAcrossSheet ///< Part grain AlongWidth: the part WIDTH must run along the sheet length.
};

/// Free when canRotateOnSheet(part, material); otherwise LengthAlongSheet for
/// GrainDirection::AlongLength and LengthAcrossSheet for GrainDirection::AlongWidth.
SheetAlignment sheetAlignment(const Part& part, const Material* material) noexcept;

} // namespace fos::domain
