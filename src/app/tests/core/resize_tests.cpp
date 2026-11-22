// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/parameter_rows.hpp"
#include "fos/app/resize.hpp"
#include "test_support.hpp"

#include "fos/furniture/panel.hpp"

namespace fos::app::test {
namespace {

double param(const Session& s, const std::string& itemId, const std::string& name) {
    for (const auto& g : parameterGroups(*s.project().findItem(itemId)->cabinet()))
        for (const auto& r : g.rows)
            if (r.name == name) return r.value;
    ADD_FAILURE() << "no parameter " << name;
    return 0;
}

struct Cabinet {
    Session s;
    std::string id;
    Cabinet() {
        EXPECT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet, TemplateSize{800, 820, 560}).ok());
        id = s.project().items.front().id();
    }
};

TEST(Resize, TargetOfCabinet) {
    Cabinet c;
    const auto t = resizeTarget(c.s, c.id);
    ASSERT_TRUE(t.has_value());
    EXPECT_DOUBLE_EQ(t->box.max.x, 800);
    EXPECT_DOUBLE_EQ(t->box.max.y, 560);
    EXPECT_DOUBLE_EQ(t->box.max.z, 820);
    ASSERT_EQ(t->handles.size(), 5u); // +X, -X, +Y, -Y, +Z
    EXPECT_EQ(t->handles[0].parameter, "width");
    EXPECT_TRUE(t->handles[1].negative);
    EXPECT_EQ(t->handles[3].parameter, "depth");
    EXPECT_EQ(t->handles[4].parameter, "height");
    EXPECT_DOUBLE_EQ(t->handles[4].value, 820);
    EXPECT_FALSE(resizeTarget(c.s, "nope").has_value());
    EXPECT_EQ(resizeLabel("width"), QStringLiteral("W"));
}

TEST(Resize, PlusXChangesWidthAndOneUndoRestores) {
    Cabinet c;
    const auto placement = c.s.project().findItem(c.id)->placement();
    ASSERT_TRUE(applyResize(c.s, c.id, ResizeAxis::X, false, 900).ok());
    EXPECT_DOUBLE_EQ(param(c.s, c.id, "width"), 900);
    EXPECT_TRUE(geometry::approxEqual(c.s.project().findItem(c.id)->placement(), placement));
    ASSERT_TRUE(c.s.undo().ok());
    EXPECT_DOUBLE_EQ(param(c.s, c.id, "width"), 800);
}

TEST(Resize, MinusXMovesPlacementInTheSameUndoStep) {
    Cabinet c;
    const geometry::Vec3 origin = c.s.project().findItem(c.id)->placement().origin;
    ASSERT_TRUE(applyResize(c.s, c.id, ResizeAxis::X, true, 900).ok());
    EXPECT_DOUBLE_EQ(param(c.s, c.id, "width"), 900);
    // The right face stays: origin.x - (900 - 800) = origin.x - 100 (identity rotation).
    const geometry::Vec3 moved = c.s.project().findItem(c.id)->placement().origin;
    EXPECT_NEAR(moved.x, origin.x - 100, 1e-9);
    EXPECT_NEAR(moved.y, origin.y, 1e-9);
    ASSERT_TRUE(c.s.undo().ok()); // ONE undo restores both
    EXPECT_DOUBLE_EQ(param(c.s, c.id, "width"), 800);
    EXPECT_NEAR(c.s.project().findItem(c.id)->placement().origin.x, origin.x, 1e-9);
    EXPECT_FALSE(c.s.document().undoLabel().has_value());
}

TEST(Resize, MinusYOfRotatedCabinetMovesAlongItsOwnAxis) {
    Cabinet c;
    auto rotated = geometry::Transform::rotationZ(90);
    rotated.origin = {2000, 0, 0};
    ASSERT_TRUE(c.s.execute(project::commands::setPlacement(c.id, rotated)).ok());
    ASSERT_TRUE(applyResize(c.s, c.id, ResizeAxis::Y, true, 600).ok());
    // Assembly Y = world -X: origin moves by -(600 - 560) * (-1, 0, 0) = (+40, 0, 0).
    EXPECT_NEAR(c.s.project().findItem(c.id)->placement().origin.x, 2040, 1e-9);
    EXPECT_DOUBLE_EQ(param(c.s, c.id, "depth"), 600);
}

TEST(Resize, RefusedValueLeavesModelUnchanged) {
    Cabinet c;
    const auto origin = c.s.project().findItem(c.id)->placement().origin;
    const auto rev = c.s.revision();
    auto r = applyResize(c.s, c.id, ResizeAxis::X, true, -50);
    ASSERT_FALSE(r.ok());
    EXPECT_FALSE(r.error().message.empty());
    EXPECT_DOUBLE_EQ(param(c.s, c.id, "width"), 800);
    EXPECT_NEAR(c.s.project().findItem(c.id)->placement().origin.x, origin.x, 1e-9);
    EXPECT_EQ(c.s.revision(), rev);
    EXPECT_FALSE(applyResize(c.s, c.id, ResizeAxis::Z, true, 900).ok()); // no -Z handle
    EXPECT_FALSE(applyResize(c.s, "missing", ResizeAxis::X, false, 900).ok());
}

TEST(Resize, PanelAxesByOrientation) {
    project::Item item;
    item.definition = project::PanelItem{domain::AssemblyId("p"), {},
                                         furniture::makePanel(domain::PartId("p/panel"), "Filler", domain::MaterialId("m"))};
    EXPECT_EQ(resizeParameter(item, ResizeAxis::X), std::optional<std::string>("length"));
    EXPECT_EQ(resizeParameter(item, ResizeAxis::Y), std::optional<std::string>("width"));
    EXPECT_FALSE(resizeParameter(item, ResizeAxis::Z).has_value());
    item.panel()->panel.orientation = furniture::PanelOrientation::VerticalSide;
    EXPECT_EQ(resizeParameter(item, ResizeAxis::Z), std::optional<std::string>("length"));
    EXPECT_FALSE(resizeParameter(item, ResizeAxis::X).has_value());
}

} // namespace
} // namespace fos::app::test
