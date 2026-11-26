// SPDX-License-Identifier: AGPL-3.0-only
// Bought-in items (glass panes, domain::isBoughtIn): left out of the workshop outputs, listed in
// boughtInList() and quoted by area.
#include "fos/production/bought_in.hpp"
#include "fos/production/edge_banding_jobs.hpp"
#include "fos/production/labels.hpp"
#include "fos/production/quote.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

#include <algorithm>

using namespace fos;
using namespace fos::production;

namespace {

domain::Part glass(const std::string& id, double l, double w, const std::string& material = "g4") {
    domain::Part p = production::testing::panel(id, "Door glass", l, w, 4, material);
    p.custom[domain::kGlassPaneKey] = "true";
    return p;
}

/// smallModel() (m1 board, 10.00 / 1 m2 sheet) + glass "g4" (4 mm, 50.00 / m2, 999 / sheet that
/// must never be used) + edge band e1 (1 mm). Parts in number order:
///  1 p1     board 500 x 400, e1 on L1
///  2 gA     glass 400 x 300 (with a stray band on L1: still never banded)
///  3 gB     glass 300 x 400 (same pane turned: merges with gA)
///  4 gC     glass 200 x 100
///  5 bead   glazing bead 400 x 10 x 6 of the board, no bands (a normal part)
domain::Model glassModel() {
    domain::Model m = production::testing::smallModel();
    domain::Material g;
    g.id = domain::MaterialId("g4");
    g.name = "Float glass 4";
    g.kind = domain::MaterialKind::Glass;
    g.thickness = 4;
    g.costPerSquareMeter = 50.0;
    g.costPerSheet = 999.0;
    m.materials.push_back(g);
    domain::EdgeBand e;
    e.id = domain::EdgeBandId("e1");
    e.name = "ABS 1";
    e.thickness = 1;
    e.width = 22;
    m.edgeBands = {e};
    auto& parts = m.assemblies.front().parts;
    parts.push_back(production::testing::panel("p1", "Panel", 500, 400, 18, "m1"));
    parts.back().edge(domain::EdgeSide::L1) = domain::EdgeBandId("e1");
    parts.push_back(glass("gA", 400, 300));
    parts.back().edge(domain::EdgeSide::L1) = domain::EdgeBandId("e1");
    parts.push_back(glass("gB", 300, 400));
    parts.push_back(glass("gC", 200, 100));
    parts.push_back(production::testing::panel("bead", "Door top glazing bead", 400, 10, 6, "m1"));
    parts.back().custom["furniture.front_piece"] = "bead";
    return m;
}

int count(const core::Diagnostics& ds, const std::string& code) {
    return static_cast<int>(std::count_if(ds.begin(), ds.end(), [&](const auto& d) { return d.code == code; }));
}

const core::Diagnostic* find(const core::Diagnostics& ds, const std::string& code) {
    for (const auto& d : ds)
        if (d.code == code) return &d;
    return nullptr;
}

} // namespace

TEST(BoughtIn, PredicateIsTheGlassFlag) {
    domain::Part p = production::testing::panel("x", "X", 1, 1, 1, "m");
    EXPECT_FALSE(domain::isBoughtIn(p));
    p.custom[domain::kGlassPaneKey] = "false";
    EXPECT_FALSE(domain::isBoughtIn(p));
    p.custom[domain::kGlassPaneKey] = "true";
    EXPECT_TRUE(domain::isBoughtIn(p));
    EXPECT_TRUE(domain::isGlassPane(p));
}

TEST(BoughtIn, CutListHasNoPaneButKeepsTheBead) {
    const CutList cl = cutList(glassModel());
    std::vector<std::string> ids;
    for (const auto& r : cl.rows)
        for (const auto& id : r.partIds) ids.push_back(id.str());
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids, (std::vector<std::string>{"bead", "p1"}));
    for (const auto& r : cl.rows)
        if (r.partIds.front().str() == "bead") {
            for (const auto& e : r.edges) EXPECT_FALSE(e.banded()); // beads are not edge banded
        }
    ASSERT_EQ(count(cl.diagnostics, "production.bought_in.excluded"), 1);
    const auto* d = find(cl.diagnostics, "production.bought_in.excluded");
    EXPECT_EQ(d->severity, core::Severity::Info);
    EXPECT_EQ(d->message.rfind("3 bought-in part(s)", 0), 0u) << d->message;
    // Labels are made from the cut list: none for a pane.
    const LabelSet ls = labels(glassModel());
    ASSERT_EQ(ls.labels.size(), 2u);
    for (const auto& l : ls.labels) EXPECT_FALSE(l.partId.str().starts_with("g"));
}

TEST(BoughtIn, ListGroupsBySizeEitherWayAndMaterial) {
    const BoughtInList list = boughtInList(glassModel());
    ASSERT_EQ(list.rows.size(), 2u);
    const BoughtInRow& a = list.rows[0];
    EXPECT_EQ(a.kind, "Glass pane");
    EXPECT_EQ(a.materialName, "Float glass 4");
    EXPECT_DOUBLE_EQ(a.width, 400);
    EXPECT_DOUBLE_EQ(a.height, 300);
    EXPECT_DOUBLE_EQ(a.thickness, 4);
    EXPECT_EQ(a.quantity, 2);
    EXPECT_EQ(a.partNumbers, (std::vector<int>{2, 3}));
    EXPECT_NEAR(a.areaEachM2, 0.12, 1e-12);  // 400 * 300 / 1e6
    EXPECT_NEAR(a.totalAreaM2, 0.24, 1e-12); // 2 * 0.12
    const BoughtInRow& c = list.rows[1];
    EXPECT_EQ(c.quantity, 1);
    EXPECT_NEAR(c.totalAreaM2, 0.02, 1e-12); // 200 * 100 / 1e6
    EXPECT_EQ(list.itemCount, 3);
    EXPECT_NEAR(list.totalAreaM2, 0.26, 1e-12);
    EXPECT_TRUE(list.diagnostics.empty());

    const Table t = boughtInTable(list);
    ASSERT_EQ(t.rows.size(), 2u);
    EXPECT_EQ(t.header[0], "Item");
    EXPECT_EQ(t.rows[0], (std::vector<std::string>{"Glass pane", "Float glass 4", "400", "300", "4", "2", "0.12",
                                                   "0.24", "Door glass", "2 3"}));

    // Different thickness never merges.
    domain::Model m = glassModel();
    m.assemblies.front().parts[2].thickness = 6; // gB
    EXPECT_EQ(boughtInList(m).rows.size(), 3u);
}

TEST(BoughtIn, MaterialsBandingAndJobsLeaveOutPanes) {
    const domain::Model m = glassModel();
    const MaterialReport mats = materialReport(m);
    ASSERT_EQ(mats.rows.size(), 1u);
    EXPECT_EQ(mats.rows[0].materialId.str(), "m1");
    EXPECT_EQ(mats.rows[0].partCount, 2); // p1 + bead
    EXPECT_EQ(count(mats.diagnostics, "production.bought_in.excluded"), 1);

    const EdgeBandingReport eb = edgeBandingReport(m, {0.0, 0.0});
    ASSERT_EQ(eb.rows.size(), 1u);
    EXPECT_EQ(eb.rows[0].edgeCount, 1);                 // p1 L1 only, the pane's stray band is ignored
    EXPECT_NEAR(eb.rows[0].appliedLengthM, 0.5, 1e-12); // 500 mm

    const EdgeBandingJobs jobs = edgeBandingJobs(m);
    ASSERT_EQ(jobs.groups.size(), 1u);
    ASSERT_EQ(jobs.groups[0].parts.size(), 1u);
    EXPECT_EQ(jobs.groups[0].parts[0].partId.str(), "p1");
    EXPECT_EQ(count(jobs.diagnostics, "production.bought_in.excluded"), 1);

    // No bought-in part, no notice.
    EXPECT_EQ(count(cutList(production::testing::smallModel()).diagnostics, "production.bought_in.excluded"), 0);
}

TEST(BoughtIn, QuotePricesPanesByAreaNeverBySheet) {
    auto q = quote(glassModel(), PricingSettings{});
    ASSERT_TRUE(q.ok()) << q.error().message;
    std::vector<const QuoteLine*> bought;
    for (const auto& l : q.value().lines) {
        if (l.category == QuoteCategory::Material) {
            EXPECT_NE(l.id, "g4") << "glass is not a sheet material";
        }
        if (l.category == QuoteCategory::BoughtIn) bought.push_back(&l);
    }
    ASSERT_EQ(bought.size(), 2u);
    EXPECT_EQ(bought[0]->description, "Glass pane 400 x 300 x 4 mm (Float glass 4)");
    EXPECT_EQ(bought[0]->unit, "m2");
    EXPECT_NEAR(bought[0]->quantity, 0.24, 1e-12);
    ASSERT_TRUE(bought[0]->priced());
    EXPECT_DOUBLE_EQ(*bought[0]->unitPrice, 50.0);
    EXPECT_NEAR(bought[0]->amount, 12.0, 1e-9); // 0.24 m2 * 50
    EXPECT_NEAR(bought[1]->amount, 1.0, 1e-9);  // 0.02 m2 * 50
    EXPECT_NEAR(q.value().boughtInSubtotal, 13.0, 1e-9);
    EXPECT_NE(quoteText(q.value()).find("Bought-in"), std::string::npos);

    // No price per m2: unpriced (the 999 / sheet is never turned into a glass price).
    domain::Model m = glassModel();
    m.materials[1].costPerSquareMeter = 0;
    auto u = quote(m, PricingSettings{});
    ASSERT_TRUE(u.ok());
    EXPECT_FALSE(u.value().complete);
    EXPECT_DOUBLE_EQ(u.value().boughtInSubtotal, 0.0);
    int unpriced = 0;
    for (const auto& l : u.value().lines)
        if (l.category == QuoteCategory::BoughtIn) unpriced += l.priced() ? 0 : 1;
    EXPECT_EQ(unpriced, 2);
}

TEST(BoughtIn, MissingGlassMaterialIsListedUnpriced) {
    domain::Model m = glassModel();
    m.assemblies.front().parts.push_back(glass("gX", 100, 100, "nope"));
    const BoughtInList list = boughtInList(m);
    ASSERT_EQ(list.rows.size(), 3u);
    EXPECT_TRUE(list.rows[0].materialMissing); // "" sorts before "Float glass 4"
    EXPECT_EQ(count(list.diagnostics, "production.material.missing"), 1);
    auto q = quote(m, PricingSettings{});
    ASSERT_TRUE(q.ok());
    EXPECT_FALSE(q.value().complete);
}
