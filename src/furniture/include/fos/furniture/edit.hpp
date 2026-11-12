// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Editing operations on a CabinetDefinition for a UI / undo system.
///
/// EVERY function here is atomic: the edit is applied to a copy, the copy is resolved and
/// generated, and only when that succeeds is `definition` replaced. On error the definition
/// is exactly as before and the error says precisely what is wrong. Because definitions are
/// value types, an undo system simply keeps the previous CabinetDefinition.
#include "fos/furniture/generate.hpp"

#include <functional>

namespace fos::furniture {

struct EditResult {
    /// Names of all parameters whose value changed, was added or removed (sorted).
    std::vector<std::string> changedParameters;
    GenerationResult generated; ///< The regenerated assembly with its diagnostics.
};

/// Generic atomic edit: `mutate` changes the copy (returning an error aborts the edit).
core::Result<EditResult> applyEdit(CabinetDefinition& definition, const Libraries& libraries,
                                   const std::function<core::Result<void>(CabinetDefinition&)>& mutate);

/// Makes an existing parameter the constant `value`. Setting a derived or material-
/// synchronised standard parameter pins it (see CabinetDefinition::pinnedParameters).
core::Result<EditResult> setParameter(CabinetDefinition& definition, const Libraries& libraries, const std::string& name,
                                      double value);
/// Makes an existing parameter an expression over the graph (pins like setParameter).
core::Result<EditResult> setParameterExpression(CabinetDefinition& definition, const Libraries& libraries,
                                                const std::string& name, const std::string& expression);
/// Un-pins a standard parameter: derived ones get their standard formula back, thickness
/// parameters follow the material again, inputs return to their default value.
core::Result<EditResult> resetParameter(CabinetDefinition& definition, const Libraries& libraries, const std::string& name);
/// Adds a custom parameter (the name must not be a standard name or exist already).
core::Result<EditResult> addParameter(CabinetDefinition& definition, const Libraries& libraries,
                                      const parametric::ParameterDef& parameter);
/// Removes a custom parameter (standard parameters cannot be removed).
core::Result<EditResult> removeParameter(CabinetDefinition& definition, const Libraries& libraries, const std::string& name);

core::Result<EditResult> setConstruction(CabinetDefinition& definition, const Libraries& libraries,
                                         const ConstructionOptions& options);
core::Result<EditResult> setMaterials(CabinetDefinition& definition, const Libraries& libraries,
                                      const MaterialAssignment& materials);
core::Result<EditResult> setHardware(CabinetDefinition& definition, const Libraries& libraries,
                                     const HardwareSelection& hardware);
/// Sets or clears (std::nullopt) the override of one part slot, e.g. "side.left".
core::Result<EditResult> setPartOverride(CabinetDefinition& definition, const Libraries& libraries, const std::string& slot,
                                         const std::optional<PartOverride>& override);

/// Replaces the whole interior tree.
core::Result<EditResult> setInterior(CabinetDefinition& definition, const Libraries& libraries, const Section& root);
/// Replaces the section at `path` ("" = root) but keeps its `size` within the parent.
core::Result<EditResult> replaceSection(CabinetDefinition& definition, const Libraries& libraries, const std::string& path,
                                        const Section& section);
core::Result<EditResult> setShelfCount(CabinetDefinition& definition, const Libraries& libraries, const std::string& path,
                                       int count);
/// Sets or removes (std::nullopt) the front of the section at `path`.
core::Result<EditResult> setFront(CabinetDefinition& definition, const Libraries& libraries, const std::string& path,
                                  const std::optional<FrontSpec>& front);
/// Splits the leaf at `path` into `sizes.size()` (>= 2) children; shelves of the leaf move
/// into the first child, its front stays on the (now split) section.
core::Result<EditResult> splitSection(CabinetDefinition& definition, const Libraries& libraries, const std::string& path,
                                      SplitDirection direction, const std::vector<SectionSize>& sizes);
/// Sets the size of the child section at `path` (not the root) within its parent.
core::Result<EditResult> setSectionSize(CabinetDefinition& definition, const Libraries& libraries, const std::string& path,
                                        const SectionSize& size);

} // namespace fos::furniture
