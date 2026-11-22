// SPDX-License-Identifier: AGPL-3.0-only
// Project nesting settings (.fosp cutting/nesting) and cutting plans that cover shaped parts.
#include "test_support.hpp"

#include "fos/furniture/edit.hpp"
#include "fos/project/codec.hpp"

namespace fos::project::test {
namespace {

const char* kSavedAt = "2026-11-12T12:00:00Z";

Project shelfWithRoundedFront() {
    Project p = newProject(meta());
    Item cab = baseCabinetItem();
    furniture::ContourSpec s;
    s.kind = furniture::ContourKind::RoundedCorners;
    s.radii = {"30", "30", "", ""};
    EXPECT_TRUE(furniture::setPartContour(*cab.cabinet(), p.libraries, "shelf.1", s).ok());
    p.items.push_back(cab);
    return p;
}

} // namespace

TEST(ProjectNesting, AbsentKeyIsNotWrittenAndSetValueRoundTrips) {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem());
    auto plain = serializeProject(p, kSavedAt);
    ASSERT_TRUE(plain.ok());
    EXPECT_EQ(plain.value().find("\"nesting\""), std::string::npos);
    // Explicit defaults are kept (lossless) and written in full.
    p.cutting.nesting = NestingSettings{};
    auto withDefaults = serializeProject(p, kSavedAt);
    ASSERT_TRUE(withDefaults.ok());
    EXPECT_NE(withDefaults.value().find("\"chordTolerance\""), std::string::npos);
    auto back = parseProject(withDefaults.value());
    ASSERT_TRUE(back.ok()) << back.error().message;
    ASSERT_TRUE(back.value().project.cutting.nesting.has_value());
    // Non-default values, seed above INT_MAX.
    NestingSettings n;
    n.toolDiameter = 6;
    n.clearance = 1.5;
    n.chordTolerance = 0.05;
    n.margins = {12, 13, 14, 15};
    n.minRemnantLength = 250;
    n.iterations = 3;
    n.seed = 4000000000u;
    p.cutting.nesting = n;
    auto text = serializeProject(p, kSavedAt);
    ASSERT_TRUE(text.ok());
    auto loaded = parseProject(text.value());
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    ASSERT_TRUE(loaded.value().project.cutting.nesting.has_value());
    EXPECT_TRUE(*loaded.value().project.cutting.nesting == n);
    EXPECT_EQ(serializeProject(loaded.value().project, kSavedAt).value(), text.value());
}

TEST(ProjectNesting, InvalidValuesAreRejectedOnLoadAndByTheCommand) {
    Project p = newProject(meta());
    formats::Json j = projectToJson(p);
    j["cutting"]["nesting"] = toJson(NestingSettings{});
    ASSERT_TRUE(projectFromJson(j, "/payload").ok());
    auto expectError = [&](const char* key, formats::Json value, const std::string& needle) {
        formats::Json bad = j;
        bad["cutting"]["nesting"][key] = value;
        auto r = projectFromJson(bad, "/payload");
        ASSERT_FALSE(r.ok()) << key;
        EXPECT_NE(r.error().message.find(needle), std::string::npos) << r.error().message;
    };
    expectError("toolDiameter", 0, "/payload/cutting/nesting/toolDiameter");
    expectError("chordTolerance", 0.001, "'chord tolerance' must be a finite number >= 0.01");
    expectError("iterations", 1001, "/payload/cutting/nesting/iterations");
    expectError("seed", -1, "expected an integer from 0 to 4294967295");
    expectError("colour", "red", "colour");

    Document doc(p);
    NestingSettings bad;
    bad.clearance = -1;
    auto r = doc.execute(commands::setNestingSettings(bad));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().message, "Nesting setting 'clearance' must be a finite number >= 0 and <= 100 mm, got -1.");
    EXPECT_EQ(doc.undoCount(), 0u);
    NestingSettings good;
    good.toolDiameter = 6;
    ASSERT_TRUE(doc.execute(commands::setNestingSettings(good)).ok());
    EXPECT_DOUBLE_EQ(doc.project().cutting.nestingSettings().toolDiameter, 6);
    ASSERT_TRUE(doc.execute(commands::setNestingSettings(std::nullopt)).ok());
    EXPECT_FALSE(doc.project().cutting.nesting.has_value());
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_DOUBLE_EQ(doc.project().cutting.nesting->toolDiameter, 6);
}

TEST(ProjectNesting, CuttingPlansCoverShapedParts) {
    Project p = shelfWithRoundedFront();
    NestingSettings n;
    n.toolDiameter = 6;
    n.iterations = 2;
    p.cutting.nesting = n;
    const Resolved r = resolved(p);
    const CuttingPlans plans = cuttingPlans(r, p.cutting);
    // Particleboard: 8 rectangular particleboard parts by saw, the shelf nested; HDF back by saw.
    const CuttingPlan* saw = plans.guillotineFor(defaults::kParticleboard18);
    const NestingPlan* nest = plans.nestingFor(defaults::kParticleboard18);
    ASSERT_NE(saw, nullptr);
    ASSERT_NE(nest, nullptr);
    EXPECT_EQ(plans.nestingFor(defaults::kHdf3), nullptr);
    ASSERT_EQ(plans.routedParts.size(), 1u);
    EXPECT_EQ(plans.routedParts[0].partId, "cab1/shelf.1");
    for (const auto& piece : saw->job.pieces)
        for (const auto& id : piece.sourceIds) EXPECT_NE(id, "cab1/shelf.1");
    ASSERT_EQ(nest->job.job.parts.size(), 1u);
    EXPECT_EQ(nest->job.job.parts[0].id, "cab1/shelf.1");
    // partSpacing = 6 + 0 + 2 * 0.1 = 6.2; edgeSpacing = 6 + 0.1 = 6.1.
    EXPECT_DOUBLE_EQ(nest->job.job.partSpacing, 6.2);
    EXPECT_DOUBLE_EQ(nest->job.job.edgeSpacing, 6.1);
    ASSERT_TRUE(nest->result.has_value());
    EXPECT_EQ(nest->result->placedCount, 1);
    EXPECT_TRUE(nest->result->isHeuristic);
    EXPECT_TRUE(nest->validation.empty());
    EXPECT_TRUE(saw->validation.empty());
    EXPECT_FALSE(core::hasErrors(plans.diagnostics));
    // Index in model material order: pb18 has both kinds.
    ASSERT_FALSE(plans.materials.empty());
    bool foundBoth = false;
    for (const auto& m : plans.materials)
        if (m.materialId == defaults::kParticleboard18) foundBoth = m.guillotine.has_value() && m.nesting.has_value();
    EXPECT_TRUE(foundBoth);
}

TEST(ProjectNesting, RectangularProjectsHaveNoNestingPlans) {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem());
    const CuttingPlans plans = cuttingPlans(resolved(p), p.cutting);
    EXPECT_TRUE(plans.nestingPlans.empty());
    EXPECT_TRUE(plans.routedParts.empty());
    EXPECT_TRUE(plans.diagnostics.empty());
    EXPECT_EQ(plans.materials.size(), plans.plans.size());
}

TEST(ProjectQuote, SheetUsageSkipsOwnedRemnantsAndWarnsOnFailedPlans) {
    CuttingPlans plans;
    // Material A: stock 0 = full board, stock 1 = listed remnant. Three sheets: two full boards
    // (cut length 1000 + 2000) and one remnant (cut length 500) -> 2 purchased sheets, 3500 mm.
    CuttingPlan a;
    a.job.id = "mat.a";
    a.job.stock = {cutting::StockSheet{}, cutting::StockSheet{}};
    a.job.stock[1].isRemnant = true;
    a.result = cutting::CuttingResult{};
    for (const auto& [index, cut] : std::vector<std::pair<int, double>>{{0, 1000}, {1, 500}, {0, 2000}}) {
        cutting::SheetLayout sheet;
        sheet.stockIndex = index;
        sheet.cutLength = cut;
        a.result->sheets.push_back(sheet);
    }
    plans.plans.push_back(a);
    // Material B: guillotine plan failed, but its nesting plan worked -> left out entirely.
    CuttingPlan b;
    b.job.id = "mat.b";
    plans.plans.push_back(b);
    NestingPlan bn;
    bn.job.materialId = "mat.b";
    bn.result = nesting::NestResult{};
    bn.result->sheets.resize(2);
    plans.nestingPlans.push_back(bn);
    // Material A also has a nesting plan with 1 sheet -> 2 + 1 = 3 sheets.
    NestingPlan an;
    an.job.materialId = "mat.a";
    an.result = nesting::NestResult{};
    an.result->sheets.resize(1);
    plans.nestingPlans.push_back(an);

    core::Diagnostics d;
    const auto usage = quoteSheetUsage(plans, d);
    ASSERT_EQ(usage.size(), 1u);
    const auto& ua = usage.at(domain::MaterialId("mat.a"));
    EXPECT_EQ(ua.sheets, 3);
    EXPECT_DOUBLE_EQ(ua.cutLengthMm, 3500.0);
    ASSERT_EQ(d.size(), 1u);
    EXPECT_EQ(d[0].code, "project.quote.plan_failed");
    EXPECT_EQ(d[0].severity, core::Severity::Warning);
    EXPECT_EQ(d[0].entityId, "mat.b");
}

} // namespace fos::project::test
