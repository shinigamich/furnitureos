// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/quote.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

#include <limits>

using namespace fos;
using namespace fos::production;

namespace {

/// smallModel() (m1: 1000 x 1000 sheet, 10.00 / sheet) with two raw 500 x 400 panels
/// (cut area 2 * 0.2 = 0.4 m2) and hardware: 4 x h1 (0.50 each) + 1 x h2 (no price).
domain::Model quoteModel() {
    domain::Model m = production::testing::smallModel();
    auto& a = m.assemblies.front();
    a.parts.push_back(production::testing::panel("p1", "Panel 1", 500, 400, 18, "m1"));
    a.parts.push_back(production::testing::panel("p2", "Panel 2", 500, 400, 18, "m1"));
    domain::HardwareDefinition h1;
    h1.id = domain::HardwareId("h1");
    h1.name = "Pin";
    h1.unitCost = 0.5;
    domain::HardwareDefinition h2;
    h2.id = domain::HardwareId("h2");
    h2.name = "Handle";
    m.hardwareCatalog = {h1, h2};
    domain::HardwareInstance i1;
    i1.id = domain::HardwareInstanceId("i1");
    i1.definition = h1.id;
    i1.quantity = 4;
    domain::HardwareInstance i2;
    i2.id = domain::HardwareInstanceId("i2");
    i2.definition = h2.id;
    a.hardware = {i1, i2};
    return m;
}

const QuoteLine* findLine(const Quote& q, const std::string& id) {
    for (const auto& l : q.lines)
        if (l.id == id) return &l;
    return nullptr;
}

} // namespace

TEST(Quote, ByAreaWithWasteMarkupTaxAndUnpricedHardware) {
    auto m = quoteModel();
    m.materials[0].costPerSquareMeter = 20.0;
    PricingSettings s;
    s.currency = "EUR";
    s.wastePercent = 10;
    s.markupPercent = 20;
    s.taxPercent = 10;
    auto r = quote(m, s);
    ASSERT_TRUE(r) << r.error().message;
    const Quote& q = r.value();
    // Material: 0.4 m2 * 1.10 = 0.44 m2 * 20.00 = 8.80 (per-m2 price wins over the sheet price).
    const auto* mat = findLine(q, "m1");
    ASSERT_NE(mat, nullptr);
    EXPECT_EQ(mat->unit, "m2");
    EXPECT_NEAR(mat->quantity, 0.44, 1e-12);
    EXPECT_NEAR(mat->amount, 8.80, 1e-9);
    // Hardware: 4 * 0.50 = 2.00; h2 unpriced (never invented).
    EXPECT_NEAR(findLine(q, "h1")->amount, 2.0, 1e-12);
    const auto* h2 = findLine(q, "h2");
    ASSERT_NE(h2, nullptr);
    EXPECT_FALSE(h2->priced());
    EXPECT_EQ(h2->amount, 0.0);
    // subtotal 10.80; markup 20 % = 2.16; tax 10 % of 12.96 = 1.296; total 14.256.
    EXPECT_NEAR(q.subtotal, 10.80, 1e-9);
    EXPECT_NEAR(q.markup, 2.16, 1e-9);
    EXPECT_NEAR(q.tax, 1.296, 1e-9);
    EXPECT_NEAR(q.total, 14.256, 1e-9);
    EXPECT_FALSE(q.complete);
    EXPECT_EQ(q.unpricedCount, 1);
    bool warned = false;
    for (const auto& d : q.diagnostics) warned = warned || (d.code == "production.quote.unpriced" && d.entityId == "h2");
    EXPECT_TRUE(warned);
    EXPECT_EQ(formatMoney(q.total), "14.26");
    const std::string text = quoteText(q);
    EXPECT_NE(text.find("UNPRICED"), std::string::npos);
    EXPECT_NE(text.find("INCOMPLETE QUOTE"), std::string::npos);
    EXPECT_NE(text.find("TOTAL (EUR)"), std::string::npos);
}

TEST(Quote, AreaPriceDerivedFromSheetPrice) {
    const auto m = quoteModel(); // costPerSheet 10.00, sheet 1 m2 -> 10.00 / m2
    auto r = quote(m, {});
    ASSERT_TRUE(r);
    const auto* mat = findLine(r.value(), "m1");
    ASSERT_TRUE(mat->priced());
    EXPECT_NEAR(*mat->unitPrice, 10.0, 1e-12);
    EXPECT_NEAR(mat->amount, 4.0, 1e-9); // 0.4 m2 * 10.00
    EXPECT_NE(mat->note.find("sheet price / sheet area"), std::string::npos);
}

TEST(Quote, BySheetsWithLabour) {
    const auto m = quoteModel();
    PricingSettings s;
    s.wastePercent = 50; // ignored when sheets come from a plan
    s.labour.perCutMetre = 1.5;
    s.labour.perPart = 0.25;
    QuoteOptions o;
    o.sheetUsage[domain::MaterialId("m1")] = {2, 3000.0};
    auto r = quote(m, s, o);
    ASSERT_TRUE(r);
    const Quote& q = r.value();
    EXPECT_TRUE(q.bySheets);
    const auto* mat = findLine(q, "m1");
    EXPECT_EQ(mat->unit, "sheet");
    EXPECT_NEAR(mat->amount, 20.0, 1e-12);                       // 2 * 10.00
    EXPECT_NEAR(findLine(q, "labour.cut")->amount, 4.5, 1e-12);  // 3 m * 1.50 (no routed parts)
    EXPECT_NEAR(findLine(q, "labour.part")->amount, 0.5, 1e-12); // 2 parts * 0.25
    EXPECT_NEAR(q.labourSubtotal, 5.0, 1e-12);
    // 20 + 2 (h1) + 5 = 27; no markup / tax.
    EXPECT_NEAR(q.total, 27.0, 1e-12);
    EXPECT_EQ(findLine(q, "labour.hole"), nullptr); // rate not set -> no line
}

TEST(Quote, CutLabourWithoutPlanIsUnpriced) {
    PricingSettings s;
    s.labour.perCutMetre = 1.5;
    auto r = quote(quoteModel(), s);
    ASSERT_TRUE(r);
    const auto* cut = findLine(r.value(), "labour.cut");
    ASSERT_NE(cut, nullptr);
    EXPECT_FALSE(cut->priced());
    EXPECT_EQ(r.value().unpricedCount, 2); // h2 + cutting
}

TEST(Quote, EdgeBandsAndEdgeLabour) {
    auto m = production::testing::smallModel();
    domain::EdgeBand e;
    e.id = domain::EdgeBandId("e1");
    e.name = "ABS 1";
    e.thickness = 1;
    e.costPerMeter = 2.0;
    m.edgeBands = {e};
    auto p = production::testing::panel("p1", "Panel", 500, 400, 18, "m1");
    p.edges[static_cast<std::size_t>(domain::EdgeSide::L1)] = e.id; // L1 runs along the 500 mm length
    m.assemblies.front().parts.push_back(p);
    PricingSettings s;
    s.labour.perEdgeMetre = 0.4;
    QuoteOptions o;
    o.edgeBanding.overhangPerEnd = 0;
    auto r = quote(m, s, o);
    ASSERT_TRUE(r);
    EXPECT_NEAR(findLine(r.value(), "e1")->amount, 1.0, 1e-12);          // 0.5 m * 2.00
    EXPECT_NEAR(findLine(r.value(), "labour.edge")->amount, 0.2, 1e-12); // 0.5 m * 0.40
    // Cut blank 500 x 399 = 0.1995 m2 at 10.00 / m2 (sheet price / 1 m2) = 1.995.
    EXPECT_NEAR(findLine(r.value(), "m1")->amount, 1.995, 1e-9);
    EXPECT_TRUE(r.value().complete);
}

TEST(Quote, MissingMaterialIsUnpriced) {
    auto m = quoteModel();
    m.assemblies.front().parts[1].material = domain::MaterialId("nope");
    auto r = quote(m, {});
    ASSERT_TRUE(r);
    const auto* mat = findLine(r.value(), "nope");
    ASSERT_NE(mat, nullptr);
    EXPECT_FALSE(mat->priced());
}

TEST(Quote, RejectsInvalidSettings) {
    PricingSettings s;
    s.markupPercent = -5;
    auto r = quote(quoteModel(), s);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_NE(r.error().message.find("markupPercent"), std::string::npos);
    PricingSettings t;
    t.labour.perHole = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(quote(quoteModel(), t));
    QuoteOptions o;
    o.sheetUsage[domain::MaterialId("m1")] = {-1, 0};
    EXPECT_FALSE(quote(quoteModel(), {}, o));
}

TEST(Quote, FormatMoney) {
    EXPECT_EQ(formatMoney(0), "0.00");
    EXPECT_EQ(formatMoney(-0.001), "0.00");
    EXPECT_EQ(formatMoney(1234.5), "1234.50");
    EXPECT_EQ(formatMoney(0.125), "0.13"); // 12.5 (exact in binary) rounds half away from zero
}
