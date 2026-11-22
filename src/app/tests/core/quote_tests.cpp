// SPDX-License-Identifier: AGPL-3.0-only
// Pricing settings command (undoable, validated) and the quote view-model over the
// cutting plans of the Cutting workspace.
#include "fos/app/command_text.hpp"
#include "fos/app/cutting_rows.hpp"
#include "fos/app/quote_rows.hpp"
#include "test_support.hpp"

namespace fos::app::test {
namespace {

TEST(PricingSettings, CommandIsValidatedAndUndoable) {
    Session s;
    s.newProject();
    EXPECT_FALSE(s.project().settings.pricing.has_value());
    EXPECT_EQ(pricingInput(s.project()), PricingInput{});

    PricingInput in;
    in.wastePercent = 12.5;
    in.markupPercent = 30;
    in.taxPercent = 20;
    in.labour.perHole = 0.25;
    const auto cmd = pricingSettingsCommand(in);
    EXPECT_EQ(commandText(cmd.label, s.project()), QStringLiteral("Change pricing settings"));
    ASSERT_TRUE(s.execute(cmd).ok());
    ASSERT_TRUE(s.project().settings.pricing.has_value());
    EXPECT_EQ(pricingInput(s.project()), in);
    EXPECT_TRUE(s.project().settings.pricing->currency.empty()); // currency = Metadata::currency
    EXPECT_FALSE(s.project().settings.pricing->labour.perPart.has_value());

    // A negative markup is refused by checkPricingSettings and changes nothing.
    PricingInput bad = in;
    bad.markupPercent = -5;
    const auto r = s.execute(pricingSettingsCommand(bad));
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("-5"), std::string::npos) << r.error().message;
    EXPECT_EQ(pricingInput(s.project()), in);

    ASSERT_TRUE(s.execute(resetPricingSettingsCommand()).ok());
    EXPECT_FALSE(s.project().settings.pricing.has_value());
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(pricingInput(s.project()), in);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_FALSE(s.project().settings.pricing.has_value());
}

TEST(QuoteView, UnpricedDefaultsThenPricedBySheets) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    ASSERT_NE(s.resolved(), nullptr);

    // Generic default libraries have cost 0 = unknown: lines are UNPRICED, quote incomplete.
    auto r0 = projectQuote(s.project(), s.resolved()->model, nullptr);
    ASSERT_TRUE(r0.ok()) << r0.error().message;
    const production::Quote* q0 = &r0.value();
    EXPECT_FALSE(q0->complete);
    EXPECT_GT(q0->unpricedCount, 0);
    EXPECT_FALSE(QuoteText::incompleteBanner(*q0).isEmpty());
    const QuoteRows rows0 = QuoteText::rows(*q0);
    ASSERT_EQ(rows0.table.rows.size(), q0->lines.size() + 8); // 4 category subtotals + subtotal, markup, tax, total
    EXPECT_EQ(rows0.summaryStart, q0->lines.size());
    EXPECT_TRUE(rows0.unpriced[0]);
    EXPECT_EQ(rows0.table.rows[0][4], "UNPRICED");
    EXPECT_EQ(rows0.table.rows.back()[1], "TOTAL (EUR)");

    // Price everything: 50 per sheet, 1 per metre of edge band, 2 per hardware piece.
    // Copy the libraries first: every execute() replaces the project the loops would iterate.
    const furniture::Libraries libraries = s.project().libraries;
    for (auto m : libraries.materials) {
        m.costPerSheet = 50;
        ASSERT_TRUE(s.execute(project::commands::upsertMaterial(m)).ok());
    }
    for (auto e : libraries.edgeBands) {
        e.costPerMeter = 1;
        ASSERT_TRUE(s.execute(project::commands::upsertEdgeBand(e)).ok());
    }
    for (auto h : libraries.hardware) {
        h.unitCost = 2;
        ASSERT_TRUE(s.execute(project::commands::upsertHardware(h)).ok());
    }
    PricingInput in;
    in.markupPercent = 10;
    in.taxPercent = 20;
    ASSERT_TRUE(s.execute(pricingSettingsCommand(in)).ok());

    const auto& plans = s.cuttingPlans();
    ASSERT_TRUE(plans.nestingPlans.empty()); // rectangular parts only
    int sheets = 0;
    for (const auto& row : cuttingSummary(plans)) sheets += row.sheets;
    ASSERT_GT(sheets, 0);

    auto r = projectQuote(s.project(), s.resolved()->model, &plans);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const production::Quote* q = &r.value();
    EXPECT_TRUE(q->complete);
    EXPECT_TRUE(q->bySheets);
    EXPECT_TRUE(QuoteText::incompleteBanner(*q).isEmpty());
    // Materials by sheets: sum of the plan sheet counts * 50.
    EXPECT_NEAR(q->materialsSubtotal, 50.0 * sheets, 1e-9);
    // total = subtotal * 1.10 * 1.20
    EXPECT_NEAR(q->total, q->subtotal * 1.1 * 1.2, 1e-6);
    const QuoteRows rows = QuoteText::rows(*q);
    for (const bool u : rows.unpriced) EXPECT_FALSE(u);
    EXPECT_EQ(rows.table.rows.back()[5], production::formatMoney(q->total));
    EXPECT_EQ(rows.table.rows[rows.summaryStart + 5][1], "Markup 10 %");
}

} // namespace
} // namespace fos::app::test
