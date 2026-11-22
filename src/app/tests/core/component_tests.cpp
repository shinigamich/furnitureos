// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/component_browser.hpp"
#include "test_support.hpp"

#include <gtest/gtest.h>

#include <fstream>

using namespace fos;
using namespace fos::app;

namespace {

std::size_t partCount(const Session& s, const std::string& itemId) {
    const project::Resolved* r = s.resolved();
    if (!r) return 0;
    const domain::Assembly* a = r->model.findAssembly(domain::AssemblyId(itemId));
    return a ? a->parts.size() : 0;
}

} // namespace

TEST(ComponentBrowser, SaveScanInsertUndo) {
    test::TempDir dir;
    Session source;
    ASSERT_TRUE(source.newFromTemplate(TemplateKind::BaseCabinet));
    const std::string srcId = source.project().items.front().id();
    const std::size_t originalParts = partCount(source, srcId);
    ASSERT_GT(originalParts, 0u);

    formats::ComponentMeta meta;
    meta.name = "Base 600";
    meta.category = "Base cabinets";
    meta.tags = parseTagList("kitchen, base, kitchen");
    meta.description = "Standard base cabinet";
    auto saved = saveItemAsComponent(source.project(), srcId, meta, dir.path / "lib");
    ASSERT_TRUE(saved) << saved.error().message;
    // Default id "comp." + slug("Base 600") = "comp.base-600".
    EXPECT_EQ(saved.value().filename().string(), "comp.base-600.foscomp");
    // A second save without overwrite is refused and names the file.
    auto again = saveItemAsComponent(source.project(), srcId, meta, dir.path / "lib");
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error().code, core::ErrorCode::AlreadyExists);
    EXPECT_NE(again.error().message.find("comp.base-600.foscomp"), std::string::npos);

    ComponentBrowserModel model;
    const auto notes = model.rescan({dir.path / "lib", dir.path / "missing"});
    ASSERT_EQ(notes.size(), 1u); // only the missing folder (a warning)
    EXPECT_EQ(notes[0].code, "project.components.missing_path");
    ASSERT_EQ(model.library().entries().size(), 1u);

    auto rows = model.rows("kitchen", "base 6");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].category, "Base cabinets");
    ASSERT_EQ(rows[0].entries.size(), 1u);
    EXPECT_TRUE(model.rows("wardrobe", {}).empty());
    EXPECT_TRUE(model.rows({}, "no such text").empty());

    const auto& entry = *rows[0].entries[0];
    const ComponentDetails details = componentDetails(entry);
    EXPECT_EQ(details.name, "Base 600");
    EXPECT_EQ(details.tags, (QStringList{"kitchen", "base"}));
    EXPECT_FALSE(details.materials.isEmpty());
    // Preview size = the generated bounding box: width and height are the template's own
    // size; the depth also includes the front in front of the carcass (583 > 560 mm).
    const TemplateSize ts = defaultTemplateSize(TemplateKind::BaseCabinet);
    EXPECT_DOUBLE_EQ(entry.component.meta.previewSize.width, ts.width);
    EXPECT_DOUBLE_EQ(entry.component.meta.previewSize.height, ts.height);
    EXPECT_GT(entry.component.meta.previewSize.depth, ts.depth);

    Session target;
    target.newProject();
    // The new project has the same default libraries: identical entries are no conflicts.
    auto conflicts = componentConflicts(target.project(), entry.component);
    ASSERT_TRUE(conflicts);
    EXPECT_TRUE(conflicts.value().isEmpty()) << conflicts.value().join(", ").toStdString();

    auto id = insertComponentIntoSession(target, entry.component, formats::ConflictPolicy::Skip);
    ASSERT_TRUE(id) << id.error().message;
    EXPECT_EQ(id.value(), "cab1");
    EXPECT_EQ(target.selection().itemId, "cab1");
    ASSERT_EQ(target.project().items.size(), 1u);
    EXPECT_EQ(target.project().items[0].name(), "Base 600");
    EXPECT_EQ(partCount(target, "cab1"), originalParts);

    // A second insert goes to the right of the first: x = right edge of cab1 (>= its width).
    const double edge = target.rightEdge();
    EXPECT_GE(edge, ts.width - 1e-9);
    auto id2 = insertComponentIntoSession(target, entry.component, formats::ConflictPolicy::Skip);
    ASSERT_TRUE(id2);
    EXPECT_EQ(id2.value(), "cab2");
    EXPECT_NEAR(target.project().findItem("cab2")->placement().origin.x, edge, 1e-9);

    ASSERT_TRUE(target.undo());
    ASSERT_TRUE(target.undo());
    EXPECT_TRUE(target.project().items.empty());
}

TEST(ComponentBrowser, ConflictingMaterialIsReported) {
    test::TempDir dir;
    Session source;
    ASSERT_TRUE(source.newFromTemplate(TemplateKind::BaseCabinet));
    auto component = project::makeComponent(source.project(), source.project().items.front().id(), {});
    ASSERT_TRUE(component);
    ASSERT_FALSE(component.value().library.materials.empty());
    // Same id, different content in the target project -> a conflict.
    auto& m = component.value().library.materials.front();
    const std::string conflictId = m.id.str();
    m.name += " (changed)";

    Session target;
    target.newProject();
    auto conflicts = componentConflicts(target.project(), component.value());
    ASSERT_TRUE(conflicts);
    ASSERT_EQ(conflicts.value().size(), 1);
    EXPECT_TRUE(conflicts.value()[0].endsWith(QString::fromStdString(conflictId)));

    // Rename keeps the project's entry and adds the incoming one as "cab1.lib1".
    auto id = insertComponentIntoSession(target, component.value(), formats::ConflictPolicy::RenameWithNewId);
    ASSERT_TRUE(id) << id.error().message;
    const auto& mats = target.project().libraries.materials;
    EXPECT_TRUE(std::any_of(mats.begin(), mats.end(), [](const auto& x) { return x.id.str() == "cab1.lib1"; }));
}

TEST(ComponentBrowser, BrokenFileAndHelpers) {
    test::TempDir dir;
    { std::ofstream(dir.path / "bad.foscomp") << "{not json"; }
    ComponentBrowserModel model;
    const auto notes = model.rescan({dir.path});
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].code, "project.components.load_failed");
    EXPECT_TRUE(model.library().entries().empty());

    EXPECT_EQ(componentFileName("plug:a b/c"), "plug-a-b-c.foscomp");
    EXPECT_EQ(componentFileName(".x"), "component.x.foscomp");
    EXPECT_EQ(parseTagList(" a ,,b, a "), (std::vector<std::string>{"a", "b"}));

    Session s;
    s.newProject();
    auto r = saveItemAsComponent(s.project(), "nope", {}, dir.path);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound);
}
