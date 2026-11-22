// SPDX-License-Identifier: AGPL-3.0-only
// Batch: loading the listed projects and merging them into one namespaced model.
#include "fos/project/batch.hpp"

#include "fos/core/units.hpp"
#include "fos/formats/codec.hpp"

#include <cmath>

namespace fos::project {

namespace {

std::filesystem::path utf8Path(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string utf8Text(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string namespaced(const std::string& instance, const std::string& id) { return instance + "/" + id; }

domain::Assembly namespacedCopy(const domain::Assembly& source, const std::string& instance) {
    domain::Assembly a = source;
    a.id = domain::AssemblyId(namespaced(instance, source.id.str()));
    for (auto& p : a.parts) p.id = domain::PartId(namespaced(instance, p.id.str()));
    for (auto& h : a.hardware) {
        h.id = domain::HardwareInstanceId(namespaced(instance, h.id.str()));
        for (auto& ref : h.parts) ref = domain::PartId(namespaced(instance, ref.str()));
    }
    return a;
}

/// Library entries by id with the key of the project that brought them in.
template <class T> struct LibraryIndex {
    std::vector<T>* items = nullptr;
    std::map<std::string, std::pair<std::size_t, std::string>> byId; // id -> (index, owner key)
};

template <class T> std::string idOf(const T& v) { return v.id.str(); }

/// Conflicts of `incoming` against `index` (same id, different data), as messages.
template <class T>
void findConflicts(const LibraryIndex<T>& index, const std::vector<T>& incoming, const char* what,
                   const BatchProject& project, const std::vector<BatchProject>& all,
                   core::Diagnostics& out) {
    for (const T& v : incoming) {
        const auto it = index.byId.find(idOf(v));
        if (it == index.byId.end()) continue;
        if (formats::toJson((*index.items)[it->second.first]).dump() == formats::toJson(v).dump()) continue;
        std::string ownerName = it->second.second;
        for (const auto& p : all)
            if (p.key == it->second.second) ownerName = p.name();
        out.push_back({core::Severity::Error, "batch.library.conflict",
                       std::string(what) + " \"" + idOf(v) + "\" has different data in project \"" + project.name() +
                           "\" (key " + project.key + ") than in project \"" + ownerName + "\" (key " +
                           it->second.second + "); project \"" + project.name() +
                           "\" is left out of the batch. Give one of them a different id, make the data equal, or "
                           "list it as a material substitution.",
                       project.key});
    }
}

template <class T> void addEntries(LibraryIndex<T>& index, const std::vector<T>& incoming, const std::string& owner) {
    for (const T& v : incoming) {
        if (index.byId.count(idOf(v)) != 0) continue;
        index.byId.emplace(idOf(v), std::make_pair(index.items->size(), owner));
        index.items->push_back(v);
    }
}

} // namespace

std::string BatchProject::name() const {
    if (loaded && !loaded->project.metadata.name.empty()) return loaded->project.metadata.name;
    return key;
}

LoadedBatch loadBatchProjects(const Batch& batch, const std::filesystem::path& baseDir,
                              const formats::LoadOptions& options) {
    LoadedBatch out;
    out.batch = batch;
    out.baseDir = baseDir;
    for (const auto& entry : batch.entries) {
        BatchProject bp;
        bp.entry = entry;
        bp.key = batchKey(entry);
        const std::filesystem::path p = utf8Path(entry.path);
        bp.file = p.is_absolute() ? p : baseDir / p;
        auto r = loadProject(bp.file, options);
        if (!r) {
            bp.diagnostics.push_back({core::Severity::Error, "batch.project.load_failed",
                                      "Project \"" + bp.key + "\" of the batch (file '" + utf8Text(bp.file) +
                                          "') could not be loaded and is left out: " + r.error().message,
                                      bp.key});
        } else {
            for (auto d : r.value().diagnostics) {
                d.entityId = d.entityId.empty() ? bp.key : namespaced(bp.key, d.entityId);
                bp.diagnostics.push_back(std::move(d));
            }
            bp.loaded = std::move(r.value());
        }
        out.projects.push_back(std::move(bp));
    }
    return out;
}

core::Result<LoadedBatch> loadBatch(const std::filesystem::path& path, const formats::LoadOptions& options) {
    auto batch = loadBatchDocument(path, options);
    if (!batch) return batch.error();
    return loadBatchProjects(batch.value(), path.parent_path(), options);
}

const BatchProjectModel* BatchModel::projectOf(const std::string& id) const {
    const std::string instance = id.substr(0, id.find('/'));
    for (const auto& p : projects)
        for (const auto& k : p.instanceKeys)
            if (k == instance) return &p;
    return nullptr;
}

BatchModel mergeBatch(const LoadedBatch& loaded) {
    BatchModel out;
    domain::Model libs;
    LibraryIndex<domain::Material> materials{&libs.materials, {}};
    LibraryIndex<domain::EdgeBand> bands{&libs.edgeBands, {}};
    LibraryIndex<domain::HardwareDefinition> hardware{&libs.hardwareCatalog, {}};

    for (const auto& bp : loaded.projects)
        out.diagnostics.insert(out.diagnostics.end(), bp.diagnostics.begin(), bp.diagnostics.end());

    // 1. Libraries (conflicts exclude the later project) and namespaced copies.
    for (const auto& bp : loaded.projects) {
        BatchProjectModel pm;
        pm.key = bp.key;
        pm.name = bp.name();
        pm.note = bp.entry.note;
        pm.quantity = bp.entry.quantity;
        for (int c = 1; c <= bp.entry.quantity; ++c)
            pm.instanceKeys.push_back(bp.entry.quantity == 1 ? bp.key : bp.key + "#" + std::to_string(c));
        if (bp.loaded) {
            const domain::Model& m = bp.loaded->resolved.model;
            const std::size_t before = out.diagnostics.size();
            findConflicts(materials, m.materials, "Material", bp, loaded.projects, out.diagnostics);
            findConflicts(bands, m.edgeBands, "Edge band", bp, loaded.projects, out.diagnostics);
            findConflicts(hardware, m.hardwareCatalog, "Hardware definition", bp, loaded.projects, out.diagnostics);
            if (out.diagnostics.size() == before) {
                addEntries(materials, m.materials, bp.key);
                addEntries(bands, m.edgeBands, bp.key);
                addEntries(hardware, m.hardwareCatalog, bp.key);
                pm.included = true;
                for (const auto& instance : pm.instanceKeys)
                    for (const auto& a : m.assemblies) pm.model.assemblies.push_back(namespacedCopy(a, instance));
            }
        }
        out.projects.push_back(std::move(pm));
    }

    // 2. Explicit material substitutions (never silent).
    for (const auto& [from, to] : loaded.batch.materialSubstitutions) {
        const domain::Material* target = libs.findMaterial(domain::MaterialId(to));
        if (target == nullptr) {
            out.diagnostics.push_back({core::Severity::Error, "batch.substitution.unknown_target",
                                       "Material substitution \"" + from + "\" -> \"" + to + "\" is not applied: \"" +
                                           to + "\" is not in the library of any batch project.",
                                       from});
            continue;
        }
        if (const domain::Material* source = libs.findMaterial(domain::MaterialId(from));
            source != nullptr && std::abs(source->thickness - target->thickness) > core::kLengthEps) {
            out.diagnostics.push_back({core::Severity::Error, "batch.substitution.thickness_mismatch",
                                       "Material substitution \"" + from + "\" -> \"" + to + "\" is not applied: \"" +
                                           from + "\" is " + core::formatLength(source->thickness) + " mm thick but \"" +
                                           to + "\" is " + core::formatLength(target->thickness) + " mm.",
                                       from});
            continue;
        }
        for (auto& pm : out.projects) {
            int changed = 0;
            for (auto& a : pm.model.assemblies)
                for (auto& p : a.parts)
                    if (p.material.str() == from) {
                        p.material = domain::MaterialId(to);
                        ++changed;
                    }
            if (changed > 0)
                out.diagnostics.push_back({core::Severity::Info, "batch.substitution.applied",
                                           std::to_string(changed) + " part(s) of project \"" + pm.name +
                                               "\" made of \"" + from + "\" are cut from \"" + to + "\" (" +
                                               target->name + ") by the batch material substitution.",
                                           pm.key});
        }
    }

    // 3. Merged model and per-project models share the merged libraries.
    out.model.materials = libs.materials;
    out.model.edgeBands = libs.edgeBands;
    out.model.hardwareCatalog = libs.hardwareCatalog;
    bool any = false;
    for (auto& pm : out.projects) {
        pm.model.materials = libs.materials;
        pm.model.edgeBands = libs.edgeBands;
        pm.model.hardwareCatalog = libs.hardwareCatalog;
        any = any || pm.included;
        out.model.assemblies.insert(out.model.assemblies.end(), pm.model.assemblies.begin(), pm.model.assemblies.end());
    }
    if (!any)
        out.diagnostics.push_back({core::Severity::Warning, "batch.empty",
                                   "The batch has no usable project: every output is empty.", ""});
    return out;
}

} // namespace fos::project
