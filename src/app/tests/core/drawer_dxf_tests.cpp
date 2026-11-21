// SPDX-License-Identifier: AGPL-3.0-only
// Drawer boxes in the front editor (FrontInput -> FrontSpec) and File > Import DXF outline
// (buildDxfPanelItem) into a project.
#include "fos/app/contour_rows.hpp"
#include "fos/app/dxf_panel_import.hpp"
#include "fos/app/interior_rows.hpp"
#include "fos/app/templates.hpp"
#include "test_support.hpp"

#include <numbers>

namespace fos::app::test {
namespace {

TEST(DrawerBoxInput, BuildsTheSpecFromEditorValues) {
    FrontInput in;
    in.choice = FrontInput::Choice::DrawerStack;
    in.drawerHeights = QStringLiteral("140; 1*");
    in.drawerBox = true;
    in.slide = hardware::ids::kSlideUndermount;
    in.drawerBottom = furniture::DrawerBottomMount::Grooved;
    auto spec = frontSpec(in);
    ASSERT_TRUE(spec.ok()) << spec.error().message;
    ASSERT_TRUE(spec.value().drawerBox.has_value());
    EXPECT_EQ(spec.value().drawerBox->slide.str(), "hw.slide_undermount");
    EXPECT_EQ(spec.value().drawerBox->bottom, furniture::DrawerBottomMount::Grooved);
    EXPECT_EQ(spec.value().drawerHeights.size(), 2u);
    // Round trip through the editor state.
    const FrontInput back = frontInput(spec.value());
    EXPECT_TRUE(back.drawerBox);
    EXPECT_EQ(back.slide, "hw.slide_undermount");

    // "None (fronts only)" -> no box; a door ignores the drawer-box fields.
    in.drawerBox = false;
    EXPECT_FALSE(frontSpec(in).value().drawerBox.has_value());
    in.drawerBox = true;
    in.choice = FrontInput::Choice::Door;
    EXPECT_FALSE(frontSpec(in).value().drawerBox.has_value());
    in.choice = FrontInput::Choice::None;
    EXPECT_FALSE(frontSpec(in).ok());
}

TEST(DrawerBoxInput, SlideChoicesAndEngineRefusal) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::DrawerUnit).ok());
    const auto choices = slideChoices(s.project().libraries, hardware::ids::kSlideBallBearing);
    ASSERT_EQ(choices.size(), 2u); // the two generic DrawerSlide entries, none missing
    EXPECT_FALSE(choices[0].missing);
    EXPECT_FALSE(choices[1].missing);

    const project::Item* item = s.project().findItem("cab1");
    ASSERT_TRUE(item && item->cabinet());
    std::string path;
    FrontInput in;
    for (const auto& r : sectionRows(item->cabinet()->interior))
        if (r.front && r.front->kind == furniture::FrontKind::DrawerStack) {
            path = r.path;
            in = frontInput(r.front);
        }
    ASSERT_EQ(in.choice, FrontInput::Choice::DrawerStack);
    in.drawerBox = true;
    auto cmd = frontCommand("cab1", path, in);
    ASSERT_TRUE(cmd.ok());
    ASSERT_TRUE(s.execute(cmd.value()).ok());
    const std::string drawer = "cab1/drawer." + (path.empty() ? std::string() : path + ".") + "1";
    EXPECT_NE(part(s, drawer + ".side.left"), nullptr);
    EXPECT_NE(part(s, drawer + ".bottom"), nullptr);

    // Nailed-under bottom with an undermount slide is refused by the furniture module.
    in.slide = hardware::ids::kSlideUndermount;
    in.drawerBottom = furniture::DrawerBottomMount::NailedUnder;
    const auto before = s.project().findItem("cab1")->cabinet()->interior;
    EXPECT_FALSE(s.execute(frontCommand("cab1", path, in).value()).ok());
    EXPECT_EQ(s.project().findItem("cab1")->cabinet()->interior, before);
}

/// 400 x 300 rectangle with 20 mm corner radii (LWPOLYLINE, bulge tan(22.5 deg) per quarter
/// arc) and a circular cut-out of radius 25 at (200, 150); no $INSUNITS (read as mm).
std::string roundedPlateDxf() {
    const char* b = "0.41421356237309503";
    std::string t = "0\nSECTION\n2\nENTITIES\n0\nLWPOLYLINE\n8\n0\n90\n8\n70\n1\n";
    const double pts[8][2] = {{20, 0}, {380, 0}, {400, 20}, {400, 280}, {380, 300}, {20, 300}, {0, 280}, {0, 20}};
    for (int i = 0; i < 8; ++i) {
        t += "10\n" + std::to_string(pts[i][0]) + "\n20\n" + std::to_string(pts[i][1]) + "\n";
        if (i % 2 == 1) t += std::string("42\n") + b + "\n";
    }
    t += "0\nCIRCLE\n8\n0\n10\n200\n20\n150\n40\n25\n0\nENDSEC\n0\nEOF\n";
    return t;
}

TEST(DxfPanelImport, RoundedPlateWithHoleIntoProject) {
    Session s;
    s.newProject();
    auto dxf = formats::importDxf(roundedPlateDxf());
    ASSERT_TRUE(dxf.ok()) << dxf.error().message;
    DxfPanelRequest req;
    req.name = "Table top";
    req.thickness = 18;
    const std::string id = dxfPanelItemId(s.project(), req.name);
    EXPECT_EQ(id, "table-top");
    auto built = buildDxfPanelItem(s.project(), dxf.value(), req, id, geometry::Transform::identity());
    ASSERT_TRUE(built.ok()) << built.error().message;
    EXPECT_DOUBLE_EQ(built.value().length, 400);
    EXPECT_DOUBLE_EQ(built.value().width, 300);
    EXPECT_EQ(built.value().arcs, 4u);
    EXPECT_EQ(built.value().circles, 1u);
    bool unitsAssumed = false;
    for (const auto& d : built.value().diagnostics) unitsAssumed |= d.code == "formats.dxf.units_assumed";
    EXPECT_TRUE(unitsAssumed);

    ASSERT_TRUE(s.execute(project::commands::addItem(built.value().item)).ok());
    const domain::Part* p = part(s, "table-top/panel");
    ASSERT_NE(p, nullptr);
    EXPECT_DOUBLE_EQ(p->length, 400);
    EXPECT_DOUBLE_EQ(p->width, 300);
    EXPECT_DOUBLE_EQ(p->thickness, 18);
    // Area: 400 * 300 - 4 corners * (20^2 - pi 20^2 / 4) - pi 25^2
    //     = 120000 - 343.36 - 1963.50 = 117693.14 mm2.
    const double expected = 120000 - 4 * (400 - std::numbers::pi * 100) - std::numbers::pi * 625;
    EXPECT_NEAR(shapeFacts(*p).outlineArea, expected, 0.01);
    EXPECT_EQ(shapeFacts(*p).cutouts, 1u);
    // Undoable; the id is now taken.
    EXPECT_EQ(dxfPanelItemId(s.project(), req.name), "table-top-2");
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(part(s, "table-top/panel"), nullptr);

    // Refusals name the problem.
    req.thickness = 99;
    auto noMaterial = buildDxfPanelItem(s.project(), dxf.value(), req, id, geometry::Transform::identity());
    ASSERT_FALSE(noMaterial.ok());
    EXPECT_EQ(noMaterial.error().message, "The project library has no material 99 mm thick.");
    req.thickness.reset();
    formats::DxfImport empty;
    req.materialId = s.project().libraries.materials.front().id.str();
    EXPECT_FALSE(buildDxfPanelItem(s.project(), empty, req, id, geometry::Transform::identity()).ok());
}

} // namespace
} // namespace fos::app::test
