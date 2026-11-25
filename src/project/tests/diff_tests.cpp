// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/diff.hpp"

#include "test_support.hpp"

namespace fos::project::test {
namespace {

Project kitchen() {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem("cab1"));
    return p;
}

ProjectDiff diffOf(const Project& a, const Project& b, DiffOptions o = {}) {
    auto d = diffProjects(a, b, o);
    EXPECT_TRUE(d.ok()) << (d.ok() ? "" : d.error().message);
    return d.ok() ? d.value() : ProjectDiff{};
}

TEST(ProjectDiff, IdenticalProjectsAreEmpty) {
    const Project a = kitchen();
    DiffOptions o;
    o.model = true;
    const auto d = diffOf(a, a, o);
    EXPECT_TRUE(d.empty());
    ASSERT_TRUE(d.model.has_value());
    EXPECT_TRUE(d.model->empty());
    EXPECT_EQ(diffText(d), "No differences.\n");
    EXPECT_TRUE(diffToJson(d)["identical"].get<bool>());
}

TEST(ProjectDiff, ParameterChangeIsReportedWithOldAndNewValue) {
    const Project a = kitchen();
    Project b = a;
    ASSERT_TRUE(b.items[0].cabinet()->parameters.setValue("width", 700).ok());
    const auto d = diffOf(a, b);
    EXPECT_FALSE(d.empty());
    ASSERT_EQ(d.itemsChanged.size(), 1u);
    const auto& c = d.itemsChanged[0];
    EXPECT_EQ(c.id, "cab1");
    ASSERT_EQ(c.parameters.size(), 1u); // only the stored definition of "width" changed
    EXPECT_EQ(c.parameters[0].name, "width");
    EXPECT_EQ(c.parameters[0].before, std::optional<std::string>("600"));
    EXPECT_EQ(c.parameters[0].after, std::optional<std::string>("700"));
    EXPECT_FALSE(c.placement.has_value());
    EXPECT_TRUE(c.materials.empty() && c.interior.empty() && c.fronts.empty());
    EXPECT_NE(diffText(d).find("parameter width: 600 -> 700"), std::string::npos) << diffText(d);
}

TEST(ProjectDiff, ToleranceHidesTinyNumericNoise) {
    const Project a = kitchen();
    Project b = a;
    ASSERT_TRUE(b.items[0].cabinet()->parameters.setValue("width", 600.0000001).ok());
    EXPECT_TRUE(diffOf(a, b).empty()); // |600.0000001 - 600| = 1e-7 <= 1e-6
}

TEST(ProjectDiff, LibraryFieldChangeNamesTheField) {
    const Project a = kitchen();
    Project b = a;
    b.libraries.materials[0].thickness = 19.0; // mat.generic.pb18
    b.libraries.edgeBands.erase(b.libraries.edgeBands.begin() + 1); // eb.generic.abs04
    const auto d = diffOf(a, b);
    ASSERT_EQ(d.libraries.size(), 2u);
    EXPECT_EQ(d.libraries[0].category, "materials");
    ASSERT_EQ(d.libraries[0].changed.size(), 1u);
    EXPECT_EQ(d.libraries[0].changed[0].id, "mat.generic.pb18");
    ASSERT_EQ(d.libraries[0].changed[0].fields.size(), 1u);
    EXPECT_EQ(d.libraries[0].changed[0].fields[0].path, "/thickness");
    EXPECT_EQ(d.libraries[0].changed[0].fields[0].before->get<double>(), 18.0);
    EXPECT_EQ(d.libraries[0].changed[0].fields[0].after->get<double>(), 19.0);
    EXPECT_EQ(d.libraries[1].category, "edgeBands");
    EXPECT_EQ(d.libraries[1].removed, std::vector<std::string>{"eb.generic.abs04"});
    EXPECT_TRUE(d.itemsChanged.empty()); // stored items are untouched
}

TEST(ProjectDiff, ItemAddedRenamedAndMoved) {
    const Project a = kitchen();
    Project b = a;
    b.items.push_back(baseCabinetItem("cab2"));
    b.items[0].cabinet()->name = "Sink cabinet";
    b.items[0].cabinet()->placement = geometry::Transform::translation({700, 0, 0});
    const auto d = diffOf(a, b);
    ASSERT_EQ(d.itemsAdded.size(), 1u);
    EXPECT_EQ(d.itemsAdded[0].id, "cab2");
    EXPECT_EQ(d.itemsAdded[0].type, "cabinet");
    EXPECT_TRUE(d.itemsRemoved.empty());
    EXPECT_FALSE(d.itemOrderChanged);
    ASSERT_EQ(d.itemsChanged.size(), 1u);
    const auto& c = d.itemsChanged[0];
    EXPECT_EQ(c.name, "Sink cabinet");
    ASSERT_TRUE(c.renamedFrom.has_value());
    EXPECT_EQ(*c.renamedFrom, a.items[0].name());
    ASSERT_TRUE(c.placement.has_value());
    EXPECT_TRUE(c.placement->moved);
    EXPECT_FALSE(c.placement->rotated);
    EXPECT_EQ(c.placement->afterOrigin.x, 700.0);
    const auto j = diffToJson(d);
    EXPECT_EQ(j["items"]["added"][0]["id"], "cab2");
    EXPECT_FALSE(j["identical"].get<bool>());
}

TEST(ProjectDiff, SettingsAndMetadataGroups) {
    const Project a = kitchen();
    Project b = a;
    b.cutting.defaults.kerf = 3.2;
    b.settings.room = RoomSettings{4000, 3000, std::nullopt, 100, {0, 0}};
    b.metadata.modifiedAt = "2026-11-20T10:00:00Z";
    auto d = diffOf(a, b);
    ASSERT_EQ(d.settings.size(), 2u);
    EXPECT_EQ(d.settings[0].group, "cutting");
    ASSERT_EQ(d.settings[0].changes.size(), 1u);
    EXPECT_EQ(d.settings[0].changes[0].path, "/defaults/kerf");
    EXPECT_EQ(d.settings[1].group, "room");
    EXPECT_FALSE(d.settings[1].changes[0].before.has_value()); // room configured only in B
    ASSERT_EQ(d.metadata.size(), 1u);
    EXPECT_EQ(d.metadata[0].path, "/modifiedAt");
    DiffOptions o;
    o.ignoreTimestamps = true;
    EXPECT_TRUE(diffOf(a, b, o).metadata.empty());
}

TEST(ProjectDiff, ModelLevelPartSizeChange) {
    const Project a = kitchen();
    Project b = a;
    ASSERT_TRUE(b.items[0].cabinet()->parameters.setValue("width", 700).ok());
    DiffOptions o;
    o.model = true;
    const auto d = diffOf(a, b, o);
    ASSERT_TRUE(d.model.has_value());
    const auto& m = *d.model;
    EXPECT_TRUE(m.added.empty());
    EXPECT_TRUE(m.removed.empty());
    // Bottom: inner width 600 - 2 * 18 = 564 -> 700 - 36 = 664; depth 560 unchanged.
    const PartChange* bottom = nullptr;
    const PartChange* side = nullptr;
    for (const auto& c : m.changed) {
        if (c.after.id == "cab1/bottom") bottom = &c;
        if (c.after.id == "cab1/side.left") side = &c;
    }
    ASSERT_NE(bottom, nullptr);
    EXPECT_TRUE(bottom->sizeChanged);
    EXPECT_FALSE(bottom->materialChanged);
    EXPECT_DOUBLE_EQ(bottom->before.length, 564.0);
    EXPECT_DOUBLE_EQ(bottom->after.length, 664.0);
    EXPECT_DOUBLE_EQ(bottom->after.width, 560.0);
    EXPECT_EQ(side, nullptr); // sides stay 820 x 560 x 18
    EXPECT_NE(diffText(d).find("~ part cab1/bottom"), std::string::npos) << diffText(d);

    // An added item adds its parts and its hardware.
    Project c = a;
    c.items.push_back(baseCabinetItem("cab2"));
    const auto d2 = diffOf(a, c, o);
    ASSERT_TRUE(d2.model.has_value());
    EXPECT_EQ(d2.model->added.size(), 10u); // the base cabinet has 10 parts (resolve_tests)
    EXPECT_TRUE(d2.model->changed.empty());
    for (const auto& h : d2.model->hardware) EXPECT_EQ(h.after, 2 * h.before) << h.definition;
}

TEST(ProjectDiff, ModelDiffFailsForStructurallyInvalidProject) {
    const Project a = kitchen();
    Project b = a;
    b.items.push_back(baseCabinetItem("cab1")); // duplicate id
    DiffOptions o;
    o.model = true;
    auto d = diffProjects(a, b, o);
    ASSERT_FALSE(d.ok());
    EXPECT_EQ(d.error().message.rfind("B: ", 0), 0u) << d.error().message;
    EXPECT_TRUE(diffProjects(a, b).ok()); // the project-level diff still works
}

} // namespace
} // namespace fos::project::test
