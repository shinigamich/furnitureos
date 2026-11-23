// SPDX-License-Identifier: AGPL-3.0-only
// Plinth builder (plinth.hpp): height from the feet, setback, splitting, end returns and the
// interruption at appliance (dishwasher) end panels.
#include "test_support.hpp"

#include "fos/furniture/edit.hpp"
#include "fos/furniture/parameters.hpp"
#include "fos/furniture/templates.hpp"
#include "fos/project/plinth.hpp"

namespace fos::project::test {
namespace {

/// Base cabinet (600 wide) switched to Feet, placed at x = `x`.
Item feetCabinet(const Project& p, const std::string& id, double x) {
    Item item = baseCabinetItem(id);
    furniture::ConstructionOptions o = item.cabinet()->construction;
    o.plinth = furniture::PlinthKind::Feet;
    EXPECT_TRUE(furniture::setConstruction(*item.cabinet(), p.libraries, o).ok());
    item.cabinet()->placement = geometry::Transform::translation({x, 0, 0});
    return item;
}

Project feetKitchen(int count) {
    Project p = newProject(meta());
    for (int i = 0; i < count; ++i) p.items.push_back(feetCabinet(p, "cab" + std::to_string(i + 1), 600.0 * i));
    return p;
}

std::vector<std::string> ids(int count) {
    std::vector<std::string> v;
    for (int i = 0; i < count; ++i) v.push_back("cab" + std::to_string(i + 1));
    return v;
}

double plinthHeightOf(const Project& p, const std::string& id) {
    return p.findItem(id)->cabinet()->parameters.value(furniture::param::kPlinthHeight).value();
}

/// Bounds of the generated part "<item>/panel" in the project frame.
geometry::Aabb panelBox(const Project& p, const std::string& item) {
    const Resolved r = resolved(p);
    const domain::Part* part = findPart(r, item + "/panel");
    EXPECT_NE(part, nullptr) << item;
    if (part == nullptr) return {};
    return part->bounds().transformed(p.findItem(item)->placement());
}

TEST(ProjectPlinth, SinglePieceUnderThreeCabinets) {
    Project p = feetKitchen(3);
    const double h = plinthHeightOf(p, "cab1"); // template value, independent of the builder
    PlinthRequest req;
    req.itemIds = ids(3);
    auto plan = addPlinth(p, req);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    const PlinthPlan& w = plan.value();
    // Run x 0 .. 1800 (3 x 600), carcass fronts at y = 0, default setback 50, carcass board pb18.
    EXPECT_NEAR(w.height, h, 1e-9);
    EXPECT_NEAR(w.length, 1800, 1e-9);
    EXPECT_NEAR(w.front, 50, 1e-9);
    EXPECT_EQ(w.thickness, 18.0);
    ASSERT_EQ(w.items.size(), 1u);
    EXPECT_EQ(w.items.front().id(), "plinth");
    EXPECT_EQ(w.runOrder, ids(3));
    const geometry::Aabb b = panelBox(p, "plinth");
    // VerticalFront: x 0 .. 1800, y 50 .. 50 + 18, z 0 .. h.
    EXPECT_NEAR(b.min.x, 0, 1e-9);
    EXPECT_NEAR(b.max.x, 1800, 1e-9);
    EXPECT_NEAR(b.min.y, 50, 1e-9);
    EXPECT_NEAR(b.max.y, 68, 1e-9);
    EXPECT_NEAR(b.min.z, 0, 1e-9);
    EXPECT_NEAR(b.max.z, h, 1e-9);
    const Resolved r = resolved(p);
    EXPECT_EQ(findPart(r, "plinth/panel")->role, domain::PartRole::Plinth);
}

TEST(ProjectPlinth, SplitIntoEqualPiecesWithReturns) {
    Project p = feetKitchen(5);
    const geometry::Aabb cab = generateItem(p, "cab1").value().bounds();
    PlinthRequest req;
    req.itemIds = ids(5);
    req.setback = 40;
    req.height = 90;
    req.maxPieceLength = 1100; // 3000 / 1100 = 2.7 -> 3 equal pieces of 1000
    req.returnLeft = req.returnRight = true;
    auto plan = addPlinth(p, req);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    const PlinthPlan& w = plan.value();
    ASSERT_EQ(w.items.size(), 5u);
    EXPECT_EQ(w.frontPieces, 3u);
    EXPECT_EQ(w.items[0].id(), "plinth.1");
    EXPECT_EQ(w.items[3].id(), "plinth.left");
    EXPECT_EQ(w.items[4].id(), "plinth.right");
    const geometry::Aabb mid = panelBox(p, "plinth.2");
    EXPECT_NEAR(mid.min.x, 1000, 1e-9);
    EXPECT_NEAR(mid.max.x, 2000, 1e-9);
    EXPECT_NEAR(mid.max.z, 90, 1e-9);
    int joints = 0;
    for (const auto& d : w.notes) joints += d.code == "project.plinth.butt_joint";
    EXPECT_EQ(joints, 2);
    // Left return: x 0 .. 18, y from the plinth back face 40 + 18 = 58 to the cabinet back.
    const geometry::Aabb left = panelBox(p, "plinth.left");
    EXPECT_NEAR(left.min.x, 0, 1e-9);
    EXPECT_NEAR(left.max.x, 18, 1e-9);
    EXPECT_NEAR(left.min.y, 58, 1e-9);
    EXPECT_NEAR(left.max.y, cab.max.y, 1e-9);
    EXPECT_NEAR(left.max.z, 90, 1e-9);
    // Right return: x 3000 - 18 = 2982 .. 3000.
    const geometry::Aabb right = panelBox(p, "plinth.right");
    EXPECT_NEAR(right.min.x, 2982, 1e-9);
    EXPECT_NEAR(right.max.x, 3000, 1e-9);
}

TEST(ProjectPlinth, InterruptedAtDishwasherEndPanel) {
    Project p = newProject(meta());
    p.items.push_back(feetCabinet(p, "cab1", 0));
    Item dw;
    dw.definition = furniture::templates::dishwasherFront(domain::AssemblyId("dw"), defaultTemplateMaterials());
    dw.cabinet()->placement = geometry::Transform::translation({600, 0, 0});
    p.items.push_back(dw);
    // Dishwasher: width 618 (600 gap + 18 end panel on the RIGHT, x 600 + 600 = 1200 .. 1218).
    p.items.push_back(feetCabinet(p, "cab3", 1218));
    PlinthRequest req;
    req.itemIds = {"cab1", "dw", "cab3"};
    auto plan = planPlinth(p, req);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    const PlinthPlan& w = plan.value();
    // Segments [0, 1200] (cab1 + the strip in front of the dishwasher) and [1218, 1818].
    ASSERT_EQ(w.pieces.size(), 2u);
    EXPECT_NEAR(w.pieces[0].first, 0, 1e-9);
    EXPECT_NEAR(w.pieces[0].second, 1200, 1e-9);
    EXPECT_NEAR(w.pieces[1].first, 1218, 1e-9);
    EXPECT_NEAR(w.pieces[1].second, 1818, 1e-9);
    bool appliance = false, joint = false;
    for (const auto& d : w.notes) {
        appliance |= d.code == "project.plinth.appliance" && d.entityId == "plinth.1";
        joint |= d.code == "project.plinth.butt_joint";
    }
    EXPECT_TRUE(appliance);
    EXPECT_FALSE(joint); // the pieces are separated by the end panel, not joined
    // A return at an end closed by an appliance end panel is refused.
    Project q = newProject(meta());
    q.items.push_back(feetCabinet(q, "cab1", 0));
    dw.cabinet()->placement = geometry::Transform::translation({600, 0, 0});
    q.items.push_back(dw);
    PlinthRequest r2;
    r2.itemIds = {"cab1", "dw"};
    r2.returnRight = true;
    auto refused = planPlinth(q, r2);
    ASSERT_FALSE(refused.ok());
    EXPECT_NE(refused.error().message.find("appliance gap \"dw\""), std::string::npos) << refused.error().message;
}

TEST(ProjectPlinth, Refusals) {
    Project p = feetKitchen(2);
    PlinthRequest req;
    req.itemIds = ids(2);
    req.height = plinthHeightOf(p, "cab1") + 5; // taller than the feet
    EXPECT_FALSE(planPlinth(p, req).ok());
    req.height.reset();
    req.setback = 400;
    EXPECT_FALSE(planPlinth(p, req).ok());
    req.setback = 50;
    req.materialId = "mat.none";
    EXPECT_EQ(planPlinth(p, req).error().code, core::ErrorCode::NotFound);
    req.materialId.clear();
    // Different feet heights (100 vs 120).
    Project q = p;
    ASSERT_TRUE(furniture::setParameter(*q.findItem("cab2")->cabinet(), q.libraries, furniture::param::kPlinthHeight, 120).ok());
    auto diff = planPlinth(q, req);
    ASSERT_FALSE(diff.ok());
    EXPECT_NE(diff.error().message.find("different heights"), std::string::npos) << diff.error().message;
    // Sides to the floor (the template default) has its own plinth panel.
    Project s = newProject(meta());
    s.items.push_back(baseCabinetItem("cab1"));
    PlinthRequest r1;
    r1.itemIds = {"cab1"};
    auto sides = planPlinth(s, r1);
    ASSERT_FALSE(sides.ok());
    EXPECT_NE(sides.error().message.find("Feet"), std::string::npos);
    // Id in use.
    ASSERT_TRUE(addPlinth(p, req).ok());
    EXPECT_EQ(planPlinth(p, req).error().code, core::ErrorCode::AlreadyExists);
}

TEST(ProjectPlinth, CommandIsUndoable) {
    Document doc(feetKitchen(2));
    PlinthRequest req;
    req.itemIds = ids(2);
    req.returnLeft = true;
    ASSERT_TRUE(doc.execute(commands::addPlinth(req)).ok());
    EXPECT_EQ(doc.project().items.size(), 4u);
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(doc.project().items.size(), 2u);
}

} // namespace
} // namespace fos::project::test
