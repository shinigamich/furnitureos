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

/// THE definition of cut size (ADR-0004):
///   cut.length = length - thickness(W1) - thickness(W2)
///   cut.width  = width  - thickness(L1) - thickness(L2)
CutSize cutSize(const Part& part, const Model& model) noexcept;

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
