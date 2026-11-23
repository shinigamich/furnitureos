// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/templates.hpp"

#include "fos/furniture/edit.hpp"
#include "fos/furniture/parameters.hpp"
#include "fos/furniture/templates.hpp"

#include <QCoreApplication>

#include <cmath>
#include <utility>

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
    case TemplateKind::DrawerUnitWithBoxes: return t::drawerUnitWithBoxes(aid, materials);
    case TemplateKind::WardrobeSection: return t::wardrobeSection(aid, materials);
    case TemplateKind::OpenShelving: return t::openShelving(aid, materials);
    case TemplateKind::BlindCornerCabinet: return t::blindCornerCabinet(aid, materials);
    case TemplateKind::LCornerCabinet: return t::lCornerCabinet(aid, materials);
    case TemplateKind::OvenTallHousing: return t::ovenTallHousing(aid, materials);
    case TemplateKind::DishwasherFront: return t::dishwasherFront(aid, materials);
    }
    return t::baseCabinet(aid, materials);
}

} // namespace

const std::vector<TemplateKind>& allTemplates() {
    static const std::vector<TemplateKind> kinds{TemplateKind::BaseCabinet,  TemplateKind::WallCabinet,
                                                 TemplateKind::TallCabinet,  TemplateKind::DrawerUnit,
                                                 TemplateKind::DrawerUnitWithBoxes, TemplateKind::WardrobeSection, TemplateKind::OpenShelving,
                                                 TemplateKind::BlindCornerCabinet, TemplateKind::LCornerCabinet,
                                                 TemplateKind::OvenTallHousing, TemplateKind::DishwasherFront};
    return kinds;
}

QString templateTitle(TemplateKind kind) {
    switch (kind) {
    case TemplateKind::BaseCabinet: return QCoreApplication::translate("fos::app::Templates", "Base cabinet");
    case TemplateKind::WallCabinet: return QCoreApplication::translate("fos::app::Templates", "Wall cabinet");
    case TemplateKind::TallCabinet: return QCoreApplication::translate("fos::app::Templates", "Tall cabinet");
    case TemplateKind::DrawerUnit: return QCoreApplication::translate("fos::app::Templates", "Drawer unit");
    case TemplateKind::DrawerUnitWithBoxes:
        return QCoreApplication::translate("fos::app::Templates", "Drawer unit with boxes");
    case TemplateKind::WardrobeSection: return QCoreApplication::translate("fos::app::Templates", "Wardrobe section");
    case TemplateKind::OpenShelving: return QCoreApplication::translate("fos::app::Templates", "Open shelving");
    case TemplateKind::BlindCornerCabinet:
        return QCoreApplication::translate("fos::app::Templates", "Blind corner cabinet");
    case TemplateKind::LCornerCabinet:
        return QCoreApplication::translate("fos::app::Templates", "L-shaped corner cabinet");
    case TemplateKind::OvenTallHousing:
        return QCoreApplication::translate("fos::app::Templates", "Tall oven housing");
    case TemplateKind::DishwasherFront:
        return QCoreApplication::translate("fos::app::Templates", "Dishwasher gap with decor front");
    }
    return {};
}

const char* templateToken(TemplateKind kind) noexcept {
    switch (kind) {
    case TemplateKind::BaseCabinet: return "base";
    case TemplateKind::WallCabinet: return "wall";
    case TemplateKind::TallCabinet: return "tall";
    case TemplateKind::DrawerUnit: return "drawers";
    case TemplateKind::DrawerUnitWithBoxes: return "drawer-boxes";
    case TemplateKind::WardrobeSection: return "wardrobe";
    case TemplateKind::OpenShelving: return "shelving";
    case TemplateKind::BlindCornerCabinet: return "corner-blind";
    case TemplateKind::LCornerCabinet: return "corner-l";
    case TemplateKind::OvenTallHousing: return "oven-tall";
    case TemplateKind::DishwasherFront: return "dishwasher";
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
            // Only sizes that differ from the template's current value are set, so a template
            // formula (dishwasher: width = appliance_gap_width + thickness) survives an
            // unchanged size.
            parametric::Transaction tx;
            const std::pair<const char*, double> wanted[] = {
                {furniture::param::kWidth, size->width},
                {furniture::param::kHeight, size->height},
                {furniture::param::kDepth, size->depth}};
            bool any = false;
            for (const auto& [param, value] : wanted) {
                const auto current = d.parameters.value(param);
                if (current && std::abs(current.value() - value) <= 1e-9) continue;
                tx.setValue(param, value);
                any = true;
            }
            if (!any) return {};
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
