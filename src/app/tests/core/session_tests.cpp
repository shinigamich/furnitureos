// SPDX-License-Identifier: AGPL-3.0-only
// Session lifecycle: templates, Scenario B at view-model level, save / open / recovery,
// item commands, selection, autosave.
#include "fos/app/parameter_rows.hpp"
#include "test_support.hpp"

#include <QSignalSpy>

namespace fos::app::test {
namespace {

namespace fs = std::filesystem;

// Base-cabinet template (furniture templates.hpp): 600 x 820 x 560, 18 mm board.
// Adjustable shelf length = inner_width - 2 * shelf_clearance = (W - 2*18) - 2*1:
//   W = 600 -> 562,  W = 700 -> 662,  W = 900 -> 862.
double shelfLength(const Session& s) {
    const auto* p = s.findPart("cab1/shelf.1");
    return p ? p->length : -1;
}

TEST(Session, StartsEmptyAndClean) {
    Session s;
    EXPECT_TRUE(s.project().items.empty());
    EXPECT_FALSE(s.isDirty());
    EXPECT_TRUE(s.filePath().empty());
    ASSERT_NE(s.resolved(), nullptr);
    EXPECT_EQ(s.project().metadata.id.size(), 36u); // UUID text
}

TEST(Session, NewFromEveryTemplate) {
    for (TemplateKind kind : allTemplates()) {
        Session s;
        ASSERT_TRUE(s.newFromTemplate(kind).ok()) << templateToken(kind);
        ASSERT_EQ(s.project().items.size(), 1u);
        ASSERT_NE(s.resolved(), nullptr);
        EXPECT_EQ(s.resolved()->model.assemblies.size(), 1u) << templateToken(kind);
        EXPECT_FALSE(s.isDirty());
        EXPECT_EQ(s.selection().itemId, "cab1");
        const auto size = defaultTemplateSize(kind);
        EXPECT_GT(size.width, 0);
    }
    EXPECT_DOUBLE_EQ(defaultTemplateSize(TemplateKind::BaseCabinet).width, 600);
    EXPECT_DOUBLE_EQ(defaultTemplateSize(TemplateKind::BaseCabinet).height, 820);
    EXPECT_DOUBLE_EQ(defaultTemplateSize(TemplateKind::WardrobeSection).width, 1000);
}

TEST(Session, TemplateSizeFromDialog) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet, TemplateSize{900, 820, 560}).ok());
    EXPECT_DOUBLE_EQ(shelfLength(s), 862); // 900 - 36 - 2
    // A depth that cannot be built is refused with the engine's message; nothing changes.
    const auto before = s.project().metadata.id;
    auto r = s.newFromTemplate(TemplateKind::BaseCabinet, TemplateSize{900, 820, -5});
    ASSERT_FALSE(r.ok());
    EXPECT_FALSE(r.error().message.empty());
    EXPECT_EQ(s.project().metadata.id, before);
}

TEST(Session, ScenarioB_WidthThroughInspectorThenUndoRedo) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    ASSERT_DOUBLE_EQ(shelfLength(s), 562);
    QSignalSpy changed(&s, &Session::projectChanged);
    const auto rev = s.revision();

    ASSERT_TRUE(applyParameterInput(s, "cab1", "width", QStringLiteral("700")).ok());
    EXPECT_DOUBLE_EQ(shelfLength(s), 662); // +100
    EXPECT_DOUBLE_EQ(s.findPart("cab1/bottom")->length, 664); // inner width 700 - 36
    EXPECT_TRUE(s.isDirty());
    EXPECT_EQ(changed.count(), 1);
    EXPECT_GT(s.revision(), rev);
    ASSERT_TRUE(s.document().undoLabel().has_value());
    EXPECT_EQ(s.document().undoLabel()->english(), "Set width of cab1 to 700");
    // Production follows the resolved model: shelf cut length = 662 (no end banding).
    bool found = false;
    for (const auto& row : s.productionReports().cutList.rows)
        for (const auto& id : row.partIds)
            if (id.str() == "cab1/shelf.1") {
                EXPECT_DOUBLE_EQ(row.cutLength, 662);
                found = true;
            }
    EXPECT_TRUE(found);

    ASSERT_TRUE(s.undo().ok());
    EXPECT_DOUBLE_EQ(shelfLength(s), 562);
    EXPECT_FALSE(s.isDirty()); // back at the save point
    ASSERT_TRUE(s.redo().ok());
    EXPECT_DOUBLE_EQ(shelfLength(s), 662);
}

TEST(Session, ExpressionInputAndEngineErrors) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    // Expression over the graph: width = depth + 140 = 560 + 140 = 700.
    ASSERT_TRUE(applyParameterInput(s, "cab1", "width", QStringLiteral("depth + 140")).ok());
    EXPECT_DOUBLE_EQ(shelfLength(s), 662);
    const auto rev = s.revision();
    const auto undoCount = s.document().undoCount();
    // Unknown name / syntax error / impossible value: error from the engine, nothing changes.
    for (const char* bad : {"no_such_parameter * 2", "width +", "-5", "width"}) {
        auto r = applyParameterInput(s, "cab1", "width", QString::fromUtf8(bad));
        EXPECT_FALSE(r.ok()) << bad;
        if (!r.ok()) EXPECT_FALSE(r.error().message.empty()) << bad;
        EXPECT_EQ(s.revision(), rev) << bad;
        EXPECT_EQ(s.document().undoCount(), undoCount) << bad;
        EXPECT_DOUBLE_EQ(shelfLength(s), 662) << bad;
    }
    EXPECT_FALSE(applyParameterInput(s, "cab1", "width", QStringLiteral("   ")).ok());
    EXPECT_FALSE(applyParameterInput(s, "nope", "width", QStringLiteral("700")).ok());
}

TEST(Session, SaveOpenRoundTripAndDirtyFlag) {
    TempDir dir;
    const fs::path file = dir.path / "kitchen.fosp";
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    EXPECT_FALSE(s.save().ok()); // untitled
    ASSERT_TRUE(applyParameterInput(s, "cab1", "width", QStringLiteral("900")).ok());
    QSignalSpy saved(&s, &Session::savedStateChanged);
    ASSERT_TRUE(s.saveAs(file).ok());
    EXPECT_EQ(s.filePath(), file);
    EXPECT_FALSE(s.isDirty());
    EXPECT_GE(saved.count(), 1);

    Session other;
    auto opened = other.open(file);
    ASSERT_TRUE(opened.ok()) << opened.error().message;
    EXPECT_FALSE(other.isDirty());
    EXPECT_DOUBLE_EQ(other.project().items[0].cabinet()->parameters.value("width").value(), 900);
    EXPECT_DOUBLE_EQ(shelfLength(other), 862);

    // A broken file is refused with the core's message and the open document stays.
    std::ofstream(dir.path / "broken.fosp") << "{ not json";
    auto bad = other.open(dir.path / "broken.fosp");
    ASSERT_FALSE(bad.ok());
    EXPECT_FALSE(bad.error().message.empty());
    EXPECT_EQ(other.filePath(), file);
}

TEST(Session, AutosaveAndRecovery) {
    TempDir dir;
    const fs::path file = dir.path / "k.fosp";
    Session s;
    s.setUntitledAutosaveDirectory(dir.path / "untitled");
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    // Untitled autosave goes to the untitled directory.
    ASSERT_TRUE(s.autosave().ok());
    EXPECT_TRUE(fs::exists(formats::autosavePath(s.autosaveTarget())));
    EXPECT_EQ(s.autosaveTarget().parent_path(), dir.path / "untitled");
    const fs::path untitledAutosave = formats::autosavePath(s.autosaveTarget());
    ASSERT_TRUE(s.saveAs(file).ok());
    EXPECT_FALSE(fs::exists(untitledAutosave)); // discarded after the first save

    ASSERT_TRUE(applyParameterInput(s, "cab1", "width", QStringLiteral("900")).ok());
    ASSERT_TRUE(s.autosave().ok());
    EXPECT_EQ(s.lastAutosavedRevision(), s.revision());
    EXPECT_TRUE(s.isDirty()); // autosave does not clear the dirty flag
    fs::last_write_time(formats::autosavePath(file), fs::last_write_time(file) + std::chrono::minutes(1));

    Session r;
    auto recovered = r.openRecovered(file);
    ASSERT_TRUE(recovered.ok()) << recovered.error().message;
    EXPECT_EQ(r.filePath(), file);
    EXPECT_TRUE(r.isDirty());
    EXPECT_DOUBLE_EQ(shelfLength(r), 862);
    ASSERT_TRUE(r.undo().ok()); // back to what is on disk (width 600)
    EXPECT_DOUBLE_EQ(shelfLength(r), 562);
    EXPECT_FALSE(r.isDirty());

    // Saving discards the autosave.
    ASSERT_TRUE(s.save().ok());
    EXPECT_FALSE(fs::exists(formats::autosavePath(file)));
}

TEST(Session, AddDuplicateDeleteItems) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    auto added = s.addFromTemplate(TemplateKind::WallCabinet);
    ASSERT_TRUE(added.ok()) << added.error().message;
    EXPECT_EQ(added.value(), "cab2");
    EXPECT_EQ(s.selection().itemId, "cab2");
    // Placed right of the base cabinet: its envelope ends at x = 600 (overlay back adds depth only).
    const auto* a = s.resolved()->model.findAssembly(domain::AssemblyId("cab2"));
    ASSERT_NE(a, nullptr);
    EXPECT_NEAR(a->placement.applyPoint({0, 0, 0}).x, 600, 1e-9);

    auto dup = s.duplicateItem("cab1");
    ASSERT_TRUE(dup.ok()) << dup.error().message;
    EXPECT_EQ(dup.value(), "cab3");
    EXPECT_EQ(s.project().findItem("cab3")->name(), "Base cabinet 2");
    EXPECT_EQ(s.project().items.size(), 3u);

    s.selectPart("cab3/shelf.1");
    EXPECT_EQ(s.selection().itemId, "cab3");
    ASSERT_TRUE(s.deleteItem("cab3").ok());
    EXPECT_TRUE(s.selection().itemId.empty()); // pruned
    EXPECT_EQ(s.project().items.size(), 2u);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(s.project().items.size(), 3u);
    EXPECT_FALSE(s.duplicateItem("zzz").ok());
}

TEST(Session, SelectionNormalisation) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    QSignalSpy spy(&s, &Session::selectionChanged);
    s.setSelection({{}, {"cab1/side.left", "nonexistent", "cab1/side.left"}});
    EXPECT_EQ(s.selection().itemId, "cab1");
    EXPECT_EQ(s.selection().partIds, std::vector<std::string>{"cab1/side.left"});
    EXPECT_EQ(spy.count(), 1);
    s.setSelection(s.selection()); // unchanged -> no signal
    EXPECT_EQ(spy.count(), 1);
    s.selectPart("");
    EXPECT_TRUE(s.selection().itemId.empty());
    s.selectItem("unknown");
    EXPECT_TRUE(s.selection().itemId.empty());
}

} // namespace
} // namespace fos::app::test
