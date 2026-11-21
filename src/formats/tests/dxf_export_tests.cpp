// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/dxf_export.hpp"
#include "fos/formats/dxf_import.hpp"

#include "shape_fixtures.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace fos::formats {
namespace {

using geometry::segmentInfo;

bool contains(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

DxfImport reimport(const std::string& text, const std::string& layer) {
    DxfImportOptions o;
    o.layer = layer;
    auto r = importDxf(text, o);
    EXPECT_TRUE(static_cast<bool>(r)) << (r ? "" : r.error().message);
    return r ? r.value() : DxfImport{};
}

/// `got` equals `want` up to the choice of the start vertex (both CCW).
void expectSameContour(const geometry::Contour& got, const geometry::Contour& want) {
    ASSERT_EQ(got.size(), want.size());
    std::size_t start = got.size();
    for (std::size_t k = 0; k < got.size(); ++k)
        if (got.vertices[k].point == want.vertices[0].point) start = k;
    ASSERT_LT(start, got.size()) << "start vertex not found";
    for (std::size_t i = 0; i < want.size(); ++i) {
        const auto& g = got.vertices[(start + i) % got.size()];
        EXPECT_EQ(g.point, want.vertices[i].point) << "vertex " << i; // shortest round-trip text: exact
        EXPECT_EQ(g.bulge, want.vertices[i].bulge) << "vertex " << i;
    }
}

domain::Model roundModel() {
    domain::Model m = test::shapedModel();
    return m;
}

TEST(DxfExport, RawOutlineRoundTripKeepsExactRadii) {
    const domain::Model m = roundModel();
    DxfExportOptions o;
    o.partId = "shaped/round";
    auto r = exportPartOutlinesDxf(m, o);
    ASSERT_TRUE(r) << r.error().message;
    ASSERT_EQ(r.value().files.size(), 1u);
    const std::string& text = r.value().files[0].text;
    EXPECT_TRUE(contains(text, "$ACADVER\n1\nAC1009\n"));
    EXPECT_TRUE(contains(text, "$INSUNITS\n70\n4\n"));
    const DxfImport back = reimport(text, "OUTLINE");
    ASSERT_EQ(back.loops.size(), 1u);
    EXPECT_EQ(back.unit, DxfUnit::Millimetre);

    const domain::Part& part = *m.findAssembly(domain::AssemblyId("asm.shaped"))->findPart(domain::PartId("shaped/round"));
    const auto want = domain::cutContour(part, m);
    ASSERT_TRUE(want);
    expectSameContour(back.loops[0].contour, want.value());
    // Arcs keep their centre and shrink by their band: three corners with 0.4 mm ABS ->
    // R 50 - 0.4 = 49.6; the last corner is raw -> R 50.
    std::vector<double> radii;
    for (std::size_t i = 0; i < back.loops[0].contour.size(); ++i) {
        const auto s = segmentInfo(back.loops[0].contour, i);
        if (s.isArc) radii.push_back(s.radius);
    }
    std::sort(radii.begin(), radii.end());
    ASSERT_EQ(radii.size(), 4u);
    EXPECT_NEAR(radii[0], 49.6, 1e-9);
    EXPECT_NEAR(radii[1], 49.6, 1e-9);
    EXPECT_NEAR(radii[2], 49.6, 1e-9);
    EXPECT_NEAR(radii[3], 50.0, 1e-9);
}

TEST(DxfExport, FinishedOutlineAndCutoutsRoundTrip) {
    const domain::Model m = roundModel();
    DxfExportOptions o;
    o.finishedOutline = true;
    o.partId = "shaped/worktop";
    auto r = exportPartOutlinesDxf(m, o);
    ASSERT_TRUE(r) << r.error().message;
    const auto& part = *m.findAssembly(domain::AssemblyId("asm.shaped"))->findPart(domain::PartId("shaped/worktop"));
    const DxfImport outline = reimport(r.value().files[0].text, "OUTLINE");
    ASSERT_EQ(outline.loops.size(), 1u);
    expectSameContour(outline.loops[0].contour, part.shape->outer); // finished 1200 x 600
    const DxfImport cut = reimport(r.value().files[0].text, "CUTOUTS");
    ASSERT_EQ(cut.loops.size(), 2u);
    // Sink 500 x 400 at (350, 100); tap hole R 30 at (1000, 300).
    const auto sink = geometry::bounds(cut.loops[0].contour);
    EXPECT_EQ(sink.min, (geometry::Vec2{350, 100}));
    EXPECT_EQ(sink.max, (geometry::Vec2{850, 500}));
    const auto arc = segmentInfo(cut.loops[1].contour, 0);
    EXPECT_TRUE(arc.isArc);
    EXPECT_NEAR(arc.radius, 30, 1e-12);
    EXPECT_NEAR(arc.center.x, 1000, 1e-12);
    EXPECT_NEAR(arc.center.y, 300, 1e-12);
}

domain::Model drilledModel() {
    domain::Model m = test::shapedModel();
    domain::Part p = test::shapedPart("drilled", 500, 300, 18, {});
    p.shape.reset(); // rectangular, no bands: raw = finished = [0,500] x [0,300]
    const auto op = [](const char* id, domain::OperationShape s) { return domain::Operation{domain::OperationId(id), s, "user", ""}; };
    p.operations = {op("h1", domain::Hole{domain::Face::A, 37, 32, 5, 13, false}),
                    op("h2", domain::Hole{domain::Face::B, 100, 150, 8.5, 0, true}),
                    op("h3", domain::Hole{domain::Face::B, 200, 150, 35, 13, false}), // blind on B: skipped
                    op("h4", domain::Hole{domain::Face::L1, 50, 9, 8, 30, false}),    // edge: skipped
                    op("g1", domain::Groove{domain::Face::A, 10, 280, 490, 280, 4, 8})};
    domain::Assembly a;
    a.id = domain::AssemblyId("asm.drilled");
    a.parts = {p};
    m.assemblies = {a};
    return m;
}

TEST(DxfExport, DrillLayersPerDiameterAndGrooveFootprint) {
    auto r = exportPartOutlinesDxf(drilledModel());
    ASSERT_TRUE(r) << r.error().message;
    const std::string& text = r.value().files[0].text;
    const DxfImport d5 = reimport(text, "DRILL_5");
    ASSERT_EQ(d5.loops.size(), 1u);
    const auto c5 = segmentInfo(d5.loops[0].contour, 0);
    EXPECT_NEAR(c5.radius, 2.5, 1e-12);
    EXPECT_NEAR(c5.center.x, 37, 1e-12);
    EXPECT_NEAR(c5.center.y, 32, 1e-12);
    const DxfImport d85 = reimport(text, "DRILL_8_5"); // through hole from face B, same (u, v)
    ASSERT_EQ(d85.loops.size(), 1u);
    EXPECT_NEAR(segmentInfo(d85.loops[0].contour, 0).radius, 4.25, 1e-12);
    EXPECT_FALSE(contains(text, "DRILL_35"));
    EXPECT_FALSE(contains(text, "DRILL_8\n"));
    // Groove centre line (10,280)-(490,280), width 4 -> footprint [10,490] x [278,282].
    const DxfImport g = reimport(text, "GROOVES");
    ASSERT_EQ(g.loops.size(), 1u);
    const auto gb = geometry::bounds(g.loops[0].contour);
    EXPECT_EQ(gb.min, (geometry::Vec2{10, 278}));
    EXPECT_EQ(gb.max, (geometry::Vec2{490, 282}));
    EXPECT_TRUE(contains(text, "TEXT\n8\nLABELS\n"));
    EXPECT_TRUE(contains(text, "\n1\ndrilled\n"));
    ASSERT_EQ(r.value().diagnostics.size(), 1u);
    EXPECT_EQ(r.value().diagnostics[0].code, "formats.dxf_export.operations_skipped");
    EXPECT_TRUE(contains(r.value().diagnostics[0].message, "2 hole(s)")) << r.value().diagnostics[0].message;
}

TEST(DxfExport, PartsInARowAndOneFilePerPart) {
    domain::Model m = drilledModel();
    domain::Part second = m.assemblies[0].parts[0];
    second.id = domain::PartId("drilled/2");
    second.operations.clear();
    m.assemblies[0].parts.push_back(second);
    DxfExportOptions o;
    o.spacing = 20;
    auto row = exportPartOutlinesDxf(m, o);
    ASSERT_TRUE(row);
    const DxfImport outlines = reimport(row.value().files[0].text, "OUTLINE");
    ASSERT_EQ(outlines.loops.size(), 2u);
    // Second part shifted by 500 + 20 = 520 along x.
    EXPECT_EQ(geometry::bounds(outlines.loops[1].contour).min, (geometry::Vec2{520, 0}));
    EXPECT_EQ(geometry::bounds(outlines.loops[1].contour).max, (geometry::Vec2{1020, 300}));

    o.oneFilePerPart = true;
    auto each = exportPartOutlinesDxf(m, o);
    ASSERT_TRUE(each);
    ASSERT_EQ(each.value().files.size(), 2u);
    EXPECT_EQ(each.value().files[0].fileName, "drilled.dxf");
    EXPECT_EQ(each.value().files[1].fileName, "drilled_2.dxf");
    EXPECT_EQ(each.value().files[1].partId, "drilled/2");
    const DxfImport own = reimport(each.value().files[1].text, "OUTLINE");
    ASSERT_EQ(own.loops.size(), 1u);
    EXPECT_EQ(geometry::bounds(own.loops[0].contour).min, (geometry::Vec2{0, 0})); // own frame
}

TEST(DxfExport, Errors) {
    DxfExportOptions o;
    o.partId = "nope";
    auto r = exportPartOutlinesDxf(drilledModel(), o);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound);
    EXPECT_TRUE(contains(r.error().message, "'nope'"));
    EXPECT_FALSE(exportPartOutlinesDxf(domain::Model{}));
    o = {};
    o.spacing = -1;
    EXPECT_FALSE(exportPartOutlinesDxf(drilledModel(), o));
}

} // namespace
} // namespace fos::formats
