// SPDX-License-Identifier: AGPL-3.0-only
// Cutting settings editor: objective and remnant list per material through
// commands::setCuttingSettings; refusals leave the project unchanged; undo / redo.
#include "fos/app/cutting_rows.hpp"
#include "test_support.hpp"

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

} // namespace
} // namespace fos::app::test
