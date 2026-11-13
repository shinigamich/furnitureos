// SPDX-License-Identifier: AGPL-3.0-only
// Scenario A - simple cabinet, end to end:
//   new project -> base cabinet 800 x 720 x 560 -> materials and edge banding assigned
//   -> drawing (SVG with the overall dimensions) -> part list / cut list (finished vs cut)
//   -> cutting layout (independent validator: zero errors) -> save -> reopen -> identical model.
//
// Hand computation. Base-cabinet template (furniture/templates.hpp): plinth 100 with the sides
// to the floor, stretcher rails 100 wide, overlay back, 1 adjustable shelf, a pair of doors.
// Inputs: W = 800, H = 720, D = 560, board thickness T = 18 (18 mm material), back 3 mm HDF.
// Standard parameters (furniture/parameters.hpp): shelf_clearance 1, shelf_setback 20,
// back_inset 2, front_reveal 2, front_gap 3, plinth_height 100.
//   inner_width     = W - 2T          = 800 - 36        = 764   (bottom, both rails, plinth)
//   shelf           = (764 - 2*1) x (560 - 20)          = 762 x 540
//   sides           = H x D                              = 720 x 560   (sides run to the floor)
//   carcass_height  = H - plinth                          = 720 - 100 = 620
//   back            = (W - 2*2) x (620 - 2*2)            = 796 x 616
//   doors           = (620 - 2*2) x (W - 2*2 - 3) / 2    = 616 x 396.5 (length vertical)
// Materials assigned here: carcass pb18, back hdf3, FRONTS mdf18; visible edge ABS 2 mm,
// hidden edge ABS 0.4 mm, front edge ABS 0.4 mm (all four door edges).
// Cut size = finished - band thickness on the banded edges (ADR-0004):
//   side   L1 front 2, W1/W2 ends 0.4  -> (720 - 0.4 - 0.4) x (560 - 2) = 719.2 x 558
//   bottom L1 2                         -> 764 x 558
//   front rail L1 2                     -> 764 x 98;  rear rail, plinth raw -> 764 x 100
//   shelf  L1 2                         -> 762 x 538
//   door   0.4 all round                -> (616 - 0.8) x (396.5 - 0.8) = 615.2 x 395.7
//   back   raw                          -> 796 x 616
#include "scenario_support.hpp"

#include "fos/drawings/svg.hpp"
#include "fos/validation/validation.hpp"

namespace fos::scenario {
namespace {

project::Project buildScenarioA() {
    project::Document doc(project::newProject(meta("Scenario A")));
    EXPECT_TRUE(doc.execute(project::commands::addItem(templateItem(&furniture::templates::baseCabinet, "cab1"))).ok());
    EXPECT_TRUE(doc.execute(project::commands::setParameter("cab1", "width", 800)).ok());
    EXPECT_TRUE(doc.execute(project::commands::setParameter("cab1", "height", 720)).ok());
    furniture::MaterialAssignment m;
    m.carcass = domain::MaterialId(project::defaults::kParticleboard18);
    m.back = domain::MaterialId(project::defaults::kHdf3);
    m.front = domain::MaterialId(project::defaults::kMdf18);
    m.visibleEdge = domain::EdgeBandId(project::defaults::kAbs2);
    m.hiddenEdge = domain::EdgeBandId(project::defaults::kAbs04);
    m.frontEdge = domain::EdgeBandId(project::defaults::kAbs04);
    auto r = doc.execute(project::commands::setMaterials("cab1", m));
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return doc.project();
}

struct Size {
    const char* part;
    double length, width, cutLength, cutWidth;
    const char* material;
};

// All values from the hand computation in the file header.
constexpr Size kExpected[] = {
    {"cab1/side.left", 720, 560, 719.2, 558, "mat.generic.pb18"},
    {"cab1/side.right", 720, 560, 719.2, 558, "mat.generic.pb18"},
    {"cab1/bottom", 764, 560, 764, 558, "mat.generic.pb18"},
    {"cab1/rail.front", 764, 100, 764, 98, "mat.generic.pb18"},
    {"cab1/rail.back", 764, 100, 764, 100, "mat.generic.pb18"},
    {"cab1/plinth", 764, 100, 764, 100, "mat.generic.pb18"},
    {"cab1/shelf.1", 762, 540, 762, 538, "mat.generic.pb18"},
    {"cab1/door.left", 616, 396.5, 615.2, 395.7, "mat.generic.mdf18"},
    {"cab1/door.right", 616, 396.5, 615.2, 395.7, "mat.generic.mdf18"},
    {"cab1/back", 796, 616, 796, 616, "mat.generic.hdf3"},
};

TEST(ScenarioA, PartListAndCutListMatchTheHandComputation) {
    const project::Project p = buildScenarioA();
    const project::Resolved r = resolved(p);
    EXPECT_FALSE(core::hasErrors(r.diagnostics));
    ASSERT_EQ(r.model.assemblies.size(), 1u);
    EXPECT_EQ(r.model.assemblies[0].parts.size(), 10u);

    const auto reports = project::productionReports(r, project::productionOptions(p));
    ASSERT_EQ(reports.parts.rows.size(), 10u);
    for (const auto& e : kExpected) {
        const production::PartRow* row = nullptr;
        for (const auto& pr : reports.parts.rows)
            if (pr.partId.str() == e.part) row = &pr;
        ASSERT_NE(row, nullptr) << e.part;
        EXPECT_DOUBLE_EQ(row->length, e.length) << e.part;
        EXPECT_DOUBLE_EQ(row->width, e.width) << e.part;
        EXPECT_NEAR(row->cutLength, e.cutLength, 1e-9) << e.part;
        EXPECT_NEAR(row->cutWidth, e.cutWidth, 1e-9) << e.part;
        EXPECT_EQ(row->materialId.str(), e.material) << e.part;
        // The cut list row of the part carries the same finished and cut size.
        const auto* cut = cutRow(reports.cutList, e.part);
        ASSERT_NE(cut, nullptr) << e.part;
        EXPECT_NEAR(cut->cutLength, e.cutLength, 1e-9) << e.part;
        EXPECT_NEAR(cut->cutWidth, e.cutWidth, 1e-9) << e.part;
        EXPECT_DOUBLE_EQ(cut->length, e.length) << e.part;
    }
    // Grouping: parts merge only when size, edges, grain AND machining signature match. The
    // left/right sides and the left/right doors are drilled as mirror images (holes on the
    // opposite face / hinge side), so they stay separate rows; the rear rail and the plinth
    // (764 x 100 raw, 2 confirmat holes in each end edge) merge. 10 parts -> 9 rows.
    EXPECT_EQ(reports.cutList.rows.size(), 9u);
    EXPECT_EQ(cutRow(reports.cutList, "cab1/plinth"), cutRow(reports.cutList, "cab1/rail.back"));
    EXPECT_EQ(cutRow(reports.cutList, "cab1/plinth")->quantity, 2);
    EXPECT_NE(cutRow(reports.cutList, "cab1/door.left"), cutRow(reports.cutList, "cab1/door.right"));
    EXPECT_NE(cutRow(reports.cutList, "cab1/side.left"), cutRow(reports.cutList, "cab1/side.right"));

    // Material areas (finished):
    //   pb18: sides 2*720*560 = 806400, bottom 764*560 = 427840, rails 2*764*100 = 152800,
    //         plinth 76400, shelf 762*540 = 411480                       -> 1874920 mm2
    //   mdf18: doors 2*616*396.5 = 488488 mm2;  hdf3: 796*616 = 490336 mm2
    EXPECT_NEAR(materialRow(reports.materials, "mat.generic.pb18")->finishedAreaM2, 1.874920, 1e-9);
    EXPECT_NEAR(materialRow(reports.materials, "mat.generic.mdf18")->finishedAreaM2, 0.488488, 1e-9);
    EXPECT_NEAR(materialRow(reports.materials, "mat.generic.hdf3")->finishedAreaM2, 0.490336, 1e-9);
    EXPECT_EQ(materialRow(reports.materials, "mat.generic.pb18")->partCount, 7);

    // Edge banding (applied length = finished edge length):
    //   ABS 2:   side fronts 2*720 + bottom 764 + front rail 764 + shelf 762 = 3730 mm, 5 edges
    //   ABS 0.4: side ends 4*560 = 2240 + doors 2*(2*616 + 2*396.5) = 4050   = 6290 mm, 12 edges
    //   overhang 25 mm per end: 5*50 = 250 mm and 12*50 = 600 mm
    ASSERT_EQ(reports.edgeBanding.rows.size(), 2u);
    for (const auto& row : reports.edgeBanding.rows) {
        if (row.edgeBandId.str() == project::defaults::kAbs2) {
            EXPECT_EQ(row.edgeCount, 5);
            EXPECT_NEAR(row.appliedLengthM, 3.730, 1e-9);
            EXPECT_NEAR(row.overhangAllowanceM, 0.250, 1e-9);
        } else {
            EXPECT_EQ(row.edgeBandId.str(), project::defaults::kAbs04);
            EXPECT_EQ(row.edgeCount, 12);
            EXPECT_NEAR(row.appliedLengthM, 6.290, 1e-9);
            EXPECT_NEAR(row.overhangAllowanceM, 0.600, 1e-9);
        }
    }
}

TEST(ScenarioA, DrawingShowsTheOverallDimensions) {
    const project::Project p = buildScenarioA();
    const project::Resolved r = resolved(p);
    auto drawing = project::assemblyDrawing(p, r, "cab1");
    ASSERT_TRUE(drawing.ok()) << drawing.error().message;
    // Front view: width 800 (x 0..800), height 720 (z 0..720). Depth (top view):
    // doors 2 in front of the carcass (front_offset) and 18 thick -> y = -20; overlay back
    // 3 mm behind y = 560 -> y = 563; 563 + 20 = 583.
    const auto* w = findDimension(drawing.value(), "cab1/view.front/dim.overall.width");
    const auto* h = findDimension(drawing.value(), "cab1/view.front/dim.overall.height");
    const auto* d = findDimension(drawing.value(), "cab1/view.top/dim.overall.depth");
    ASSERT_NE(w, nullptr);
    ASSERT_NE(h, nullptr);
    ASSERT_NE(d, nullptr);
    EXPECT_NEAR(drawings::measuredValue(*w), 800.0, 1e-9);
    EXPECT_NEAR(drawings::measuredValue(*h), 720.0, 1e-9);
    EXPECT_NEAR(drawings::measuredValue(*d), 583.0, 1e-9);
    ASSERT_EQ(drawing.value().sheets.size(), 1u);
    const std::string svg = drawings::toSvg(drawing.value().sheets[0]);
    EXPECT_TRUE(contains(svg, "data-element=\"cab1/view.front/dim.overall.width\">800</text>"));
    EXPECT_TRUE(contains(svg, "data-element=\"cab1/view.front/dim.overall.height\">720</text>"));
    EXPECT_TRUE(contains(svg, "data-element=\"cab1/view.top/dim.overall.depth\">583</text>"));
}

TEST(ScenarioA, CuttingLayoutIsValidAndComplete) {
    const project::Project p = buildScenarioA();
    const project::Resolved r = resolved(p);
    const auto plans = project::cuttingPlans(r, p.cutting);
    // One job per used material, model material order: pb18 (7 parts), mdf18 (2 doors), hdf3 (back).
    ASSERT_EQ(plans.plans.size(), 3u);
    EXPECT_FALSE(core::hasErrors(plans.diagnostics));
    const int expectedPlaced[] = {7, 2, 1};
    for (std::size_t i = 0; i < plans.plans.size(); ++i) {
        const auto& plan = plans.plans[i];
        ASSERT_TRUE(plan.result.has_value()) << plan.job.id;
        EXPECT_EQ(core::hasErrors(plan.validation), false) << plan.job.id;
        EXPECT_TRUE(plan.validation.empty()) << plan.job.id;
        EXPECT_TRUE(plan.result->unplaced.empty()) << plan.job.id;
        EXPECT_EQ(plan.result->totals.placedCount, expectedPlaced[i]) << plan.job.id;
        EXPECT_DOUBLE_EQ(plan.job.kerf, 4.0); // project default
    }
    EXPECT_EQ(plans.plans[1].job.id, project::defaults::kMdf18);
    // The door piece is the CUT size 615.2 x 395.7 (never the finished size), quantity 2.
    ASSERT_EQ(plans.plans[1].job.pieces.size(), 1u);
    EXPECT_NEAR(plans.plans[1].job.pieces[0].length, 615.2, 1e-9);
    EXPECT_NEAR(plans.plans[1].job.pieces[0].width, 395.7, 1e-9);
    EXPECT_EQ(plans.plans[1].job.pieces[0].quantity, 2);
}

TEST(ScenarioA, ModelValidationHasNoErrors) {
    const project::Project p = buildScenarioA();
    const auto report = validation::validateModel(resolved(p).model);
    EXPECT_TRUE(report.ok()) << validation::toText(report);
    EXPECT_EQ(report.stats.parts, 10u);
}

TEST(ScenarioA, SaveAndReopenGivesTheIdenticalResolvedModel) {
    const project::Project p = buildScenarioA();
    const std::string before = modelText(resolved(p).model);
    TempDir dir;
    const auto path = dir.path / "scenario_a.fosp";
    auto saved = project::saveProject(path, p, fixedClock());
    ASSERT_TRUE(saved.ok()) << saved.error().message;
    auto loaded = project::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_EQ(modelText(loaded.value().resolved.model), before);
    EXPECT_TRUE(loaded.value().diagnostics.empty());
    // Saving the reopened project again reproduces the file byte for byte (same savedAt).
    auto text1 = project::serializeProject(p, "2026-11-13T08:00:00Z");
    auto text2 = project::serializeProject(loaded.value().project, "2026-11-13T08:00:00Z");
    ASSERT_TRUE(text1.ok());
    ASSERT_TRUE(text2.ok());
    EXPECT_EQ(text1.value(), text2.value());
}

} // namespace
} // namespace fos::scenario
