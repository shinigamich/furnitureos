// SPDX-License-Identifier: AGPL-3.0-only
// DXF outline import (fixtures are written inline as group-code text).
#include "fos/formats/dxf_import.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace fos::formats {
namespace {

using geometry::segmentInfo;

/// "code\nvalue\n" pairs of a whole file: optional $INSUNITS, then the entities.
std::string dxfFile(const std::string& entities, int insUnits = 4) {
    std::string s = "0\nSECTION\n2\nHEADER\n";
    if (insUnits >= 0) s += "9\n$INSUNITS\n70\n" + std::to_string(insUnits) + "\n";
    s += "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n" + entities + "0\nENDSEC\n0\nEOF\n";
    return s;
}

std::string line(double x1, double y1, double x2, double y2, const std::string& layer = "0") {
    return "0\nLINE\n8\n" + layer + "\n10\n" + std::to_string(x1) + "\n20\n" + std::to_string(y1) + "\n11\n" +
           std::to_string(x2) + "\n21\n" + std::to_string(y2) + "\n";
}

std::string circle(double x, double y, double r, const std::string& extra = "", const std::string& layer = "0") {
    return "0\nCIRCLE\n8\n" + layer + "\n" + extra + "10\n" + std::to_string(x) + "\n20\n" + std::to_string(y) + "\n40\n" +
           std::to_string(r) + "\n";
}

std::string full(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

/// Closed LWPOLYLINE; each vertex {x, y, bulge}.
std::string lwpoly(const std::vector<std::array<double, 3>>& v) {
    std::string s = "0\nLWPOLYLINE\n8\n0\n90\n" + std::to_string(v.size()) + "\n70\n1\n";
    for (const auto& p : v)
        s += "10\n" + std::to_string(p[0]) + "\n20\n" + std::to_string(p[1]) + "\n42\n" + full(p[2]) + "\n";
    return s;
}

bool hasCode(const core::Diagnostics& ds, const std::string& code, const std::string& text = "") {
    for (const auto& d : ds)
        if (d.code == code && d.message.find(text) != std::string::npos) return true;
    return false;
}

TEST(DxfImport, RoundedRectangleLwpolylineGivesExactRadii) {
    // 600 x 400, R50 corners, placed at (1000, 2000). Quarter arc bulge = tan(90deg / 4) = 0.41421356.
    const double b = std::tan(std::numbers::pi / 8);
    const double X = 1000, Y = 2000;
    const auto r = importDxf(dxfFile(lwpoly({{X + 50, Y, 0}, {X + 550, Y, b}, {X + 600, Y + 50, 0}, {X + 600, Y + 350, b},
                                             {X + 550, Y + 400, 0}, {X + 50, Y + 400, b}, {X, Y + 350, 0},
                                             {X, Y + 50, b}})));
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.value().loops.size(), 1u);
    const auto& c = r.value().loops[0].contour;
    ASSERT_EQ(c.size(), 8u);
    int arcs = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        const auto s = segmentInfo(c, i);
        if (!s.isArc) continue;
        ++arcs;
        EXPECT_NEAR(s.radius, 50.0, 1e-9);
        EXPECT_NEAR(s.sweep, 90.0, 1e-9);
    }
    EXPECT_EQ(arcs, 4);
    const auto panel = importPanelShape({c});
    ASSERT_TRUE(panel.ok());
    EXPECT_DOUBLE_EQ(panel.value().length, 600.0);
    EXPECT_NEAR(panel.value().width, 400.0, 1e-9);
    EXPECT_NEAR(geometry::bounds(panel.value().shape.outer).min.x, 0.0, 1e-9);
}

TEST(DxfImport, LinesAndArcChainIntoOneLoop) {
    // 800 x 300 box with a semicircular top: ARC centre (400, 300) R 400, 0..180 deg; pieces
    // given out of order and one LINE reversed. Area = 800 * 300 + pi * 400^2 / 2.
    const std::string arc = "0\nARC\n8\n0\n10\n400\n20\n300\n40\n400\n50\n0\n51\n180\n";
    const auto r = importDxf(dxfFile(line(0, 300, 0, 0) + arc + line(0, 0, 800, 0) + line(800, 300, 800, 0)));
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.value().loops.size(), 1u);
    const auto& c = r.value().loops[0].contour;
    EXPECT_EQ(c.size(), 4u);
    EXPECT_NEAR(geometry::signedArea(c), 800.0 * 300 + std::numbers::pi * 400 * 400 / 2, 1e-6);
    EXPECT_NEAR(geometry::bounds(c).max.y, 700.0, 1e-9);
    EXPECT_EQ(r.value().loops[0].line, 15u); // first entity of the chain in file order (line 0,300-0,0)
}

TEST(DxfImport, CircleAndRectangleCutoutsInsideOutline) {
    // Worktop 1200 x 600 at (100, 100); hob hole R30 centred at (400, 300); sink 500 x 400 at
    // (750, 200); one circle outside the worktop is reported, not used.
    const std::string dxf = dxfFile(lwpoly({{100, 100, 0}, {1300, 100, 0}, {1300, 700, 0}, {100, 700, 0}}) +
                                    circle(400, 300, 30) +
                                    lwpoly({{750, 200, 0}, {1250, 200, 0}, {1250, 600, 0}, {750, 600, 0}}) +
                                    circle(3000, 0, 10));
    const auto r = importDxf(dxf);
    ASSERT_TRUE(r.ok());
    std::vector<geometry::Contour> loops;
    for (const auto& l : r.value().loops) loops.push_back(l.contour);
    const auto p = importPanelShape(loops);
    ASSERT_TRUE(p.ok());
    const auto& s = p.value();
    EXPECT_DOUBLE_EQ(s.length, 1200);
    EXPECT_DOUBLE_EQ(s.width, 600);
    ASSERT_EQ(s.shape.cutouts.size(), 2u);
    // Sorted by minimum corner y: sink (y 100) before hob (y 200 - 30 = 170).
    EXPECT_EQ(s.cutoutKinds[0].kind, CutoutPrimitive::Kind::Rectangle);
    EXPECT_DOUBLE_EQ(s.cutoutKinds[0].x, 650);
    EXPECT_DOUBLE_EQ(s.cutoutKinds[0].y, 100);
    EXPECT_DOUBLE_EQ(s.cutoutKinds[0].length, 500);
    EXPECT_DOUBLE_EQ(s.cutoutKinds[0].width, 400);
    EXPECT_EQ(s.cutoutKinds[1].kind, CutoutPrimitive::Kind::Circle);
    EXPECT_DOUBLE_EQ(s.cutoutKinds[1].x, 300);
    EXPECT_DOUBLE_EQ(s.cutoutKinds[1].y, 200);
    EXPECT_DOUBLE_EQ(s.cutoutKinds[1].radius, 30);
    EXPECT_TRUE(hasCode(s.diagnostics, "formats.dxf.loop_ignored", "(2990, -10)"));
}

TEST(DxfImport, InchUnitsScaleToMillimetres) {
    // 10 in square = 254 mm; 0.1 in = 2.54 mm snapped by importPanelShape.
    const std::string sq = line(0.1, 0.1, 10.1, 0.1) + line(10.1, 0.1, 10.1, 10.1) + line(10.1, 10.1, 0.1, 10.1) +
                           line(0.1, 10.1, 0.1, 0.1);
    const auto r = importDxf(dxfFile(sq, 1));
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().unit, DxfUnit::Inch);
    ASSERT_EQ(r.value().loops.size(), 1u);
    EXPECT_NEAR(geometry::bounds(r.value().loops[0].contour).min.x, 2.54, 1e-9);
    const auto p = importPanelShape({r.value().loops[0].contour});
    ASSERT_TRUE(p.ok());
    EXPECT_DOUBLE_EQ(p.value().length, 254.0);
    // Explicit unit overrides the header; absent $INSUNITS assumes mm with a warning.
    DxfImportOptions cm;
    cm.units = DxfUnit::Centimetre;
    EXPECT_DOUBLE_EQ(geometry::bounds(importDxf(dxfFile(sq, 1), cm).value().loops[0].contour).max.x, 101.0);
    const auto none = importDxf(dxfFile(sq, -1));
    ASSERT_TRUE(none.ok());
    EXPECT_TRUE(hasCode(none.value().diagnostics, "formats.dxf.units_assumed"));
    EXPECT_EQ(importDxf(dxfFile(sq, 14)).error().code, core::ErrorCode::Unsupported);
}

TEST(DxfImport, OpenChainIsReportedWithEndpoints) {
    const auto r = importDxf(dxfFile(line(0, 0, 100, 0) + line(100, 0, 100, 50) + line(100, 50, 0, 50)));
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().loops.empty());
    EXPECT_TRUE(hasCode(r.value().diagnostics, "formats.dxf.open_chain", "line 15 (layer '0'): 3 piece(s) from (0, 0) to (0, 50), gap 50 mm"));
    EXPECT_EQ(importPanelShape({}).error().code, core::ErrorCode::InvalidArgument);
    // Within the chain tolerance the gap closes.
    DxfImportOptions loose;
    loose.chainTolerance = 0.1;
    EXPECT_EQ(importDxf(dxfFile(line(0, 0, 100, 0) + line(100, 0, 50, 80) + line(50, 80, 0, 0.05)), loose)
                  .value()
                  .loops.size(),
              1u);
}

TEST(DxfImport, MalformedInputNamesTheLine) {
    const std::string good = dxfFile(circle(0, 0, 10));
    // Truncated: cut after a group code line.
    const auto t = importDxf(good.substr(0, good.find("40\n") + 3));
    ASSERT_FALSE(t.ok());
    EXPECT_EQ(t.error().code, core::ErrorCode::ParseError);
    EXPECT_NE(t.error().message.find("line 23: group code 40 has no value"), std::string::npos) << t.error().message;
    // Cut between pairs: ends inside ENTITIES.
    EXPECT_NE(importDxf(good.substr(0, good.find("0\nENDSEC\n0\nEOF"))).error().message.find("truncated"),
              std::string::npos);
    // Bad number on the value line of code 40 (line 24).
    std::string bad = good;
    bad.replace(bad.find("10.000000"), 9, "1O.5");
    const auto b = importDxf(bad);
    ASSERT_FALSE(b.ok());
    EXPECT_NE(b.error().message.find("DXF line 24: group code 40 needs a finite number, found '1O.5'"),
              std::string::npos)
        << b.error().message;
    EXPECT_NE(importDxf("0\nSECTION\nx\nENTITIES\n").error().message.find("line 3: expected an integer group code"),
              std::string::npos);
    EXPECT_FALSE(importDxf("").ok());
    EXPECT_FALSE(importDxf("AutoCAD Binary DXF\r\n\x1a").ok());
    EXPECT_FALSE(importDxf(std::string(200, '\n')).ok());
}

TEST(DxfImport, InsertsAndUnsupportedEntitiesAreReported) {
    const std::string ents = circle(0, 0, 10) + "0\nINSERT\n8\n0\n2\nHANDLE_BLOCK\n10\n5\n20\n5\n" +
                             "0\nSPLINE\n8\n0\n" + circle(0, 0, 5, "", "holes");
    const auto r = importDxf(dxfFile(ents));
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(hasCode(r.value().diagnostics, "formats.dxf.insert_unsupported", "block 'HANDLE_BLOCK'"));
    EXPECT_TRUE(hasCode(r.value().diagnostics, "formats.dxf.entity_unsupported", "1 SPLINE entity"));
    EXPECT_EQ(r.value().loops.size(), 2u);
    // Layer filter (case-insensitive); the INSERT on layer 0 is then skipped as well.
    DxfImportOptions o;
    o.layer = "HOLES";
    const auto f = importDxf(dxfFile(ents), o);
    ASSERT_EQ(f.value().loops.size(), 1u);
    EXPECT_FALSE(hasCode(f.value().diagnostics, "formats.dxf.insert_unsupported"));
    EXPECT_TRUE(hasCode(f.value().diagnostics, "formats.dxf.layer_skipped", "3 entities"));
}

TEST(DxfImport, MirroredExtrusionAndCaps) {
    // OCS with extrusion (0,0,-1): x is mirrored, so the circle at OCS (100, 0) lies at world (-100, 0).
    const auto r = importDxf(dxfFile(circle(100, 0, 10, "230\n-1\n")));
    ASSERT_TRUE(r.ok());
    EXPECT_NEAR(geometry::bounds(r.value().loops[0].contour).max.x, -90.0, 1e-9);
    DxfImportOptions o;
    o.maxEntities = 2;
    EXPECT_EQ(importDxf(dxfFile(circle(0, 0, 1) + circle(5, 0, 1) + circle(10, 0, 1)), o).error().code,
              core::ErrorCode::Unsupported);
    o = {};
    o.chainTolerance = 0;
    EXPECT_EQ(importDxf(dxfFile(""), o).error().code, core::ErrorCode::InvalidArgument);
    EXPECT_FALSE(importDxf(dxfFile(circle(1e12, 0, 1))).ok());
}

TEST(DxfImport, EveryTruncationIsHandled) {
    // Each prefix of a valid file either imports or fails cleanly (no crash, no hang).
    const std::string arc = "0\nARC\n8\n0\n10\n400\n20\n300\n40\n400\n50\n0\n51\n180\n";
    const std::string f = dxfFile(line(0, 300, 0, 0) + arc + line(0, 0, 800, 0) + line(800, 300, 800, 0) +
                                  lwpoly({{10, 10, 2.5}, {20, 10, 0}, {20, 20, -0.3}}));
    std::size_t ok = 0;
    for (std::size_t n = 0; n < f.size(); ++n) ok += importDxf(f.substr(0, n)).ok() ? 1 : 0;
    EXPECT_EQ(ok, 1u); // only the prefix lacking the final newline after EOF is complete
    EXPECT_TRUE(importDxf(f).ok());
}

} // namespace
} // namespace fos::formats
