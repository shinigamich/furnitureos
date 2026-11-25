// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/merge.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace fos;
using namespace fos::project;
using formats::ConflictPolicy;
using formats::MergeAction;
using ::testing::HasSubstr;

namespace {

/// Target: cab1, cab2 and a local 16 mm "mat.generic.pb18" (the source has the generic 18 mm board).
Project target() {
    Project p = newProject(test::meta());
    for (auto& m : p.libraries.materials)
        if (m.id.str() == defaults::kParticleboard18) {
            m.thickness = 16;
            m.name = "Local board";
        }
    p.items.push_back(test::baseCabinetItem("cab1"));
    p.items.push_back(test::baseCabinetItem("cab2"));
    return p;
}

/// Source: cab1 at the origin, cab2 at x = 600.
Project source() {
    Project p = newProject(test::meta());
    p.metadata.name = "Other kitchen";
    p.items.push_back(test::baseCabinetItem("cab1"));
    Item second = test::baseCabinetItem("cab2");
    second.cabinet()->placement = geometry::Transform::translation({600, 0, 0});
    p.items.push_back(second);
    return p;
}

const domain::Material* material(const Project& p, const std::string& id) {
    for (const auto& m : p.libraries.materials)
        if (m.id.str() == id) return &m;
    return nullptr;
}

/// Thickness of the left side of item `itemId` after resolving.
double sideThickness(const Project& p, const std::string& itemId) {
    const auto r = test::resolved(p);
    for (const auto& a : r.model.assemblies)
        if (a.id.str() == itemId)
            for (const auto& part : a.parts)
                if (part.id.str().find("side") != std::string::npos) return part.thickness;
    return -1;
}

TEST(Merge, FreeItemIdStripsTrailingDigits) {
    EXPECT_EQ(freeItemId("cab1", {"cab1", "cab2"}), "cab3");
    EXPECT_EQ(freeItemId("cab1", {"cab2"}), "cab1");
    EXPECT_EQ(freeItemId("sink", {"sink"}), "sink1");
    EXPECT_EQ(freeItemId("12", {"12", "1"}), "2"); // prefix "" -> "1" taken -> "2"
}

TEST(Merge, ClashingItemIdsAreRenamedAndOffsetApplied) {
    MergeRequest req;
    req.offset = geometry::Transform::translation({0, 1000, 0});
    auto plan = planMerge(target(), source(), req);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    // Target uses cab1, cab2: source cab1 -> cab3 (first free), then cab2 -> cab4 (cab3 now taken).
    ASSERT_EQ(plan.value().items.size(), 2u);
    EXPECT_EQ(plan.value().items[0].newId, "cab3");
    EXPECT_EQ(plan.value().items[1].newId, "cab4");
    EXPECT_TRUE(plan.value().items[1].renamed());

    auto merged = mergeProjects(target(), source(), req);
    ASSERT_TRUE(merged.ok()) << merged.error().message;
    const Project& p = merged.value().project;
    ASSERT_EQ(p.items.size(), 4u);
    EXPECT_EQ(p.items[2].id(), "cab3");
    EXPECT_EQ(p.items[3].id(), "cab4");
    // cab4: source (600, 0, 0) then offset (0, 1000, 0) -> origin (600, 1000, 0).
    const auto o = p.findItem("cab4")->placement().applyPoint({0, 0, 0});
    EXPECT_EQ(o.x, 600);
    EXPECT_EQ(o.y, 1000);
    EXPECT_EQ(o.z, 0);
    // Part ids derive from the new item id; the whole project resolves without duplicates.
    const auto r = test::resolved(p);
    EXPECT_FALSE(core::hasErrors(r.diagnostics));
    EXPECT_EQ(r.model.assemblies.size(), 4u);
    EXPECT_EQ(merged.value().affected.items, (std::vector<std::string>{"cab3", "cab4"}));

    // Selecting one item: only cab2 is imported, as cab3.
    req.itemIds = {"cab2"};
    merged = mergeProjects(target(), source(), req);
    ASSERT_TRUE(merged.ok());
    ASSERT_EQ(merged.value().project.items.size(), 3u);
    EXPECT_EQ(merged.value().project.items[2].id(), "cab3");
}

TEST(Merge, LibraryConflictPolicies) {
    const std::size_t materials = target().libraries.materials.size();
    const std::size_t hardware = target().libraries.hardware.size();
    {   // Keep: the target's 16 mm board stays and the imported cabinets use it.
        auto merged = mergeProjects(target(), source(), {});
        ASSERT_TRUE(merged.ok()) << merged.error().message;
        const auto& plan = merged.value().plan;
        ASSERT_EQ(plan.conflicts().size(), 1u); // only pb18 differs; other entries are identical
        EXPECT_EQ(plan.conflicts()[0]->entry.incomingId, defaults::kParticleboard18);
        EXPECT_EQ(plan.conflicts()[0]->entry.action, MergeAction::Skipped);
        EXPECT_EQ(plan.withAction(MergeAction::Skipped).size(), plan.library.size());
        EXPECT_FALSE(merged.value().affected.libraries);
        EXPECT_EQ(sideThickness(merged.value().project, "cab3"), 16);
        EXPECT_EQ(merged.value().project.libraries.materials.size(), materials);
    }
    {   // Replace: overwritten in place, so the target's own cab1 also becomes 18 mm.
        MergeRequest req;
        req.conflictPolicy = ConflictPolicy::Replace;
        auto merged = mergeProjects(target(), source(), req);
        ASSERT_TRUE(merged.ok());
        const Project& p = merged.value().project;
        EXPECT_EQ(material(p, defaults::kParticleboard18)->thickness, 18);
        EXPECT_EQ(p.libraries.materials.size(), materials);
        EXPECT_EQ(sideThickness(p, "cab1"), 18);
        EXPECT_EQ(merged.value().affected.items.size(), 4u); // every item may change
        EXPECT_TRUE(merged.value().affected.libraries);
    }
    {   // Rename: the 18 mm board is added ONCE as merge.lib1 although both items use it.
        MergeRequest req;
        req.conflictPolicy = ConflictPolicy::RenameWithNewId;
        auto merged = mergeProjects(target(), source(), req);
        ASSERT_TRUE(merged.ok()) << merged.error().message;
        const Project& p = merged.value().project;
        ASSERT_EQ(merged.value().plan.withAction(MergeAction::Renamed).size(), 1u);
        EXPECT_EQ(merged.value().plan.withAction(MergeAction::Renamed)[0]->entry.resultId, "merge.lib1");
        EXPECT_EQ(p.libraries.materials.size(), materials + 1);
        EXPECT_EQ(p.libraries.hardware.size(), hardware); // identical built-in hardware not duplicated
        EXPECT_EQ(material(p, defaults::kParticleboard18)->thickness, 16);
        EXPECT_EQ(material(p, "merge.lib1")->thickness, 18);
        for (const char* id : {"cab3", "cab4"}) {
            EXPECT_EQ(p.findItem(id)->cabinet()->materials.carcass.str(), "merge.lib1");
            EXPECT_EQ(sideThickness(p, id), 18);
        }
        EXPECT_EQ(sideThickness(p, "cab1"), 16);
        // A second merge picks the next free id.
        auto again = mergeProjects(p, source(), req);
        ASSERT_TRUE(again.ok());
        // pb18 still conflicts (16 vs 18 mm); merge.lib1 is taken, so the copy is merge.lib2
        // (an equal entry under ANOTHER id is not detected).
        ASSERT_EQ(again.value().plan.withAction(MergeAction::Renamed).size(), 1u);
        EXPECT_EQ(again.value().plan.withAction(MergeAction::Renamed)[0]->entry.resultId, "merge.lib2");
        EXPECT_EQ(again.value().project.items.size(), 6u);
    }
}

TEST(Merge, UndoableCommand) {
    Document doc(target());
    const Project before = doc.project();
    MergeRequest req;
    req.conflictPolicy = ConflictPolicy::RenameWithNewId;
    ASSERT_TRUE(doc.execute(commands::mergeItems(source(), req)).ok());
    EXPECT_EQ(doc.project().items.size(), 4u);
    EXPECT_NE(material(doc.project(), "merge.lib1"), nullptr);
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(doc.project().items.size(), 2u);
    EXPECT_EQ(material(doc.project(), "merge.lib1"), nullptr);
    EXPECT_EQ(doc.project().libraries.materials.size(), before.libraries.materials.size());
    ASSERT_TRUE(doc.redo().ok());
    EXPECT_EQ(doc.project().items.size(), 4u);
}

TEST(Merge, ErrorsChangeNothing) {
    MergeRequest req;
    req.itemIds = {"nope"};
    auto r = mergeProjects(target(), source(), req);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound);
    EXPECT_THAT(r.error().message, HasSubstr("\"nope\" (it has cab1, cab2)"));

    req.itemIds = {"cab1", "cab1"};
    EXPECT_EQ(mergeProjects(target(), source(), req).error().code, core::ErrorCode::InvalidArgument);

    EXPECT_EQ(mergeProjects(target(), newProject(test::meta()), {}).error().code, core::ErrorCode::InvalidArgument);

    // A referenced material missing from the source libraries.
    Project broken = source();
    std::erase_if(broken.libraries.materials, [](const domain::Material& m) { return m.id.str() == defaults::kParticleboard18; });
    r = mergeProjects(target(), broken, {});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound);
    EXPECT_THAT(r.error().message, HasSubstr("material \"mat.generic.pb18\""));

    // The document stays unchanged when the command fails.
    Document doc(target());
    EXPECT_FALSE(doc.execute(commands::mergeItems(broken, {})).ok());
    EXPECT_FALSE(doc.canUndo());
    EXPECT_EQ(doc.project().items.size(), 2u);
}

} // namespace
