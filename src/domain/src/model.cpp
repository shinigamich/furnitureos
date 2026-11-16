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
    if (part.shape) {
        auto raw = cutContour(part, model);
        if (raw) {
            const geometry::Rect2 b = geometry::bounds(raw.value());
            return {b.width(), b.height()};
        }
        return {part.length, part.width};
    }
    return {part.length - edgeBandThickness(part, EdgeSide::W1, model) -
                edgeBandThickness(part, EdgeSide::W2, model),
            part.width - edgeBandThickness(part, EdgeSide::L1, model) -
                edgeBandThickness(part, EdgeSide::L2, model)};
}

bool hasShape(const Part& part) noexcept { return part.shape.has_value(); }

bool isRectangularOutline(const Part& part) noexcept {
    if (!part.shape) return true;
    const PartShape& sh = *part.shape;
    if (!sh.cutouts.empty() || sh.outer.size() != 4) return false;
    for (const auto& v : sh.outer.vertices)
        if (v.bulge != 0.0) return false;
    const geometry::Rect2 b = geometry::bounds(sh.outer);
    if (!core::approxEqual(b.min.x, 0) || !core::approxEqual(b.min.y, 0) ||
        !core::approxEqual(b.max.x, part.length) || !core::approxEqual(b.max.y, part.width))
        return false;
    // Four straight segments spanning exactly the bounding box: every vertex is a box corner.
    for (const auto& v : sh.outer.vertices) {
        const bool xOk = core::approxEqual(v.point.x, 0) || core::approxEqual(v.point.x, part.length);
        const bool yOk = core::approxEqual(v.point.y, 0) || core::approxEqual(v.point.y, part.width);
        if (!xOk || !yOk) return false;
    }
    return true;
}

geometry::Contour finishedContour(const Part& part) {
    if (part.shape) return part.shape->outer;
    return geometry::rectangleContour(part.length, part.width);
}

double contourBandThickness(const Part& part, std::size_t segment, const Model& model) noexcept {
    std::optional<EdgeBandId> ref;
    if (part.shape) {
        if (segment < part.shape->outerEdges.size()) ref = part.shape->outerEdges[segment];
    } else {
        static constexpr EdgeSide kRectSegments[4] = {EdgeSide::L1, EdgeSide::W2, EdgeSide::L2, EdgeSide::W1};
        if (segment < 4) ref = part.edge(kRectSegments[segment]);
    }
    if (!ref) return 0.0;
    const EdgeBand* band = model.findEdgeBand(*ref);
    return band ? band->thickness : 0.0;
}

core::Result<geometry::Contour> cutContour(const Part& part, const Model& model) {
    const geometry::Contour finished = finishedContour(part);
    std::vector<double> distance(finished.size());
    bool any = false;
    for (std::size_t i = 0; i < finished.size(); ++i) {
        distance[i] = -contourBandThickness(part, i, model); // inwards
        any = any || distance[i] != 0.0;
    }
    if (!any) return finished;
    return geometry::offsetContour(finished, distance);
}

bool canRotateOnSheet(const Part& part, const Material* material) noexcept {
    return part.grain == GrainDirection::None || (material != nullptr && !material->hasGrain);
}

SheetAlignment sheetAlignment(const Part& part, const Material* material) noexcept {
    if (canRotateOnSheet(part, material)) return SheetAlignment::Free;
    return part.grain == GrainDirection::AlongWidth ? SheetAlignment::LengthAcrossSheet
                                                    : SheetAlignment::LengthAlongSheet;
}

} // namespace fos::domain
