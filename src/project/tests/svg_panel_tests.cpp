// SPDX-License-Identifier: AGPL-3.0-only
// SVG outline -> panel item through the shared buildOutlinePanelItem.
#include "fos/formats/svg_import.hpp"
#include "fos/project/dxf_panel.hpp"

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <numbers>

namespace fos::project {
namespace {

using test::meta;

// 800 x 500 mm panel with R 25 corners, a d=50 hole centred 100 mm from the left and 100 mm from
// the top (panel frame: (100, 500 - 100) = (100, 400)), and a triangle cut-out.
const char* kSvg = "<svg width=\"800mm\" height=\"500mm\" viewBox=\"0 0 800 500\">"
                   "<rect width=\"800\" height=\"500\" rx=\"25\"/>"
                   "<circle cx=\"100\" cy=\"100\" r=\"25\"/>"
                   "<polygon points=\"400,200 500,200 450,300\"/></svg>";

TEST(SvgPanel, BuildsShapedPanelThroughSharedConversion) {
    const Project p = newProject(meta());
    auto svg = formats::importSvg(kSvg);
    ASSERT_TRUE(svg) << svg.error().message;
    OutlinePanelRequest req;
    req.name = "Shelf";
    req.thickness = 18.0;
    req.diagnosticPrefix = "project.import_svg";
    core::Diagnostics diags;
    auto refused = buildOutlinePanelItem(p, formats::contoursOf(svg.value()), svg.value().diagnostics, req, "shelf",
                                         geometry::Transform::identity(), &diags);
    ASSERT_FALSE(refused); // the triangle is neither a circle nor a rectangle
    bool found = false;
    for (const auto& d : diags) found |= d.code == "project.import_svg.cutout_unsupported" && d.severity == core::Severity::Error;
    EXPECT_TRUE(found);

    req.skipUnsupportedCutouts = true;
    auto r = buildOutlinePanelItem(p, formats::contoursOf(svg.value()), svg.value().diagnostics, req, "shelf");
    ASSERT_TRUE(r) << r.error().message;
    const OutlinePanelImport& imp = r.value();
    EXPECT_DOUBLE_EQ(imp.length, 800);
    EXPECT_DOUBLE_EQ(imp.width, 500);
    EXPECT_EQ(imp.outlineSegments, 8u); // 4 sides + 4 corner arcs
    EXPECT_EQ(imp.arcs, 4u);
    EXPECT_EQ(imp.circles, 1u);
    EXPECT_EQ(imp.skipped, 1u);
    const PanelItem* panel = imp.item.panel();
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->panel.contour->cutouts.size(), 1u);
    EXPECT_EQ(panel->panel.contour->cutouts[0].x, "100");
    EXPECT_EQ(panel->panel.contour->cutouts[0].y, "400");
    EXPECT_EQ(panel->panel.contour->cutouts[0].radius, "25");
}

TEST(SvgPanel, DxfPathStillUsesItsOwnPrefix) {
    const Project p = newProject(meta());
    formats::DxfImport d;
    d.loops.push_back({geometry::rectangleContour(400, 300), 1, "0"});
    geometry::Contour t;
    t.vertices = {{{50, 50}, 0}, {{150, 50}, 0}, {{100, 120}, 0}};
    d.loops.push_back({t, 2, "0"});
    DxfPanelRequest req;
    req.name = "X";
    req.thickness = 18.0;
    core::Diagnostics diags;
    ASSERT_FALSE(buildDxfPanelItem(p, d, req, "x", geometry::Transform::identity(), &diags));
    ASSERT_FALSE(diags.empty());
    EXPECT_EQ(diags.back().code, "project.import_dxf.cutout_unsupported");
}

} // namespace
} // namespace fos::project
