// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/production/common.hpp"

#include <array>

namespace fos::production {

/// One row per part instance.
struct PartRow {
    /// Short sequential part number, 1-based, counted over the WHOLE model in model
    /// order (assemblies in `Model::assemblies` order, parts in `Assembly::parts`
    /// order). Unique within the model, independent of `Scope`, and identical on
    /// every regeneration of an unchanged model. Inserting a part renumbers the
    /// parts after it (ids, not numbers, are the persistent identity).
    int number = 0;
    domain::AssemblyId assemblyId;
    std::string assemblyName;
    domain::PartId partId;
    std::string name;
    domain::PartRole role = domain::PartRole::Generic;
    domain::MaterialId materialId;
    std::string materialName;     ///< Empty when `materialMissing`.
    bool materialMissing = false; ///< Material id is empty or not in the model.
    double length = 0, width = 0, thickness = 0; ///< Finished size, mm.
    double cutLength = 0, cutWidth = 0;          ///< domain::cutSize(), mm.
    domain::GrainDirection grain = domain::GrainDirection::None;
    std::array<EdgeCell, 4> edges; ///< Indexed by domain::EdgeSide (L1, L2, W1, W2).
    int operationCount = 0;

    const EdgeCell& edge(domain::EdgeSide s) const { return edges[static_cast<std::size_t>(s)]; }
    /// length * width in m2; 0 when either size is not a positive finite number.
    double finishedAreaM2() const noexcept;
    /// cutLength * cutWidth in m2; 0 when either size is not a positive finite number.
    double cutAreaM2() const noexcept;
};

struct PartList {
    std::vector<PartRow> rows; ///< Ordered by `number`.
    core::Diagnostics diagnostics;
};

/// Part list (BOM of panels). Flags missing materials / edge bands and parts whose
/// finished or cut size is not a positive finite number.
PartList partList(const domain::Model& model, const Scope& scope = {});

} // namespace fos::production
