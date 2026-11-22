// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/components.hpp"

#include "fos/project/codec.hpp"
#include "fos/project/resolve.hpp"

#include <algorithm>
#include <set>

#ifndef FOS_PROJECT_APP_VERSION
#define FOS_PROJECT_APP_VERSION "0.0.0"
#endif

namespace fos::project {

namespace {

struct RefSets {
    std::set<std::string> materials, edgeBands, hardware;
};

void addId(std::set<std::string>& set, const std::string& id) {
    if (!id.empty()) set.insert(id);
}

void addEdge(RefSets& r, const std::optional<domain::EdgeBandId>& e) {
    if (e) addId(r.edgeBands, e->str());
}

void collectSection(RefSets& r, const furniture::Section& s) {
    if (s.front) {
        if (s.front->drawerBox) addId(r.hardware, s.front->drawerBox->slide.str());
        for (const auto& box : s.front->drawerBoxes)
            if (box) addId(r.hardware, box->slide.str());
    }
    for (const auto& child : s.children) collectSection(r, child);
}

std::vector<std::string> toList(const std::set<std::string>& s) { return {s.begin(), s.end()}; }

/// Lower-case ASCII slug: letters and digits kept, other runs -> '-'.
std::string slug(const std::string& name) {
    std::string out;
    bool dash = false;
    for (char c : name) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        const bool upper = c >= 'A' && c <= 'Z';
        if (alnum || upper) {
            if (dash && !out.empty()) out += '-';
            dash = false;
            out += upper ? static_cast<char>(c - 'A' + 'a') : c;
        } else {
            dash = true;
        }
    }
    return out.empty() ? std::string("component") : out;
}

template <class T, class Id>
const T* findById(const std::vector<T>& list, const Id& id) {
    const auto it = std::find_if(list.begin(), list.end(), [&](const T& e) { return e.id.str() == id; });
    return it == list.end() ? nullptr : &*it;
}

/// Copies the entries named by `ids` from `from` into `to` (in `ids` order).
template <class T>
core::Result<void> embed(const std::vector<T>& from, const std::vector<std::string>& ids, std::vector<T>& to,
                         const char* what, const std::string& itemId) {
    for (const auto& id : ids) {
        const T* entry = findById(from, id);
        if (entry == nullptr)
            return core::makeError(core::ErrorCode::NotFound,
                                   "Item \"" + itemId + "\" references " + what + " \"" + id +
                                       "\", which is not in the project libraries; it cannot become a component.",
                                   id);
        to.push_back(*entry);
    }
    return {};
}

template <class T>
core::Result<void> checkEmbedded(const std::vector<T>& list, const std::vector<std::string>& ids, const char* what,
                                 std::string_view path, const std::string& componentId) {
    for (const auto& id : ids)
        if (findById(list, id) == nullptr)
            return core::makeError(core::ErrorCode::ParseError,
                                   std::string(path) + "/library: component \"" + componentId + "\" references " + what +
                                       " \"" + id + "\" which is not embedded in the component.",
                                   std::string(path) + "/library");
    return {};
}

void resetPlacement(Item& item) {
    if (auto* c = item.cabinet()) c->placement = geometry::Transform::identity();
    else item.panel()->placement = geometry::Transform::identity();
}

} // namespace

LibraryRefs libraryReferences(const Item& item) {
    RefSets r;
    if (const auto* c = item.cabinet()) {
        const auto& m = c->materials;
        for (const auto* id : {&m.carcass, &m.back, &m.front, &m.drawer, &m.drawerBottom}) addId(r.materials, id->str());
        addId(r.edgeBands, m.visibleEdge.str());
        addEdge(r, m.hiddenEdge);
        addEdge(r, m.frontEdge);
        for (const auto& [slot, o] : m.overrides) {
            if (o.material) addId(r.materials, o.material->str());
            for (const auto& [side, e] : o.edges) addEdge(r, e);
        }
        const auto& h = c->hardware;
        for (const auto* id : {&h.hinge, &h.hingeHalf, &h.hingeInset, &h.lift, &h.handle, &h.shelfPin, &h.confirmat,
                                &h.dowel, &h.foot, &h.slidingSystem})
            addId(r.hardware, id->str());
        collectSection(r, c->interior);
    } else {
        const auto& p = item.panel()->panel;
        addId(r.materials, p.material.str());
        for (const auto& e : p.edges) addEdge(r, e);
    }
    return {toList(r.materials), toList(r.edgeBands), toList(r.hardware)};
}

core::Result<Component> makeComponent(const Project& project, const std::string& itemId, formats::ComponentMeta meta) {
    const Item* item = project.findItem(itemId);
    if (item == nullptr)
        return core::makeError(core::ErrorCode::NotFound, "The project has no furniture item \"" + itemId + "\".", itemId);
    Component c;
    c.item = *item;
    resetPlacement(c.item);
    const LibraryRefs refs = libraryReferences(*item);
    const auto& lib = project.libraries;
    if (auto r = embed(lib.materials, refs.materials, c.library.materials, "material", itemId); !r) return r.error();
    if (auto r = embed(lib.edgeBands, refs.edgeBands, c.library.edgeBands, "edge band", itemId); !r) return r.error();
    if (auto r = embed(lib.hardware, refs.hardware, c.library.hardware, "hardware definition", itemId); !r)
        return r.error();
    if (meta.name.empty()) meta.name = item->name().empty() ? itemId : item->name();
    if (meta.id.empty()) meta.id = "comp." + slug(meta.name);
    if (meta.previewSize == formats::PreviewSize{}) {
        core::Diagnostics diagnostics;
        auto assembly = generateItem(project, itemId, &diagnostics);
        if (!assembly)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Item \"" + itemId + "\" cannot be generated, so it cannot become a component: " +
                                       assembly.error().message,
                                   itemId);
        const geometry::Vec3 size = assembly.value().bounds().size();
        meta.previewSize = {size.x, size.z, size.y};
    }
    c.meta = std::move(meta);
    return c;
}

formats::ComponentRecord toRecord(const Component& component) {
    formats::ComponentRecord rec;
    rec.meta = component.meta;
    rec.library = component.library;
    rec.definition = toJson(component.item);
    rec.extra = component.extra;
    return rec;
}

core::Result<Component> componentFromRecord(const formats::ComponentRecord& record, std::string_view path) {
    auto item = itemFromJson(record.definition, std::string(path) + "/definition");
    if (!item) return item.error();
    Component c;
    c.meta = record.meta;
    c.item = std::move(item.value());
    c.library = record.library;
    c.extra = record.extra;
    const LibraryRefs refs = libraryReferences(c.item);
    if (auto r = checkEmbedded(c.library.materials, refs.materials, "material", path, c.meta.id); !r) return r.error();
    if (auto r = checkEmbedded(c.library.edgeBands, refs.edgeBands, "edge band", path, c.meta.id); !r) return r.error();
    if (auto r = checkEmbedded(c.library.hardware, refs.hardware, "hardware definition", path, c.meta.id); !r)
        return r.error();
    return c;
}

core::Result<formats::SaveReport> saveComponents(const std::filesystem::path& path,
                                                 const std::vector<Component>& components,
                                                 const formats::SaveOptions& options) {
    formats::ComponentFile file;
    for (const auto& c : components) file.components.push_back(toRecord(c));
    // Decode once more so that a component that would not load back is refused before writing.
    for (std::size_t i = 0; i < file.components.size(); ++i)
        if (auto check = componentFromRecord(file.components[i], "/payload/components/" + std::to_string(i)); !check) {
            core::Error error = check.error();
            error.code = core::ErrorCode::InvalidArgument;
            error.message = "The components cannot be saved to '" + path.string() +
                            "' because the file would not load again: " + error.message;
            return error;
        }
    return formats::exportComponentFile(path, file, formats::AppInfo{"FurnitureOS", FOS_PROJECT_APP_VERSION}, options);
}

namespace {

core::Result<std::vector<Component>> decodeAll(const formats::ComponentFile& file, const std::string& source) {
    std::vector<Component> out;
    for (std::size_t i = 0; i < file.components.size(); ++i) {
        auto c = componentFromRecord(file.components[i], "/payload/components/" + std::to_string(i));
        if (!c) {
            core::Error error = c.error();
            error.message = "'" + source + "' is not a valid component file: " + error.message;
            return error;
        }
        out.push_back(std::move(c.value()));
    }
    return out;
}

} // namespace

core::Result<std::vector<Component>> loadComponents(const std::filesystem::path& path,
                                                    const formats::LoadOptions& options,
                                                    core::Diagnostics* warnings) {
    auto file = formats::importComponentFile(path, options, warnings);
    if (!file) return file.error();
    return decodeAll(file.value(), path.string());
}

core::Result<std::vector<Component>> parseComponents(std::string_view text, std::string_view sourceName,
                                                     const formats::LoadOptions& options,
                                                     core::Diagnostics* warnings) {
    auto file = formats::importComponentFileText(text, sourceName, options, warnings);
    if (!file) return file.error();
    return decodeAll(file.value(), std::string(sourceName));
}

const Component* findComponent(const std::vector<Component>& components, std::string_view idOrName) {
    for (const auto& c : components)
        if (c.meta.id == idOrName) return &c;
    for (const auto& c : components)
        if (c.meta.name == idOrName) return &c;
    return nullptr;
}

} // namespace fos::project
