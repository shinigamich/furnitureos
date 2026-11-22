// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/assembly_sheet.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

namespace fos::production {
namespace {

bool contains(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

domain::HardwareDefinition def(const char* id, domain::HardwareKind kind, const char* name) {
    domain::HardwareDefinition d;
    d.id = domain::HardwareId(id);
    d.kind = kind;
    d.name = name;
    return d;
}

domain::HardwareInstance inst(const char* id, const char* definition, int quantity, std::vector<std::string> parts) {
    domain::HardwareInstance h;
    h.id = domain::HardwareInstanceId(id);
    h.definition = domain::HardwareId(definition);
    h.quantity = quantity;
    for (auto& p : parts) h.parts.push_back(domain::PartId(p));
    return h;
}

/// basicCabinet (#1 left, #2 right, #3 bottom, #4 top, #5 shelf, #6 back) + door #7 and hardware:
///   8 confirmats sides<->bottom/top      -> Carcass (latest connected role: Side/Bottom = Carcass)
///   2 confirmats side<->shelf            -> Shelves (Shelf is later than Side)
///   4 shelf pins (fixture)               -> Shelves (kind)
///   2 hinges, 1 handle                   -> Fronts, Handles (kind)
///   1 dowel connected to no known part   -> Other
domain::Model cabinetWithDoor() {
    auto m = domain::fixtures::basicCabinet();
    using H = domain::HardwareKind;
    m.hardwareCatalog.push_back(def("hw.conf", H::Confirmat, "Confirmat 7x50"));
    m.hardwareCatalog.push_back(def("hw.hinge", H::Hinge, "Hinge 110"));
    m.hardwareCatalog.push_back(def("hw.handle", H::Handle, "Bar handle 128"));
    m.hardwareCatalog.push_back(def("hw.dowel", H::Dowel, "Dowel 8x35"));
    auto& a = m.assemblies[0];
    auto door = testing::panel("cab1/door", "Door", 716, 796, 18, "mat.pb18");
    door.role = domain::PartRole::Door;
    a.parts.push_back(door);
    a.hardware.push_back(inst("cab1/conf.carcass", "hw.conf", 8, {"cab1/side.left", "cab1/bottom"}));
    a.hardware.push_back(inst("cab1/conf.shelf", "hw.conf", 2, {"cab1/side.right", "cab1/shelf.1"}));
    a.hardware.push_back(inst("cab1/hinges", "hw.hinge", 2, {"cab1/door", "cab1/side.left"}));
    a.hardware.push_back(inst("cab1/handle", "hw.handle", 1, {"cab1/door"}));
    a.hardware.push_back(inst("cab1/dowel", "hw.dowel", 1, {"cab1/ghost"}));
    return m;
}

TEST(AssemblySheet, StepsFollowRolesAndHardwareKinds) {
    const auto sheets = assemblySheets(cabinetWithDoor());
    ASSERT_EQ(sheets.sheets.size(), 1u);
    const auto& steps = sheets.sheets[0].steps;
    ASSERT_EQ(steps.size(), 6u);
    EXPECT_EQ(steps[0].kind, AssemblyStepKind::Carcass);
    EXPECT_EQ(steps[1].kind, AssemblyStepKind::Back);
    EXPECT_EQ(steps[2].kind, AssemblyStepKind::ShelvesAndDividers);
    EXPECT_EQ(steps[3].kind, AssemblyStepKind::Fronts);
    EXPECT_EQ(steps[4].kind, AssemblyStepKind::Handles);
    EXPECT_EQ(steps[5].kind, AssemblyStepKind::Other);
    for (std::size_t i = 0; i < steps.size(); ++i) EXPECT_EQ(steps[i].number, static_cast<int>(i + 1));

    ASSERT_EQ(steps[0].parts.size(), 4u); // #1 #2 #3 #4
    EXPECT_EQ(steps[0].parts[0].partNumber, 1);
    EXPECT_EQ(steps[0].parts[3].partNumber, 4);
    ASSERT_EQ(steps[0].hardware.size(), 1u);
    EXPECT_EQ(steps[0].hardware[0].quantity, 8);

    ASSERT_EQ(steps[1].parts.size(), 1u);
    EXPECT_EQ(steps[1].parts[0].partNumber, 6);
    EXPECT_TRUE(steps[1].hardware.empty());

    // Shelves: shelf #5; hardware in catalogue order: pins (fixture entry first) then confirmats.
    ASSERT_EQ(steps[2].hardware.size(), 2u);
    EXPECT_EQ(steps[2].hardware[0].name, "Shelf support pin 5 mm (generic)");
    EXPECT_EQ(steps[2].hardware[0].quantity, 4);
    EXPECT_EQ(steps[2].hardware[1].quantity, 2);

    EXPECT_EQ(steps[3].parts[0].partNumber, 7);
    EXPECT_EQ(steps[3].hardware[0].name, "Hinge 110");
    EXPECT_TRUE(steps[4].parts.empty());
    EXPECT_EQ(steps[4].hardware[0].quantity, 1);
    EXPECT_EQ(steps[5].hardware[0].name, "Dowel 8x35");

    const std::string text = assemblySheetText(sheets.sheets[0]);
    EXPECT_TRUE(contains(text, "Generic step order")) << text;
    EXPECT_TRUE(contains(text, "Step 1: Carcass")) << text;
    EXPECT_TRUE(contains(text, "8 x Confirmat 7x50")) << text;
    EXPECT_TRUE(contains(text, "Step 4: Doors and drawer fronts")) << text;

    const std::string svg = assemblySheetSvg(sheets.sheets[0], "Kitchen & Co");
    EXPECT_TRUE(contains(svg, "width=\"210mm\""));
    EXPECT_TRUE(contains(svg, "Kitchen &amp; Co"));
    EXPECT_TRUE(contains(svg, "Step 5: Handles"));
}

TEST(AssemblySheet, RoleMappingAndBadHardware) {
    EXPECT_EQ(stepForRole(domain::PartRole::Plinth), AssemblyStepKind::Carcass);
    EXPECT_EQ(stepForRole(domain::PartRole::Partition), AssemblyStepKind::ShelvesAndDividers);
    EXPECT_EQ(stepForRole(domain::PartRole::DrawerBottom), AssemblyStepKind::DrawerBoxes);
    EXPECT_EQ(stepForRole(domain::PartRole::DrawerFront), AssemblyStepKind::Fronts);
    EXPECT_EQ(stepForRole(domain::PartRole::Worktop), AssemblyStepKind::Other);

    auto m = domain::fixtures::basicCabinet();
    m.assemblies[0].hardware.push_back(inst("cab1/bad", "hw.shelf_pin_5", 0, {}));
    m.assemblies[0].hardware.push_back(inst("cab1/unknown", "hw.nope", 3, {"cab1/back"}));
    const auto sheets = assemblySheets(m);
    ASSERT_EQ(sheets.diagnostics.size(), 2u);
    EXPECT_EQ(sheets.diagnostics[0].code, "production.hardware.invalid_quantity");
    EXPECT_EQ(sheets.diagnostics[1].code, "production.hardware.unknown_definition");
    // The unknown definition is placed by its connected part (back) and listed by its id.
    const auto& back = sheets.sheets[0].steps[1];
    ASSERT_EQ(back.kind, AssemblyStepKind::Back);
    ASSERT_EQ(back.hardware.size(), 1u);
    EXPECT_TRUE(back.hardware[0].unknownDefinition);
    EXPECT_EQ(back.hardware[0].name, "hw.nope");
    EXPECT_EQ(back.hardware[0].quantity, 3);

    EXPECT_TRUE(assemblySheets(domain::Model{}).sheets.empty());
}

} // namespace
} // namespace fos::production
