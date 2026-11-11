// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/model.hpp"

#include <algorithm>

namespace fos::domain {
namespace {
template <class C, class Id> auto findById(C& items, const Id& id) -> decltype(&*items.begin()) {
    auto it = std::find_if(items.begin(), items.end(), [&](const auto& x) { return x.id == id; });
    return it == items.end() ? nullptr : &*it;
}
} // namespace

geometry::Aabb Assembly::bounds() const {
    geometry::Aabb box;
    for (const auto& p : parts) box.expand(p.bounds());
    return box;
}

const Part* Assembly::findPart(const PartId& pid) const { return findById(parts, pid); }
Part* Assembly::findPart(const PartId& pid) { return findById(parts, pid); }

const Material* Model::findMaterial(const MaterialId& mid) const { return findById(materials, mid); }
const EdgeBand* Model::findEdgeBand(const EdgeBandId& eid) const { return findById(edgeBands, eid); }
const HardwareDefinition* Model::findHardware(const HardwareId& hid) const {
    return findById(hardwareCatalog, hid);
}
const Assembly* Model::findAssembly(const AssemblyId& aid) const { return findById(assemblies, aid); }

double edgeBandThickness(const Part& part, EdgeSide s, const Model& model) noexcept {
    const auto& ref = part.edge(s);
    if (!ref) return 0.0;
    const EdgeBand* band = model.findEdgeBand(*ref);
    return band ? band->thickness : 0.0;
}

CutSize cutSize(const Part& part, const Model& model) noexcept {
    return {part.length - edgeBandThickness(part, EdgeSide::W1, model) -
                edgeBandThickness(part, EdgeSide::W2, model),
            part.width - edgeBandThickness(part, EdgeSide::L1, model) -
                edgeBandThickness(part, EdgeSide::L2, model)};
}

} // namespace fos::domain
