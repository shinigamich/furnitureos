// SPDX-License-Identifier: AGPL-3.0-only
// Multi-selection, grouped commands, move / rotate / mirror / array / align / distribute.
#include "fos/app/edit_tools.hpp"
#include "test_support.hpp"

#include "fos/furniture/panel.hpp"

namespace fos::app::test {
namespace {

using geometry::Transform;
using geometry::Vec3;

/// Horizontal 600 x 300 x 18 panel (pb18) with its minimum corner at `at`.
project::Item flatPanel(const std::string& id, Vec3 at,
                        furniture::PanelOrientation orientation = furniture::PanelOrientation::Horizontal) {
    auto panel = furniture::makePanel(domain::PartId(id + "/panel"), "Panel " + id,
                                      domain::MaterialId(project::defaults::kParticleboard18));
    panel.orientation = orientation;
    return project::Item{project::PanelItem{domain::AssemblyId(id), Transform::translation(at), std::move(panel)}};
}

geometry::Aabb boxOf(const Session& s, const std::string& id) {
    const auto b = itemBounds(*s.resolved(), id);
    EXPECT_TRUE(b.has_value()) << id;
    return b.value_or(geometry::Aabb{});
}

void expectBox(const geometry::Aabb& b, Vec3 min, Vec3 max) {
    EXPECT_NEAR(b.min.x, min.x, 1e-9);
    EXPECT_NEAR(b.min.y, min.y, 1e-9);
    EXPECT_NEAR(b.min.z, min.z, 1e-9);
    EXPECT_NEAR(b.max.x, max.x, 1e-9);
    EXPECT_NEAR(b.max.y, max.y, 1e-9);
    EXPECT_NEAR(b.max.z, max.z, 1e-9);
}

/// p1 at the origin -> box [0,600] x [0,300] x [0,18]; p2 at x = 800 -> [800,1400].
struct Panels {
    Session s;
    explicit Panels(std::vector<std::pair<std::string, Vec3>> list = {{"p1", {0, 0, 0}}, {"p2", {800, 0, 0}}}) {
        s.newProject();
        for (const auto& [id, at] : list) EXPECT_TRUE(s.execute(project::commands::addItem(flatPanel(id, at))).ok());
    }
    const project::Project& p() const { return s.project(); }
    const project::Resolved& r() const { return *s.resolved(); }
};

TEST(EditSelection, MultiSelectionNormalisesAndToggles) {
    Panels t({{"p1", {}}, {"p2", {800, 0, 0}}, {"p3", {1600, 0, 0}}});
    t.s.selectItems({"nope", "p2", "p1", "p2"});
    // Unknown dropped, the first existing id is primary, duplicates removed.
    EXPECT_EQ(t.s.selection().itemId, "p2");
    EXPECT_EQ(t.s.selection().extraItemIds, std::vector<std::string>{"p1"});
    EXPECT_EQ(t.s.selection().allItems(), (std::vector<std::string>{"p2", "p1"}));
    t.s.toggleItemSelected("p3");
    EXPECT_EQ(t.s.selection().allItems(), (std::vector<std::string>{"p2", "p1", "p3"}));
    t.s.toggleItemSelected("p2"); // removing the primary promotes the next one
    EXPECT_EQ(t.s.selection().itemId, "p1");
    EXPECT_EQ(t.s.selection().allItems(), (std::vector<std::string>{"p1", "p3"}));
    // Deleting an extra item prunes it from the selection.
    ASSERT_TRUE(t.s.execute(project::commands::removeItem("p3")).ok());
    EXPECT_EQ(t.s.selection().allItems(), std::vector<std::string>{"p1"});
    t.s.selectItem("p2");
    EXPECT_TRUE(t.s.selection().extraItemIds.empty());
}

TEST(EditTools, RotationAboutIsExactForQuarterTurns) {
    const auto r = rotationAbout(Axis::Z, 90, {0, 0, 0});
    // CCW about Z: X -> Y, Y -> -X, exactly.
    EXPECT_EQ(r.xAxis, (Vec3{0, 1, 0}));
    EXPECT_EQ(r.yAxis, (Vec3{-1, 0, 0}));
    const auto h = rotationAbout(Axis::Y, -180, {10, 0, 0});
    EXPECT_EQ(h.xAxis, (Vec3{-1, 0, 0}));
    // Point (0,0,0) rotated 180 about the Y axis through x = 10 -> (20, 0, 0).
    EXPECT_EQ(h.applyPoint({0, 0, 0}), (Vec3{20, 0, 0}));
    const auto a = rotationAbout(Axis::Z, 30, {});
    EXPECT_NEAR(a.xAxis.x, std::sqrt(3.0) / 2, 1e-12);
    EXPECT_NEAR(a.xAxis.y, 0.5, 1e-12);
}

TEST(EditTools, TranslateSeveralItemsIsOneUndoStep) {
    Panels t;
    const auto undoBefore = t.s.document().undoCount();
    ASSERT_TRUE(applyItemsCommand(t.s, translateItemsCommand(t.p(), {"p1", "p2"}, {10, 20, 30})).ok());
    EXPECT_EQ(t.s.document().undoCount(), undoBefore + 1);
    expectBox(boxOf(t.s, "p1"), {10, 20, 30}, {610, 320, 48});
    expectBox(boxOf(t.s, "p2"), {810, 20, 30}, {1410, 320, 48});
    EXPECT_EQ(t.s.document().undoLabel()->key, "Move {0} items");
    ASSERT_TRUE(t.s.undo().ok());
    expectBox(boxOf(t.s, "p1"), {0, 0, 0}, {600, 300, 18});
    expectBox(boxOf(t.s, "p2"), {800, 0, 0}, {1400, 300, 18});
    // Errors: empty list, repeated id, unknown id, non-finite delta.
    EXPECT_FALSE(translateItemsCommand(t.p(), {}, {1, 0, 0}).ok());
    EXPECT_FALSE(translateItemsCommand(t.p(), {"p1", "p1"}, {1, 0, 0}).ok());
    EXPECT_EQ(translateItemsCommand(t.p(), {"zz"}, {1, 0, 0}).error().code, core::ErrorCode::NotFound);
    EXPECT_FALSE(translateItemsCommand(t.p(), {"p1"}, {std::nan(""), 0, 0}).ok());
}

TEST(EditTools, GroupFailureChangesNothing) {
    Panels t;
    std::vector<project::Command> list{project::commands::setPlacement("p1", Transform::translation({5, 0, 0})),
                                       project::commands::removeItem("missing")};
    EXPECT_FALSE(t.s.execute(groupCommands({"x", {}}, std::move(list))).ok());
    expectBox(boxOf(t.s, "p1"), {0, 0, 0}, {600, 300, 18});
}

TEST(EditTools, MoveToAbsolutePositionMovesTheGroupRigidly) {
    Panels t;
    // Union min (0,0,0) -> (100,50,700): delta (100,50,700) for both.
    ASSERT_TRUE(applyItemsCommand(t.s, moveItemsToCommand(t.p(), t.r(), {"p1", "p2"}, {100, 50, 700})).ok());
    expectBox(boxOf(t.s, "p1"), {100, 50, 700}, {700, 350, 718});
    expectBox(boxOf(t.s, "p2"), {900, 50, 700}, {1500, 350, 718});
}

TEST(EditTools, RotateAboutZAndXThroughTheBoxCentre) {
    Panels t;
    // p1 box [0,600]x[0,300]x[0,18], centre (300,150,9). +90 about Z swaps the XY extents about
    // the centre: x in [300-150, 300+150] = [150,450], y in [150-300, 150+300] = [-150,450].
    ASSERT_TRUE(applyItemsCommand(t.s, rotateItemsCommand(t.p(), t.r(), {"p1"}, Axis::Z, 90)).ok());
    expectBox(boxOf(t.s, "p1"), {150, -150, 0}, {450, 450, 18});
    ASSERT_TRUE(t.s.undo().ok());
    // +90 about X: Y and Z extents swap about (150, 9): y in [150-9,150+9], z in [9-150, 9+150].
    ASSERT_TRUE(applyItemsCommand(t.s, rotateItemsCommand(t.p(), t.r(), {"p1"}, Axis::X, 90)).ok());
    expectBox(boxOf(t.s, "p1"), {0, 141, -141}, {600, 159, 159});
    // Cabinets rotate only about Z.
    ASSERT_TRUE(t.s.addFromTemplate(TemplateKind::BaseCabinet).ok());
    const std::string cab = t.s.selection().itemId;
    const auto refused = rotateItemsCommand(t.p(), t.r(), {cab}, Axis::Y, 90);
    ASSERT_FALSE(refused.ok());
    EXPECT_NE(refused.error().message.find("cabinet"), std::string::npos);
    EXPECT_TRUE(rotateItemsCommand(t.p(), t.r(), {cab}, Axis::Z, 180).ok());
}

TEST(EditTools, MirrorAcrossTheSelectionCentre) {
    Panels t;
    // Give p1 a band on W1 (x = 0 end) and L1 (front, y = 0).
    auto def = t.p().findItem("p1")->panel()->panel;
    def.edges[static_cast<std::size_t>(domain::EdgeSide::W1)] = domain::EdgeBandId(project::defaults::kAbs2);
    def.edges[static_cast<std::size_t>(domain::EdgeSide::L1)] = domain::EdgeBandId(project::defaults::kAbs04);
    ASSERT_TRUE(t.s.execute(project::commands::setPanel("p1", def)).ok());
    const auto steps = t.s.document().undoCount();
    // Union X [0,1400], centre 700. p1 [0,600] -> [1400-600, 1400-0] = [800,1400];
    // p2 [800,1400] -> [0,600].
    auto plan = mirrorItemsCommand(t.p(), t.r(), {"p1", "p2"}, Axis::X);
    ASSERT_TRUE(plan.ok());
    ASSERT_EQ(plan.value().newIds, (std::vector<std::string>{"p3", "p4"}));
    EXPECT_TRUE(plan.value().notReflected.empty());
    ASSERT_TRUE(applyCopyPlan(t.s, plan).ok());
    EXPECT_EQ(t.s.document().undoCount(), steps + 1);
    EXPECT_EQ(t.s.selection().allItems(), (std::vector<std::string>{"p3", "p4"}));
    expectBox(boxOf(t.s, "p3"), {800, 0, 0}, {1400, 300, 18});
    expectBox(boxOf(t.s, "p4"), {0, 0, 0}, {600, 300, 18});
    // Mirrored along the length: the W1 band moves to W2; L1 stays.
    const auto& e = t.p().findItem("p3")->panel()->panel.edges;
    EXPECT_FALSE(e[static_cast<std::size_t>(domain::EdgeSide::W1)].has_value());
    EXPECT_EQ(e[static_cast<std::size_t>(domain::EdgeSide::W2)]->str(), project::defaults::kAbs2);
    EXPECT_EQ(e[static_cast<std::size_t>(domain::EdgeSide::L1)]->str(), project::defaults::kAbs04);
    EXPECT_EQ(t.p().findItem("p3")->panel()->panel.id.str(), "p3/panel");
    ASSERT_TRUE(t.s.undo().ok());
    EXPECT_EQ(t.p().items.size(), 2u);

    // Across the XZ plane (normal Y) of p1 alone: centre y 150, box maps onto itself, L1 <-> L2.
    auto alone = mirrorItemsCommand(t.p(), t.r(), {"p1"}, Axis::Y);
    ASSERT_TRUE(applyCopyPlan(t.s, alone).ok());
    const auto& f = t.p().findItem(alone.value().newIds[0])->panel()->panel.edges;
    EXPECT_EQ(f[static_cast<std::size_t>(domain::EdgeSide::L2)]->str(), project::defaults::kAbs04);
    EXPECT_FALSE(f[static_cast<std::size_t>(domain::EdgeSide::L1)].has_value());
    expectBox(boxOf(t.s, alone.value().newIds[0]), {0, 0, 0}, {600, 300, 18});

    // A cabinet is only placed mirrored (reported as not reflected).
    ASSERT_TRUE(t.s.addFromTemplate(TemplateKind::BaseCabinet).ok());
    const std::string cab = t.s.selection().itemId;
    auto cabPlan = mirrorItemsCommand(t.p(), t.r(), {cab}, Axis::X);
    ASSERT_TRUE(cabPlan.ok());
    EXPECT_EQ(cabPlan.value().notReflected, cabPlan.value().newIds);
}

TEST(EditTools, LinearArrayCreatesCountCopiesInOneStep) {
    Panels t({{"p1", {}}});
    const auto steps = t.s.document().undoCount();
    auto plan = arrayItemsCommand(t.p(), {"p1"}, 3, {0, 0, 400});
    ASSERT_TRUE(plan.ok());
    ASSERT_TRUE(applyCopyPlan(t.s, plan).ok());
    EXPECT_EQ(t.s.document().undoCount(), steps + 1);
    ASSERT_EQ(plan.value().newIds.size(), 3u);
    // Copy k at z = 400 k.
    for (int k = 1; k <= 3; ++k)
        expectBox(boxOf(t.s, plan.value().newIds[static_cast<std::size_t>(k - 1)]), {0, 0, 400.0 * k},
                  {600, 300, 400.0 * k + 18});
    EXPECT_EQ(t.p().findItem(plan.value().newIds[1])->name(), "Panel p1 3");
    ASSERT_TRUE(t.s.undo().ok());
    EXPECT_EQ(t.p().items.size(), 1u);
    EXPECT_FALSE(arrayItemsCommand(t.p(), {"p1"}, 0, {1, 0, 0}).ok());
    EXPECT_FALSE(arrayItemsCommand(t.p(), {"p1"}, 501, {1, 0, 0}).ok());
    EXPECT_FALSE(arrayItemsCommand(t.p(), {"p1"}, 2, {0, 0, 0}).ok());
}

TEST(EditTools, AlignToTheFirstSelected) {
    Panels t({{"p1", {0, 0, 0}}, {"p2", {800, 40, 250}}});
    ASSERT_TRUE(applyItemsCommand(t.s, alignItemsCommand(t.p(), t.r(), {"p1", "p2"}, AlignEdge::Bottom)).ok());
    expectBox(boxOf(t.s, "p2"), {800, 40, 0}, {1400, 340, 18});
    // Right: p2 max x 1400 -> 600 (p1's max x), so min x 0.
    ASSERT_TRUE(applyItemsCommand(t.s, alignItemsCommand(t.p(), t.r(), {"p1", "p2"}, AlignEdge::Right)).ok());
    expectBox(boxOf(t.s, "p2"), {0, 40, 0}, {600, 340, 18});
    // Back: p1 max y 300 -> p2 max y 340 (p2 first = reference).
    ASSERT_TRUE(applyItemsCommand(t.s, alignItemsCommand(t.p(), t.r(), {"p2", "p1"}, AlignEdge::Back)).ok());
    expectBox(boxOf(t.s, "p1"), {0, 40, 0}, {600, 340, 18});
    // Already aligned: no undo step.
    const auto steps = t.s.document().undoCount();
    ASSERT_TRUE(applyItemsCommand(t.s, alignItemsCommand(t.p(), t.r(), {"p2", "p1"}, AlignEdge::Front)).ok());
    EXPECT_EQ(t.s.document().undoCount(), steps);
    EXPECT_FALSE(alignItemsCommand(t.p(), t.r(), {"p1"}, AlignEdge::Left).ok());
}

TEST(EditTools, DistributeWithEqualGaps) {
    Panels t({{"p1", {0, 0, 0}}, {"p3", {2000, 0, 0}}, {"p2", {700, 0, 0}}});
    // Sorted by min x: p1 [0,600], p2 [700,1300], p3 [2000,2600]. Span 2600, sizes 1800,
    // gap (2600 - 1800) / 2 = 400 -> p2 min x = 600 + 400 = 1000.
    ASSERT_TRUE(applyItemsCommand(t.s, distributeItemsCommand(t.p(), t.r(), {"p1", "p3", "p2"}, Axis::X)).ok());
    expectBox(boxOf(t.s, "p1"), {0, 0, 0}, {600, 300, 18});
    expectBox(boxOf(t.s, "p2"), {1000, 0, 0}, {1600, 300, 18});
    expectBox(boxOf(t.s, "p3"), {2000, 0, 0}, {2600, 300, 18});
    // Not fitting: p3 moved to x = 1100 -> order p1 (0), p2 (1000), p3 (1100); span 1700,
    // gap (1700 - 1800) / 2 = -50: p2 min = 600 - 50 = 550 (equal overlaps).
    ASSERT_TRUE(applyItemsCommand(t.s, translateItemsCommand(t.p(), {"p3"}, {-900, 0, 0})).ok());
    ASSERT_TRUE(applyItemsCommand(t.s, distributeItemsCommand(t.p(), t.r(), {"p1", "p2", "p3"}, Axis::X)).ok());
    expectBox(boxOf(t.s, "p2"), {550, 0, 0}, {1150, 300, 18});
    EXPECT_FALSE(distributeItemsCommand(t.p(), t.r(), {"p1", "p2"}, Axis::X).ok());
}

TEST(EditTools, DuplicateAndDeleteSelectedItems) {
    Panels t;
    t.s.selectItems({"p1", "p2"});
    const auto steps = t.s.document().undoCount();
    const auto copies = duplicateSelectedItems(t.s);
    ASSERT_TRUE(copies.ok());
    EXPECT_EQ(t.s.document().undoCount(), steps + 1);
    // Group [0,1400] shifted to start at the right edge 1400: +1400.
    expectBox(boxOf(t.s, copies.value()[0]), {1400, 0, 0}, {2000, 300, 18});
    expectBox(boxOf(t.s, copies.value()[1]), {2200, 0, 0}, {2800, 300, 18});
    EXPECT_EQ(t.s.selection().allItems(), copies.value());
    ASSERT_TRUE(deleteSelectedItems(t.s).ok());
    EXPECT_EQ(t.s.document().undoCount(), steps + 2);
    EXPECT_EQ(t.p().items.size(), 2u);
    EXPECT_TRUE(t.s.selection().allItems().empty());
    ASSERT_TRUE(t.s.undo().ok());
    EXPECT_EQ(t.p().items.size(), 4u);
    // Nudge: arrow keys in the plan.
    t.s.selectItems({"p1"});
    ASSERT_TRUE(nudgeSelectedItems(t.s, {10, 0, 0}).ok());
    expectBox(boxOf(t.s, "p1"), {10, 0, 0}, {610, 300, 18});
    t.s.setSelection({});
    EXPECT_FALSE(deleteSelectedItems(t.s).ok());
}

} // namespace
} // namespace fos::app::test
