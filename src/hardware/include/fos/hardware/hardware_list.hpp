// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/model.hpp"

#include <string>
#include <vector>

namespace fos::hardware {

/// One line of the aggregated hardware list of a model.
struct HardwareListRow {
    domain::HardwareId definition;
    /// False when no instance's definition id could be found in Model::hardwareCatalog;
    /// name is then empty, kind Other, costs 0.
    bool known = false;
    std::string name;
    domain::HardwareKind kind = domain::HardwareKind::Other;
    std::string manufacturer;
    std::string article;
    bool dimensionsVerified = false;
    int quantity = 0;      ///< Sum of HardwareInstance::quantity over all assemblies.
    double unitCost = 0.0; ///< Project currency; 0 = unknown.
    double totalCost = 0.0; ///< quantity * unitCost.
};

/// Aggregates all hardware instances of all assemblies by definition id. Rows are sorted
/// by definition id (deterministic). Instances with quantity <= 0 are ignored.
std::vector<HardwareListRow> hardwareList(const domain::Model& model);

/// Sum of totalCost over the rows.
double totalHardwareCost(const std::vector<HardwareListRow>& rows) noexcept;

} // namespace fos::hardware
