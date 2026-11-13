// SPDX-License-Identifier: AGPL-3.0-only
#include "example_materials.hpp"

namespace fos::plugins::example {
namespace {

std::string ns(const char* local) { return std::string(kPluginId) + ":" + local; }

domain::Material board(const char* local, const char* name, domain::MaterialKind kind, double thickness, bool grain,
                       domain::Color color) {
    domain::Material m;
    m.id = domain::MaterialId(ns(local));
    m.name = name;
    m.kind = kind;
    m.thickness = thickness;
    m.sheetLength = 2800.0;
    m.sheetWidth = 2070.0;
    m.hasGrain = grain;
    m.color = color;
    m.custom["source"] = "FurnitureOS example plugin (generic, not a real product)";
    return m;
}

domain::EdgeBand band(const char* local, const char* name, domain::EdgeBandKind kind, double thickness, double width,
                      domain::Color color) {
    domain::EdgeBand e;
    e.id = domain::EdgeBandId(ns(local));
    e.name = name;
    e.kind = kind;
    e.thickness = thickness;
    e.width = width;
    e.color = color;
    return e;
}

} // namespace

formats::Library library() {
    using domain::EdgeBandKind;
    using domain::MaterialKind;
    formats::Library lib;
    lib.materials = {
        board("white-pb-18", "Generic white melamine particleboard 18 mm", MaterialKind::Particleboard, 18.0, false, {245, 245, 242}),
        board("grey-pb-18", "Generic light grey melamine particleboard 18 mm", MaterialKind::Particleboard, 18.0, false, {200, 201, 203}),
        board("oak-decor-pb-18", "Generic oak-decor melamine particleboard 18 mm", MaterialKind::Particleboard, 18.0, true, {196, 160, 112}),
        board("raw-mdf-19", "Generic raw MDF 19 mm", MaterialKind::Mdf, 19.0, false, {181, 151, 115}),
        board("white-hdf-3", "Generic white HDF back panel 3 mm", MaterialKind::Hdf, 3.0, false, {240, 240, 238}),
    };
    lib.edgeBands = {
        band("white-abs-22x2", "Generic white ABS edge band 22 x 2 mm", EdgeBandKind::Abs, 2.0, 22.0, {245, 245, 242}),
        band("white-abs-22x0.8", "Generic white ABS edge band 22 x 0.8 mm", EdgeBandKind::Abs, 0.8, 22.0, {245, 245, 242}),
        band("grey-abs-22x2", "Generic light grey ABS edge band 22 x 2 mm", EdgeBandKind::Abs, 2.0, 22.0, {200, 201, 203}),
        band("oak-decor-abs-22x1", "Generic oak-decor ABS edge band 22 x 1 mm", EdgeBandKind::Abs, 1.0, 22.0, {196, 160, 112}),
    };
    return lib;
}

core::Result<formats::SaveReport> writeLibrary(const std::filesystem::path& path) {
    formats::SaveOptions options;
    options.backupCount = 0;
    options.clock = [] { return std::chrono::system_clock::time_point{kSavedAt}; };
    return formats::exportLibrary(path, library(), formats::AppInfo{"FurnitureOS", std::string(kAppVersion)}, options);
}

} // namespace fos::plugins::example
