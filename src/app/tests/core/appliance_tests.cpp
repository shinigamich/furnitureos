// SPDX-License-Identifier: AGPL-3.0-only
// Appliance housings in the application: templates, browser text, niche vent edits.
#include "fos/app/appliance_text.hpp"
#include "fos/app/browser_tree.hpp"
#include "fos/app/interior_rows.hpp"
#include "fos/app/templates.hpp"
#include "fos/furniture/parameters.hpp"
#include "test_support.hpp"

#include <algorithm>

namespace fos::app::test {
namespace {

void expectGeneratedWithoutErrors(const Session& s) {
    ASSERT_NE(s.resolved(), nullptr) << s.resolveError();
    ASSERT_EQ(s.resolved()->items.size(), 1u);
    EXPECT_TRUE(s.resolved()->items[0].generated);
    for (const auto& d : s.resolved()->diagnostics)
        EXPECT_NE(d.severity, core::Severity::Error) << d.message;
}

/// Ids of the generated parts that carry a through cut-out (sorted, model order is stable).
std::vector<std::string> partsWithCutouts(const Session& s) {
    std::vector<std::string> ids;
    if (!s.resolved()) return ids;
    for (const auto& a : s.resolved()->model.assemblies)
        for (const auto& p : a.parts)
            if (p.shape && !p.shape->cutouts.empty()) ids.push_back(p.id.str());
    std::sort(ids.begin(), ids.end());
    return ids;
}

/// Front of section "2" (the oven niche of ovenTallHousing()).
std::optional<furniture::FrontSpec> nicheFront(const Session& s) {
    auto section = furniture::findSection(s.project().items[0].cabinet()->interior, "2");
    EXPECT_TRUE(section.ok());
    return section.ok() ? section.value()->front : std::nullopt;
}

furniture::ApplianceNicheSpec nicheOf(const Session& s) {
    const auto front = nicheFront(s);
    EXPECT_TRUE(front.has_value());
    return front ? front->niche : furniture::ApplianceNicheSpec{};
}

} // namespace

TEST(ApplianceTemplates, BothAreOfferedAndGenerate) {
    const auto& kinds = allTemplates();
    for (TemplateKind k : {TemplateKind::OvenTallHousing, TemplateKind::DishwasherFront}) {
        EXPECT_NE(std::find(kinds.begin(), kinds.end(), k), kinds.end());
        Session s;
        ASSERT_TRUE(s.newFromTemplate(k).ok()) << templateToken(k);
        expectGeneratedWithoutErrors(s);
    }
    EXPECT_STREQ(templateToken(TemplateKind::OvenTallHousing), "oven-tall");
    EXPECT_STREQ(templateToken(TemplateKind::DishwasherFront), "dishwasher");
    // Defaults from the graph: oven 600 x 2100 x 560; dishwasher width = 600 gap + 18 board.
    EXPECT_DOUBLE_EQ(defaultTemplateSize(TemplateKind::OvenTallHousing).height, 2100);
    EXPECT_DOUBLE_EQ(defaultTemplateSize(TemplateKind::DishwasherFront).width, 618);

    // The size dialog passes the unchanged default size: the width formula survives.
    Session dw;
    ASSERT_TRUE(dw.newFromTemplate(TemplateKind::DishwasherFront, defaultTemplateSize(TemplateKind::DishwasherFront)).ok());
    const auto& cab = *dw.project().items[0].cabinet();
    ASSERT_NE(cab.parameters.find(furniture::param::kWidth), nullptr);
    EXPECT_TRUE(cab.parameters.find(furniture::param::kWidth)->isExpression());
    EXPECT_TRUE(isApplianceGap(cab));
    const auto tree = buildBrowserTree(dw.project(), dw.resolved());
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree[0].detail, "Appliance gap 600 mm, end panel right");
    // Ordinary cabinets: no description.
    Session plain;
    ASSERT_TRUE(plain.newFromTemplate(TemplateKind::BaseCabinet).ok());
    EXPECT_FALSE(isApplianceGap(*plain.project().items[0].cabinet()));
    EXPECT_TRUE(applianceGapDescription(*plain.project().items[0].cabinet()).isEmpty());
}

TEST(ApplianceNicheEdit, VentRoundTripsRefusesAndUndoes) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::OvenTallHousing).ok());
    const std::string id = s.project().items[0].id();
    // Template: ShelfSlot 50 / 20 / 80 -> the only cut-out is in the fixed shelf below the niche.
    FrontInput in = frontInput(nicheFront(s));
    ASSERT_EQ(in.choice, FrontInput::Choice::ApplianceNiche);
    EXPECT_EQ(in.niche.vent, furniture::NicheVent::ShelfSlot);
    EXPECT_EQ(partsWithCutouts(s), std::vector<std::string>{id + "/fixed_shelf.1"});
    const furniture::ApplianceNicheSpec original = nicheOf(s);

    // Back cut-out 60 high, 30 below the niche top, 100 from the sides.
    in.niche.vent = furniture::NicheVent::BackCutout;
    in.niche.ventSize = 60;
    in.niche.ventEdgeDistance = 30;
    in.niche.ventEndMargin = 100;
    auto cmd = frontCommand(id, "2", in);
    ASSERT_TRUE(cmd.ok());
    ASSERT_TRUE(s.execute(cmd.value()).ok());
    EXPECT_EQ(nicheOf(s).vent, furniture::NicheVent::BackCutout);
    EXPECT_DOUBLE_EQ(nicheOf(s).ventSize, 60);
    EXPECT_DOUBLE_EQ(nicheOf(s).ventEdgeDistance, 30);
    EXPECT_DOUBLE_EQ(nicheOf(s).ventEndMargin, 100);
    const auto cut = partsWithCutouts(s);
    ASSERT_EQ(cut.size(), 1u);
    EXPECT_NE(cut[0], id + "/fixed_shelf.1");
    // The editor reads back what was submitted.
    const FrontInput back = frontInput(nicheFront(s));
    EXPECT_EQ(back.niche, in.niche);

    // A 0 mm opening is refused by the furniture module; the project is unchanged.
    const auto before = s.project().items[0].cabinet()->interior;
    in.niche.ventSize = 0;
    EXPECT_FALSE(s.execute(frontCommand(id, "2", in).value()).ok());
    EXPECT_EQ(s.project().items[0].cabinet()->interior, before);

    // Undo restores the template's shelf slot.
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(nicheOf(s), original);
    EXPECT_EQ(partsWithCutouts(s), std::vector<std::string>{id + "/fixed_shelf.1"});
}

} // namespace fos::app::test
