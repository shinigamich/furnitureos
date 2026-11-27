// SPDX-License-Identifier: AGPL-3.0-only
// Add panel (placements, gap filling) and the item clipboard.
#include "fos/app/item_clipboard.hpp"
#include "fos/app/panel_tools.hpp"
#include "test_support.hpp"

#include "fos/furniture/panel.hpp"

namespace fos::app::test {
namespace {

using furniture::PanelOrientation;
using geometry::Vec3;

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

AddPanelRequest request(double length, double width, PanelOrientation o, PanelPlacement where = PanelPlacement::Origin,
                        std::vector<std::string> refs = {}) {
    AddPanelRequest r;
    r.length = length;
    r.width = width;
    r.orientation = o;
    r.materialId = project::defaults::kParticleboard18;
    r.placement = where;
    r.referenceIds = std::move(refs);
    return r;
}

TEST(PanelTools, BoxSizeByOrientation) {
    EXPECT_EQ(panelBoxSize(PanelOrientation::Horizontal, 600, 300, 18), (Vec3{600, 300, 18}));
    EXPECT_EQ(panelBoxSize(PanelOrientation::VerticalSide, 720, 560, 18), (Vec3{18, 560, 720}));
    EXPECT_EQ(panelBoxSize(PanelOrientation::VerticalFront, 800, 100, 18), (Vec3{800, 18, 100}));
}

TEST(PanelTools, AddAtOriginWithEdgesIsOneUndoStep) {
    Session s;
    s.newProject();
    auto r = request(800, 400, PanelOrientation::Horizontal);
    r.name = "Shelf";
    r.edges[0] = project::defaults::kAbs2;
    const auto id = applyAddPanel(s, r);
    ASSERT_TRUE(id.ok()) << id.error().message;
    EXPECT_EQ(id.value(), "panel1");
    EXPECT_EQ(s.selection().itemId, "panel1");
    EXPECT_EQ(s.document().undoCount(), 1u);
    EXPECT_EQ(s.document().undoLabel()->key, "Add panel {0}");
    expectBox(boxOf(s, "panel1"), {0, 0, 0}, {800, 400, 18});
    const domain::Part* part = s.findPart("panel1/panel");
    ASSERT_NE(part, nullptr);
    EXPECT_NEAR(part->length, 800, 1e-9);
    EXPECT_NEAR(part->width, 400, 1e-9);
    EXPECT_NEAR(part->thickness, 18, 1e-9);
    EXPECT_EQ(s.project().findItem("panel1")->name(), "Shelf");
    ASSERT_TRUE(s.undo().ok());
    EXPECT_TRUE(s.project().items.empty());
    // Errors.
    auto bad = request(800, 400, PanelOrientation::Horizontal);
    bad.materialId = "mat.none";
    EXPECT_EQ(applyAddPanel(s, bad).error().code, core::ErrorCode::NotFound);
    bad = request(0, 400, PanelOrientation::Horizontal);
    EXPECT_FALSE(applyAddPanel(s, bad).ok());
    bad = request(800, 400, PanelOrientation::Horizontal);
    bad.edges[2] = "eb.none";
    EXPECT_EQ(applyAddPanel(s, bad).error().code, core::ErrorCode::NotFound);
    EXPECT_FALSE(applyAddPanel(s, request(800, 400, PanelOrientation::Horizontal, PanelPlacement::OnTop)).ok());
}

TEST(PanelTools, PlacementsRelativeToTheSelection) {
    Session s;
    s.newProject();
    ASSERT_TRUE(applyAddPanel(s, request(600, 300, PanelOrientation::Horizontal)).ok()); // panel1 [0,600]x[0,300]x[0,18]
    const std::vector<std::string> ref{"panel1"};
    struct Case {
        PanelPlacement where;
        PanelOrientation o;
        Vec3 min, max;
    };
    const Case cases[] = {
        // On top: z starts at 18; 600 x 300 x 18 -> z [18,36].
        {PanelPlacement::OnTop, PanelOrientation::Horizontal, {0, 0, 18}, {600, 300, 36}},
        // Below: z = 0 - 18.
        {PanelPlacement::Below, PanelOrientation::Horizontal, {0, 0, -18}, {600, 300, 0}},
        // Left of, vertical side 600 x 300: box 18 x 300 x 600 -> x [-18,0].
        {PanelPlacement::LeftOf, PanelOrientation::VerticalSide, {-18, 0, 0}, {0, 300, 600}},
        {PanelPlacement::RightOf, PanelOrientation::VerticalSide, {600, 0, 0}, {618, 300, 600}},
        // In front, vertical front 600 x 300: box 600 x 18 x 300 -> y [-18,0].
        {PanelPlacement::InFront, PanelOrientation::VerticalFront, {0, -18, 0}, {600, 0, 300}},
        {PanelPlacement::Behind, PanelOrientation::VerticalFront, {0, 300, 0}, {600, 318, 300}},
        {PanelPlacement::AtSelection, PanelOrientation::Horizontal, {0, 0, 0}, {600, 300, 18}},
    };
    for (const auto& c : cases) {
        const auto id = applyAddPanel(s, request(600, 300, c.o, c.where, ref));
        ASSERT_TRUE(id.ok()) << id.error().message;
        expectBox(boxOf(s, id.value()), c.min, c.max);
        ASSERT_TRUE(s.undo().ok());
    }
}

TEST(PanelTools, FillTheGapBetweenTwoSides) {
    Session s;
    s.newProject();
    // Two sides 720 x 560: s1 box [0,18]x[0,560]x[0,720], s2 at x = 600 -> [600,618].
    auto side = request(720, 560, PanelOrientation::VerticalSide);
    ASSERT_TRUE(applyAddPanel(s, side).ok());
    ASSERT_TRUE(applyAddPanel(s, side).ok());
    ASSERT_TRUE(s.execute(project::commands::setPlacement("panel2", geometry::Transform::translation({600, 0, 0}))).ok());
    // Shelf: clear width 600 - 18 = 582 along X, depth 560, at height 300.
    auto shelf = request(1, 1, PanelOrientation::Horizontal, PanelPlacement::FillGap, {"panel1", "panel2"});
    shelf.thicknessOffset = 300;
    const auto plan = planAddPanel(s.project(), s.resolved(), shelf);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    EXPECT_NEAR(plan.value().length, 582, 1e-9);
    EXPECT_NEAR(plan.value().width, 560, 1e-9);
    const auto id = applyAddPanel(s, shelf);
    ASSERT_TRUE(id.ok());
    expectBox(boxOf(s, id.value()), {18, 0, 300}, {600, 560, 318});
    // A vertical divider in the same gap: thickness along X from 18 + 282 = 300; length = height 720.
    auto divider = request(1, 1, PanelOrientation::VerticalSide, PanelPlacement::FillGap, {"panel2", "panel1"});
    divider.thicknessOffset = 282;
    const auto d = applyAddPanel(s, divider);
    ASSERT_TRUE(d.ok()) << d.error().message;
    expectBox(boxOf(s, d.value()), {300, 0, 0}, {318, 560, 720});
    // Does not fit: offset 710 + 18 > 720.
    shelf.thicknessOffset = 710;
    const auto tooHigh = planAddPanel(s.project(), s.resolved(), shelf);
    ASSERT_FALSE(tooHigh.ok());
    EXPECT_NE(tooHigh.error().message.find("720"), std::string::npos);
    // One reference, or overlapping references.
    shelf.thicknessOffset = 0;
    shelf.referenceIds = {"panel1"};
    EXPECT_FALSE(planAddPanel(s.project(), s.resolved(), shelf).ok());
    shelf.referenceIds = {"panel1", d.value()};
    shelf.referenceIds = {id.value(), d.value()}; // shelf and divider intersect
    EXPECT_FALSE(planAddPanel(s.project(), s.resolved(), shelf).ok());
}

/// Session with one horizontal panel "p1" (600 x 300 x 18 at the origin).
struct OnePanel {
    Session s;
    OnePanel() {
        s.newProject();
        auto panel = furniture::makePanel(domain::PartId("p1/panel"), "Top",
                                          domain::MaterialId(project::defaults::kParticleboard18));
        EXPECT_TRUE(s.execute(project::commands::addItem(project::Item{project::PanelItem{
                                  domain::AssemblyId("p1"), geometry::Transform::identity(), std::move(panel)}}))
                        .ok());
    }
};

TEST(ItemClipboard, RoundTripAndPasteBesideTheOriginal) {
    OnePanel t;
    const auto text = itemsToClipboard(t.s.project(), {"p1"});
    ASSERT_TRUE(text.ok());
    const auto back = itemsFromClipboard(text.value());
    ASSERT_TRUE(back.ok()) << back.error().message;
    ASSERT_EQ(back.value().items.size(), 1u);
    EXPECT_EQ(back.value().items[0].name(), "Top");
    EXPECT_EQ(back.value().items[0].panel()->panel.material.str(), project::defaults::kParticleboard18);
    // Pasting into the same project overlaps the original: shifted to the right edge (+600).
    const auto steps = t.s.document().undoCount();
    const auto ids = pasteItems(t.s, text.value());
    ASSERT_TRUE(ids.ok()) << ids.error().message;
    EXPECT_EQ(t.s.document().undoCount(), steps + 1);
    ASSERT_EQ(ids.value(), std::vector<std::string>{"p2"});
    EXPECT_EQ(t.s.selection().allItems(), ids.value());
    expectBox(boxOf(t.s, "p2"), {600, 0, 0}, {1200, 300, 18});
    ASSERT_TRUE(t.s.undo().ok());
    EXPECT_EQ(t.s.project().items.size(), 1u);
}

TEST(ItemClipboard, CutAndPasteBetweenProjects) {
    OnePanel source;
    source.s.selectItem("p1");
    const auto text = cutSelectedItems(source.s);
    ASSERT_TRUE(text.ok());
    EXPECT_TRUE(source.s.project().items.empty());
    // Target project whose pb18 differs (19 mm): the pasted panel keeps its 18 mm material under a new id.
    Session target;
    target.newProject();
    auto mat = *target.project().libraries.findMaterial(domain::MaterialId(project::defaults::kParticleboard18));
    mat.thickness = 19;
    ASSERT_TRUE(target.execute(project::commands::upsertMaterial(mat)).ok());
    const auto ids = pasteItems(target, text.value());
    ASSERT_TRUE(ids.ok()) << ids.error().message;
    ASSERT_EQ(ids.value(), std::vector<std::string>{"p1"});
    // No overlap in the empty target: pasted in place.
    expectBox(boxOf(target, "p1"), {0, 0, 0}, {600, 300, 18});
    EXPECT_NE(target.project().findItem("p1")->panel()->panel.material.str(), project::defaults::kParticleboard18);
    // Cut is one undo step in the source.
    ASSERT_TRUE(source.s.undo().ok());
    EXPECT_EQ(source.s.project().items.size(), 1u);
    // Text that is not FurnitureOS item JSON.
    EXPECT_FALSE(itemsFromClipboard("hello").ok());
    EXPECT_FALSE(itemsFromClipboard(R"({"format":"other","version":1,"project":{}})").ok());
    EXPECT_FALSE(itemsFromClipboard(R"({"format":"furnitureos-items","version":2,"project":{}})").ok());
    EXPECT_FALSE(itemsToClipboard(source.s.project(), {}).ok());
    EXPECT_FALSE(pasteItems(target, "{}").ok());
}

} // namespace
} // namespace fos::app::test
