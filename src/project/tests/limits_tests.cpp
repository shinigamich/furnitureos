// SPDX-License-Identifier: AGPL-3.0-only
// Hostile / absurd project files are rejected on load with specific messages, quickly; the
// same limits hold for documents edited through commands (limits.hpp, furniture/limits.hpp).
#include "fos/furniture/limits.hpp"
#include "fos/project/codec.hpp"
#include "fos/project/commands.hpp"
#include "fos/project/limits.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>

#include <chrono>
#include <functional>

namespace fos::project::test {
using ::testing::HasSubstr;

namespace {

Project oneCabinet() {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem());
    return p;
}

/// File text of `p` after `edit` changed its payload JSON (as an attacker would), with a
/// valid checksum.
std::string hostile(const Project& p, const std::function<void(formats::Json&)>& edit) {
    formats::Document d = toDocument(p, "2026-01-01T00:00:00Z");
    d.payload = projectToJson(p);
    edit(d.payload);
    return formats::serializeDocument(d).value();
}

struct Outcome {
    bool ok = false;
    std::string message;
    double seconds = 0;
};
Outcome load(const std::string& text) {
    const auto t0 = std::chrono::steady_clock::now();
    auto r = parseProject(text, {}, "hostile.fosp");
    Outcome o;
    o.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    o.ok = r.ok();
    if (!r) o.message = r.error().message;
    return o;
}

furniture::Section leaf(int shelves = 0) {
    furniture::Section s;
    s.shelfCount = shelves;
    return s;
}

/// A cabinet item of 50 columns of 30-drawer stacks: furniture::estimatePartCount =
/// 8 carcass + 49 partitions + 1500 fronts = 1557.
Item drawerWall(const std::string& id) {
    Item item = baseCabinetItem(id);
    furniture::Section root;
    root.split = furniture::SplitDirection::Vertical;
    furniture::Section column;
    furniture::FrontSpec f;
    f.kind = furniture::FrontKind::DrawerStack;
    f.drawerHeights.assign(30, furniture::SectionSize::weight());
    column.front = f;
    root.children.assign(50, column);
    item.cabinet()->interior = root;
    return item;
}

} // namespace

TEST(ProjectLimits, HostileCabinetFilesAreRejectedOnLoadQuickly) {
    const Project p = oneCabinet();
    ASSERT_TRUE(load(hostile(p, [](formats::Json&) {})).ok);

    // The reviewer's file: shelfCount 2000 (11 s to load before) / 10000 (> 120 s).
    for (const int n : {2000, 10000}) {
        const Outcome o = load(hostile(p, [n](formats::Json& j) { j["items"][0]["interior"]["shelfCount"] = n; }));
        EXPECT_FALSE(o.ok);
        EXPECT_THAT(o.message, HasSubstr("/payload/items/0/interior/shelfCount: expected an integer from 0 to 100, got " +
                                         std::to_string(n)));
        EXPECT_LT(o.seconds, 1.0);
    }
    // 51 children are refused before any of them is decoded.
    const Outcome children = load(hostile(p, [](formats::Json& j) {
        furniture::Section root;
        root.split = furniture::SplitDirection::Vertical;
        root.children.assign(51, leaf());
        j["items"][0]["interior"] = toJson(root);
    }));
    EXPECT_THAT(children.message, HasSubstr("/payload/items/0/interior/children: has 51 child sections; the limit is 50"));
    // Total shelves of a cabinet (3 x 100 > 200) come from the furniture limit check.
    const Outcome shelves = load(hostile(p, [](formats::Json& j) {
        furniture::Section root;
        root.split = furniture::SplitDirection::Vertical;
        root.children.assign(3, leaf(100));
        j["items"][0]["interior"] = toJson(root);
    }));
    EXPECT_THAT(shelves.message, HasSubstr("/payload/items/0: The sections of cabinet 'cab1' hold more than 200 adjustable "
                                           "shelves together"));
    const Outcome drawers = load(hostile(p, [](formats::Json& j) {
        auto& front = j["items"][0]["interior"]["front"];
        front["kind"] = "DrawerStack";
        front["drawerHeights"] = formats::Json::array();
        for (int i = 0; i < 31; ++i) front["drawerHeights"].push_back({{"kind", "Weight"}, {"value", 1}});
    }));
    EXPECT_THAT(drawers.message,
                HasSubstr("/payload/items/0/interior/front/drawerHeights: has 31 drawer heights; the limit is 30"));
    // Absurd dimensions: height = 1e308 used to give parts of infinite length.
    const Outcome huge = load(hostile(p, [](formats::Json& j) {
        for (auto& prm : j["items"][0]["parameters"])
            if (prm["name"] == "height") prm["value"] = 1e308;
    }));
    EXPECT_FALSE(huge.ok);
    EXPECT_THAT(huge.message, HasSubstr("Parameter 'height' of cabinet 'cab1' is 1e+308 mm; lengths are limited to "
                                        "100000 mm."));
}

TEST(ProjectLimits, BackupCountIsCappedInFilesCommandsAndSaves) {
    Project p = oneCabinet();
    // A file with backupCount 1e6 used to make every save probe 1e6 (1e9: 1e9) backup names.
    const Outcome o = load(hostile(p, [](formats::Json& j) { j["settings"]["backupCount"] = 1000000; }));
    EXPECT_FALSE(o.ok);
    EXPECT_THAT(o.message, HasSubstr("/payload/settings/backupCount: expected an integer from 0 to 100, got 1000000"));
    EXPECT_TRUE(load(hostile(p, [](formats::Json& j) { j["settings"]["backupCount"] = 100; })).ok);

    p.settings.backupCount = 101;
    auto text = serializeProject(p, "2026-01-01T00:00:00Z");
    ASSERT_FALSE(text.ok());
    // saveProject / serializeProject round-trip through the codec, which refuses it.
    EXPECT_THAT(text.error().message, HasSubstr("would not load back: /payload/settings/backupCount: expected an "
                                                "integer from 0 to 100, got 101"));

    Document doc(oneCabinet());
    ProjectSettings s;
    s.backupCount = 101;
    auto r = doc.execute(commands::setSettings(s));
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("from 0 to 100 (is 101)"));

    // Rotation through saveProject: 2 backups kept, newest first.
    TempDir dir;
    Project q = oneCabinet();
    q.settings.backupCount = 2;
    const auto file = dir.path / "k.fosp";
    for (int rev = 1; rev <= 4; ++rev) {
        q.metadata.description = "rev " + std::to_string(rev);
        ASSERT_TRUE(saveProject(file, q, fixedClock()).ok());
    }
    EXPECT_EQ(loadProject(formats::backupPath(file, 1)).value().project.metadata.description, "rev 3");
    EXPECT_EQ(loadProject(formats::backupPath(file, 2)).value().project.metadata.description, "rev 2");
    EXPECT_FALSE(std::filesystem::exists(formats::backupPath(file, 3)));
}

TEST(ProjectLimits, ItemCountAndEstimatedPartsAreLimited) {
    // 501 items in a file are refused before any item is decoded.
    Project p = oneCabinet();
    const Outcome items = load(hostile(p, [](formats::Json& j) {
        const formats::Json first = j["items"][0];
        for (int i = 1; i <= 500; ++i) {
            formats::Json copy = first;
            copy["id"] = "c" + std::to_string(i);
            j["items"].push_back(copy);
        }
    }));
    EXPECT_THAT(items.message, HasSubstr("/payload/items: has 501 furniture items; the limit is 500"));

    // Through commands: a project at the item limit refuses one more item.
    Project full = newProject(meta());
    for (std::size_t i = 0; i < limits::kMaxItems; ++i) {
        Item panel;
        PanelItem pi;
        pi.id = domain::AssemblyId("p" + std::to_string(i));
        pi.panel = furniture::makePanel(domain::PartId("p" + std::to_string(i) + "/panel"), "Panel",
                                        domain::MaterialId(defaults::kParticleboard18));
        panel.definition = pi;
        full.items.push_back(panel);
    }
    ASSERT_TRUE(validateStructure(full).ok());
    Document doc(full);
    auto add = doc.execute(commands::addItem(baseCabinetItem("one-more")));
    ASSERT_FALSE(add.ok());
    EXPECT_THAT(add.error().message, HasSubstr("The project has 501 furniture items; the limit is 500 items per project."));

    // Estimated parts: 12 drawer walls = 12 * 1557 = 18684 <= 20000; a 13th = 20241 > 20000.
    Project big = newProject(meta());
    for (int i = 0; i < 12; ++i) big.items.push_back(drawerWall("w" + std::to_string(i)));
    EXPECT_EQ(estimatePartCount(big), 18684u);
    EXPECT_TRUE(checkProjectLimits(big).ok());
    big.items.push_back(drawerWall("w12"));
    auto refused = validateStructure(big);
    ASSERT_FALSE(refused.ok());
    EXPECT_THAT(refused.error().message,
                HasSubstr("would generate up to 20241 parts; the limit is 20000 parts per project."));
}

TEST(ProjectLimits, CabinetEditCommandsAreRefusedBeyondTheFurnitureLimits) {
    Document doc(oneCabinet());
    auto r = doc.execute(commands::setShelfCount("cab1", "", 101));
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("has 101 adjustable shelves; the limit is 100 per section."));
    auto w = doc.execute(commands::setParameter("cab1", "width", 1e308));
    ASSERT_FALSE(w.ok());
    EXPECT_THAT(w.error().message, HasSubstr("lengths are limited to 100000 mm"));
    EXPECT_EQ(doc.undoCount(), 0u);
}

} // namespace fos::project::test
