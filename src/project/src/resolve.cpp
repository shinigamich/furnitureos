// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/resolve.hpp"

#include "fos/furniture/generate.hpp"
#include "fos/project/limits.hpp"

#include <set>

namespace fos::project {
namespace {

template <class T> core::Result<void> uniqueIds(const std::vector<T>& list, const char* what) {
    std::set<std::string> seen;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::string& id = list[i].id.str();
        if (id.empty())
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   std::string("The ") + what + " at position " + std::to_string(i + 1) + " has an empty id.");
        if (!seen.insert(id).second)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   std::string("Two ") + what + "s share the id \"" + id + "\"; ids must be unique.", id);
    }
    return {};
}

core::Result<domain::Assembly> generateOne(const Item& item, const furniture::Libraries& libs,
                                           core::Diagnostics& diagnostics) {
    if (const auto* cab = item.cabinet()) {
        auto gen = furniture::generate(*cab, libs);
        if (!gen) return gen.error();
        domain::Assembly a = std::move(gen.value().assembly);
        a.id = cab->id;
        a.name = cab->name;
        a.placement = cab->placement;
        for (auto& d : gen.value().diagnostics) diagnostics.push_back(std::move(d));
        return a;
    }
    const PanelItem& p = *item.panel();
    auto gen = furniture::generatePanel(p.panel, libs);
    if (!gen) return gen.error();
    domain::Assembly a;
    a.id = p.id;
    a.name = p.panel.name;
    a.placement = p.placement;
    a.parts.push_back(std::move(gen.value().part));
    for (auto& d : gen.value().diagnostics) diagnostics.push_back(std::move(d));
    return a;
}

} // namespace

core::Result<void> validateStructure(const Project& project) {
    if (auto r = checkProjectLimits(project); !r) return r;
    if (auto r = uniqueIds(project.libraries.materials, "material"); !r) return r;
    if (auto r = uniqueIds(project.libraries.edgeBands, "edge band"); !r) return r;
    if (auto r = uniqueIds(project.libraries.hardware, "hardware definition"); !r) return r;
    std::set<std::string> seen;
    for (std::size_t i = 0; i < project.items.size(); ++i) {
        const Item& item = project.items[i];
        if (item.id().empty())
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "The furniture item at position " + std::to_string(i + 1) + " has an empty id.");
        if (!seen.insert(item.id()).second)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Two furniture items share the id \"" + item.id() + "\"; item ids must be unique.",
                                   item.id());
        if (const auto* p = item.panel(); p != nullptr && p->panel.id.empty())
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Panel item \"" + item.id() + "\" has an empty part id.", item.id());
    }
    return {};
}

core::Result<domain::Assembly> generateItem(const Project& project, const std::string& itemId,
                                            core::Diagnostics* diagnostics) {
    const Item* item = project.findItem(itemId);
    if (item == nullptr)
        return core::makeError(core::ErrorCode::NotFound, "The project has no furniture item \"" + itemId + "\".", itemId);
    core::Diagnostics local;
    auto a = generateOne(*item, project.libraries, local);
    if (diagnostics != nullptr) diagnostics->insert(diagnostics->end(), local.begin(), local.end());
    return a;
}

core::Result<Resolved> resolve(const Project& project) {
    if (auto r = validateStructure(project); !r) return r.error();
    Resolved out;
    out.model.materials = project.libraries.materials;
    out.model.edgeBands = project.libraries.edgeBands;
    out.model.hardwareCatalog = project.libraries.hardware;
    core::Diagnostics projectLevel;
    for (const Item& item : project.items) {
        ItemResolution res;
        res.itemId = item.id();
        auto a = generateOne(item, project.libraries, res.diagnostics);
        if (a) {
            res.generated = true;
            out.model.assemblies.push_back(std::move(a.value()));
        } else {
            res.diagnostics.push_back({core::Severity::Error, "project.item.generation_failed",
                                       "Furniture item \"" + item.name() + "\" (" + item.id() +
                                           ") could not be generated: " + a.error().message,
                                       item.id()});
        }
        out.diagnostics.insert(out.diagnostics.end(), res.diagnostics.begin(), res.diagnostics.end());
        out.items.push_back(std::move(res));
    }
    std::set<std::string> partIds;
    for (const auto& a : out.model.assemblies)
        for (const auto& part : a.parts)
            if (!partIds.insert(part.id.str()).second)
                projectLevel.push_back({core::Severity::Error, "project.part.duplicate_id",
                                        "Part id \"" + part.id.str() + "\" occurs more than once (again in item \"" +
                                            a.id.str() + "\"); part drawings and labels cannot tell the parts apart.",
                                        part.id.str()});
    out.diagnostics.insert(out.diagnostics.end(), projectLevel.begin(), projectLevel.end());
    return out;
}

} // namespace fos::project
