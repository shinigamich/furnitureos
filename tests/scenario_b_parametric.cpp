// SPDX-License-Identifier: AGPL-3.0-only
// Scenario B - parametric modification: a base cabinet (template materials: everything on
// pb18 except the 3 mm HDF back, ABS 2 visible / ABS 0.4 hidden edges, doors banded ABS 2 all
// round) is widened 800 -> 900 through an undoable document command.
//
// Hand computation (T = 18, H = 820 template default -> carcass height 720, D = 560):
//                          W = 800                    W = 900
//   inner width W - 36     764                        864        bottom, rails, plinth
//   shelf  inner - 2       762 x 540                  862 x 540
//   back   W - 4           796 x 716                  896 x 716
//   door   (W - 7) / 2     396.5 x 716                446.5 x 716   (height 720 - 4)
//   sides                  820 x 560                  820 x 560     (independent of W)
// pb18 finished-area change = 100 * (bottom 560 + rail 100 + rail 100 + plinth 100 + shelf 540)
//                           + 2 doors * 50 * 716 = 140000 + 71600 = 211600 mm2
// pb18 cut-area change: bottom 100*558 + front rail 100*98 + rear rail 100*100 + plinth 100*100
//                     + shelf 100*538 + doors 2 * 50 * (716 - 4) = 55800 + 9800 + 10000 + 10000
//                     + 53800 + 71200 = 210600 mm2
// hdf3 change: 100 * 716 = 71600 mm2.
#include "scenario_support.hpp"

#include "fos/drawings/svg.hpp"

namespace fos::scenario {
namespace {

struct Expected {
    double inner, shelf, back, door;
};
Expected expectedFor(double w) { return {w - 36, w - 38, w - 4, (w - 7) / 2}; }

void checkModel(const project::Project& p, double w) {
    const Expected e = expectedFor(w);
    const auto r = resolved(p);
    EXPECT_FALSE(core::hasErrors(r.diagnostics));
    for (const char* slot : {"cab1/bottom", "cab1/rail.front", "cab1/rail.back", "cab1/plinth"}) {
        ASSERT_NE(findPart(r, slot), nullptr) << slot;
        EXPECT_DOUBLE_EQ(findPart(r, slot)->length, e.inner) << slot << " at W " << w;
    }
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/shelf.1")->length, e.shelf);
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/shelf.1")->width, 540.0);
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/back")->length, e.back);
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/back")->width, 716.0);
    for (const char* door : {"cab1/door.left", "cab1/door.right"}) {
        EXPECT_DOUBLE_EQ(findPart(r, door)->width, e.door) << door;
        EXPECT_DOUBLE_EQ(findPart(r, door)->length, 716.0) << door;
    }
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/side.left")->length, 820.0);
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/side.left")->width, 560.0);

    // Cut list: bottom (front edge 2 mm) inner x 558; door (2 mm all round) 712 x (door - 4).
    const auto reports = project::productionReports(r, project::productionOptions(p));
    EXPECT_DOUBLE_EQ(cutRow(reports.cutList, "cab1/bottom")->cutLength, e.inner);
    EXPECT_DOUBLE_EQ(cutRow(reports.cutList, "cab1/bottom")->cutWidth, 558.0);
    EXPECT_DOUBLE_EQ(cutRow(reports.cutList, "cab1/door.left")->cutLength, 712.0);
    EXPECT_DOUBLE_EQ(cutRow(reports.cutList, "cab1/door.left")->cutWidth, e.door - 4);

    // Drawing: overall width of the front view = W, also in the exported SVG text.
    auto drawing = project::assemblyDrawing(p, r, "cab1");
    ASSERT_TRUE(drawing.ok()) << drawing.error().message;
    const auto* dim = findDimension(drawing.value(), "cab1/view.front/dim.overall.width");
    ASSERT_NE(dim, nullptr);
    EXPECT_NEAR(drawings::measuredValue(*dim), w, 1e-9);
    EXPECT_TRUE(contains(drawings::toSvg(drawing.value().sheets[0]),
                         "data-element=\"cab1/view.front/dim.overall.width\">" + core::formatLength(w) + "</text>"));

    // Cutting plan still complete and valid.
    const auto plans = project::cuttingPlans(r, p.cutting);
    EXPECT_FALSE(core::hasErrors(plans.diagnostics));
    for (const auto& plan : plans.plans) {
        ASSERT_TRUE(plan.result.has_value());
        EXPECT_TRUE(plan.result->unplaced.empty());
        EXPECT_TRUE(plan.validation.empty());
    }
}

struct Areas {
    double pb18Finished, pb18Cut, hdf3Finished;
};
Areas areas(const project::Project& p) {
    const auto reports = project::productionReports(resolved(p), project::productionOptions(p));
    return {materialRow(reports.materials, "mat.generic.pb18")->finishedAreaM2,
            materialRow(reports.materials, "mat.generic.pb18")->cutAreaM2,
            materialRow(reports.materials, "mat.generic.hdf3")->finishedAreaM2};
}

TEST(ScenarioB, WidthChangePropagatesEverywhereAndUndoRestores) {
    project::Document doc(project::newProject(meta("Scenario B")));
    ASSERT_TRUE(doc.execute(project::commands::addItem(templateItem(&furniture::templates::baseCabinet, "cab1"))).ok());
    ASSERT_TRUE(doc.execute(project::commands::setParameter("cab1", "width", 800)).ok());
    checkModel(doc.project(), 800);
    const std::string model800 = modelText(resolved(doc.project()).model);
    const Areas a800 = areas(doc.project());

    ASSERT_TRUE(doc.execute(project::commands::setParameter("cab1", "width", 900)).ok());
    EXPECT_TRUE(doc.isDirty());
    checkModel(doc.project(), 900);
    const Areas a900 = areas(doc.project());
    EXPECT_NEAR(a900.pb18Finished - a800.pb18Finished, 0.211600, 1e-9); // see header
    EXPECT_NEAR(a900.pb18Cut - a800.pb18Cut, 0.210600, 1e-9);
    EXPECT_NEAR(a900.hdf3Finished - a800.hdf3Finished, 0.071600, 1e-9);

    // Save and reopen: the reopened project resolves to exactly the same model.
    TempDir dir;
    const auto path = dir.path / "b.fosp";
    ASSERT_TRUE(doc.save(path, fixedClock()).ok());
    EXPECT_FALSE(doc.isDirty());
    auto loaded = project::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    const std::string model900 = modelText(resolved(doc.project()).model);
    EXPECT_EQ(modelText(loaded.value().resolved.model), model900);
    checkModel(loaded.value().project, 900);

    // Undo restores the 800 model exactly; redo the 900 one.
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(modelText(resolved(doc.project()).model), model800);
    EXPECT_TRUE(doc.isDirty()); // the saved state is the 900 one
    checkModel(doc.project(), 800);
    ASSERT_TRUE(doc.redo().ok());
    EXPECT_EQ(modelText(resolved(doc.project()).model), model900);
    EXPECT_FALSE(doc.isDirty());
}

TEST(ScenarioB, RefusedChangeLeavesEverythingUntouched) {
    project::Document doc(project::newProject(meta("Scenario B")));
    ASSERT_TRUE(doc.execute(project::commands::addItem(templateItem(&furniture::templates::baseCabinet, "cab1"))).ok());
    const std::string before = modelText(resolved(doc.project()).model);
    const std::size_t steps = doc.undoCount();
    // 30 - 36 = -6 mm inner width: refused by the std.inner_width constraint.
    auto r = doc.execute(project::commands::setParameter("cab1", "width", 30));
    ASSERT_FALSE(r.ok());
    EXPECT_TRUE(contains(r.error().message, "inner_width"));
    EXPECT_EQ(modelText(resolved(doc.project()).model), before);
    EXPECT_EQ(doc.undoCount(), steps);
}

} // namespace
} // namespace fos::scenario
