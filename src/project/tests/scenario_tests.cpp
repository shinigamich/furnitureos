// SPDX-License-Identifier: AGPL-3.0-only
// Scenario B: a base cabinet is widened 800 -> 900 through an undoable command; every derived
// output (parts, cut list, cutting plan, drawing) follows; undo restores the exact model.
#include "test_support.hpp"

#include <algorithm>

namespace fos::project::test {
namespace {

// Hand computation for the base-cabinet template (furniture/templates.hpp: 600 x 820 x 560,
// plinth 100 with sides to the floor, stretcher rails, overlay back, 1 shelf, pair of doors)
// with the generic 18 mm board, 3 mm HDF back, ABS 2 mm front edges, ABS 0.4 mm end edges.
// Standard parameters (furniture/parameters.hpp): thickness T = 18, shelf_clearance 1,
// shelf_setback 20, back_inset 2, front_reveal 2, front_gap 3, rail_width 100.
//   inner_width  = W - 2T
//   bottom/rails/plinth length = inner_width;  shelf length = inner_width - 2 * 1
//   shelf width  = depth - setback = 560 - 20 = 540
//   back         = (W - 2*2) x (carcass_height - 2*2) = (W - 4) x (720 - 4)
//   sides        = height x depth = 820 x 560 (sides run to the floor)
//   cut sizes    = finished - edge band thickness (ADR-0004)
struct Expect {
    double inner, shelf, back, door;
};
Expect expectFor(double W) {
    const double inner = W - 2 * 18;            // 800 -> 764, 900 -> 864
    return {inner, inner - 2 * 1, W - 4, (W - 2 * 2 - 3) / 2}; // door: (W - 2 reveals - gap) / 2
}

const production::CutListRow* rowWith(const production::CutList& list, const std::string& partId) {
    for (const auto& row : list.rows)
        for (const auto& id : row.partIds)
            if (id.str() == partId) return &row;
    return nullptr;
}

const drawings::Dimension* overallWidth(const drawings::DrawingResult& d, const std::string& itemId) {
    for (const auto& v : d.views)
        for (const auto& dim : v.annotations.dimensions)
            if (dim.id == itemId + "/view.front/dim.overall.width") return &dim;
    return nullptr;
}

void checkWidth(const Project& p, double W) {
    const Expect e = expectFor(W);
    const Resolved r = resolved(p);
    ASSERT_EQ(r.model.assemblies.size(), 1u);
    EXPECT_FALSE(core::hasErrors(r.diagnostics));
    for (const char* slot : {"bottom", "rail.front", "rail.back", "plinth"}) {
        const auto* part = findPart(r, std::string("cab1/") + slot);
        ASSERT_NE(part, nullptr) << slot;
        EXPECT_DOUBLE_EQ(part->length, e.inner) << slot;
    }
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/shelf.1")->length, e.shelf);
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/shelf.1")->width, 540.0);
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/back")->length, e.back);
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/back")->width, 716.0);
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/door.left")->width, e.door);
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/side.left")->length, 820.0); // independent of the width
    EXPECT_DOUBLE_EQ(findPart(r, "cab1/side.left")->width, 560.0);

    // Cut list: bottom cut = inner x (560 - 2); shelf cut = (inner - 2) x (540 - 2).
    const auto reports = productionReports(r, productionOptions(p));
    const auto* bottom = rowWith(reports.cutList, "cab1/bottom");
    ASSERT_NE(bottom, nullptr);
    EXPECT_DOUBLE_EQ(bottom->cutLength, e.inner);
    EXPECT_DOUBLE_EQ(bottom->cutWidth, 558.0);
    const auto* shelf = rowWith(reports.cutList, "cab1/shelf.1");
    ASSERT_NE(shelf, nullptr);
    EXPECT_DOUBLE_EQ(shelf->cutLength, e.shelf);
    EXPECT_DOUBLE_EQ(shelf->cutWidth, 538.0);

    // Cutting plan: 2 jobs (pb18: 2 sides, bottom, 2 rails, plinth, shelf, 2 doors = 9 parts;
    // hdf3: back = 1), every piece placed, the independent layout validation is clean.
    const CuttingPlans plans = cuttingPlans(r, p.cutting);
    ASSERT_EQ(plans.plans.size(), 2u);
    EXPECT_FALSE(core::hasErrors(plans.diagnostics));
    int pb18 = 0;
    for (const auto& piece : plans.plans[0].job.pieces) pb18 += piece.quantity;
    EXPECT_EQ(plans.plans[0].job.id, defaults::kParticleboard18);
    EXPECT_EQ(pb18, 9);
    for (const auto& plan : plans.plans) {
        ASSERT_TRUE(plan.result.has_value());
        EXPECT_TRUE(plan.result->unplaced.empty());
        EXPECT_FALSE(core::hasErrors(plan.validation));
        EXPECT_EQ(plan.result->totals.placedCount, plan.job.id == defaults::kHdf3 ? 1 : 9);
    }

    // Drawing: overall width of the front view = W (sides span x = 0 .. W).
    auto drawing = assemblyDrawing(p, r, "cab1");
    ASSERT_TRUE(drawing.ok()) << drawing.error().message;
    const auto* dim = overallWidth(drawing.value(), "cab1");
    ASSERT_NE(dim, nullptr);
    EXPECT_NEAR(drawings::measuredValue(*dim), W, 1e-9);
}

} // namespace

TEST(ScenarioB, WidthChangeUndoRedo) {
    Project p = newProject(meta());
    Document doc(p);
    ASSERT_TRUE(doc.execute(commands::addItem(baseCabinetItem())).ok());
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "width", 800)).ok());
    checkWidth(doc.project(), 800);
    const std::string model800 = modelText(resolved(doc.project()).model);

    std::vector<ChangeNotice> notices;
    doc.addObserver([&](const ChangeNotice& n) { notices.push_back(n); });
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "width", 900)).ok());
    ASSERT_EQ(notices.size(), 1u);
    EXPECT_EQ(notices[0].affected.items, std::vector<std::string>{"cab1"});
    EXPECT_EQ(notices[0].label.english(), "Set width of cab1 to 900");
    checkWidth(doc.project(), 900);
    const std::string model900 = modelText(resolved(doc.project()).model);
    EXPECT_NE(model800, model900);

    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(modelText(resolved(doc.project()).model), model800); // whole model exactly as before
    checkWidth(doc.project(), 800);
    ASSERT_TRUE(doc.redo().ok());
    EXPECT_EQ(modelText(resolved(doc.project()).model), model900);
    EXPECT_EQ(notices.size(), 3u);
    EXPECT_EQ(notices[1].kind, ChangeKind::Undo);
    EXPECT_EQ(notices[2].kind, ChangeKind::Redo);
}

TEST(ScenarioB, StoredDrawingEditIsApplied) {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem());
    Document doc(p);
    drawings::ElementEdit edit;
    edit.textOverride = "approx. 600";
    edit.offset = -30.0;
    ASSERT_TRUE(doc.execute(commands::setElementEdit(assemblyDrawingId("cab1"), "cab1/view.front/dim.overall.width", edit)).ok());
    const Resolved r = resolved(doc.project());
    auto drawing = assemblyDrawing(doc.project(), r, "cab1");
    ASSERT_TRUE(drawing.ok());
    const auto* dim = overallWidth(drawing.value(), "cab1");
    ASSERT_NE(dim, nullptr);
    EXPECT_EQ(dim->textOverride, std::optional<std::string>("approx. 600"));
    EXPECT_DOUBLE_EQ(dim->offset, -30.0);
    EXPECT_NE(std::find(drawing.value().edits.appliedIds.begin(), drawing.value().edits.appliedIds.end(),
                        "cab1/view.front/dim.overall.width"),
              drawing.value().edits.appliedIds.end());
}

} // namespace fos::project::test
