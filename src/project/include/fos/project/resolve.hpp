// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Resolution of a Project into the derived engineering model. Every report, drawing and
/// cutting plan is produced from a Resolved (never from stored copies).
#include "fos/core/result.hpp"
#include "fos/project/project.hpp"

namespace fos::project {

/// Outcome of generating one item.
struct ItemResolution {
    std::string itemId;
    bool generated = false;        ///< False: generation failed; the item has no assembly in the model.
    core::Diagnostics diagnostics; ///< Generator findings (warnings / errors), entityId = part or item id.
};

struct Resolved {
    /// Libraries copied from the project + one assembly per successfully generated item, in
    /// project order. Assembly id = item id, placement = item placement (assembly -> project frame).
    domain::Model model;
    std::vector<ItemResolution> items; ///< One per project item, project order.
    /// All findings: per-item generator diagnostics followed by project-level ones.
    /// Codes added here:
    ///   project.item.generation_failed (Error)   the generator refused the item (message = why);
    ///   project.part.duplicate_id     (Error)    two generated parts share an id.
    core::Diagnostics diagnostics;
};

/// Structural checks that make a project unusable (not mere generation findings): empty or
/// duplicate item ids, empty or duplicate library ids, a panel item whose part id is empty.
/// InvalidArgument error naming the offending id.
core::Result<void> validateStructure(const Project& project);

/// Generates every item with the furniture generator (furniture::generate / generatePanel)
/// against the project libraries. Pure and deterministic. Fails only for validateStructure()
/// errors; an item that cannot be generated is reported as a diagnostic and left out.
core::Result<Resolved> resolve(const Project& project);

/// Generates only the item `itemId` (NotFound when absent); used by commands to check an edit.
core::Result<domain::Assembly> generateItem(const Project& project, const std::string& itemId,
                                            core::Diagnostics* diagnostics = nullptr);

} // namespace fos::project
