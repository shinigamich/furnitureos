// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/production.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

#include <limits>

using namespace fos;
using namespace fos::production;
using production::testing::panel;
using production::testing::smallModel;

namespace {
int countCode(const core::Diagnostics& ds, const std::string& code) {
    int n = 0;
    for (const auto& d : ds) n += d.code == code;
    return n;
}
} // namespace

TEST(Robustness, EmptyModel) {
    const domain::Model m;
    EXPECT_TRUE(partList(m).rows.empty());
    EXPECT_TRUE(partList(m).diagnostics.empty());
    EXPECT_TRUE(cutList(m).rows.empty());
    EXPECT_TRUE(materialReport(m).rows.empty());
    EXPECT_TRUE(edgeBandingReport(m).rows.empty());
    EXPECT_TRUE(hardwareReport(m).rows.empty());
    EXPECT_TRUE(machiningSummary(m).rows.empty());
    EXPECT_TRUE(labels(m).labels.empty());
    const auto c = costEstimate(m);
    EXPECT_DOUBLE_EQ(c.total, 0.0);
    EXPECT_TRUE(c.complete);
    EXPECT_FALSE(c.materialsAtAreaLowerBound);
    const auto s = projectSummary(m);
    EXPECT_EQ(s.assemblyCount, 0);
    EXPECT_EQ(s.partCount, 0);
    EXPECT_DOUBLE_EQ(s.finishedAreaM2, 0.0);
    // Writers still produce a header-only CSV and a subtotal/total-only cost table.
    EXPECT_EQ(toCsv(cutListTable(cutList(m))).find('\n'), toCsv(cutListTable(cutList(m))).size() - 1);
    EXPECT_EQ(costTable(c).rows.size(), 4u); // three subtotals + total
    ASSERT_TRUE(renderLabelSheetsSvg(labels(m).labels).ok());
}

TEST(Robustness, AssemblyWithoutParts) {
    const auto m = smallModel();
    const auto s = projectSummary(m);
    ASSERT_EQ(s.assemblies.size(), 1u);
    EXPECT_DOUBLE_EQ(s.assemblies[0].envelopeWidth, 0.0); // empty bounds -> zero size, not infinity
    EXPECT_DOUBLE_EQ(s.assemblies[0].envelopeHeight, 0.0);
    const auto html = productionSheetHtml(m, domain::AssemblyId("a1"));
    ASSERT_TRUE(html.ok());
    EXPECT_NE(html.value().find("class=\"empty\""), std::string::npos);
}

TEST(Robustness, UnknownScopeIsFlagged) {
    const auto m = domain::fixtures::basicCabinet();
    const Scope ghost{domain::AssemblyId("ghost")};
    const auto pl = partList(m, ghost);
    EXPECT_TRUE(pl.rows.empty());
    ASSERT_EQ(pl.diagnostics.size(), 1u);
    EXPECT_EQ(pl.diagnostics[0].code, "production.scope.unknown_assembly");
    EXPECT_NE(pl.diagnostics[0].message.find("'ghost'"), std::string::npos);
    EXPECT_TRUE(hardwareReport(m, ghost).rows.empty());
    EXPECT_EQ(costEstimate(m, {}, ghost).diagnostics.size(), 1u); // reported once, not per sub-report
    EXPECT_TRUE(labels(m, {}, ghost).labels.empty());
}

TEST(Robustness, MissingMaterialIsFlagged) {
    auto m = smallModel();
    m.assemblies[0].parts = {panel("p1", "Known", 500, 400, 18, "m1"), panel("p2", "Ghost", 600, 300, 18, "m.ghost"),
                             panel("p3", "Unassigned", 200, 100, 18, "")};
    const auto pl = partList(m);
    ASSERT_EQ(pl.rows.size(), 3u);
    EXPECT_FALSE(pl.rows[0].materialMissing);
    EXPECT_TRUE(pl.rows[1].materialMissing);
    EXPECT_TRUE(pl.rows[2].materialMissing);
    ASSERT_EQ(countCode(pl.diagnostics, "production.material.missing"), 2);
    EXPECT_NE(pl.diagnostics[0].message.find("'Ghost' (p2)"), std::string::npos);
    EXPECT_NE(pl.diagnostics[0].message.find("'m.ghost'"), std::string::npos);
    EXPECT_EQ(pl.diagnostics[0].entityId, "p2");
    EXPECT_NE(pl.diagnostics[1].message.find("no material assigned"), std::string::npos);

    const auto mr = materialReport(m);
    // Known material first (model order), then missing ids sorted: "" < "m.ghost".
    ASSERT_EQ(mr.rows.size(), 3u);
    EXPECT_FALSE(mr.rows[0].missing);
    EXPECT_TRUE(mr.rows[1].missing);
    EXPECT_EQ(mr.rows[1].materialId.str(), "");
    EXPECT_TRUE(mr.rows[2].missing);
    EXPECT_EQ(mr.rows[2].materialId.str(), "m.ghost");
    EXPECT_NEAR(mr.rows[2].cutAreaM2, 0.18, 1e-12); // 0.6 x 0.3, still totalled
    EXPECT_FALSE(mr.rows[2].sheetEstimateAvailable);
    EXPECT_EQ(mr.rows[2].sheetsLowerBoundByArea, 0);
    EXPECT_FALSE(mr.rows[2].costKnown);
    const auto mt = materialTable(mr);
    EXPECT_EQ(mt.rows[2][0], "m.ghost [MISSING]");
    EXPECT_EQ(mt.rows[1][0], "[MISSING]");
    EXPECT_EQ(mt.rows[2][7], ""); // no sheet figure invented
    EXPECT_EQ(mt.rows[2][10], "MISSING");

    const auto cl = cutList(m);
    ASSERT_EQ(cl.rows.size(), 3u);
    EXPECT_TRUE(cl.rows[0].materialMissing); // empty material name sorts first
    EXPECT_TRUE(cl.rows[0].rotationAllowed); // grain None
    EXPECT_EQ(countCode(cl.diagnostics, "production.material.missing"), 2);

    const auto c = costEstimate(m);
    EXPECT_FALSE(c.complete);
    EXPECT_EQ(c.materials[2].source, SheetCountSource::Unavailable);
    EXPECT_NEAR(c.materialsTotal, 10.0, 1e-12); // only the known material: 1 sheet x 10.00
    EXPECT_NE(costTable(c).rows.back()[7].find("incomplete"), std::string::npos);

    const auto ls = labels(m);
    EXPECT_TRUE(ls.labels[1].materialMissing);
    EXPECT_EQ(ls.labels[1].materialName, "m.ghost");
    EXPECT_EQ(labelTable(ls).rows[1][10], "m.ghost [MISSING]");

    // A missing material with a grain direction must not be freely rotated.
    m.assemblies[0].parts[1].grain = domain::GrainDirection::AlongLength;
    for (const auto& row : cutList(m).rows)
        if (row.materialId.str() == "m.ghost") { EXPECT_FALSE(row.rotationAllowed); }
}

TEST(Robustness, MissingEdgeBandIsFlagged) {
    auto m = smallModel();
    auto p = panel("p1", "Panel", 500, 300, 18, "m1");
    p.edge(domain::EdgeSide::L1) = domain::EdgeBandId("eb.ghost");
    m.assemblies[0].parts = {p};

    const auto pl = partList(m);
    ASSERT_EQ(pl.rows.size(), 1u);
    EXPECT_TRUE(pl.rows[0].edges[0].missing);
    EXPECT_TRUE(pl.rows[0].edges[0].banded());
    EXPECT_NEAR(pl.rows[0].cutWidth, 300.0, 1e-12); // unknown band counts as 0 mm (domain::cutSize)
    ASSERT_EQ(countCode(pl.diagnostics, "production.edgeband.missing"), 1);
    EXPECT_NE(pl.diagnostics[0].message.find("Edge L1"), std::string::npos);
    EXPECT_NE(pl.diagnostics[0].message.find("'eb.ghost'"), std::string::npos);
    EXPECT_EQ(partListTable(pl).rows[0][12], "eb.ghost [MISSING]");

    const auto er = edgeBandingReport(m);
    ASSERT_EQ(er.rows.size(), 1u);
    EXPECT_TRUE(er.rows[0].missing);
    EXPECT_EQ(er.rows[0].edgeCount, 1);
    EXPECT_NEAR(er.rows[0].appliedLengthM, 0.5, 1e-12);   // edge L1 runs along the 500 mm length
    EXPECT_NEAR(er.rows[0].purchaseLengthM, 0.55, 1e-12); // + 2 x 25 mm
    EXPECT_FALSE(er.rows[0].costKnown);
    EXPECT_DOUBLE_EQ(er.rows[0].cost, 0.0);
    EXPECT_EQ(countCode(er.diagnostics, "production.edgeband.missing"), 1);
    EXPECT_EQ(edgeBandingTable(er).rows[0][0], "eb.ghost [MISSING]");

    EXPECT_EQ(labels(m).labels[0].edgeCode, "?000");
    EXPECT_FALSE(costEstimate(m).complete);
    const auto svg = renderLabelSheetsSvg(labels(m).labels);
    ASSERT_TRUE(svg.ok());
    EXPECT_NE(svg.value()[0].find("stroke-dasharray"), std::string::npos);
}

TEST(Robustness, HardwareProblemsAreFlagged) {
    auto m = domain::fixtures::basicCabinet();
    domain::HardwareInstance ghost;
    ghost.id = domain::HardwareInstanceId("cab1/hw.ghost");
    ghost.definition = domain::HardwareId("hw.unknown");
    ghost.quantity = 3;
    domain::HardwareInstance zero = m.assemblies[0].hardware[0];
    zero.id = domain::HardwareInstanceId("cab1/hw.zero");
    zero.quantity = 0;
    m.assemblies[0].hardware.push_back(ghost);
    m.assemblies[0].hardware.push_back(zero);

    const auto r = hardwareReport(m);
    ASSERT_EQ(r.rows.size(), 2u);
    EXPECT_EQ(r.rows[0].quantity, 4); // the zero-quantity instance is not counted
    EXPECT_EQ(r.rows[0].instanceCount, 1);
    EXPECT_TRUE(r.rows[1].unknownDefinition);
    EXPECT_EQ(r.rows[1].quantity, 3);
    EXPECT_FALSE(r.rows[1].costKnown);
    EXPECT_EQ(countCode(r.diagnostics, "production.hardware.unknown_definition"), 1);
    EXPECT_EQ(countCode(r.diagnostics, "production.hardware.invalid_quantity"), 1);
    const auto t = hardwareTable(r);
    EXPECT_EQ(t.rows[1][0], "hw.unknown [UNKNOWN DEFINITION]");
    EXPECT_EQ(t.rows[1][5], "");
    EXPECT_EQ(t.rows[1][6], "no");

    const auto c = costEstimate(m);
    EXPECT_FALSE(c.complete);
    EXPECT_NEAR(c.hardwareTotal, 0.20, 1e-12);
    EXPECT_EQ(projectSummary(m).hardwareQuantity, 7); // 4 + 3; non-positive quantities ignored
}

TEST(Robustness, InvalidSizesDoNotPoisonTotals) {
    auto m = smallModel();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    m.assemblies[0].parts = {panel("ok", "Good", 500, 400, 18, "m1"), panel("nan", "NaN", nan, 400, 18, "m1"),
                             panel("neg", "Negative", -100, 400, 18, "m1"), panel("inf", "Inf", inf, 400, 18, "m1")};
    const auto pl = partList(m);
    EXPECT_EQ(countCode(pl.diagnostics, "production.part.invalid_size"), 3);
    const auto mr = materialReport(m);
    ASSERT_EQ(mr.rows.size(), 1u);
    EXPECT_EQ(mr.rows[0].partCount, 4);
    EXPECT_NEAR(mr.rows[0].cutAreaM2, 0.2, 1e-12); // only the valid 0.5 x 0.4 part
    EXPECT_EQ(mr.rows[0].sheetsLowerBoundByArea, 1);
    EXPECT_EQ(cutList(m).rows.size(), 4u); // sorting must cope with NaN
    EXPECT_EQ(labels(m).labels.size(), 4u);
    EXPECT_TRUE(std::isfinite(projectSummary(m).finishedAreaM2));
    EXPECT_TRUE(std::isfinite(costEstimate(m).total));
    EXPECT_FALSE(toCsv(partListTable(pl)).empty());
    ASSERT_TRUE(renderLabelSheetsSvg(labels(m).labels).ok());

    // Material without a usable sheet size: flagged, no estimate, cost unknown.
    m.materials[0].sheetWidth = 0;
    const auto bad = materialReport(m);
    EXPECT_FALSE(bad.rows[0].sheetEstimateAvailable);
    EXPECT_EQ(countCode(bad.diagnostics, "production.material.invalid_sheet"), 1);
    EXPECT_EQ(materialTable(bad).rows[0][10], "no sheet size");
    EXPECT_FALSE(costEstimate(m).complete);
}
