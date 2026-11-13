// SPDX-License-Identifier: AGPL-3.0-only
// Inspector parameter view-model: grouping, formatting, input classification.
#include "fos/app/parameter_rows.hpp"
#include "fos/furniture/panel.hpp"
#include "test_support.hpp"

#include <algorithm>

namespace fos::app::test {
namespace {

using parametric::ParameterKind;

const ParameterRow* findRow(const std::vector<ParameterGroup>& groups, const std::string& name) {
    for (const auto& g : groups)
        for (const auto& r : g.rows)
            if (r.name == name) return &r;
    return nullptr;
}

TEST(ParameterInput, NumbersAndExpressions) {
    auto v = parseParameterInput(QStringLiteral(" 900 "));
    ASSERT_TRUE(v.ok());
    EXPECT_FALSE(v.value().isExpression);
    EXPECT_DOUBLE_EQ(v.value().value, 900);
    v = parseParameterInput(QStringLiteral("812,5"));
    ASSERT_TRUE(v.ok());
    EXPECT_DOUBLE_EQ(v.value().value, 812.5);
    v = parseParameterInput(QStringLiteral("450 mm"));
    ASSERT_TRUE(v.ok());
    EXPECT_DOUBLE_EQ(v.value().value, 450);
    v = parseParameterInput(QStringLiteral("-2.5"));
    ASSERT_TRUE(v.ok());
    EXPECT_DOUBLE_EQ(v.value().value, -2.5);
    v = parseParameterInput(QStringLiteral("width / 2"));
    ASSERT_TRUE(v.ok());
    EXPECT_TRUE(v.value().isExpression);
    EXPECT_EQ(v.value().expression, "width / 2");
    v = parseParameterInput(QStringLiteral("1,5,5")); // not a number -> the engine judges it
    ASSERT_TRUE(v.ok());
    EXPECT_TRUE(v.value().isExpression);
    EXPECT_FALSE(parseParameterInput(QStringLiteral("  ")).ok());
}

TEST(ParameterInput, CommandsCarryTheInput) {
    const auto c1 = parameterCommand("cab1", "width", {false, 900, {}});
    EXPECT_EQ(c1.label.english(), "Set width of cab1 to 900");
    EXPECT_EQ(c1.mergeKey, "parameter:cab1:width");
    const auto c2 = parameterCommand("cab1", "width", {true, 0, "depth + 1"});
    EXPECT_EQ(c2.label.args.at(2), "depth + 1");
}

TEST(ParameterGroups, CabinetGroupedByKindInStandardOrder) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    const auto groups = parameterGroups(*s.project().items[0].cabinet());
    ASSERT_FALSE(groups.empty());
    EXPECT_EQ(groups.front().kind, ParameterKind::Length);
    // Kinds appear at most once and in the documented order.
    const std::vector<ParameterKind> order{ParameterKind::Length, ParameterKind::Count, ParameterKind::Angle,
                                           ParameterKind::Ratio,  ParameterKind::Boolean, ParameterKind::Number};
    auto rank = [&](ParameterKind k) { return std::find(order.begin(), order.end(), k) - order.begin(); };
    for (std::size_t i = 1; i < groups.size(); ++i) EXPECT_LT(rank(groups[i - 1].kind), rank(groups[i].kind));
    const auto& lengths = groups.front().rows;
    ASSERT_GE(lengths.size(), 3u);
    EXPECT_EQ(lengths[0].name, "width"); // first standard input
    EXPECT_EQ(lengths[1].name, "height");
    EXPECT_EQ(lengths[2].name, "depth");
    const auto* width = findRow(groups, "width");
    ASSERT_NE(width, nullptr);
    EXPECT_TRUE(width->standard);
    EXPECT_FALSE(width->derived);
    EXPECT_DOUBLE_EQ(width->value, 600);
    const auto* inner = findRow(groups, "inner_width");
    ASSERT_NE(inner, nullptr);
    EXPECT_TRUE(inner->derived);
    EXPECT_FALSE(inner->expression.empty());
    EXPECT_DOUBLE_EQ(inner->value, 564); // 600 - 2 * 18
    // Inputs come before derived parameters inside the group.
    std::size_t lastInput = 0, firstDerived = lengths.size();
    for (std::size_t i = 0; i < lengths.size(); ++i) {
        if (lengths[i].derived) firstDerived = std::min(firstDerived, i);
        else if (lengths[i].standard) lastInput = i;
    }
    EXPECT_LT(lastInput, firstDerived);
    EXPECT_EQ(editText(*width), QStringLiteral("600"));
    EXPECT_EQ(valueText(*inner), QStringLiteral("564 mm"));
    EXPECT_EQ(editText(*inner), QString::fromStdString(inner->expression));
}

TEST(ParameterGroups, PinnedAfterOverridingDerived) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    ASSERT_TRUE(applyParameterInput(s, "cab1", "shelf_depth", QStringLiteral("500")).ok());
    const auto* row = findRow(parameterGroups(*s.project().items[0].cabinet()), "shelf_depth");
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->pinned);
    EXPECT_DOUBLE_EQ(row->value, 500);
}

TEST(ParameterGroups, PanelAndUnits) {
    auto panel = furniture::makePanel(domain::PartId("p/panel"), "Filler", domain::MaterialId("m"));
    const auto groups = parameterGroups(panel);
    ASSERT_NE(findRow(groups, "length"), nullptr);
    EXPECT_DOUBLE_EQ(findRow(groups, "length")->value, 600);
    EXPECT_EQ(unitSuffix(ParameterKind::Length), QStringLiteral("mm"));
    EXPECT_EQ(unitSuffix(ParameterKind::Angle), QString::fromUtf8("°"));
    EXPECT_TRUE(unitSuffix(ParameterKind::Count).isEmpty());
    EXPECT_EQ(kindTitle(ParameterKind::Length), QStringLiteral("Lengths"));
}

} // namespace
} // namespace fos::app::test
