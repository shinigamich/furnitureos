// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/domain/model.hpp"

#include <vector>

/// Common result type of all placement rules in this module.
///
/// Placement rules are PURE functions: they read parts and return what should be added;
/// they never modify their inputs. Use applyPlacement() to store a result in an assembly.
///
/// Every generated operation has `source` = id of the generating hardware instance and
/// the id `<instance-id>/op.N`, N = 1, 2, ... in generation order across all parts of the
/// placement (so ids are unique in every part and stable across recalculation).
namespace fos::hardware {

/// Operations to add to one part.
struct PartOperations {
    domain::PartId part;
    std::vector<domain::Operation> operations;
};

struct Placement {
    std::vector<domain::HardwareInstance> instances;
    /// One entry per affected part, in the order documented by the producing rule.
    std::vector<PartOperations> operations;

    /// Operations for `part`, nullptr when the placement does not touch it.
    const std::vector<domain::Operation>* operationsFor(const domain::PartId& part) const noexcept;
};

/// Stores a placement in `assembly`, replacing any earlier result of the same instances:
/// operations whose `source` equals one of the placement's instance ids are removed from
/// ALL parts first, instances with the same id are replaced in place (new ones are
/// appended), then the new operations are appended to their parts. Fails with NotFound —
/// leaving the assembly untouched — when a referenced part is not in the assembly.
core::Result<void> applyPlacement(domain::Assembly& assembly, const Placement& placement);

} // namespace fos::hardware
