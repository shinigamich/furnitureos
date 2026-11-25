// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/merge.hpp"

#include "fos/formats/codec.hpp"
#include "fos/project/components.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>

namespace fos::project {

namespace {

using formats::LibraryCategory;
using formats::MergeAction;

using Remap = LibraryRemap;

// --- planning -------------------------------------------------------------------------

/// Copies the source entries named by `ids` (sorted) into `out`; NotFound names the first missing one.
template <class T>
core::Result<void> pick(const std::vector<T>& from, const std::map<std::string, std::string>& ids,
                        const char* what, std::vector<T>& out) {
    for (const auto& [id, itemId] : ids) {
        const auto it = std::find_if(from.begin(), from.end(), [&](const T& e) { return e.id.str() == id; });
        if (it == from.end())
            return core::makeError(core::ErrorCode::NotFound,
                                   "Item \"" + itemId + "\" of the source project references " + what + " \"" + id +
                                       "\", which is not in the source project's libraries.",
                                   id);
        out.push_back(*it);
    }
    return {};
}

template <class T> bool conflicts(const std::vector<T>& target, const T* incoming) {
    if (incoming == nullptr) return false;
    const auto it = std::find_if(target.begin(), target.end(), [&](const T& e) { return e.id == incoming->id; });
    return it != target.end() && formats::toJson(*it) != formats::toJson(*incoming);
}

template <class T> const T* findEntry(const std::vector<T>& list, const std::string& id) {
    const auto it = std::find_if(list.begin(), list.end(), [&](const T& e) { return e.id.str() == id; });
    return it == list.end() ? nullptr : &*it;
}


struct Prepared {
    MergePlan plan;
    std::vector<const Item*> items;   ///< Selected source items, source order.
    formats::Library incoming;        ///< Union of the referenced source entries.
};

std::set<std::string> libraryIds(const formats::Library& lib) {
    std::set<std::string> ids;
    for (const auto& e : lib.materials) ids.insert(e.id.str());
    for (const auto& e : lib.edgeBands) ids.insert(e.id.str());
    for (const auto& e : lib.hardware) ids.insert(e.id.str());
    return ids;
}

formats::Library libraryOf(const Project& p) {
    formats::Library lib;
    lib.materials = p.libraries.materials;
    lib.edgeBands = p.libraries.edgeBands;
    lib.hardware = p.libraries.hardware;
    return lib;
}

/// "merge.lib1", "merge.lib2", ...: the first id not used by any entry of `target` nor returned before.
formats::IdGenerator idGenerator(const formats::Library& target) {
    auto used = std::make_shared<std::set<std::string>>(libraryIds(target));
    return [used]() {
        for (int n = 1;; ++n) {
            std::string id = "merge.lib" + std::to_string(n);
            if (used->insert(id).second) return id;
        }
    };
}

core::Result<Prepared> prepare(const Project& target, const Project& source, const MergeRequest& request) {
    if (source.items.empty())
        return core::makeError(core::ErrorCode::InvalidArgument, "The source project has no furniture items to merge.");
    if (!request.offset.isRigid())
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The merge offset is not a proper rotation plus translation.");
    std::set<std::string> wanted;
    for (const auto& id : request.itemIds) {
        if (!wanted.insert(id).second)
            return core::makeError(core::ErrorCode::InvalidArgument, "Item \"" + id + "\" is requested twice.", id);
        if (source.findItem(id) == nullptr) {
            std::string ids;
            for (const auto& i : source.items) ids += (ids.empty() ? "" : ", ") + i.id();
            return core::makeError(core::ErrorCode::NotFound,
                                   "The source project has no furniture item \"" + id + "\" (it has " + ids + ").", id);
        }
    }
    Prepared out;
    std::vector<std::string> used;
    for (const auto& i : target.items) used.push_back(i.id());
    std::map<std::string, std::string> mats, edges, hw; // referenced id -> first item referencing it
    for (const auto& item : source.items) {
        if (!wanted.empty() && wanted.count(item.id()) == 0) continue;
        out.items.push_back(&item);
        const bool clash = std::find(used.begin(), used.end(), item.id()) != used.end();
        MergedItem m{item.id(), clash ? freeItemId(item.id(), used) : item.id(), item.name()};
        used.push_back(m.newId);
        out.plan.items.push_back(std::move(m));
        const LibraryRefs refs = libraryReferences(item);
        for (const auto& id : refs.materials) mats.emplace(id, item.id());
        for (const auto& id : refs.edgeBands) edges.emplace(id, item.id());
        for (const auto& id : refs.hardware) hw.emplace(id, item.id());
    }
    if (auto r = pick(source.libraries.materials, mats, "material", out.incoming.materials); !r) return r.error();
    if (auto r = pick(source.libraries.edgeBands, edges, "edge band", out.incoming.edgeBands); !r) return r.error();
    if (auto r = pick(source.libraries.hardware, hw, "hardware", out.incoming.hardware); !r) return r.error();

    // The library merge of insertComponent(), dry-run on the union of the referenced entries.
    Component all;
    all.item = *out.items.front();
    all.library = out.incoming;
    auto report = planComponentInsert(target, all, request.conflictPolicy, idGenerator(libraryOf(target)));
    if (!report) return report.error();
    for (const auto& e : report.value().entries) {
        bool conflict = false;
        switch (e.category) {
        case LibraryCategory::Material:
            conflict = conflicts(target.libraries.materials, findEntry(out.incoming.materials, e.incomingId));
            break;
        case LibraryCategory::EdgeBand:
            conflict = conflicts(target.libraries.edgeBands, findEntry(out.incoming.edgeBands, e.incomingId));
            break;
        case LibraryCategory::Hardware:
            conflict = conflicts(target.libraries.hardware, findEntry(out.incoming.hardware, e.incomingId));
            break;
        }
        out.plan.library.push_back({e, conflict});
    }
    return out;
}

template <class T> void takeForApply(const std::vector<T>& incoming, const formats::MergeEntry& e, std::vector<T>& out) {
    const T* entry = findEntry(incoming, e.incomingId);
    if (entry == nullptr) return;
    T copy = *entry;
    copy.id = decltype(copy.id)(e.resultId);
    out.push_back(std::move(copy));
}

/// Applies the merge to `p` in place (callers pass a copy).
core::Result<std::pair<MergePlan, Affected>> apply(Project& p, const Project& source, const MergeRequest& request) {
    auto prepared = prepare(p, source, request);
    if (!prepared) return prepared.error();
    const Prepared& pr = prepared.value();

    // 1. Library: renamed entries are added under their result id, replaced ones overwrite
    //    the target entry, skipped ones are left out (so the merge itself cannot conflict).
    formats::Library changes;
    Remap remap;
    bool replaced = false, changedLibraries = false;
    for (const auto& le : pr.plan.library) {
        const auto& e = le.entry;
        if (e.action == MergeAction::Skipped) continue;
        changedLibraries = true;
        if (e.action == MergeAction::Replaced) replaced = true;
        switch (e.category) {
        case LibraryCategory::Material:
            takeForApply(pr.incoming.materials, e, changes.materials);
            if (e.action == MergeAction::Renamed) remap.materials[e.incomingId] = e.resultId;
            break;
        case LibraryCategory::EdgeBand:
            takeForApply(pr.incoming.edgeBands, e, changes.edgeBands);
            if (e.action == MergeAction::Renamed) remap.edgeBands[e.incomingId] = e.resultId;
            break;
        case LibraryCategory::Hardware:
            takeForApply(pr.incoming.hardware, e, changes.hardware);
            if (e.action == MergeAction::Renamed) remap.hardware[e.incomingId] = e.resultId;
            break;
        }
    }
    formats::Library lib = libraryOf(p);
    if (auto r = formats::mergeLibrary(lib, changes, formats::ConflictPolicy::Replace); !r) return r.error();
    p.libraries.materials = std::move(lib.materials);
    p.libraries.edgeBands = std::move(lib.edgeBands);
    p.libraries.hardware = std::move(lib.hardware);

    // 2. Items: rewrite references, then insertComponent() (id, part ids, placement, generation
    //    check). Its own library merge is empty: every entry the item needs is now present.
    Affected affected;
    for (std::size_t i = 0; i < pr.items.size(); ++i) {
        const MergedItem& m = pr.plan.items[i];
        Component c;
        c.meta.name = m.name;
        c.item = *pr.items[i];
        if (!remap.empty()) remapLibraryReferences(c.item, remap);
        const geometry::Transform placement = request.offset * pr.items[i]->placement();
        auto inserted = insertComponent(std::move(c), m.newId, placement, formats::ConflictPolicy::Skip).apply(p);
        if (!inserted) {
            auto err = inserted.error();
            err.message = "Cannot merge item \"" + m.sourceId + "\": " + err.message;
            return err;
        }
        affected.items.push_back(m.newId);
    }
    if (replaced) {
        affected.items.clear();
        for (const auto& item : p.items) affected.items.push_back(item.id());
    }
    affected.libraries = changedLibraries;
    return std::pair{pr.plan, affected};
}

} // namespace

std::vector<const MergedLibraryEntry*> MergePlan::withAction(formats::MergeAction action) const {
    std::vector<const MergedLibraryEntry*> out;
    for (const auto& e : library)
        if (e.entry.action == action) out.push_back(&e);
    return out;
}

std::vector<const MergedLibraryEntry*> MergePlan::conflicts() const {
    std::vector<const MergedLibraryEntry*> out;
    for (const auto& e : library)
        if (e.conflict) out.push_back(&e);
    return out;
}

std::string freeItemId(const std::string& sourceId, const std::vector<std::string>& used) {
    std::size_t end = sourceId.size();
    while (end > 0 && sourceId[end - 1] >= '0' && sourceId[end - 1] <= '9') --end;
    const std::string prefix = sourceId.substr(0, end);
    for (int n = 1;; ++n) {
        std::string id = prefix + std::to_string(n);
        if (std::find(used.begin(), used.end(), id) == used.end()) return id;
    }
}

core::Result<MergePlan> planMerge(const Project& target, const Project& source, const MergeRequest& request) {
    auto prepared = prepare(target, source, request);
    if (!prepared) return prepared.error();
    return std::move(prepared.value().plan);
}

core::Result<MergeResult> mergeProjects(const Project& target, const Project& source, const MergeRequest& request) {
    MergeResult result{target, {}, {}};
    auto r = apply(result.project, source, request);
    if (!r) return r.error();
    result.plan = std::move(r.value().first);
    result.affected = std::move(r.value().second);
    return result;
}

namespace commands {

Command mergeItems(Project source, MergeRequest request) {
    Command c;
    c.label = {"Merge items from {0}", {source.metadata.name}};
    c.apply = [source = std::move(source), request = std::move(request)](Project& p) -> core::Result<Affected> {
        auto r = apply(p, source, request);
        if (!r) return r.error();
        return r.value().second;
    };
    return c;
}

} // namespace commands

} // namespace fos::project
