// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Free-standing parametric panel: ONE part whose size and position are parameters
/// (constants or expressions), for custom assemblies (worktop, filler, loose shelf).
#include "fos/core/diagnostic.hpp"
#include "fos/furniture/definition.hpp"

namespace fos::furniture {

/// Standard parameter names of a PanelDefinition (mm): `length`, `width` are the FINISHED
/// part dimensions; `x`, `y`, `z` the MINIMUM corner of the panel's box in the assembly frame.
namespace panel_param {
inline constexpr const char* kLength = "length";
inline constexpr const char* kWidth = "width";
inline constexpr const char* kX = "x";
inline constexpr const char* kY = "y";
inline constexpr const char* kZ = "z";
} // namespace panel_param

struct PanelDefinition {
    domain::PartId id;
    std::string name;
    domain::PartRole role = domain::PartRole::Generic;
    PanelOrientation orientation = PanelOrientation::Horizontal;
    domain::MaterialId material; ///< Thickness is taken from the material.
    std::array<std::optional<domain::EdgeBandId>, 4> edges{}; ///< Indexed by domain::EdgeSide.
    /// Holds at least the five standard parameters; custom parameters may be added and the
    /// standard ones may be expressions (e.g. length = "run - 2 * overhang").
    parametric::ParameterGraph parameters;
};

/// A panel 600 x 300 at the origin (all five parameters constants).
PanelDefinition makePanel(domain::PartId id, std::string name, domain::MaterialId material);

struct PanelResult {
    domain::Part part;
    core::Diagnostics diagnostics; ///< furniture.edge.missing_band warnings.
};

/// Generates the part: grain along the length when the material has grain. The box
/// occupied in the assembly frame starts at (x, y, z) and extends by
///   Horizontal:    length along X, width along Y, thickness along Z
///   VerticalSide:  thickness along X, width along Y, length along Z
///   VerticalFront: length along X, thickness along Y, width along Z
/// Errors: a standard parameter is missing, length/width not > 0, material not in the library.
core::Result<PanelResult> generatePanel(const PanelDefinition& definition, const Libraries& libraries);

} // namespace fos::furniture
