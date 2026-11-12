// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Generation of the concrete domain::Assembly from a CabinetDefinition.
#include "fos/core/diagnostic.hpp"
#include "fos/furniture/definition.hpp"

#include <map>

namespace fos::furniture {

/// Diagnostic codes emitted by generate() (stable):
///   furniture.material.thickness_mismatch   warning  pinned thickness parameter != material thickness
///   furniture.material.override_thickness   warning  per-part material override has another thickness
///   furniture.edge.missing_band             warning  edge band id not in the library (edge left raw)
///   furniture.hardware.missing_definition   error    selected fitting not in the hardware library
///   furniture.hardware.placement_failed     error    a hardware rule refused (message says why)
///   furniture.front.inset_hinges            warning  inset doors get no hinges (no rule available)
///   furniture.plinth.feet_height            warning  plinth height outside the foot's adjustment range
///   furniture.parameter.*                   warning  violated Warning-severity constraints
///   furniture.hinge.near_shelf              warning  an intermediate hinge could not be moved clear of a shelf
///   furniture.geometry.interpenetration     error    two generated parts share volume outside a groove
///                                                    (self-check, see checks.hpp; message names both parts
///                                                    and the shared volume in mm)
/// Edit functions (edit.hpp) may add:
///   furniture.parameter.pinned_formula_changed  warning  a construction change altered the standard
///                                                        formula of a pinned derived parameter
///   machining.*                             as reported by machining::validateOperations per part
struct GenerationResult {
    domain::Assembly assembly;
    core::Diagnostics diagnostics;
    /// Value of every parameter of the resolved graph (standard and custom), by name.
    std::map<std::string, double> values;
};

/// Brings a definition into its resolved state: checks the options, synchronises the
/// standard parameters (syncStandardParameters) and copies the board thicknesses of the
/// assigned materials into `thickness`, `back_thickness`, `front_thickness` unless pinned
/// (a pinned value that disagrees yields furniture.material.thickness_mismatch).
/// Atomic: on error the definition is unchanged. Idempotent.
core::Result<void> resolve(CabinetDefinition& definition, const Libraries& libraries,
                           core::Diagnostics* diagnostics = nullptr);

/// Generates the assembly. The definition is resolved on a private copy first, so any
/// definition (resolved or not) can be passed. Pure and deterministic.
///
/// Errors (Result): unsupported options, parameter/constraint failures, materials missing
/// from the libraries, an invalid section tree (messages name the section and the numbers).
/// Findings that do not prevent a geometrically consistent assembly (hardware rules that
/// refuse, machining validation) are returned as diagnostics.
///
/// Part ids are `<cabinet id>/<slot>`; see README.md for the slot scheme. Parts are placed
/// in the assembly frame of ARCHITECTURE.md 4.2 with the carcass front at y = 0 (overlay
/// fronts lie at y < 0, an Overlay back at y > depth).
core::Result<GenerationResult> generate(const CabinetDefinition& definition, const Libraries& libraries);

} // namespace fos::furniture
