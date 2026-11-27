// SPDX-License-Identifier: AGPL-3.0-only
// File > Import items from project…: source rows, preview of project::planMerge(), one undo step.
#include "fos/app/command_text.hpp"
#include "fos/app/merge_rows.hpp"
#include "fos/app/templates.hpp"
#include "fos/project/file.hpp"
#include "test_support.hpp"

namespace fos::app::test {
namespace {
/// Source project: cab1 "Base cabinet", cab2 "Wall cabinet"; its carcass material renamed so
/// that it conflicts with the target's (same id, different content).
project::Project sourceProject(std::string& carcassId) {
    Session s;
    EXPECT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    EXPECT_TRUE(s.addFromTemplate(TemplateKind::WallCabinet).ok());
    project::Project p = s.project();
    p.metadata.name = "Other kitchen";
    carcassId = p.items[0].cabinet()->materials.carcass.str();
    for (auto& m : p.libraries.materials)
        if (m.id.str() == carcassId) m.name = "Renamed board";
    return p;
}
} // namespace

TEST(Merge, RowsPreviewPoliciesAndOneUndoStep) {
    TempDir dir;
    std::string carcass;
    const project::Project source = sourceProject(carcass);
    const auto file = dir.path / "other.fosp";
    ASSERT_TRUE(project::saveProject(file, source).ok());
    auto loaded = loadMergeSource(file);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_FALSE(loadMergeSource(dir.path / "missing.fosp").ok());

    const auto rows = mergeSourceRows(loaded.value());
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].id, "cab1");
    EXPECT_EQ(rows[1].name, QStringLiteral("Wall cabinet"));
    EXPECT_EQ(rows[0].kind, QStringLiteral("Cabinet"));
    const auto policies = conflictPolicyChoices();
    ASSERT_EQ(policies.size(), 3u);
    EXPECT_EQ(policies[2].policy, formats::ConflictPolicy::RenameWithNewId);
    EXPECT_FALSE(mergeAllowed({}));

    Session target;
    ASSERT_TRUE(target.newFromTemplate(TemplateKind::BaseCabinet).ok());
    // Import only cab1: its id is taken -> "cab" + first free number = cab2.
    auto plan = project::planMerge(target.project(), loaded.value(),
                                   mergeRequest({"cab1"}, formats::ConflictPolicy::RenameWithNewId));
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    const QStringList lines = mergePreviewLines(plan.value());
    ASSERT_FALSE(lines.isEmpty());
    EXPECT_EQ(lines[0], QStringLiteral("cab1 “Base cabinet” → cab2 (id already used)"));
    EXPECT_TRUE(lines.filter(QStringLiteral("added as merge.lib1")).size() == 1) << lines.join('\n').toStdString();
    EXPECT_TRUE(lines.filter(QString::fromStdString(carcass)).size() == 1);
    // Skip keeps the target's entry and says so.
    auto skip = project::planMerge(target.project(), loaded.value(), mergeRequest({"cab1"}, formats::ConflictPolicy::Skip));
    ASSERT_TRUE(skip.ok());
    EXPECT_EQ(mergePreviewLines(skip.value()).filter(QStringLiteral("differs: this project's entry is kept")).size(), 1);
    // An unknown id is refused by planMerge.
    EXPECT_FALSE(project::planMerge(target.project(), loaded.value(), mergeRequest({"nope"}, formats::ConflictPolicy::Skip)).ok());

    // Both items in one undoable step.
    const auto materials = target.project().libraries.materials.size();
    ASSERT_TRUE(target.execute(project::commands::mergeItems(
                                   loaded.value(), mergeRequest({"cab1", "cab2"}, formats::ConflictPolicy::RenameWithNewId)))
                    .ok());
    ASSERT_EQ(target.project().items.size(), 3u);
    EXPECT_EQ(target.project().items[1].id(), "cab2");
    EXPECT_EQ(target.project().items[2].id(), "cab3");
    EXPECT_EQ(target.project().libraries.materials.size(), materials + 1); // the renamed board
    EXPECT_EQ(commandText(*target.document().undoLabel(), target.project()),
              QStringLiteral("Merge items from Other kitchen"));
    ASSERT_TRUE(target.undo().ok());
    EXPECT_EQ(target.project().items.size(), 1u);
    EXPECT_EQ(target.project().libraries.materials.size(), materials);
}

} // namespace fos::app::test
