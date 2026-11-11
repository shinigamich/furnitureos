// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/placement.hpp"

#include <algorithm>

namespace fos::hardware {

const std::vector<domain::Operation>* Placement::operationsFor(const domain::PartId& part) const noexcept {
    for (const auto& po : operations)
        if (po.part == part) return &po.operations;
    return nullptr;
}

core::Result<void> applyPlacement(domain::Assembly& assembly, const Placement& placement) {
    for (const auto& po : placement.operations)
        if (!assembly.findPart(po.part))
            return core::makeError(core::ErrorCode::NotFound,
                                   "Cannot apply hardware placement: part '" + po.part.str() +
                                       "' is not part of assembly '" + assembly.id.str() + "'.",
                                   po.part.str());

    auto generated = [&](const domain::Operation& op) {
        return std::any_of(placement.instances.begin(), placement.instances.end(),
                           [&](const domain::HardwareInstance& i) { return i.id.str() == op.source; });
    };
    for (auto& part : assembly.parts)
        part.operations.erase(std::remove_if(part.operations.begin(), part.operations.end(), generated),
                              part.operations.end());

    for (const auto& inst : placement.instances) {
        auto it = std::find_if(assembly.hardware.begin(), assembly.hardware.end(),
                               [&](const domain::HardwareInstance& h) { return h.id == inst.id; });
        if (it != assembly.hardware.end()) *it = inst;
        else assembly.hardware.push_back(inst);
    }
    for (const auto& po : placement.operations) {
        auto& ops = assembly.findPart(po.part)->operations;
        ops.insert(ops.end(), po.operations.begin(), po.operations.end());
    }
    return {};
}

} // namespace fos::hardware
