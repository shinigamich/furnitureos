// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/furniture/definition.hpp"

#include <charconv>

namespace fos::furniture {

const domain::Material* Libraries::findMaterial(const domain::MaterialId& id) const noexcept {
    for (const auto& m : materials)
        if (m.id == id) return &m;
    return nullptr;
}
const domain::EdgeBand* Libraries::findEdgeBand(const domain::EdgeBandId& id) const noexcept {
    for (const auto& e : edgeBands)
        if (e.id == id) return &e;
    return nullptr;
}
const domain::HardwareDefinition* Libraries::findHardware(const domain::HardwareId& id) const noexcept {
    for (const auto& h : hardware)
        if (h.id == id) return &h;
    return nullptr;
}

domain::Model toModel(const Libraries& libraries, std::vector<domain::Assembly> assemblies) {
    domain::Model m;
    m.materials = libraries.materials;
    m.edgeBands = libraries.edgeBands;
    m.hardwareCatalog = libraries.hardware;
    m.assemblies = std::move(assemblies);
    return m;
}

core::Result<const Section*> findSection(const Section& root, std::string_view path) {
    const Section* s = &root;
    std::string_view rest = path;
    while (!rest.empty()) {
        const auto dot = rest.find('.');
        const std::string_view seg = rest.substr(0, dot);
        rest = dot == std::string_view::npos ? std::string_view{} : rest.substr(dot + 1);
        std::size_t k = 0;
        const auto [end, ec] = std::from_chars(seg.data(), seg.data() + seg.size(), k);
        if (ec != std::errc{} || end != seg.data() + seg.size() || k < 1 || k > s->children.size() ||
            (dot != std::string_view::npos && rest.empty()))
            return core::makeError(core::ErrorCode::NotFound,
                                   "There is no section with path '" + std::string(path) +
                                       "' in the interior layout (paths look like '2.1': child 1 of child 2 of the root; "
                                       "the root is '').",
                                   std::string(path));
        s = &s->children[k - 1];
    }
    return s;
}

core::Result<Section*> findSection(Section& root, std::string_view path) {
    auto r = findSection(static_cast<const Section&>(root), path);
    if (!r) return r.error();
    return const_cast<Section*>(r.value());
}

} // namespace fos::furniture
