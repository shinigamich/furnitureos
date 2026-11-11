// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/hardware_list.hpp"

#include <map>

namespace fos::hardware {

std::vector<HardwareListRow> hardwareList(const domain::Model& model) {
    std::map<std::string, int> quantities; // ordered -> deterministic output
    for (const auto& assembly : model.assemblies)
        for (const auto& inst : assembly.hardware)
            if (inst.quantity > 0) quantities[inst.definition.str()] += inst.quantity;

    std::vector<HardwareListRow> rows;
    for (const auto& [id, quantity] : quantities) {
        HardwareListRow row;
        row.definition = domain::HardwareId(id);
        row.quantity = quantity;
        if (const auto* def = model.findHardware(row.definition)) {
            row.known = true;
            row.name = def->name;
            row.kind = def->kind;
            row.manufacturer = def->manufacturer;
            row.article = def->article;
            row.dimensionsVerified = def->dimensionsVerified;
            row.unitCost = def->unitCost;
            row.totalCost = def->unitCost * quantity;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

double totalHardwareCost(const std::vector<HardwareListRow>& rows) noexcept {
    double sum = 0;
    for (const auto& r : rows) sum += r.totalCost;
    return sum;
}

} // namespace fos::hardware
