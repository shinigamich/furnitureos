// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/components.hpp"

#include "fos/formats/codec.hpp"
#include "fos/project/resolve.hpp"

#include <algorithm>

#include <map>
#include <memory>

namespace fos::project {

namespace {

using formats::LibraryCategory;
using formats::MergeAction;

formats::Library libraryOf(const Project& p) {
    formats::Library lib;
    lib.materials = p.libraries.materials;
    lib.edgeBands = p.libraries.edgeBands;
    lib.hardware = p.libraries.hardware;
    return lib;
}

using Remap = LibraryRemap;

template <class Id> void remapId(Id& id, const std::map<std::string, std::string>& map) {
    if (const auto it = map.find(id.str()); it != map.end()) id = Id(it->second);
}

template <class Id> void remapOptional(std::optional<Id>& id, const std::map<std::string, std::string>& map) {
    if (id) remapId(*id, map);
}

void remapSection(furniture::Section& s, const Remap& r) {
    if (s.front) {
        if (s.front->drawerBox) remapId(s.front->drawerBox->slide, r.hardware);
        for (auto& box : s.front->drawerBoxes)
            if (box) remapId(box->slide, r.hardware);
    }
    if (s.hangingRail) {
        remapId(s.hangingRail->rail, r.hardware);
        remapId(s.hangingRail->supports, r.hardware);
    }
    if (s.innerDrawers) remapId(s.innerDrawers->slide, r.hardware);
    if (s.front && s.front->frame.panelMaterial) remapId(*s.front->frame.panelMaterial, r.materials);
    for (auto& child : s.children) remapSection(child, r);
}

void remapItem(Item& item, const Remap& r) {
    if (auto* c = item.cabinet()) {
        auto& m = c->materials;
        for (auto* id : {&m.carcass, &m.back, &m.front, &m.drawer, &m.drawerBottom}) remapId(*id, r.materials);
        remapId(m.visibleEdge, r.edgeBands);
        remapOptional(m.hiddenEdge, r.edgeBands);
        remapOptional(m.frontEdge, r.edgeBands);
        for (auto& [slot, o] : m.overrides) {
            remapOptional(o.material, r.materials);
            for (auto& [side, e] : o.edges) remapOptional(e, r.edgeBands);
        }
        auto& h = c->hardware;
        for (auto* id : {&h.hinge, &h.hingeHalf, &h.hingeInset, &h.lift, &h.handle, &h.shelfPin, &h.confirmat,
                          &h.dowel, &h.foot, &h.slidingSystem, &h.hanger, &h.wallRail}) remapId(*id, r.hardware);
        remapSection(c->interior, r);
    } else {
        auto& p = item.panel()->panel;
        remapId(p.material, r.materials);
        for (auto& e : p.edges) remapOptional(e, r.edgeBands);
    }
}

/// Removes from `incoming` every entry that is byte-for-byte identical (same JSON encoding) to
/// the target entry with the same id; returns their (category, id, name) in incoming order.
template <class T>
void dropIdentical(std::vector<T>& incoming, const std::vector<T>& target, LibraryCategory category,
                   std::vector<formats::MergeEntry>& identical) {
    std::vector<T> kept;
    for (auto& e : incoming) {
        const auto it = std::find_if(target.begin(), target.end(), [&](const T& t) { return t.id == e.id; });
        if (it != target.end() && formats::toJson(*it) == formats::toJson(e))
            identical.push_back({category, MergeAction::Skipped, e.id.str(), e.id.str(), e.name});
        else
            kept.push_back(std::move(e));
    }
    incoming = std::move(kept);
}

/// formats::mergeLibrary, except that incoming entries identical to the existing entry with the
/// same id are always Skipped (never duplicated by RenameWithNewId or rewritten by Replace).
/// Report entries keep the incoming order (materials, edge bands, hardware).
core::Result<formats::MergeReport> mergeComponentLibrary(formats::Library& target, const formats::Library& incoming,
                                                         formats::ConflictPolicy policy,
                                                         const formats::IdGenerator& newId) {
    formats::Library filtered = incoming;
    std::vector<formats::MergeEntry> identical;
    dropIdentical(filtered.materials, target.materials, LibraryCategory::Material, identical);
    dropIdentical(filtered.edgeBands, target.edgeBands, LibraryCategory::EdgeBand, identical);
    dropIdentical(filtered.hardware, target.hardware, LibraryCategory::Hardware, identical);
    auto merged = formats::mergeLibrary(target, filtered, policy, newId);
    if (!merged) return merged.error();
    formats::MergeReport report;
    auto take = [&](LibraryCategory category, const std::string& id) {
        for (auto* list : {&merged.value().entries, &identical})
            for (const auto& e : *list)
                if (e.category == category && e.incomingId == id) {
                    report.entries.push_back(e);
                    return;
                }
    };
    for (const auto& e : incoming.materials) take(LibraryCategory::Material, e.id.str());
    for (const auto& e : incoming.edgeBands) take(LibraryCategory::EdgeBand, e.id.str());
    for (const auto& e : incoming.hardware) take(LibraryCategory::Hardware, e.id.str());
    return report;
}

} // namespace

void remapLibraryReferences(Item& item, const LibraryRemap& remap) { remapItem(item, remap); }

formats::IdGenerator componentIdGenerator(const std::string& newItemId) {
    auto counter = std::make_shared<int>(0);
    return [newItemId, counter]() { return newItemId + ".lib" + std::to_string(++*counter); };
}

core::Result<formats::MergeReport> planComponentInsert(const Project& project, const Component& component,
                                                       formats::ConflictPolicy policy, const formats::IdGenerator& newId) {
    formats::Library target = libraryOf(project);
    return mergeComponentLibrary(target, component.library, policy, newId);
}

Command insertComponent(Component component, std::string newItemId, geometry::Transform placement,
                        formats::ConflictPolicy policy, formats::IdGenerator newId) {
    Command c;
    c.label = {"Insert component {0} as {1}", {component.meta.name, newItemId}};
    c.apply = [component = std::move(component), newItemId = std::move(newItemId), placement, policy,
               newId = std::move(newId)](Project& p) -> core::Result<Affected> {
        if (newItemId.empty())
            return core::makeError(core::ErrorCode::InvalidArgument, "The inserted component needs a non-empty item id.");
        if (p.findItem(newItemId) != nullptr)
            return core::makeError(core::ErrorCode::AlreadyExists,
                                   "The project already has a furniture item \"" + newItemId + "\".", newItemId);
        if (!placement.isRigid())
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "The placement of \"" + newItemId + "\" is not a proper rotation plus translation.",
                                   newItemId);
        formats::Library target = libraryOf(p);
        auto merged = mergeComponentLibrary(target, component.library, policy,
                                            newId ? newId : componentIdGenerator(newItemId));
        if (!merged) return merged.error();
        Remap remap;
        bool replaced = false, changedLibraries = false;
        for (const auto& e : merged.value().entries) {
            if (e.action == MergeAction::Replaced) replaced = true;
            if (e.action != MergeAction::Skipped) changedLibraries = true;
            if (e.action != MergeAction::Renamed) continue;
            auto& map = e.category == LibraryCategory::Material   ? remap.materials
                        : e.category == LibraryCategory::EdgeBand ? remap.edgeBands
                                                                  : remap.hardware;
            map[e.incomingId] = e.resultId;
        }
        Item item = component.item;
        if (!remap.empty()) remapItem(item, remap);
        if (auto* cab = item.cabinet()) {
            cab->id = domain::AssemblyId(newItemId);
            cab->name = component.meta.name;
            cab->placement = placement;
        } else {
            PanelItem& pn = *item.panel();
            pn.id = domain::AssemblyId(newItemId);
            pn.panel.name = component.meta.name;
            pn.panel.id = domain::PartId(newItemId + "/panel");
            pn.placement = placement;
        }
        p.libraries.materials = std::move(target.materials);
        p.libraries.edgeBands = std::move(target.edgeBands);
        p.libraries.hardware = std::move(target.hardware);
        p.items.push_back(std::move(item));
        if (auto generated = generateItem(p, newItemId); !generated)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Component \"" + component.meta.name + "\" cannot be generated as \"" + newItemId +
                                       "\" in this project: " + generated.error().message,
                                   newItemId);
        Affected a;
        if (replaced)
            for (const auto& i : p.items) a.items.push_back(i.id());
        else
            a.items = {newItemId};
        a.libraries = changedLibraries;
        return a;
    };
    return c;
}

} // namespace fos::project
