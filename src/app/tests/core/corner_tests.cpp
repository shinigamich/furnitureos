// SPDX-License-Identifier: AGPL-3.0-only
// Corner cabinets in the application: templates, corner view-model, undoable edits.
#include "fos/app/browser_tree.hpp"
#include "fos/app/corner_rows.hpp"
#include "fos/app/templates.hpp"
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

/// Visible width of the blind corner door = the smaller face dimension (door 716 mm tall).
double doorWidth(const Session& s) {
    const domain::Part* door = part(s, s.project().items[0].id() + "/door");
    EXPECT_NE(door, nullptr);
    return door ? std::min(door->length, door->width) : 0;
}

} // namespace

TEST(CornerTemplates, BothAreOfferedAndGenerate) {
    const auto& kinds = allTemplates();
    for (TemplateKind k : {TemplateKind::BlindCornerCabinet, TemplateKind::LCornerCabinet}) {
        EXPECT_NE(std::find(kinds.begin(), kinds.end(), k), kinds.end());
        Session s;
        ASSERT_TRUE(s.newFromTemplate(k).ok()) << templateToken(k);
        expectGeneratedWithoutErrors(s);
        EXPECT_TRUE(isCornerCabinet(*s.project().items[0].cabinet()));
    }
    EXPECT_STREQ(templateToken(TemplateKind::BlindCornerCabinet), "corner-blind");
    EXPECT_STREQ(templateToken(TemplateKind::LCornerCabinet), "corner-l");
    // Template defaults read from the graph: 1000 x 820 x 560 (blind), leg A 900 x 820 x 560 (L).
    EXPECT_DOUBLE_EQ(defaultTemplateSize(TemplateKind::BlindCornerCabinet).width, 1000);
    EXPECT_DOUBLE_EQ(defaultTemplateSize(TemplateKind::LCornerCabinet).width, 900);
}

TEST(CornerRows, ParametersDescriptionAndFiltering) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::LCornerCabinet).ok());
    const auto& cab = *s.project().items[0].cabinet();
    const auto params = cornerParameters(cab);
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0].name, "corner_width_b");
    ASSERT_TRUE(params[0].row.has_value());
    EXPECT_DOUBLE_EQ(params[0].row->value, 900);
    EXPECT_DOUBLE_EQ(params[1].row->value, 560);
    EXPECT_EQ(cornerDescription(cab), QStringLiteral("L-shaped corner, leg B 900 × 560 mm"));
    // Corner rows leave the generic groups; everything else stays.
    std::size_t before = 0, after = 0;
    for (const auto& g : parameterGroups(cab)) before += g.rows.size();
    for (const auto& g : withoutParameters(parameterGroups(cab), cornerParameterNames(cab.corner.kind))) {
        after += g.rows.size();
        for (const auto& r : g.rows) EXPECT_NE(r.name.rfind("corner_", 0), 0u) << r.name;
    }
    EXPECT_EQ(after + 2, before);
    // Browser: the item line carries the corner summary.
    const auto tree = buildBrowserTree(s.project(), s.resolved());
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree[0].detail, "L-shaped corner, leg B 900 × 560 mm");
    // Ordinary cabinets: no corner group, no description.
    Session plain;
    ASSERT_TRUE(plain.newFromTemplate(TemplateKind::BaseCabinet).ok());
    EXPECT_FALSE(isCornerCabinet(*plain.project().items[0].cabinet()));
    EXPECT_TRUE(cornerDescription(*plain.project().items[0].cabinet()).isEmpty());
    EXPECT_TRUE(cornerParameters(*plain.project().items[0].cabinet()).empty());
}

// Blind corner 1000 wide, sides 18, full overlay 16 (front x 2..998), gap 3, blind left:
// door = 998 - (blind_width + 1.5). blind_width 500 -> 496.5; 450 -> 546.5.
TEST(CornerEdits, BlindWidthUpdatesDoorAndUndoes) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BlindCornerCabinet).ok());
    const std::string id = s.project().items[0].id();
    EXPECT_NEAR(doorWidth(s), 496.5, 1e-9);
    ASSERT_TRUE(applyParameterInput(s, id, "blind_width", QStringLiteral("450")).ok());
    EXPECT_NEAR(doorWidth(s), 546.5, 1e-9);
    EXPECT_EQ(cornerDescription(*s.project().items[0].cabinet()), QStringLiteral("Blind corner, blind panel left, 450 mm"));
    // 300 -> door 998 - 301.5 = 696.5 > 600: refused, unchanged, engine message.
    const auto refused = applyParameterInput(s, id, "blind_width", QStringLiteral("300"));
    ASSERT_FALSE(refused.ok());
    EXPECT_NE(refused.error().message.find("696.5"), std::string::npos) << refused.error().message;
    EXPECT_NEAR(doorWidth(s), 546.5, 1e-9);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_NEAR(doorWidth(s), 496.5, 1e-9);
    ASSERT_TRUE(s.redo().ok());
    EXPECT_NEAR(doorWidth(s), 546.5, 1e-9);
}

// Blind on the right (blind_width 500): door x 2..498.5 = 496.5 wide, hinged left.
TEST(CornerEdits, BlindSideFlipsHingeAndUndoes) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BlindCornerCabinet).ok());
    const std::string id = s.project().items[0].id();
    ASSERT_TRUE(s.execute(blindSideCommand(id, furniture::CornerSide::Right)).ok());
    const auto* cab = s.project().items[0].cabinet();
    EXPECT_EQ(cab->corner.blindSide, furniture::CornerSide::Right);
    EXPECT_EQ(cab->interior.front->hingeSide, furniture::HingeSide::Left);
    expectGeneratedWithoutErrors(s);
    EXPECT_NEAR(doorWidth(s), 496.5, 1e-9);
    EXPECT_EQ(blindSideCommand(id, furniture::CornerSide::Right).label.english(), "Set blind side of " + id + " to Right");
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(s.project().items[0].cabinet()->corner.blindSide, furniture::CornerSide::Left);
    EXPECT_EQ(s.project().items[0].cabinet()->interior.front->hingeSide, furniture::HingeSide::Right);

    Session plain;
    ASSERT_TRUE(plain.newFromTemplate(TemplateKind::BaseCabinet).ok());
    EXPECT_FALSE(plain.execute(blindSideCommand(plain.project().items[0].id(), furniture::CornerSide::Right)).ok());
}

} // namespace fos::app::test
