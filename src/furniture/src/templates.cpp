// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/furniture/templates.hpp"

#include "fos/furniture/parameters.hpp"

#include <cassert>

namespace fos::furniture::templates {
namespace {

CabinetDefinition start(domain::AssemblyId id, const char* name, const TemplateMaterials& m,
                        const ConstructionOptions& options, double width, double height, double depth,
                        double plinthHeight = 100.0) {
    auto made = makeCabinet(std::move(id), name, options);
    assert(made.ok()); // the templates only use supported option combinations
    CabinetDefinition def = std::move(made.value());
    def.materials.carcass = m.carcass;
    def.materials.back = m.back;
    def.materials.front = m.front;
    def.materials.visibleEdge = m.visibleEdge;
    def.materials.hiddenEdge = m.hiddenEdge;
    parametric::Transaction tx;
    tx.setValue(param::kWidth, width).setValue(param::kHeight, height).setValue(param::kDepth, depth);
    tx.setValue(param::kPlinthHeight, plinthHeight);
    [[maybe_unused]] const auto applied = def.parameters.apply(tx);
    assert(applied.ok());
    return def;
}

FrontSpec door(FrontKind kind, HandlePosition handle) {
    FrontSpec f;
    f.kind = kind;
    f.handle = handle;
    return f;
}

} // namespace

CabinetDefinition baseCabinet(domain::AssemblyId id, const TemplateMaterials& m) {
    ConstructionOptions o;
    o.top = TopKind::Rails;
    o.plinth = PlinthKind::SidesToFloor;
    CabinetDefinition def = start(std::move(id), "Base cabinet", m, o, 600, 820, 560);
    def.interior.shelfCount = 1;
    def.interior.front = door(FrontKind::DoorPair, HandlePosition::Top);
    return def;
}

CabinetDefinition wallCabinet(domain::AssemblyId id, const TemplateMaterials& m) {
    CabinetDefinition def = start(std::move(id), "Wall cabinet", m, {}, 600, 720, 320);
    def.interior.shelfCount = 2;
    def.interior.front = door(FrontKind::Door, HandlePosition::Bottom);
    return def;
}

CabinetDefinition tallCabinet(domain::AssemblyId id, const TemplateMaterials& m) {
    ConstructionOptions o;
    o.plinth = PlinthKind::SidesToFloor;
    CabinetDefinition def = start(std::move(id), "Tall cabinet", m, o, 600, 2100, 560);
    Section lower, upper;
    lower.size = SectionSize::fixed(700);
    lower.shelfCount = 1;
    upper.size = SectionSize::weight(1);
    upper.shelfCount = 3;
    def.interior.split = SplitDirection::Horizontal;
    def.interior.children = {lower, upper};
    def.interior.front = door(FrontKind::Door, HandlePosition::Center);
    return def;
}

CabinetDefinition drawerUnit(domain::AssemblyId id, const TemplateMaterials& m) {
    ConstructionOptions o;
    o.plinth = PlinthKind::Feet;
    o.back = BackMount::Grooved;
    CabinetDefinition def = start(std::move(id), "Drawer unit", m, o, 600, 820, 560);
    FrontSpec f = door(FrontKind::DrawerStack, HandlePosition::Center);
    f.drawerHeights = {SectionSize::weight(2), SectionSize::weight(2), SectionSize::weight(1), SectionSize::weight(1)};
    def.interior.front = f;
    return def;
}

CabinetDefinition wardrobeSection(domain::AssemblyId id, const TemplateMaterials& m) {
    ConstructionOptions o;
    o.plinth = PlinthKind::SidesToFloor;
    CabinetDefinition def = start(std::move(id), "Wardrobe section", m, o, 1000, 2200, 580, 80);
    Section left, right;
    left.shelfCount = 4;
    right.shelfCount = 1;
    def.interior.split = SplitDirection::Vertical;
    def.interior.children = {left, right};
    def.interior.front = door(FrontKind::DoorPair, HandlePosition::Center);
    return def;
}

CabinetDefinition openShelving(domain::AssemblyId id, const TemplateMaterials& m) {
    CabinetDefinition def = start(std::move(id), "Open shelving unit", m, {}, 800, 1800, 300);
    Section half;
    half.shelfCount = 2;
    def.interior.split = SplitDirection::Horizontal;
    def.interior.children = {half, half};
    return def;
}

} // namespace fos::furniture::templates
