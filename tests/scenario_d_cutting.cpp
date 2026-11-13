// SPDX-License-Identifier: AGPL-3.0-only
// Scenario D - cutting: several components (base cabinet, wall cabinet, free panel) on two
// board materials, one of them with grain; kerf 4 mm -> optimise (heuristic) -> independent
// validateLayout with zero errors -> every part placed -> utilisation recomputed here from the
// placements equals the reported one; grain-restricted pieces are never turned; any two pieces
// are at least one kerf apart.
//
// Components:
//   cab1  base cabinet 600 x 820 x 560 (template materials, all pb18 + hdf3 back)
//   wall1 wall cabinet 600 x 720 x 320 with carcass AND fronts on "Oak 18" (grained)
//   shelf1 free panel 600 x 300 on Oak 18 (raw edges)
// Oak 18 parts (grain along the part length because the material has grain), cut sizes:
//   wall sides 2 x (720 - 0.4 - 0.4) x (320 - 2) = 719.2 x 318
//   wall top + bottom 2 x 564 x 318   (564 = 600 - 36, front edge 2 mm)
//   wall shelves 2 x 562 x 298        (562 = 564 - 2, 300 = 320 - 20 setback, front edge 2 mm)
//   wall door 712 x 592               (716 x 596 finished, ABS 2 all round)
//   free panel 600 x 300
//   -> 8 instances, area 2*719.2*318 + 2*564*318 + 2*562*298 + 712*592 + 600*300
//      = 457411.2 + 358704 + 334952 + 421504 + 180000 = 1752571.2 mm2
#include "scenario_support.hpp"

#include "fos/core/units.hpp"

namespace fos::scenario {
namespace {

constexpr const char* kOak = "mat.oak18";

project::Project buildScenarioD() {
    project::Document doc(project::newProject(meta("Scenario D")));
    domain::Material oak;
    oak.id = domain::MaterialId(kOak);
    oak.name = "Oak 18 (test decor)";
    oak.kind = domain::MaterialKind::Particleboard;
    oak.thickness = 18;
    oak.sheetLength = 2800;
    oak.sheetWidth = 2070;
    oak.hasGrain = true;
    EXPECT_TRUE(doc.execute(project::commands::upsertMaterial(oak)).ok());

    EXPECT_TRUE(doc.execute(project::commands::addItem(templateItem(&furniture::templates::baseCabinet, "cab1"))).ok());
    project::Item wall = templateItem(&furniture::templates::wallCabinet, "wall1");
    wall.cabinet()->placement = geometry::Transform::translation({0, 0, 1500});
    EXPECT_TRUE(doc.execute(project::commands::addItem(wall)).ok());
    furniture::MaterialAssignment m = doc.project().findItem("wall1")->cabinet()->materials;
    m.carcass = domain::MaterialId(kOak);
    m.front = domain::MaterialId(kOak);
    auto r = doc.execute(project::commands::setMaterials("wall1", m));
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);

    project::Item panel;
    panel.definition = project::PanelItem{domain::AssemblyId("shelf1"), geometry::Transform::translation({1000, 0, 0}),
                                          furniture::makePanel(domain::PartId("shelf1/panel"), "Loose shelf",
                                                               domain::MaterialId(kOak))};
    EXPECT_TRUE(doc.execute(project::commands::addItem(panel)).ok());

    project::CuttingSettings settings;
    settings.kerf = 4.0;
    settings.trim = {10, 10, 10, 10};
    EXPECT_TRUE(doc.execute(project::commands::setCuttingSettings(std::nullopt, settings)).ok());
    return doc.project();
}

bool separatedByKerf(const cutting::Placement& a, const cutting::Placement& b, double kerf) {
    const double eps = core::kLengthEps;
    return a.x + a.length + kerf <= b.x + eps || b.x + b.length + kerf <= a.x + eps ||
           a.y + a.width + kerf <= b.y + eps || b.y + b.width + kerf <= a.y + eps;
}

TEST(ScenarioD, OptimisedLayoutsAreValidCompleteAndHonest) {
    const project::Project p = buildScenarioD();
    const project::Resolved r = resolved(p);
    EXPECT_FALSE(core::hasErrors(r.diagnostics));
    ASSERT_EQ(r.model.assemblies.size(), 3u);
    const auto plans = project::cuttingPlans(r, p.cutting);
    EXPECT_FALSE(core::hasErrors(plans.diagnostics));
    // Jobs in model material order: pb18, hdf3 (2 backs), oak (user material appended last).
    ASSERT_EQ(plans.plans.size(), 3u);

    for (const auto& plan : plans.plans) {
        SCOPED_TRACE(plan.job.id);
        ASSERT_TRUE(plan.result.has_value());
        const auto& res = *plan.result;
        EXPECT_TRUE(plan.validation.empty());          // independent validator: no findings at all
        EXPECT_TRUE(res.unplaced.empty());             // every part placed
        EXPECT_DOUBLE_EQ(plan.job.kerf, 4.0);
        EXPECT_FALSE(res.isProvenOptimal);             // a heuristic never claims optimality

        int required = 0;
        for (const auto& piece : plan.job.pieces) required += piece.quantity;
        EXPECT_EQ(res.totals.placedCount, required);

        // Utilisation recomputed from the placements and the sheet sizes only.
        double placed = 0, sheets = 0;
        int count = 0;
        for (const auto& sheet : res.sheets) {
            sheets += sheet.sheetLength * sheet.sheetWidth;
            for (const auto& pl : sheet.placements) {
                placed += pl.length * pl.width;
                ++count;
            }
            for (std::size_t i = 0; i < sheet.placements.size(); ++i)
                for (std::size_t j = i + 1; j < sheet.placements.size(); ++j)
                    EXPECT_TRUE(separatedByKerf(sheet.placements[i], sheet.placements[j], 4.0))
                        << sheet.placements[i].pieceId << " / " << sheet.placements[j].pieceId;
            // Grain: a piece that may not rotate is never placed rotated.
            for (const auto& pl : sheet.placements)
                for (const auto& piece : plan.job.pieces)
                    if (piece.id == pl.pieceId && !piece.canRotate) {
                        EXPECT_FALSE(pl.rotated) << pl.pieceId;
                    }
        }
        EXPECT_EQ(count, required);
        ASSERT_GT(sheets, 0);
        EXPECT_NEAR(res.totals.utilisationOfSheetPct, 100.0 * placed / sheets, 1e-9);
        EXPECT_NEAR(res.totals.usedArea, placed, 1e-6);
    }

    // The grained job by hand (header): 8 instances, none may rotate, area 1752571.2 mm2.
    const auto& oak = plans.plans[2];
    EXPECT_EQ(oak.job.id, kOak);
    int instances = 0;
    for (const auto& piece : oak.job.pieces) {
        instances += piece.quantity;
        EXPECT_FALSE(piece.canRotate) << piece.id;
    }
    EXPECT_EQ(instances, 8);
    EXPECT_NEAR(oak.result->totals.usedArea, 1752571.2, 1e-6);
    // Grain-bound pieces keep their length along the sheet length: the door piece is 712 long.
    bool doorFound = false;
    for (const auto& piece : oak.job.pieces)
        if (piece.id == "wall1/door") {
            doorFound = true;
            EXPECT_DOUBLE_EQ(piece.length, 712.0);
            EXPECT_DOUBLE_EQ(piece.width, 592.0);
        }
    EXPECT_TRUE(doorFound);
    // By area one 2800 x 2070 sheet (usable 2780 x 2050 = 5699000 mm2) can hold 1752571.2 mm2,
    // so the area lower bound is 1 sheet.
    EXPECT_EQ(oak.result->sheetCountLowerBound, 1);
}

TEST(ScenarioD, ResultsAreDeterministic) {
    const project::Project p = buildScenarioD();
    const project::Resolved r = resolved(p);
    const auto a = project::cuttingPlans(r, p.cutting);
    const auto b = project::cuttingPlans(r, p.cutting);
    ASSERT_EQ(a.plans.size(), b.plans.size());
    for (std::size_t i = 0; i < a.plans.size(); ++i) {
        ASSERT_TRUE(a.plans[i].result && b.plans[i].result);
        ASSERT_EQ(a.plans[i].result->sheets.size(), b.plans[i].result->sheets.size());
        for (std::size_t s = 0; s < a.plans[i].result->sheets.size(); ++s) {
            const auto& pa = a.plans[i].result->sheets[s].placements;
            const auto& pb = b.plans[i].result->sheets[s].placements;
            ASSERT_EQ(pa.size(), pb.size());
            for (std::size_t k = 0; k < pa.size(); ++k) {
                EXPECT_EQ(pa[k].pieceId, pb[k].pieceId);
                EXPECT_EQ(pa[k].x, pb[k].x);
                EXPECT_EQ(pa[k].y, pb[k].y);
            }
        }
    }
}

TEST(ScenarioD, PieceTooLargeForTheStockIsReportedNotDropped) {
    project::Project p = buildScenarioD();
    // A 3000 mm long oak panel cannot be cut from a 2800 x 2070 board (usable 2780 x 2050) and,
    // being grain-bound, may not be turned: it must be listed as unplaced with a warning.
    project::Document doc(p);
    ASSERT_NE(doc.project().findItem("shelf1"), nullptr);
    ASSERT_TRUE(doc.execute(project::commands::setParameter("shelf1", "length", 3000)).ok());
    const auto r = resolved(doc.project());
    const auto plans = project::cuttingPlans(r, doc.project().cutting);
    const auto& oak = plans.plans[2];
    ASSERT_TRUE(oak.result.has_value());
    ASSERT_EQ(oak.result->unplaced.size(), 1u);
    EXPECT_EQ(oak.result->unplaced[0].pieceId, "shelf1/panel");
    EXPECT_EQ(oak.result->unplaced[0].reason, cutting::UnplacedReason::DoesNotFitAnyStock);
    EXPECT_TRUE(oak.validation.empty()); // the layout of the rest is still valid
    bool warned = false;
    for (const auto& d : plans.diagnostics) warned = warned || d.code == "project.cutting.unplaced";
    EXPECT_TRUE(warned);
}

} // namespace
} // namespace fos::scenario
