// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/dxf_panel.hpp"

#include "test_support.hpp"

#include <gtest/gtest.h>

namespace fos::project {
namespace {

using test::meta;

bool contains(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

/// Outline 1200 x 600 at (100, 50) in DXF coordinates, a R 30 hole at (1100, 350), a
/// 500 x 400 rectangle at (450, 150) and a triangle cut-out.
formats::DxfImport sampleDxf(bool withTriangle) {
    formats::DxfImport d;
    d.loops.push_back({geometry::translated(geometry::rectangleContour(1200, 600), {100, 50}), 1, "0"});
    d.loops.push_back({geometry::circleContour({1100, 350}, 30), 2, "0"});
    d.loops.push_back({geometry::translated(geometry::rectangleContour(500, 400), {450, 150}), 3, "0"});
    if (withTriangle) {
        geometry::Contour t;
        t.vertices = {{{150, 100}, 0}, {{250, 100}, 0}, {{200, 180}, 0}};
        d.loops.push_back({t, 4, "0"});
    }
    return d;
}

TEST(DxfPanel, BuildsPanelInPartFrame) {
    const Project p = newProject(meta());
    DxfPanelRequest req;
    req.name = "Work top";
    req.thickness = 18.0;
    auto r = buildDxfPanelItem(p, sampleDxf(false), req, dxfPanelItemId(p, req.name));
    ASSERT_TRUE(r) << r.error().message;
    const DxfPanelImport& imp = r.value();
    EXPECT_EQ(imp.length, 1200);
    EXPECT_EQ(imp.width, 600);
    EXPECT_EQ(imp.outlineSegments, 4u);
    EXPECT_EQ(imp.arcs, 0u);
    EXPECT_EQ(imp.circles, 1u);
    EXPECT_EQ(imp.rectangles, 1u);
    EXPECT_EQ(imp.skipped, 0u);
    const PanelItem* panel = imp.item.panel();
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->id.value, "work-top");
    EXPECT_EQ(panel->panel.id.value, "work-top/panel");
    ASSERT_TRUE(panel->panel.contour.has_value());
    ASSERT_EQ(panel->panel.contour->cutouts.size(), 2u);
    // Moved by (-100, -50): hole centre (1000, 300), rectangle corner (350, 100), sizes kept.
    const auto& sink = panel->panel.contour->cutouts[0]; // importPanelShape sorts by min corner (y, x)
    EXPECT_EQ(sink.x, "350");
    EXPECT_EQ(sink.y, "100");
    EXPECT_EQ(sink.length, "500");
    EXPECT_EQ(sink.width, "400");
    const auto& hole = panel->panel.contour->cutouts[1];
    EXPECT_EQ(hole.x, "1000");
    EXPECT_EQ(hole.y, "300");
    EXPECT_EQ(hole.radius, "30");
}

TEST(DxfPanel, UnsupportedCutoutRefusedOrSkipped) {
    const Project p = newProject(meta());
    DxfPanelRequest req;
    req.name = "Top";
    req.thickness = 18.0;
    core::Diagnostics diags;
    auto refused = buildDxfPanelItem(p, sampleDxf(true), req, "top", geometry::Transform::identity(), &diags);
    ASSERT_FALSE(refused);
    EXPECT_TRUE(contains(refused.error().message, "1 cut-out(s) of unsupported shape; --skip-unsupported-cutouts"));
    ASSERT_FALSE(diags.empty());
    EXPECT_EQ(diags.back().code, "project.import_dxf.cutout_unsupported");
    EXPECT_EQ(diags.back().severity, core::Severity::Error);
    EXPECT_TRUE(contains(diags.back().message, "(50, 50) .. (150, 130) with 3 segments")) << diags.back().message;

    req.skipUnsupportedCutouts = true;
    req.skipOptionLabel = "\"Skip unsupported cut-outs\"";
    auto ok = buildDxfPanelItem(p, sampleDxf(true), req, "top");
    ASSERT_TRUE(ok) << ok.error().message;
    EXPECT_EQ(ok.value().skipped, 1u);
    EXPECT_EQ(ok.value().diagnostics.back().severity, core::Severity::Warning);
    EXPECT_TRUE(contains(ok.value().diagnostics.back().message, "left out (\"Skip unsupported cut-outs\")."));
}

TEST(DxfPanel, MaterialAndIdRules) {
    const Project p = newProject(meta());
    DxfPanelRequest req;
    req.name = "X";
    EXPECT_FALSE(pickDxfPanelMaterial(p.libraries, req)); // neither material nor thickness
    req.thickness = 7.25;
    auto none = pickDxfPanelMaterial(p.libraries, req);
    ASSERT_FALSE(none);
    EXPECT_TRUE(contains(none.error().message, "no material 7.25 mm thick"));
    req.thickness.reset();
    req.materialId = "mat.missing";
    EXPECT_FALSE(pickDxfPanelMaterial(p.libraries, req));
    EXPECT_FALSE(buildDxfPanelItem(p, sampleDxf(false), req, "x"));
    DxfPanelRequest byThickness;
    byThickness.name = "X";
    byThickness.thickness = 18.0;
    EXPECT_FALSE(buildDxfPanelItem(p, formats::DxfImport{}, byThickness, "x")); // no closed loop
    EXPECT_EQ(dxfPanelItemId(p, "  Table top!! "), "table-top");
    EXPECT_EQ(dxfPanelItemId(p, "***"), "panel");
}

} // namespace
} // namespace fos::project
