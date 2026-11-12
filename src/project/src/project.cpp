// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/project.hpp"

#include "fos/hardware/catalog.hpp"

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
