// SPDX-License-Identifier: AGPL-3.0-only
// A cabinet with a glass-panel frame door pair: the panes are bought-in items (domain::isBoughtIn),
// left out of the cut list, cutting plans, nesting and edge-banding jobs, listed in the bought-in
// list and quoted by area; the glazing beads stay normal, unbanded parts.
#include "test_support.hpp"

#include "fos/cutting/from_model.hpp"
#include "fos/nesting/from_model.hpp"
#include "fos/production/edge_banding_jobs.hpp"

#include <algorithm>

namespace fos::project::test {
namespace {

const domain::MaterialId kGlass("glass.float4");

Project glassProject() {
    Project p = newProject(meta());
    domain::Material g;
    g.id = kGlass;
    g.name = "Float glass 4 mm";
    g.kind = domain::MaterialKind::Glass;
    g.thickness = 4;
    g.costPerSquareMeter = 60.0;
    p.libraries.materials.push_back(g);
    Item item = baseCabinetItem();
    furniture::FrontSpec& f = *item.cabinet()->interior.front;
    f.style = furniture::FrontStyle::FramePanel;
    f.frame.joint = furniture::FrameJoint::Dowel;
    f.frame.panelKind = furniture::FramePanelKind::Glass;
    f.frame.panelMaterial = kGlass;
    p.items = {item};
    return p;
}

int count(const core::Diagnostics& ds, const std::string& code) {
    return static_cast<int>(std::count_if(ds.begin(), ds.end(), [&](const auto& d) { return d.code == code; }));
}

bool isPane(const std::string& id) { return id.ends_with(".panel"); }

} // namespace

TEST(ProjectBoughtIn, GlassFrontPanesAreBoughtInEverywhere) {
    const Project p = glassProject();
    const Resolved r = resolved(p);
    const domain::Part* paneL = findPart(r, "cab1/door.left.panel");
    const domain::Part* paneR = findPart(r, "cab1/door.right.panel");
    ASSERT_NE(paneL, nullptr);
    ASSERT_NE(paneR, nullptr);
    ASSERT_TRUE(domain::isBoughtIn(*paneL));
    const domain::Part* bead = findPart(r, "cab1/door.left.bead.top");
    ASSERT_NE(bead, nullptr);
    EXPECT_FALSE(domain::isBoughtIn(*bead));

    // Cut list: no pane, the beads are there without edge bands.
    const ProductionReports rep = productionReports(r, productionOptions(p));
    bool beadSeen = false;
    for (const auto& row : rep.cutList.rows)
        for (const auto& id : row.partIds) {
            EXPECT_FALSE(isPane(id.str())) << id.str();
            if (id.str() == "cab1/door.left.bead.top") {
                beadSeen = true;
                for (const auto& e : row.edges) EXPECT_FALSE(e.banded());
            }
        }
    EXPECT_TRUE(beadSeen);
    EXPECT_EQ(count(rep.cutList.diagnostics, "production.bought_in.excluded"), 1);
    for (const auto& row : rep.materials.rows) EXPECT_NE(row.materialId, kGlass);

    // Bought-in list: the two equal panes in one row.
    ASSERT_EQ(rep.boughtIn.rows.size(), 1u);
    const auto& row = rep.boughtIn.rows[0];
    EXPECT_EQ(row.quantity, 2);
    EXPECT_EQ(row.materialName, "Float glass 4 mm");
    EXPECT_DOUBLE_EQ(row.width, paneL->length);
    EXPECT_DOUBLE_EQ(row.height, paneL->width);
    EXPECT_DOUBLE_EQ(row.thickness, 4);
    const double areaEach = paneL->length * paneL->width / 1e6;
    EXPECT_NEAR(row.totalAreaM2, 2 * areaEach, 1e-12);

    // Quote: one bought-in line, area x 60 / m2.
    auto q = production::quote(r.model, production::PricingSettings{});
    ASSERT_TRUE(q.ok()) << q.error().message;
    int lines = 0;
    for (const auto& l : q.value().lines) {
        if (l.category == production::QuoteCategory::Material) {
            EXPECT_NE(l.id, kGlass.str());
        }
        if (l.category != production::QuoteCategory::BoughtIn) continue;
        ++lines;
        EXPECT_EQ(l.unit, "m2");
        EXPECT_NEAR(l.quantity, 2 * areaEach, 1e-12);
        ASSERT_TRUE(l.priced());
        EXPECT_NEAR(l.amount, 2 * areaEach * 60.0, 1e-9);
    }
    EXPECT_EQ(lines, 1);

    // Edge-banding jobs: neither panes nor beads.
    const auto jobs = production::edgeBandingJobs(r.model, p.settings.edgeBanding);
    for (const auto& g : jobs.groups)
        for (const auto& pj : g.parts) {
            EXPECT_FALSE(isPane(pj.partId.str())) << pj.partId.str();
            EXPECT_EQ(pj.partId.str().find(".bead."), std::string::npos) << pj.partId.str();
        }
    EXPECT_EQ(count(jobs.diagnostics, "production.bought_in.excluded"), 1);

    // Cutting plans: no glass job, no pane piece, one Info for the two panes.
    const CuttingPlans plans = cuttingPlans(r, p.cutting);
    for (const auto& plan : plans.plans) {
        EXPECT_NE(plan.job.id, kGlass.str());
        for (const auto& piece : plan.job.pieces)
            for (const auto& id : piece.sourceIds) EXPECT_FALSE(isPane(id)) << id;
    }
    ASSERT_EQ(count(plans.diagnostics, "cutting.adapter.bought_in"), 1);
    EXPECT_EQ(count(plans.diagnostics, "nesting.adapter.bought_in"), 0);
    for (const auto& d : plans.diagnostics)
        if (d.code == "cutting.adapter.bought_in") {
            EXPECT_EQ(d.message.rfind("2 bought-in", 0), 0u) << d.message;
        }
}

// A shaped (chamfered) pane would otherwise be nested: the nesting adapter skips and counts it,
// the guillotine adapter neither cuts nor counts it (no double report).
TEST(ProjectBoughtIn, ShapedPaneIsNotNested) {
    domain::Model m = resolved(glassProject()).model;
    domain::Part* pane = nullptr;
    for (auto& a : m.assemblies)
        if (auto* p = a.findPart(domain::PartId("cab1/door.left.panel"))) pane = p;
    ASSERT_NE(pane, nullptr);
    const double L = pane->length, W = pane->width, c = 20;
    domain::PartShape shape;
    for (const auto& v : std::vector<geometry::Vec2>{
             {c, 0}, {L - c, 0}, {L, c}, {L, W - c}, {L - c, W}, {c, W}, {0, W - c}, {0, c}})
        shape.outer.vertices.push_back({v, 0.0});
    pane->shape = shape;
    ASSERT_FALSE(domain::isRectangularOutline(*pane));

    core::Diagnostics nd;
    const auto nests = nesting::nestingJobsFromModel(m, {}, nd);
    for (const auto& j : nests)
        for (const auto& part : j.job.parts) EXPECT_FALSE(isPane(part.id)) << part.id;
    EXPECT_EQ(count(nd, "nesting.adapter.bought_in"), 1);

    core::Diagnostics cd;
    const auto cut = cutting::cuttingFromModel(m, {}, cd);
    EXPECT_EQ(cut.boughtInCount, 1); // the rectangular right pane only
    EXPECT_EQ(count(cd, "cutting.adapter.bought_in"), 1);
    for (const auto& rp : cut.routedParts) EXPECT_FALSE(isPane(rp.partId)) << rp.partId;
}

} // namespace fos::project::test
