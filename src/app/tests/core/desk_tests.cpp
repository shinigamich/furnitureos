// SPDX-License-Identifier: AGPL-3.0-only
// Desks and tables in the application: templates, desk view-model, undoable desk edits.
//
// Desk template (furniture templates.cpp): 1400 x 740 x 700, 18 mm, end panels, modesty panel
// 350 high with setback 100, cable slot 300 x 60, 3-drawer pedestal 400 wide on the right.
// Table template: 1600 x 750 x 800 on four generic legs (hw.leg_generic, not in the catalogue).
#include "fos/app/browser_tree.hpp"
#include "fos/app/command_text.hpp"
#include "fos/app/desk_rows.hpp"
#include "fos/app/templates.hpp"
#include "test_support.hpp"

#include <algorithm>

namespace fos::app::test {
namespace {

const furniture::CabinetDefinition& cabinet(const Session& s) { return *s.project().items.at(0).cabinet(); }

void expectGeneratedWithoutErrors(const Session& s) {
    ASSERT_NE(s.resolved(), nullptr) << s.resolveError();
    for (const auto& item : s.resolved()->items) EXPECT_TRUE(item.generated);
    for (const auto& d : s.resolved()->diagnostics) EXPECT_NE(d.severity, core::Severity::Error) << d.message;
}

bool hasLeg(const Session& s) { return s.project().libraries.findHardware(domain::HardwareId("hw.leg_generic")) != nullptr; }

} // namespace

TEST(DeskTemplates, OfferedGenerateAndBringTheirLeg) {
    const auto& kinds = allTemplates();
    for (TemplateKind k : {TemplateKind::Desk, TemplateKind::Table})
        EXPECT_NE(std::find(kinds.begin(), kinds.end(), k), kinds.end());
    EXPECT_STREQ(templateToken(TemplateKind::Desk), "desk");
    EXPECT_STREQ(templateToken(TemplateKind::Table), "table");
    EXPECT_EQ(templateTitle(TemplateKind::Table), QStringLiteral("Table"));
    const TemplateSize d = defaultTemplateSize(TemplateKind::Desk);
    EXPECT_DOUBLE_EQ(d.width, 1400);
    EXPECT_DOUBLE_EQ(d.height, 740);
    EXPECT_DOUBLE_EQ(d.depth, 700);

    Session desk;
    ASSERT_TRUE(desk.newFromTemplate(TemplateKind::Desk).ok());
    expectGeneratedWithoutErrors(desk);
    EXPECT_TRUE(isDesk(cabinet(desk)));
    EXPECT_NE(part(desk, "cab1/modesty"), nullptr);
    EXPECT_NE(part(desk, "cab1/pedestal/side.left"), nullptr);
    EXPECT_FALSE(hasLeg(desk)); // end panels need no leg

    // A new project from the table template gets the generic leg in its library.
    Session table;
    ASSERT_TRUE(table.newFromTemplate(TemplateKind::Table, TemplateSize{1800, 750, 900}).ok());
    expectGeneratedWithoutErrors(table);
    EXPECT_TRUE(hasLeg(table));
    EXPECT_EQ(part(table, "cab1/end.left"), nullptr);
    const domain::Part* top = part(table, "cab1/top");
    ASSERT_NE(top, nullptr);
    EXPECT_DOUBLE_EQ(top->length, 1800);
    EXPECT_DOUBLE_EQ(top->width, 900);

    // Added to an existing project (commands::addItem adds the leg); undo removes both.
    Session mixed;
    ASSERT_TRUE(mixed.newFromTemplate(TemplateKind::BaseCabinet).ok());
    ASSERT_TRUE(mixed.addFromTemplate(TemplateKind::Table).ok());
    expectGeneratedWithoutErrors(mixed);
    EXPECT_TRUE(hasLeg(mixed));
    ASSERT_TRUE(mixed.undo().ok());
    EXPECT_FALSE(hasLeg(mixed));
}

TEST(DeskRows, InputSpecAndDescription) {
    furniture::DeskSpec spec;
    spec.cableCutout = furniture::DeskCableCutout{};
    spec.pedestal = furniture::DeskPedestal{furniture::DeskPedestalSide::Left, 450, 4};
    spec.legInset = 55;
    DeskInput in = deskInput(spec);
    EXPECT_TRUE(in.cableSlot);
    EXPECT_TRUE(in.pedestal);
    EXPECT_DOUBLE_EQ(in.pedestalSpec.width, 450);
    EXPECT_EQ(deskSpec(in, spec), spec); // round trip
    // Legs: modesty panel and pedestal are dropped; leg inset of the base is kept.
    in.support = furniture::DeskSupport::Legs;
    const furniture::DeskSpec legs = deskSpec(in, spec);
    EXPECT_FALSE(legs.modestyPanel);
    EXPECT_FALSE(legs.pedestal.has_value());
    EXPECT_TRUE(legs.cableCutout.has_value());
    EXPECT_DOUBLE_EQ(legs.legInset, 55);

    furniture::CabinetDefinition def;
    EXPECT_TRUE(deskDescription(def).isEmpty());
    def.desk = spec;
    EXPECT_EQ(deskDescription(def), QStringLiteral("Desk on end panels, modesty panel, cable slot, 4-drawer pedestal left"));
    def.desk = legs;
    def.desk->frontCornerRadius = 50;
    EXPECT_EQ(deskDescription(def), QStringLiteral("Table on 4 legs, corner radius 50 mm, cable slot"));
}

TEST(DeskCommand, EditsRefusalsUndoAndBrowserText) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::Desk).ok());
    const auto tree = buildBrowserTree(s.project(), s.resolved());
    ASSERT_FALSE(tree.empty());
    EXPECT_EQ(tree[0].detail, "Desk on end panels, modesty panel, cable slot, 3-drawer pedestal right");

    // Drop the pedestal and round the front corners.
    DeskInput in = deskInput(*cabinet(s).desk);
    in.pedestal = false;
    in.frontCornerRadius = 40;
    ASSERT_TRUE(s.execute(deskCommand("cab1", in)).ok());
    expectGeneratedWithoutErrors(s);
    EXPECT_EQ(part(s, "cab1/pedestal/side.left"), nullptr);
    EXPECT_DOUBLE_EQ(cabinet(s).desk->frontCornerRadius, 40);
    EXPECT_EQ(commandText(*s.document().undoLabel(), s.project()), QStringLiteral("Change desk of Desk"));

    // Refused by the generator: a pedestal 100 mm wide (minimum 250). Nothing changes.
    const auto revision = s.revision();
    DeskInput bad = in;
    bad.pedestal = true;
    bad.pedestalSpec.width = 100;
    auto refused = s.execute(deskCommand("cab1", bad));
    ASSERT_FALSE(refused.ok());
    EXPECT_NE(refused.error().message.find("100"), std::string::npos) << refused.error().message;
    EXPECT_EQ(s.revision(), revision);
    EXPECT_FALSE(cabinet(s).desk->pedestal.has_value());

    // Switch to legs: the leg enters the library in the same undo step; end panels disappear.
    DeskInput legs = in;
    legs.support = furniture::DeskSupport::Legs;
    ASSERT_TRUE(s.execute(deskCommand("cab1", legs)).ok());
    expectGeneratedWithoutErrors(s);
    EXPECT_TRUE(hasLeg(s));
    EXPECT_EQ(part(s, "cab1/end.left"), nullptr);
    EXPECT_FALSE(cabinet(s).desk->modestyPanel);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_FALSE(hasLeg(s));
    EXPECT_NE(part(s, "cab1/end.left"), nullptr);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_TRUE(cabinet(s).desk->pedestal.has_value());

    // Not a desk.
    Session base;
    ASSERT_TRUE(base.newFromTemplate(TemplateKind::BaseCabinet).ok());
    EXPECT_FALSE(base.execute(deskCommand("cab1", in)).ok());
    EXPECT_FALSE(base.execute(deskCommand("nope", in)).ok());
}

} // namespace fos::app::test
