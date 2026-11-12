// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

#include "fos/project/codec.hpp"

namespace fos::project::test {
namespace {

Document docWithCabinet() {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem());
    return Document(p);
}

std::string state(const Document& d) { return projectToJson(d.project()).dump(); }

double width(const Document& d) {
    return d.project().findItem("cab1")->cabinet()->parameters.value("width").value();
}

} // namespace

TEST(Document, FailedCommandLeavesDocumentUnchanged) {
    Document doc = docWithCabinet();
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "width", 700)).ok());
    const std::string before = state(doc);
    const bool dirty = doc.isDirty();
    int notices = 0;
    doc.addObserver([&](const ChangeNotice&) { ++notices; });

    // Width 20 mm: inner width 20 - 36 < 0 violates the standard constraint.
    auto r = doc.execute(commands::setParameter("cab1", "width", 20));
    ASSERT_FALSE(r.ok());
    EXPECT_FALSE(r.error().message.empty());
    auto unknown = doc.execute(commands::setParameter("nope", "width", 900));
    ASSERT_FALSE(unknown.ok());
    EXPECT_EQ(unknown.error().code, core::ErrorCode::NotFound);
    auto inUse = doc.execute(commands::removeMaterial(defaults::kParticleboard18));
    ASSERT_FALSE(inUse.ok());
    EXPECT_NE(inUse.error().message.find("\"cab1\""), std::string::npos);
    auto dup = doc.execute(commands::addItem(baseCabinetItem("cab1")));
    ASSERT_FALSE(dup.ok());
    EXPECT_EQ(dup.error().code, core::ErrorCode::AlreadyExists);
    // A panel edit whose result cannot be generated (unknown material) is refused by the post-check.
    PanelItem panel;
    panel.id = domain::AssemblyId("shelf");
    panel.panel = furniture::makePanel(domain::PartId("shelf/panel"), "Loose shelf", domain::MaterialId(defaults::kMdf18));
    ASSERT_TRUE(doc.execute(commands::addItem(Item{panel, formats::Json::object()})).ok());
    const std::string withPanel = state(doc);
    furniture::PanelDefinition broken = panel.panel;
    broken.material = domain::MaterialId("mat.unknown");
    auto post = doc.execute(commands::setPanel("shelf", broken));
    ASSERT_FALSE(post.ok());
    EXPECT_NE(post.error().message.find("could not be generated"), std::string::npos);
    EXPECT_EQ(state(doc), withPanel);
    ASSERT_TRUE(doc.undo().ok());

    EXPECT_EQ(state(doc), before);
    EXPECT_EQ(doc.isDirty(), dirty);
    EXPECT_EQ(doc.undoCount(), 1u);
    EXPECT_EQ(notices, 2); // add panel + its undo; no notice for failures
}

TEST(Document, DirtyFlagSemantics) {
    TempDir dir;
    Document doc = docWithCabinet();
    EXPECT_FALSE(doc.isDirty());
    EXPECT_FALSE(doc.canUndo());
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "width", 700)).ok());
    EXPECT_TRUE(doc.isDirty());
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_FALSE(doc.isDirty()); // back at the save point
    ASSERT_TRUE(doc.redo().ok());
    EXPECT_TRUE(doc.isDirty());
    ASSERT_TRUE(doc.save(dir.path / "a.fosp").ok());
    EXPECT_FALSE(doc.isDirty());
    ASSERT_TRUE(doc.undo().ok()); // before the save point
    EXPECT_TRUE(doc.isDirty());
    ASSERT_TRUE(doc.redo().ok());
    EXPECT_FALSE(doc.isDirty());
    // Undo, then a different command: the saved state cannot be reached any more.
    ASSERT_TRUE(doc.undo().ok());
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "width", 650)).ok());
    EXPECT_FALSE(doc.canRedo());
    EXPECT_TRUE(doc.isDirty());
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_TRUE(doc.isDirty()); // 600 again, but the saved state was 700
    // Autosave does not clean the document.
    ASSERT_TRUE(doc.autosave(dir.path / "a.fosp").ok());
    EXPECT_TRUE(doc.isDirty());
    doc.markSaved();
    EXPECT_FALSE(doc.isDirty());
}

TEST(Document, NoOpCommandRecordsNoStep) {
    Document doc = docWithCabinet();
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "width", 600)).ok()); // already 600
    EXPECT_FALSE(doc.canUndo());
    EXPECT_FALSE(doc.isDirty());
}

TEST(Document, MergeOfConsecutiveParameterEdits) {
    Document doc = docWithCabinet();
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "width", 610), Merge::WithPrevious).ok());
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "width", 620), Merge::WithPrevious).ok());
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "width", 630), Merge::WithPrevious).ok());
    EXPECT_EQ(doc.undoCount(), 1u);
    EXPECT_EQ(doc.undoLabel()->english(), "Set width of cab1 to 630");
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "height", 900), Merge::WithPrevious).ok()); // other key
    EXPECT_EQ(doc.undoCount(), 2u);
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "height", 910)).ok()); // merge not requested
    EXPECT_EQ(doc.undoCount(), 3u);
    ASSERT_TRUE(doc.undo().ok());
    ASSERT_TRUE(doc.undo().ok());
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_DOUBLE_EQ(width(doc), 600.0); // the merged step restores the value before the drag
    EXPECT_FALSE(doc.isDirty());
    // No merge across the save point.
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "width", 640)).ok());
    doc.markSaved();
    ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "width", 650), Merge::WithPrevious).ok());
    EXPECT_EQ(doc.undoCount(), 2u);
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_FALSE(doc.isDirty());
    EXPECT_DOUBLE_EQ(width(doc), 640.0);
}

TEST(Document, ItemCommandsAndNotifications) {
    Document doc = docWithCabinet();
    std::vector<ChangeNotice> seen;
    doc.addObserver([&](const ChangeNotice& n) { seen.push_back(n); });
    ASSERT_TRUE(doc.execute(commands::duplicateItem("cab1", "cab2", "Copy", geometry::Transform::translation({600, 0, 0}))).ok());
    ASSERT_EQ(doc.project().items.size(), 2u);
    EXPECT_EQ(doc.project().items[1].id(), "cab2");
    EXPECT_EQ(seen.back().affected.items, std::vector<std::string>{"cab2"});
    const Resolved r = resolved(doc.project());
    ASSERT_NE(findPart(r, "cab2/bottom"), nullptr); // ids follow the new item id
    EXPECT_DOUBLE_EQ(r.model.assemblies[1].placement.origin.x, 600.0);

    drawings::DrawingEdits e;
    e.texts.push_back({"t", "cab2/view.front", {0, 0}, "hello", 3.5, 0});
    ASSERT_TRUE(doc.execute(commands::setDrawingEdits(assemblyDrawingId("cab2"), e)).ok());
    EXPECT_EQ(seen.back().affected.drawings, std::vector<std::string>{"cab2/drawing.assembly"});
    ASSERT_TRUE(doc.execute(commands::removeItem("cab2")).ok());
    EXPECT_EQ(doc.project().items.size(), 1u);
    EXPECT_EQ(doc.project().drawingEdits.count("cab2/drawing.assembly"), 0u);
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(doc.project().drawingEdits.count("cab2/drawing.assembly"), 1u);
    EXPECT_EQ(seen.back().kind, ChangeKind::Undo);

    geometry::Transform skew;
    skew.xAxis = {1, 1, 0};
    EXPECT_FALSE(doc.execute(commands::setPlacement("cab1", skew)).ok());
    ASSERT_TRUE(doc.execute(commands::renameItem("cab1", "Sink unit")).ok());
    EXPECT_EQ(doc.project().items[0].name(), "Sink unit");

    domain::Material m = doc.project().libraries.materials[0];
    m.costPerSheet = 42.0;
    ASSERT_TRUE(doc.execute(commands::upsertMaterial(m)).ok());
    EXPECT_TRUE(seen.back().affected.libraries);
    EXPECT_EQ(seen.back().affected.items, (std::vector<std::string>{"cab1", "cab2"}));
    EXPECT_DOUBLE_EQ(doc.project().libraries.materials[0].costPerSheet, 42.0);
    ASSERT_TRUE(doc.execute(commands::removeMaterial(defaults::kMdf18)).ok()); // unused
    EXPECT_EQ(doc.project().libraries.materials.size(), 2u);
}

TEST(Document, CabinetEditsThroughFurnitureApi) {
    Document doc = docWithCabinet();
    ASSERT_TRUE(doc.execute(commands::setShelfCount("cab1", "", 3)).ok());
    Resolved r = resolved(doc.project());
    EXPECT_NE(findPart(r, "cab1/shelf.3"), nullptr);
    EXPECT_FALSE(doc.execute(commands::setShelfCount("cab1", "9", 1)).ok()); // no such section
    ASSERT_TRUE(doc.execute(commands::splitSection("cab1", "", furniture::SplitDirection::Vertical,
                                                   {furniture::SectionSize::weight(), furniture::SectionSize::weight()}))
                    .ok());
    r = resolved(doc.project());
    EXPECT_FALSE(core::hasErrors(r.diagnostics));
    ASSERT_TRUE(doc.execute(commands::setCuttingSettings(std::string(defaults::kHdf3), CuttingSettings{})).ok());
    EXPECT_TRUE(doc.project().cutting.materials.count(defaults::kHdf3));
    ASSERT_TRUE(doc.undo().ok());
    ASSERT_TRUE(doc.undo().ok());
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(modelText(resolved(doc.project()).model), modelText(resolved(docWithCabinet().project()).model));
}

TEST(Document, UndoLimitDropsOldestAndSavePoint) {
    Document doc = docWithCabinet();
    doc.setUndoLimit(2);
    for (double w : {610.0, 620.0, 630.0}) ASSERT_TRUE(doc.execute(commands::setParameter("cab1", "width", w)).ok());
    EXPECT_EQ(doc.undoCount(), 2u);
    ASSERT_TRUE(doc.undo().ok());
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_FALSE(doc.canUndo());
    EXPECT_DOUBLE_EQ(width(doc), 610.0);
    EXPECT_TRUE(doc.isDirty()); // the original (saved) state was dropped from the history
    EXPECT_FALSE(doc.undo().ok());
}

TEST(Document, LabelSubstitution) {
    CommandLabel l{"Set {1} of {0} to {2}; {9} {x}", {"A", "b", "3"}};
    EXPECT_EQ(l.english(), "Set b of A to 3; {9} {x}");
}

} // namespace fos::project::test
