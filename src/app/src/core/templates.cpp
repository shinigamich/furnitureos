// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/templates.hpp"

#include "fos/furniture/edit.hpp"
#include "fos/furniture/parameters.hpp"
#include "fos/furniture/templates.hpp"

#include <QCoreApplication>

namespace fos::app {
namespace {

furniture::CabinetDefinition makeDefinition(TemplateKind kind, const std::string& id) {
    namespace t = furniture::templates;
    const auto materials = project::defaultTemplateMaterials();
    const domain::AssemblyId aid(id);
    switch (kind) {
    case TemplateKind::BaseCabinet: return t::baseCabinet(aid, materials);
    case TemplateKind::WallCabinet: return t::wallCabinet(aid, materials);
    case TemplateKind::TallCabinet: return t::tallCabinet(aid, materials);
    case TemplateKind::DrawerUnit: return t::drawerUnit(aid, materials);
    case TemplateKind::WardrobeSection: return t::wardrobeSection(aid, materials);
    case TemplateKind::OpenShelving: return t::openShelving(aid, materials);
    }
    return t::baseCabinet(aid, materials);
}

} // namespace

const std::vector<TemplateKind>& allTemplates() {
    static const std::vector<TemplateKind> kinds{TemplateKind::BaseCabinet,  TemplateKind::WallCabinet,
                                                 TemplateKind::TallCabinet,  TemplateKind::DrawerUnit,
                                                 TemplateKind::WardrobeSection, TemplateKind::OpenShelving};
    return kinds;
}

QString templateTitle(TemplateKind kind) {
    switch (kind) {
    case TemplateKind::BaseCabinet: return QCoreApplication::translate("fos::app::Templates", "Base cabinet");
    case TemplateKind::WallCabinet: return QCoreApplication::translate("fos::app::Templates", "Wall cabinet");
    case TemplateKind::TallCabinet: return QCoreApplication::translate("fos::app::Templates", "Tall cabinet");
    case TemplateKind::DrawerUnit: return QCoreApplication::translate("fos::app::Templates", "Drawer unit");
    case TemplateKind::WardrobeSection: return QCoreApplication::translate("fos::app::Templates", "Wardrobe section");
    case TemplateKind::OpenShelving: return QCoreApplication::translate("fos::app::Templates", "Open shelving");
    }
    return {};
}

const char* templateToken(TemplateKind kind) noexcept {
    switch (kind) {
    case TemplateKind::BaseCabinet: return "base";
    case TemplateKind::WallCabinet: return "wall";
    case TemplateKind::TallCabinet: return "tall";
    case TemplateKind::DrawerUnit: return "drawers";
    case TemplateKind::WardrobeSection: return "wardrobe";
    case TemplateKind::OpenShelving: return "shelving";
    }
    return "";
}

TemplateSize defaultTemplateSize(TemplateKind kind) {
    const auto def = makeDefinition(kind, "t");
    const auto& g = def.parameters;
    return {g.value(furniture::param::kWidth).valueOr(0), g.value(furniture::param::kHeight).valueOr(0),
            g.value(furniture::param::kDepth).valueOr(0)};
}

core::Result<project::Item> makeTemplateItem(TemplateKind kind, const std::string& id, const std::string& name,
                                             const furniture::Libraries& libraries,
                                             const std::optional<TemplateSize>& size) {
    furniture::CabinetDefinition def = makeDefinition(kind, id);
    if (!name.empty()) def.name = name;
    if (size) {
        // One atomic edit: all three or none (the furniture module validates the result).
        auto edited = furniture::applyEdit(def, libraries, [&](furniture::CabinetDefinition& d) -> core::Result<void> {
            parametric::Transaction tx;
            tx.setValue(furniture::param::kWidth, size->width);
            tx.setValue(furniture::param::kHeight, size->height);
            tx.setValue(furniture::param::kDepth, size->depth);
            auto applied = d.parameters.apply(tx);
            if (!applied) return applied.error();
            return {};
        });
        if (!edited) return edited.error();
    }
    project::Item item;
    item.definition = std::move(def);
    return item;
}

std::string nextItemId(const project::Project& project, const std::string& prefix) {
    for (int n = 1;; ++n) {
        std::string id = prefix + std::to_string(n);
        if (!project.findItem(id)) return id;
    }
}

std::string uniqueItemName(const project::Project& project, const std::string& base) {
    auto taken = [&](const std::string& name) {
        for (const auto& item : project.items)
            if (item.name() == name) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int n = 2;; ++n) {
        std::string candidate = base + " " + std::to_string(n);
        if (!taken(candidate)) return candidate;
    }
}

} // namespace fos::app
