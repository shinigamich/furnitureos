// SPDX-License-Identifier: AGPL-3.0-only
// Production batch window view-model: entry paths as `fos batch add`, edits checked by
// checkBatch, Save As rebasing, the merged summary with a LEFT OUT project, batch cutting
// settings through the project cutting commands, exports.
#include "fos/app/batch_editor.hpp"
#include "fos/app/cutting_rows.hpp"
#include "test_support.hpp"

#include <fstream>

namespace fos::app::test {
namespace {

namespace fs = std::filesystem;

/// Saves a base-cabinet project to `file`; returns its number of parts.
int saveProject(const fs::path& file) {
    Session s;
    EXPECT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    EXPECT_TRUE(s.saveAs(file).ok());
    int parts = 0;
    for (const auto& a : s.resolved()->model.assemblies) parts += static_cast<int>(a.parts.size());
    return parts;
}

TEST(BatchEditor, EntryPathsLikeTheCli) {
    TempDir dir;
    EXPECT_EQ(batchEntryPath(dir.path / "b.fosbatch", dir.path / "orders" / "a.fosp"), "orders/a.fosp");
    EXPECT_EQ(batchEntryPath(dir.path / "sub" / "b.fosbatch", dir.path / "a.fosp"), "../a.fosp");
    // Untitled batch: absolute path.
    EXPECT_TRUE(fs::path(batchEntryPath({}, dir.path / "a.fosp")).is_absolute());
}

TEST(BatchEditor, EditsAreCheckedAndSaveAsRebases) {
    TempDir dir;
    const fs::path a = dir.path / "a.fosp";
    saveProject(a);
    BatchEditor ed;
    ed.newBatch("Week 47", "2026-11-22");
    EXPECT_FALSE(ed.isModified());
    ASSERT_TRUE(ed.addProject(a, 2).ok());
    ASSERT_EQ(ed.batch().entries.size(), 1u);
    EXPECT_TRUE(fs::path(ed.batch().entries[0].path).is_absolute()); // untitled
    // The same project again adds to its quantity (2 + 3 = 5), as `fos batch add`.
    auto again = ed.addProject(a, 3);
    ASSERT_TRUE(again.ok());
    EXPECT_EQ(again.value(), 0u);
    EXPECT_EQ(ed.batch().entries[0].quantity, 5);
    EXPECT_TRUE(ed.isModified());

    // Refused edits leave the batch unchanged.
    EXPECT_FALSE(ed.setQuantity(0, 0).ok());
    EXPECT_FALSE(ed.setQuantity(0, project::kMaxBatchQuantity + 1).ok());
    EXPECT_FALSE(ed.setKey(0, "bad key").ok());
    EXPECT_FALSE(ed.setQuantity(7, 1).ok());
    EXPECT_FALSE(ed.addProject(dir.path / "missing.fosp").ok());
    EXPECT_EQ(ed.batch().entries.size(), 1u);
    EXPECT_EQ(ed.batch().entries[0].quantity, 5);
    EXPECT_FALSE(ed.setMaterialSubstitutions({{"a", "b"}, {"b", "c"}}).ok()); // chain
    EXPECT_TRUE(ed.batch().materialSubstitutions.empty());
    ASSERT_TRUE(ed.setQuantity(0, 1).ok());
    ASSERT_TRUE(ed.setNote(0, "Order 4711").ok());

    EXPECT_FALSE(ed.save().ok()); // untitled
    fs::create_directories(dir.path / "batches");
    ASSERT_TRUE(ed.saveAs(dir.path / "batches" / "week47").ok());
    EXPECT_EQ(ed.filePath().filename(), "week47.fosbatch");
    EXPECT_FALSE(ed.isModified());
    EXPECT_EQ(ed.batch().entries[0].path, "../a.fosp");

    // Moving the batch up one folder keeps the entry pointing at the same file.
    ASSERT_TRUE(ed.saveAs(dir.path / "week47-copy.fosbatch").ok());
    EXPECT_EQ(ed.batch().entries[0].path, "a.fosp");

    BatchEditor reopened;
    ASSERT_TRUE(reopened.open(dir.path / "batches" / "week47.fosbatch").ok());
    EXPECT_EQ(reopened.batch().name, "Week 47");
    ASSERT_EQ(reopened.batch().entries.size(), 1u);
    EXPECT_EQ(reopened.batch().entries[0].path, "../a.fosp");
    EXPECT_EQ(reopened.batch().entries[0].note, "Order 4711");
}

TEST(BatchEditor, SummaryMarksLeftOutProjects) {
    TempDir dir;
    const int parts = saveProject(dir.path / "a.fosp");
    ASSERT_GT(parts, 0);
    BatchEditor ed;
    ed.newBatch("Mixed");
    ASSERT_TRUE(ed.addProject(dir.path / "a.fosp", 3).ok());
    ASSERT_TRUE(ed.saveAs(dir.path / "mixed.fosbatch").ok());
    // A second entry whose file disappears: the batch loads it as LEFT OUT.
    saveProject(dir.path / "gone.fosp");
    ASSERT_TRUE(ed.addProject(dir.path / "gone.fosp").ok());
    fs::remove(dir.path / "gone.fosp");

    const BatchEvaluation e = ed.evaluate();
    EXPECT_TRUE(e.incomplete);
    ASSERT_EQ(e.summary.projects.size(), 2u);
    EXPECT_EQ(e.summary.projects[0].key, "a");
    EXPECT_TRUE(e.summary.projects[0].included);
    EXPECT_EQ(e.summary.projects[0].quantity, 3);
    EXPECT_EQ(e.summary.projects[0].partCount, 3 * parts); // three copies of the cabinet
    EXPECT_EQ(e.summary.projects[1].key, "gone");
    EXPECT_FALSE(e.summary.projects[1].included);
    EXPECT_EQ(e.summary.total.partCount, 3 * parts);

    const auto rows = batchDiagnosticRows(e);
    bool loadFailed = false;
    for (const auto& r : rows)
        if (r.code == "batch.project.load_failed") {
            loadFailed = true;
            EXPECT_EQ(r.severity, core::Severity::Error);
            EXPECT_EQ(r.projectKey, "gone");
        }
    EXPECT_TRUE(loadFailed);
    const auto materials = batchMaterialChoices(e);
    ASSERT_FALSE(materials.empty());
    EXPECT_TRUE(std::is_sorted(materials.begin(), materials.end(),
                               [](const auto& x, const auto& y) { return x.id < y.id; }));
}

TEST(BatchEditor, CuttingSettingsThroughProjectCommands) {
    BatchEditor ed;
    CuttingSettingsInput in = cuttingSettingsInput(ed.settingsProject(), std::nullopt);
    EXPECT_DOUBLE_EQ(in.kerf, 4.0); // CuttingSettings default
    in.kerf = 3.2;
    in.objective = cutting::Objective::MinimizeWaste;
    ASSERT_TRUE(ed.executeSettingsCommand(cuttingSettingsCommand(ed.settingsProject(), std::nullopt, in)).ok());
    EXPECT_DOUBLE_EQ(ed.batch().cutting.defaults.kerf, 3.2);
    EXPECT_EQ(ed.batch().cutting.defaults.objective, cutting::Objective::MinimizeWaste);
    in.kerf = -1; // refused by the command; nothing changes
    EXPECT_FALSE(ed.executeSettingsCommand(cuttingSettingsCommand(ed.settingsProject(), std::nullopt, in)).ok());
    EXPECT_DOUBLE_EQ(ed.batch().cutting.defaults.kerf, 3.2);
}

TEST(BatchEditor, ExportsWriteFiles) {
    TempDir dir;
    saveProject(dir.path / "a.fosp");
    BatchEditor ed;
    ed.newBatch("Export");
    ASSERT_TRUE(ed.saveAs(dir.path / "e.fosbatch").ok());
    ASSERT_TRUE(ed.addProject(dir.path / "a.fosp", 2).ok());
    const BatchEvaluation e = ed.evaluate();
    ASSERT_FALSE(e.incomplete);

    auto csv = exportBatchCutListCsv(e, dir.path / "cut.csv");
    ASSERT_TRUE(csv.ok()) << csv.error().message;
    ASSERT_EQ(csv.value().size(), 1u);
    std::ifstream in(dir.path / "cut.csv");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("a#1/"), std::string::npos); // part ids of copy 1 ...
    EXPECT_NE(text.find("a#2/"), std::string::npos); // ... and copy 2

    auto labels = exportBatchLabels(e, dir.path / "labels");
    ASSERT_TRUE(labels.ok()) << labels.error().message;
    ASSERT_FALSE(labels.value().empty());
    EXPECT_EQ(labels.value()[0].filename(), "labels-1.svg");

    auto plans = exportBatchCuttingPlans(e, dir.path / "plans");
    ASSERT_TRUE(plans.ok()) << plans.error().message;
    ASSERT_FALSE(plans.value().empty());
    for (const auto& p : plans.value()) {
        EXPECT_TRUE(fs::exists(p));
        EXPECT_EQ(p.extension(), ".svg");
    }

    // An empty batch has nothing to label or cut.
    BatchEditor empty;
    const BatchEvaluation none = empty.evaluate();
    EXPECT_FALSE(exportBatchLabels(none, dir.path / "x").ok());
    EXPECT_FALSE(exportBatchCuttingPlans(none, dir.path / "y").ok());
}

} // namespace
} // namespace fos::app::test
