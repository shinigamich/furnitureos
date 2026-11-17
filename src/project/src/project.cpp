// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/project.hpp"

#include "fos/core/units.hpp"
#include "fos/hardware/catalog.hpp"

#include <cmath>

namespace fos::project {

const std::string& Item::id() const {
    if (const auto* c = cabinet()) return c->id.str();
    return panel()->id.str();
}

const std::string& Item::name() const {
    if (const auto* c = cabinet()) return c->name;
    return panel()->panel.name;
}

const geometry::Transform& Item::placement() const {
    if (const auto* c = cabinet()) return c->placement;
    return panel()->placement;
}

const CuttingSettings& ProjectCuttingSettings::forMaterial(const std::string& materialId) const {
    const auto it = materials.find(materialId);
    return it == materials.end() ? defaults : it->second;
}

NestingSettings ProjectCuttingSettings::nestingSettings() const { return nesting.value_or(NestingSettings{}); }

bool operator==(const NestingSettings& a, const NestingSettings& b) noexcept {
    return a.toolDiameter == b.toolDiameter && a.clearance == b.clearance && a.chordTolerance == b.chordTolerance &&
           a.margins.left == b.margins.left && a.margins.right == b.margins.right &&
           a.margins.bottom == b.margins.bottom && a.margins.top == b.margins.top &&
           a.minRemnantLength == b.minRemnantLength && a.iterations == b.iterations && a.seed == b.seed;
}

core::Result<void> checkNestingSettings(const NestingSettings& s) {
    struct Range {
        const char* what;
        double value, low, high;
        bool lowOpen;
    };
    const Range ranges[] = {
        {"tool diameter", s.toolDiameter, 0.0, 100.0, true},
        {"clearance", s.clearance, 0.0, 100.0, false},
        {"chord tolerance", s.chordTolerance, 0.01, 10.0, false},
        {"margin left", s.margins.left, 0.0, 1000.0, false},
        {"margin right", s.margins.right, 0.0, 1000.0, false},
        {"margin bottom", s.margins.bottom, 0.0, 1000.0, false},
        {"margin top", s.margins.top, 0.0, 1000.0, false},
        {"minimum remnant length", s.minRemnantLength, 0.0, 100000.0, false},
    };
    for (const Range& r : ranges) {
        const bool ok = std::isfinite(r.value) && (r.lowOpen ? r.value > r.low : r.value >= r.low) && r.value <= r.high;
        if (!ok)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   std::string("Nesting setting '") + r.what + "' must be a finite number " +
                                       (r.lowOpen ? "> " : ">= ") + core::formatLength(r.low, 3) + " and <= " +
                                       core::formatLength(r.high, 3) + " mm, got " + core::formatLength(r.value, 3) + ".");
    }
    if (s.iterations < 0 || s.iterations > kMaxNestingIterations)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Nesting setting 'iterations' must be an integer from 0 to " +
                                   std::to_string(kMaxNestingIterations) + ", got " + std::to_string(s.iterations) + ".");
    return {};
}

const Item* Project::findItem(const std::string& id) const {
    for (const auto& item : items)
        if (item.id() == id) return &item;
    return nullptr;
}

Item* Project::findItem(const std::string& id) {
    for (auto& item : items)
        if (item.id() == id) return &item;
    return nullptr;
}

std::string assemblyDrawingId(const std::string& itemId) { return itemId + "/drawing.assembly"; }
std::string partDrawingId(const std::string& partId) { return partId + "/drawing.part"; }

namespace {
domain::Material board(const char* id, const char* name, domain::MaterialKind kind, double thickness,
                       domain::Color color) {
    domain::Material m;
    m.id = domain::MaterialId(id);
    m.name = name;
    m.kind = kind;
    m.thickness = thickness;
    m.sheetLength = 2800.0;
    m.sheetWidth = 2070.0;
    m.hasGrain = false;
    m.color = color;
    m.costPerSheet = 0.0;
    return m;
}

domain::EdgeBand band(const char* id, const char* name, double thickness) {
    domain::EdgeBand e;
    e.id = domain::EdgeBandId(id);
    e.name = name;
    e.kind = domain::EdgeBandKind::Abs;
    e.thickness = thickness;
    e.width = 22.0;
    e.costPerMeter = 0.0;
    return e;
}
} // namespace

std::vector<domain::Material> defaultMaterials() {
    return {
        board(defaults::kParticleboard18, "Particleboard 18 mm (generic)", domain::MaterialKind::Particleboard, 18.0,
              {235, 235, 230}),
        board(defaults::kMdf18, "MDF 18 mm (generic)", domain::MaterialKind::Mdf, 18.0, {210, 190, 160}),
        board(defaults::kHdf3, "HDF 3 mm (generic)", domain::MaterialKind::Hdf, 3.0, {170, 140, 110}),
    };
}

std::vector<domain::EdgeBand> defaultEdgeBands() {
    return {band(defaults::kAbs2, "ABS 2 x 22 mm (generic)", 2.0), band(defaults::kAbs04, "ABS 0.4 x 22 mm (generic)", 0.4)};
}

Project newProject(Metadata metadata) {
    Project p;
    p.metadata = std::move(metadata);
    p.libraries.materials = defaultMaterials();
    p.libraries.edgeBands = defaultEdgeBands();
    p.libraries.hardware = hardware::builtinCatalog();
    return p;
}

furniture::templates::TemplateMaterials defaultTemplateMaterials() {
    furniture::templates::TemplateMaterials m;
    m.carcass = domain::MaterialId(defaults::kParticleboard18);
    m.back = domain::MaterialId(defaults::kHdf3);
    m.visibleEdge = domain::EdgeBandId(defaults::kAbs2);
    m.hiddenEdge = domain::EdgeBandId(defaults::kAbs04);
    return m;
}

} // namespace fos::project
