// SPDX-License-Identifier: AGPL-3.0-only
// Cutting settings editor: objective and remnant list per material through
// commands::setCuttingSettings; refusals leave the project unchanged; undo / redo.
#include "fos/app/contour_rows.hpp"
#include "fos/app/cutting_rows.hpp"
#include "fos/app/nesting_rows.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fos::app::test {
namespace {

const std::string kPb = project::defaults::kParticleboard18;

const project::CuttingPlan* planOf(Session& s, const std::string& materialId) {
    for (const auto& p : s.cuttingPlans().plans)
        if (p.job.id == materialId)
            return &p;
    return nullptr;
}

TEST(CuttingSettingsEditor, ObjectiveAndRemnantsPerMaterial) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    EXPECT_FALSE(hasOwnCuttingSettings(s.project(), kPb));
    CuttingSettingsInput in = cuttingSettingsInput(s.project(), kPb); // the defaults
    EXPECT_DOUBLE_EQ(in.kerf, 4.0);
    EXPECT_EQ(in.objective, cutting::Objective::MinimizeSheets);
    EXPECT_TRUE(in.remnants.empty());
    EXPECT_EQ(nextRemnantId(in.remnants), "R1");

    in.objective = cutting::Objective::PreferRemnants;
    in.remnants.push_back({nextRemnantId(in.remnants), "Offcut shelf", 1200, 600, 2});
    EXPECT_EQ(nextRemnantId(in.remnants), "R2");
    ASSERT_TRUE(s.execute(cuttingSettingsCommand(s.project(), kPb, in)).ok());
    EXPECT_TRUE(hasOwnCuttingSettings(s.project(), kPb));
    const auto& own = s.project().cutting.forMaterial(kPb);
    EXPECT_EQ(own.objective, cutting::Objective::PreferRemnants);
    ASSERT_EQ(own.remnants.size(), 1u);
    EXPECT_EQ(own.remnants[0].id, "R1");
    EXPECT_DOUBLE_EQ(own.minRemnantLength, 300); // not edited here: kept
    // The defaults are untouched; the job of pb18 now has the remnant as extra stock.
    EXPECT_EQ(s.project().cutting.defaults.objective, cutting::Objective::MinimizeSheets);
    const auto* plan = planOf(s, kPb);
    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(plan->job.objective, cutting::Objective::PreferRemnants);
    bool remnantStock = false;
    for (const auto& sheet : plan->job.stock)
        remnantStock = remnantStock || sheet.id == kPb + "/remnant/R1";
    EXPECT_TRUE(remnantStock);
    EXPECT_EQ(cuttingSettingsInput(s.project(), kPb).remnants.at(0).label, "Offcut shelf");

    // Refused: a remnant with zero width, a quantity of 0, a negative kerf. Nothing changes.
    const auto revision = s.revision();
    for (auto bad : {project::Remnant{"R2", "", 500, 0, 1}, project::Remnant{"R2", "", 500, 300, 0}}) {
        CuttingSettingsInput b = cuttingSettingsInput(s.project(), kPb);
        b.remnants.push_back(bad);
        auto r = s.execute(cuttingSettingsCommand(s.project(), kPb, b));
        ASSERT_FALSE(r.ok());
        EXPECT_NE(r.error().message.find("R2"), std::string::npos) << r.error().message;
    }
    CuttingSettingsInput negative = cuttingSettingsInput(s.project(), kPb);
    negative.kerf = -1;
    EXPECT_FALSE(s.execute(cuttingSettingsCommand(s.project(), kPb, negative)).ok());
    EXPECT_EQ(s.revision(), revision);
    EXPECT_EQ(s.project().cutting.forMaterial(kPb).remnants.size(), 1u);

    // Back to the project defaults (the own entry is removed); undo restores it, redo removes it.
    ASSERT_TRUE(s.execute(useDefaultCuttingSettingsCommand(kPb)).ok());
    EXPECT_FALSE(hasOwnCuttingSettings(s.project(), kPb));
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(s.project().cutting.forMaterial(kPb).remnants.size(), 1u);
    ASSERT_TRUE(s.redo().ok());
    EXPECT_FALSE(hasOwnCuttingSettings(s.project(), kPb));
    ASSERT_TRUE(s.undo().ok());
    ASSERT_TRUE(s.undo().ok());
    EXPECT_FALSE(hasOwnCuttingSettings(s.project(), kPb));
    EXPECT_EQ(planOf(s, kPb)->job.objective, cutting::Objective::MinimizeSheets);
}

TEST(CuttingSettingsEditor, DefaultsObjective) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    CuttingSettingsInput in = cuttingSettingsInput(s.project(), std::nullopt);
    in.objective = cutting::Objective::MinimizeCuts;
    in.kerf = 3;
    ASSERT_TRUE(s.execute(cuttingSettingsCommand(s.project(), std::nullopt, in)).ok());
    for (const auto& p : s.cuttingPlans().plans) {
        EXPECT_EQ(p.job.objective, cutting::Objective::MinimizeCuts);
        EXPECT_DOUBLE_EQ(p.job.kerf, 3);
    }
}

/// Open shelving with R 30 on the two front corners of shelf.1.1 (762 x W, particleboard 18):
/// the shelf leaves the saw job and is nested on the router.
void roundShelf(Session& s) {
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::OpenShelving).ok());
    const auto target = contourTarget(s.project(), "cab1", "cab1/shelf.1.1");
    ASSERT_TRUE(target.has_value());
    ContourInput in;
    in.kind = furniture::ContourKind::RoundedCorners;
    in.radii = {QStringLiteral("30"), QStringLiteral("30"), {}, {}};
    ASSERT_TRUE(applyContourInput(s, *target, in).ok());
}

TEST(NestingRows, SummaryOfShapedShelf) {
    Session s;
    roundShelf(s);
    const auto& plans = s.cuttingPlans();
    const auto rows = nestingSummary(plans);
    ASSERT_EQ(rows.size(), 1u);
    const auto& r = rows[0];
    EXPECT_EQ(r.materialId, kPb);
    EXPECT_DOUBLE_EQ(r.thickness, 18);
    EXPECT_EQ(r.parts, 1); // one shaped shelf
    EXPECT_TRUE(r.nested);
    EXPECT_TRUE(r.heuristic);
    EXPECT_EQ(r.sheets, 1); // a 762 mm shelf fits one sheet
    EXPECT_EQ(r.unplaced, 0);
    EXPECT_EQ(r.problems, 0); // validateNest found nothing
    EXPECT_GE(r.iterationsRun, 0);
    EXPECT_LE(r.iterationsRun, 20); // default iterations
    // Utilisation = outline area / sheet area. The nested outline is the RAW outline before edge
    // banding (domain::cutContour: banded segments offset inwards, arcs keep their centre), so
    // its exact area is taken from the domain; the nesting tessellates the two R 30 corner arcs
    // inscribed with chord tolerance 0.1 (10 chords per quarter, loss 450 (pi/2 - 10 sin(pi/20))
    // = 2.9 mm2 each), so the reported area is at most 10 mm2 below the exact one.
    const domain::Part* shelf = s.findPart("cab1/shelf.1.1");
    ASSERT_NE(shelf, nullptr);
    const auto& job = plans.nestingFor(kPb)->job.job;
    ASSERT_FALSE(job.sheets.empty());
    const double sheetArea = job.sheets[0].length * job.sheets[0].width;
    const auto raw = domain::cutContour(*shelf, s.resolved()->model);
    ASSERT_TRUE(raw.ok());
    const double outline = std::abs(geometry::signedArea(raw.value()));
    // Sanity: below the finished 762 x W rounded outline (banding taken off), not by more than 3 %.
    const double finished = 762.0 * shelf->width - 2 * (900 - 225 * std::numbers::pi);
    EXPECT_LT(outline, finished);
    EXPECT_GT(outline, 0.97 * finished);
    EXPECT_LE(r.utilisationPct, 100.0 * outline / sheetArea + 1e-9);
    EXPECT_GE(r.utilisationPct, 100.0 * (outline - 10.0) / sheetArea);

    // Table rows: particleboard has both plans (guillotine first); no other material is nested.
    const auto table = cuttingTableRows(plans);
    std::vector<PlanKind> pbKinds;
    for (const auto& t : table)
        if (t.materialId == kPb) pbKinds.push_back(t.kind);
    EXPECT_EQ(pbKinds, (std::vector<PlanKind>{PlanKind::Guillotine, PlanKind::Nesting}));
    EXPECT_EQ(std::count_if(table.begin(), table.end(), [](const CuttingTableRow& t) { return t.kind == PlanKind::Nesting; }),
              1);
    EXPECT_EQ(table.size(), plans.plans.size() + plans.nestingPlans.size());

    // A rectangular project has no nesting rows.
    Session plain;
    ASSERT_TRUE(plain.newFromTemplate(TemplateKind::OpenShelving).ok());
    EXPECT_TRUE(nestingSummary(plain.cuttingPlans()).empty());
}

TEST(NestingRows, SettingsCommandUndoAndRefusal) {
    Session s;
    roundShelf(s);
    project::NestingSettings in = nestingSettingsInput(s.project());
    EXPECT_DOUBLE_EQ(in.toolDiameter, 8);
    EXPECT_EQ(in.seed, 12345u);
    in.toolDiameter = 6;
    in.iterations = 5;
    in.seed = 4294967295u;
    ASSERT_TRUE(s.execute(nestingSettingsCommand(in)).ok());
    EXPECT_DOUBLE_EQ(s.project().cutting.nestingSettings().toolDiameter, 6);
    EXPECT_EQ(s.project().cutting.nestingSettings().seed, 4294967295u);
    // Part spacing of the job = tool diameter + clearance + 2 * chord tolerance = 6 + 0 + 0.2.
    EXPECT_NEAR(s.cuttingPlans().nestingFor(kPb)->job.job.partSpacing, 6.2, 1e-9);
    EXPECT_LE(nestingSummary(s.cuttingPlans()).at(0).iterationsRun, 5);

    // Refused with the core message; nothing changes.
    const auto revision = s.revision();
    project::NestingSettings bad = nestingSettingsInput(s.project());
    bad.toolDiameter = 0;
    auto r = s.execute(nestingSettingsCommand(bad));
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("tool diameter"), std::string::npos) << r.error().message;
    bad = nestingSettingsInput(s.project());
    bad.iterations = 1001;
    r = s.execute(nestingSettingsCommand(bad));
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("1001"), std::string::npos) << r.error().message;
    EXPECT_EQ(s.revision(), revision);

    ASSERT_TRUE(s.undo().ok());
    EXPECT_DOUBLE_EQ(s.project().cutting.nestingSettings().toolDiameter, 8);
    ASSERT_TRUE(s.redo().ok());
    EXPECT_DOUBLE_EQ(s.project().cutting.nestingSettings().toolDiameter, 6);
}

TEST(NestingRows, SeedParsing) {
    EXPECT_EQ(parseNestingSeed("12345"), 12345u);
    EXPECT_EQ(parseNestingSeed(" 0 "), 0u);
    EXPECT_EQ(parseNestingSeed("4294967295"), 4294967295u);
    EXPECT_FALSE(parseNestingSeed("4294967296").has_value());
    EXPECT_FALSE(parseNestingSeed("-1").has_value());
    EXPECT_FALSE(parseNestingSeed("12a").has_value());
    EXPECT_FALSE(parseNestingSeed("").has_value());
}

} // namespace
} // namespace fos::app::test
