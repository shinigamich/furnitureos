// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/batch.hpp"

#include "test_support.hpp"

#include <algorithm>

namespace fos::project::test {
namespace {

Project kitchen(const std::string& name) {
    Metadata m = meta();
    m.name = name;
    Project p = newProject(m);
    p.items.push_back(baseCabinetItem("cab1"));
    return p;
}

int partCount(const domain::Model& m) {
    int n = 0;
    for (const auto& a : m.assemblies) n += static_cast<int>(a.parts.size());
    return n;
}

bool hasCode(const core::Diagnostics& ds, const std::string& code) {
    return std::any_of(ds.begin(), ds.end(), [&](const core::Diagnostic& d) { return d.code == code; });
}

struct BatchFixture : ::testing::Test {
    TempDir dir;
    void SetUp() override {
        ASSERT_TRUE(saveProject(dir.path / "a.fosp", kitchen("Kitchen A")).ok());
        ASSERT_TRUE(saveProject(dir.path / "b.fosp", kitchen("Office B")).ok());
    }
    Batch twoProjects() const {
        Batch b;
        b.name = "Week 47";
        b.date = "2026-11-22";
        b.entries = {{"a.fosp", 1, "", "order 1001"}, {"b.fosp", 2, "", ""}};
        return b;
    }
};

TEST_F(BatchFixture, MergesNamespacedCopiesAndMultipliesQuantities) {
    const Resolved single = resolved(kitchen("Kitchen A"));
    const int p = partCount(single.model);
    ASSERT_GT(p, 0);
    const BatchModel bm = mergeBatch(loadBatchProjects(twoProjects(), dir.path));
    EXPECT_FALSE(core::hasErrors(bm.diagnostics));
    ASSERT_EQ(bm.projects.size(), 2u);
    EXPECT_TRUE(bm.projects[0].included);
    EXPECT_EQ(bm.projects[1].instanceKeys, (std::vector<std::string>{"b#1", "b#2"}));
    // 1 x A + 2 x B, both the same cabinet: 3 * p parts, 3 assemblies.
    EXPECT_EQ(partCount(bm.model), 3 * p);
    ASSERT_EQ(bm.model.assemblies.size(), 3u);
    EXPECT_EQ(bm.model.assemblies[0].id.str(), "a/cab1");
    EXPECT_EQ(bm.model.assemblies[2].id.str(), "b#2/cab1");
    const std::string firstPart = single.model.assemblies[0].parts[0].id.str();
    for (const char* inst : {"a/", "b#1/", "b#2/"}) {
        bool found = false;
        for (const auto& a : bm.model.assemblies)
            if (a.findPart(domain::PartId(inst + firstPart)) != nullptr) found = true;
        EXPECT_TRUE(found) << inst << firstPart;
    }
    for (const auto& a : bm.model.assemblies)
        for (const auto& h : a.hardware)
            for (const auto& ref : h.parts) EXPECT_EQ(ref.str().substr(0, 2), a.id.str().substr(0, 2));
    EXPECT_EQ(bm.projectOf("b#2/" + firstPart), &bm.projects[1]);
    EXPECT_EQ(bm.projectOf("zzz/x"), nullptr);

    // Cut list: every row of the single project appears with 3 x its quantity (identical cabinets).
    const auto one = production::cutList(single.model);
    const auto batchList = batchCutList(bm, twoProjects());
    ASSERT_EQ(batchList.rows.size(), one.rows.size());
    int total = 0;
    for (std::size_t i = 0; i < one.rows.size(); ++i) {
        EXPECT_EQ(batchList.rows[i].quantity, 3 * one.rows[i].quantity) << i;
        total += batchList.rows[i].quantity;
    }
    EXPECT_EQ(total, 3 * p);

    // Summary: rows p and 2p parts, total 3p; hardware totals add up the same way.
    const BatchSummary s = batchSummary(bm, twoProjects());
    ASSERT_EQ(s.projects.size(), 2u);
    EXPECT_EQ(s.projects[0].partCount, p);
    EXPECT_EQ(s.projects[1].partCount, 2 * p);
    EXPECT_EQ(s.total.partCount, 3 * p);
    EXPECT_EQ(s.total.quantity, 3);
    EXPECT_EQ(s.total.hardwareQuantity, s.projects[0].hardwareQuantity + s.projects[1].hardwareQuantity);
    EXPECT_NEAR(s.total.edgeBandingAppliedM, 3 * s.projects[0].edgeBandingAppliedM, 1e-9);
    EXPECT_LE(s.total.sheetsLowerBound, s.projects[0].sheetsLowerBound + s.projects[1].sheetsLowerBound);
    EXPECT_EQ(batchSummaryTable(s).rows.size(), 3u);
}

TEST_F(BatchFixture, CuttingPlansAreValidAndLabelsNameTheOrder) {
    const Batch b = twoProjects();
    const BatchModel bm = mergeBatch(loadBatchProjects(b, dir.path));
    const CuttingPlans plans = batchCuttingPlans(bm, b);
    EXPECT_FALSE(core::hasErrors(plans.diagnostics));
    ASSERT_FALSE(plans.plans.empty());
    int placed = 0;
    for (const auto& plan : plans.plans) {
        ASSERT_TRUE(plan.result.has_value());
        EXPECT_FALSE(core::hasErrors(plan.validation));
        EXPECT_EQ(plan.result->totals.unplacedCount, 0);
        placed += plan.result->totals.placedCount;
    }
    EXPECT_EQ(placed + static_cast<int>(plans.routedParts.size()), partCount(bm.model));

    const auto set = batchLabels(bm, b);
    ASSERT_EQ(static_cast<int>(set.labels.size()), partCount(bm.model));
    for (const auto& l : set.labels) {
        const std::string inst = l.partId.str().substr(0, l.partId.str().find('/'));
        EXPECT_NE(l.projectName.find("order " + inst), std::string::npos) << l.projectName;
        EXPECT_EQ(l.projectName.rfind("Week 47 | ", 0), 0u);
    }
    EXPECT_NE(set.labels.back().projectName.find("Office B"), std::string::npos);
}

TEST_F(BatchFixture, MissingProjectIsReportedWithoutAbortingTheOthers) {
    Batch b = twoProjects();
    b.entries[1].path = "missing.fosp";
    const LoadedBatch lb = loadBatchProjects(b, dir.path);
    EXPECT_TRUE(lb.projects[0].loaded.has_value());
    EXPECT_FALSE(lb.projects[1].loaded.has_value());
    const BatchModel bm = mergeBatch(lb);
    ASSERT_TRUE(hasCode(bm.diagnostics, "batch.project.load_failed"));
    EXPECT_TRUE(bm.projects[0].included);
    EXPECT_FALSE(bm.projects[1].included);
    EXPECT_EQ(bm.model.assemblies.size(), 1u);
    EXPECT_EQ(partCount(bm.model), partCount(resolved(kitchen("x")).model));
}

TEST_F(BatchFixture, MaterialConflictNamesBothProjectsAndExcludesTheLaterOne) {
    Project c = kitchen("Bathroom C");
    for (auto& m : c.libraries.materials)
        if (m.id.str() == defaults::kParticleboard18) m.costPerSheet = 42.0; // same id, different data
    ASSERT_TRUE(saveProject(dir.path / "c.fosp", c).ok());
    Batch b = twoProjects();
    b.entries[1].path = "c.fosp";
    const BatchModel bm = mergeBatch(loadBatchProjects(b, dir.path));
    const auto it = std::find_if(bm.diagnostics.begin(), bm.diagnostics.end(),
                                 [](const core::Diagnostic& d) { return d.code == "batch.library.conflict"; });
    ASSERT_NE(it, bm.diagnostics.end());
    EXPECT_EQ(it->severity, core::Severity::Error);
    EXPECT_NE(it->message.find("Kitchen A"), std::string::npos);
    EXPECT_NE(it->message.find("Bathroom C"), std::string::npos);
    EXPECT_NE(it->message.find(defaults::kParticleboard18), std::string::npos);
    EXPECT_TRUE(bm.projects[0].included);
    EXPECT_FALSE(bm.projects[1].included);
    EXPECT_EQ(bm.model.assemblies.size(), 1u);
}

TEST_F(BatchFixture, MaterialCostPerM2DifferenceIsAConflict) {
    // Only costPerSquareMeter differs (written as "costPerM2" by the formats material codec that
    // the merge compares through), so the libraries are NOT identical and the later project is out.
    Project c = kitchen("Bathroom C");
    bool changed = false;
    for (auto& m : c.libraries.materials)
        if (m.id.str() == defaults::kParticleboard18) {
            m.costPerSquareMeter += 7.5;
            changed = true;
        }
    ASSERT_TRUE(changed);
    ASSERT_TRUE(saveProject(dir.path / "c.fosp", c).ok());
    Batch b = twoProjects();
    b.entries[1].path = "c.fosp";
    const BatchModel bm = mergeBatch(loadBatchProjects(b, dir.path));
    const auto it = std::find_if(bm.diagnostics.begin(), bm.diagnostics.end(),
                                 [](const core::Diagnostic& d) { return d.code == "batch.library.conflict"; });
    ASSERT_NE(it, bm.diagnostics.end());
    EXPECT_NE(it->message.find(defaults::kParticleboard18), std::string::npos) << it->message;
    EXPECT_TRUE(bm.projects[0].included);
    EXPECT_FALSE(bm.projects[1].included);
}

TEST_F(BatchFixture, SubstitutionIsAppliedVisiblyAndRefusedOnThicknessMismatch) {
    Batch b = twoProjects();
    b.materialSubstitutions[defaults::kParticleboard18] = defaults::kMdf18; // both 18 mm
    BatchModel bm = mergeBatch(loadBatchProjects(b, dir.path));
    EXPECT_FALSE(core::hasErrors(bm.diagnostics));
    EXPECT_TRUE(hasCode(bm.diagnostics, "batch.substitution.applied"));
    for (const auto& a : bm.model.assemblies)
        for (const auto& p : a.parts) EXPECT_NE(p.material.str(), defaults::kParticleboard18);

    b.materialSubstitutions = {{defaults::kParticleboard18, defaults::kHdf3}}; // 18 mm vs 3 mm
    bm = mergeBatch(loadBatchProjects(b, dir.path));
    EXPECT_TRUE(hasCode(bm.diagnostics, "batch.substitution.thickness_mismatch"));
    EXPECT_FALSE(hasCode(bm.diagnostics, "batch.substitution.applied"));
}

TEST_F(BatchFixture, DocumentRoundTripAndChecks) {
    Batch b = twoProjects();
    b.cutting.defaults.kerf = 3.2;
    b.cutting.nesting = NestingSettings{};
    b.materialSubstitutions["mat.x"] = "mat.y";
    const auto path = dir.path / "week.fosbatch";
    ASSERT_TRUE(saveBatch(path, b).ok());
    auto back = loadBatchDocument(path);
    ASSERT_TRUE(back.ok()) << back.error().message;
    EXPECT_EQ(batchToJson(back.value()).dump(), batchToJson(b).dump());
    auto lb = loadBatch(path);
    ASSERT_TRUE(lb.ok());
    EXPECT_EQ(lb.value().projects.size(), 2u);

    Batch dup = b;
    dup.entries.push_back({"sub/a.fosp", 1, "", ""}); // same derived key "a"
    auto r = checkBatch(dup);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("\"a\""), std::string::npos);
    dup.entries.back().key = "a2";
    EXPECT_TRUE(checkBatch(dup).ok());
    dup.entries.back().quantity = 0;
    EXPECT_FALSE(checkBatch(dup).ok());

    formats::Json j = batchToJson(b);
    j["projects"][1]["quantity"] = kMaxBatchQuantity + 1;
    auto bad = batchFromJson(j, "/payload");
    ASSERT_FALSE(bad.ok());
    EXPECT_NE(bad.error().message.find("/payload/projects/1/quantity"), std::string::npos);
    EXPECT_EQ(batchKey({"dir/Küche 1.fosp", 1, "", ""}), "K__che_1"); // 'ü' is two UTF-8 bytes
}

} // namespace
} // namespace fos::project::test
