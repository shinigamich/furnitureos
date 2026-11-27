// SPDX-License-Identifier: AGPL-3.0-only
// SVG outline import (fixtures inline).
#include "fos/formats/svg_import.hpp"

#include "fos/formats/dxf_import.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <string>

namespace fos::formats {
namespace {

/// A document in mm: width/height in mm over an equal viewBox, so 1 user unit = 1 mm.
std::string mmDoc(const std::string& body, double w = 1000, double h = 1000) {
    const std::string W = std::to_string(static_cast<int>(w)), H = std::to_string(static_cast<int>(h));
    return "<?xml version=\"1.0\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + W + "mm\" height=\"" + H +
           "mm\" viewBox=\"0 0 " + W + " " + H + "\">\n" + body + "\n</svg>\n";
}

bool hasCode(const core::Diagnostics& ds, const std::string& code, const std::string& text = "") {
    for (const auto& d : ds)
        if (d.code == code && d.message.find(text) != std::string::npos) return true;
    return false;
}

geometry::Rect2 boundsOf(const SvgImport& s, std::size_t i) { return geometry::bounds(s.loops.at(i).contour); }

std::string errorOf(const std::string& text, const SvgImportOptions& o = {}) {
    auto r = importSvg(text, o);
    return r ? std::string("<no error>") : r.error().message;
}

TEST(SvgImport, RectWithFlippedY) {
    // rect x 10..110, y 20..70 (SVG y down) -> y flipped: -70..-20.
    const auto r = importSvg(mmDoc("<rect x=\"10\" y=\"20\" width=\"100\" height=\"50\"/>"));
    ASSERT_TRUE(r) << r.error().message;
    ASSERT_EQ(r.value().loops.size(), 1u);
    const auto& c = r.value().loops[0].contour;
    EXPECT_EQ(c.size(), 4u);
    EXPECT_NEAR(geometry::signedArea(c), 5000.0, 1e-9); // 100 * 50, CCW
    const auto b = boundsOf(r.value(), 0);
    EXPECT_NEAR(b.min.x, 10, 1e-12);
    EXPECT_NEAR(b.max.x, 110, 1e-12);
    EXPECT_NEAR(b.min.y, -70, 1e-12);
    EXPECT_NEAR(b.max.y, -20, 1e-12);
    EXPECT_EQ(r.value().loops[0].element, "<rect>");
    EXPECT_EQ(r.value().loops[0].line, 3u);
}

TEST(SvgImport, RoundedRectGivesQuarterArcs) {
    // 200 x 100, rx 10 (ry defaults to rx). Area = 200*100 - (4 - pi) * 10^2 = 20000 - 85.8407.
    const auto r = importSvg(mmDoc("<rect width=\"200\" height=\"100\" rx=\"10\"/>"));
    ASSERT_TRUE(r) << r.error().message;
    const auto& c = r.value().loops.at(0).contour;
    ASSERT_EQ(c.size(), 8u);
    int arcs = 0;
    for (const auto& v : c.vertices)
        if (v.bulge != 0) {
            ++arcs;
            EXPECT_NEAR(v.bulge, std::tan(std::numbers::pi / 8), 1e-12); // 90 deg CCW
        }
    EXPECT_EQ(arcs, 4);
    EXPECT_NEAR(geometry::signedArea(c), 20000 - (4 - std::numbers::pi) * 100, 1e-6);
}

TEST(SvgImport, CircleAndEllipse) {
    const auto r = importSvg(mmDoc("<circle cx=\"50\" cy=\"50\" r=\"20\"/><ellipse cx=\"200\" cy=\"50\" rx=\"5\" ry=\"5\"/>"));
    ASSERT_TRUE(r) << r.error().message;
    ASSERT_EQ(r.value().loops.size(), 2u);
    EXPECT_NEAR(geometry::signedArea(r.value().loops[0].contour), std::numbers::pi * 400, 1e-6);
    EXPECT_NEAR(geometry::signedArea(r.value().loops[1].contour), std::numbers::pi * 25, 1e-6);
    // An ellipse that is not a circle refuses the import.
    const auto e = importSvg(mmDoc("<ellipse cx=\"0\" cy=\"0\" rx=\"10\" ry=\"5\"/>"));
    ASSERT_FALSE(e);
    EXPECT_EQ(e.error().code, core::ErrorCode::Unsupported);
    EXPECT_NE(e.error().message.find("not a circle"), std::string::npos) << e.error().message;
    // ... unless a transform makes it one: scale(1, 2) turns ry 5 into 10.
    EXPECT_TRUE(importSvg(mmDoc("<ellipse transform=\"scale(1 2)\" rx=\"10\" ry=\"5\"/>")));
}

TEST(SvgImport, PolygonAndChainedPolylineAndLines) {
    // Triangle (0,0) (100,0) (0,100): area 5000.
    const auto r = importSvg(mmDoc("<polygon points=\"0,0 100,0 0,100\"/>"
                                   "<polyline points=\"200 0 300 0 300 100\"/>"
                                   "<line x1=\"300\" y1=\"100\" x2=\"200\" y2=\"0\"/>"));
    ASSERT_TRUE(r) << r.error().message;
    ASSERT_EQ(r.value().loops.size(), 2u);
    EXPECT_NEAR(geometry::signedArea(r.value().loops[0].contour), 5000, 1e-9);
    EXPECT_NEAR(geometry::signedArea(r.value().loops[1].contour), 5000, 1e-9);
    // An open polyline alone is reported, not imported.
    const auto o = importSvg(mmDoc("<polyline id=\"p\" points=\"0 0 10 0 10 10\"/>"));
    ASSERT_TRUE(o);
    EXPECT_TRUE(o.value().loops.empty());
    EXPECT_TRUE(hasCode(o.value().diagnostics, "formats.svg.open_chain", "(element '<polyline id=\"p\">')"));
}

TEST(SvgImport, PathAbsoluteRelativeHV) {
    // M0 0 h100 v50 H0 z: 100 x 50; m relative after z starts at (0,0): square 10..20 cut-out.
    const auto r = importSvg(mmDoc("<path d=\"M0 0 h100 v50 H0 z m10 10 l10 0 0 10 -10 0 Z\"/>"));
    ASSERT_TRUE(r) << r.error().message;
    ASSERT_EQ(r.value().loops.size(), 2u);
    EXPECT_NEAR(geometry::signedArea(r.value().loops[0].contour), 5000, 1e-9);
    const auto b = boundsOf(r.value(), 1);
    EXPECT_NEAR(b.min.x, 10, 1e-12);
    EXPECT_NEAR(b.max.y, -10, 1e-12);
    EXPECT_NEAR(b.min.y, -20, 1e-12);
}

TEST(SvgImport, PathArcs) {
    // Slot: two semicircles of r 10 joined by lines: 100 long (centres 0 and 100) -> area
    // 100 * 20 + pi * 100.
    const auto r = importSvg(mmDoc("<path d=\"M0 0 L100 0 A10 10 0 0 1 100 20 L0 20 A10 10 0 0 1 0 0 Z\"/>"));
    ASSERT_TRUE(r) << r.error().message;
    EXPECT_NEAR(geometry::signedArea(r.value().loops.at(0).contour), 2000 + std::numbers::pi * 100, 1e-6);
    // A 270-degree arc (large-arc flag) is split into two arcs of 135 degrees:
    // M10 0 A10 10 0 1 1 0 -10 (centre (0,0)) then back to the centre: three quarters of a disc.
    const auto t = importSvg(mmDoc("<path d=\"M10 0 A10 10 0 1 1 0 -10 L0 0 Z\"/>"));
    ASSERT_TRUE(t) << t.error().message;
    const auto& c = t.value().loops.at(0).contour;
    EXPECT_NEAR(std::fabs(geometry::signedArea(c)), 0.75 * std::numbers::pi * 100, 1e-6);
    for (const auto& v : c.vertices) EXPECT_LE(std::fabs(v.bulge), 1.0);
    // Flags written without separators ("a10 10 0 011 1" style) parse.
    EXPECT_TRUE(importSvg(mmDoc("<path d=\"M0 0h20a10 10 0 01-20 0z\"/>")));
}

TEST(SvgImport, EllipticalArcAndBeziersRefused) {
    const auto e = importSvg(mmDoc("<path d=\"M0 0 A20 10 0 0 1 40 0 Z\"/>"));
    ASSERT_FALSE(e);
    EXPECT_EQ(e.error().code, core::ErrorCode::Unsupported);
    EXPECT_NE(e.error().message.find("elliptical arc (rx 20, ry 10)"), std::string::npos) << e.error().message;
    const auto c = importSvg(mmDoc("<path id=\"curvy\" d=\"M0 0 C10 10 20 10 30 0 Z\"/>"));
    ASSERT_FALSE(c);
    EXPECT_EQ(c.error().code, core::ErrorCode::Unsupported);
    EXPECT_NE(c.error().message.find("SVG line 3: <path id=\"curvy\">: Bezier curve command 'C'"), std::string::npos)
        << c.error().message;
    for (const char* q : {"q 1 1 2 2", "S1 1 2 2", "T5 5"})
        EXPECT_FALSE(importSvg(mmDoc(std::string("<path d=\"M0 0 ") + q + "\"/>")));
}

TEST(SvgImport, GroupTransforms) {
    // translate(100 50) scale(2) rotate(90): unit square (0,0)-(10,10) -> rotate -> x -10..0,
    // y 0..10 -> scale 2 -> x -20..0, y 0..20 -> translate -> x 80..100, y 50..70 -> flip y.
    const auto r = importSvg(mmDoc("<g transform=\"translate(100 50)\"><g transform=\"scale(2) rotate(90)\">"
                                   "<rect width=\"10\" height=\"10\"/></g></g>"));
    ASSERT_TRUE(r) << r.error().message;
    const auto b = boundsOf(r.value(), 0);
    EXPECT_DOUBLE_EQ(b.min.x, 80);
    EXPECT_DOUBLE_EQ(b.max.x, 100);
    EXPECT_DOUBLE_EQ(b.min.y, -70);
    EXPECT_DOUBLE_EQ(b.max.y, -50);
    // matrix(1 0 0 1 5 6) = translate(5 6); rotate about a point: rotate(180 5 5) maps (0,0)-(10,10) onto itself.
    const auto m = importSvg(mmDoc("<rect transform=\"matrix(1,0,0,1,5,6) rotate(180 5 5)\" width=\"10\" height=\"10\"/>"));
    ASSERT_TRUE(m);
    EXPECT_DOUBLE_EQ(boundsOf(m.value(), 0).min.x, 5);
    EXPECT_DOUBLE_EQ(boundsOf(m.value(), 0).max.y, -6);
    // A mirrored circle still gives a valid CCW loop.
    const auto f = importSvg(mmDoc("<circle transform=\"scale(-1 1)\" cx=\"10\" r=\"5\"/>"));
    ASSERT_TRUE(f);
    EXPECT_GT(geometry::signedArea(f.value().loops.at(0).contour), 0);
    // Skew is refused.
    const auto s = importSvg(mmDoc("<g transform=\"skewX(10)\"><rect width=\"1\" height=\"1\"/></g>"));
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error().code, core::ErrorCode::Unsupported);
    EXPECT_NE(s.error().message.find("skewX"), std::string::npos);
}

TEST(SvgImport, Units) {
    const std::string rect = "<rect width=\"96\" height=\"48\"/>";
    // No viewBox: px at 96 per inch -> 96 px = 25.4 mm.
    auto px = importSvg("<svg width=\"200\" height=\"200\">" + rect + "</svg>");
    ASSERT_TRUE(px);
    EXPECT_NEAR(boundsOf(px.value(), 0).max.x, 25.4, 1e-12);
    // width 10in over viewBox 960 -> 1 unit = 254 / 960 mm; 96 units = 25.4 mm.
    auto in = importSvg("<svg width=\"10in\" height=\"5in\" viewBox=\"0 0 960 480\">" + rect + "</svg>");
    ASSERT_TRUE(in);
    EXPECT_NEAR(boundsOf(in.value(), 0).max.x, 25.4, 1e-12);
    // width 20cm over viewBox width 100 (origin 50 0): 1 unit = 2 mm; x 0..96 -> (0-50)*2 .. (96-50)*2.
    auto cm = importSvg("<svg width=\"20cm\" height=\"20cm\" viewBox=\"50 0 100 100\">" + rect + "</svg>");
    ASSERT_TRUE(cm);
    EXPECT_NEAR(boundsOf(cm.value(), 0).min.x, -100, 1e-12);
    EXPECT_NEAR(boundsOf(cm.value(), 0).max.x, 92, 1e-12);
    EXPECT_DOUBLE_EQ(cm.value().mmPerUnitX, 2.0);
    // preserveAspectRatio none: x 2 mm/unit, y 1 mm/unit.
    auto none = importSvg("<svg width=\"200mm\" height=\"100mm\" viewBox=\"0 0 100 100\" preserveAspectRatio=\"none\">" +
                          rect + "</svg>");
    ASSERT_TRUE(none);
    EXPECT_NEAR(boundsOf(none.value(), 0).max.x, 192, 1e-12);
    EXPECT_NEAR(boundsOf(none.value(), 0).min.y, -48, 1e-12);
    // --scale overrides.
    SvgImportOptions o;
    o.scale = 0.5;
    auto s = importSvg("<svg width=\"10in\" height=\"5in\" viewBox=\"0 0 960 480\">" + rect + "</svg>", o);
    ASSERT_TRUE(s);
    EXPECT_NEAR(boundsOf(s.value(), 0).max.x, 48, 1e-12);
    // Unitless viewBox only: warning.
    auto u = importSvg("<svg viewBox=\"0 0 10 10\">" + rect + "</svg>");
    ASSERT_TRUE(u);
    EXPECT_TRUE(hasCode(u.value().diagnostics, "formats.svg.unitless"));
    // Percent sizes need --scale.
    EXPECT_NE(errorOf("<svg width=\"100%\" height=\"100%\" viewBox=\"0 0 1 1\"/>").find("--scale"), std::string::npos);
    // Units on shape attributes are refused.
    EXPECT_NE(errorOf(mmDoc("<rect width=\"10mm\" height=\"10\"/>")).find("units on shape attributes"), std::string::npos);
}

TEST(SvgImport, IgnoredAndHiddenContent) {
    const auto r = importSvg(mmDoc("<defs><rect width=\"5\" height=\"5\"/></defs>"
                                   "<g style=\"display:none\"><rect width=\"9\" height=\"9\"/></g>"
                                   "<text x=\"0\">Label &amp; more</text><use href=\"#x\"/>"
                                   "<image href=\"http://example.invalid/a.png\"/>"
                                   "<rect width=\"20\" height=\"10\"/><!-- comment --><![CDATA[ x ]]>"));
    ASSERT_TRUE(r) << r.error().message;
    ASSERT_EQ(r.value().loops.size(), 1u);
    EXPECT_NEAR(geometry::signedArea(r.value().loops[0].contour), 200, 1e-9);
    EXPECT_TRUE(hasCode(r.value().diagnostics, "formats.svg.hidden"));
    EXPECT_TRUE(hasCode(r.value().diagnostics, "formats.svg.element_ignored", "<text>"));
    EXPECT_TRUE(hasCode(r.value().diagnostics, "formats.svg.element_ignored", "references are not followed"));
    EXPECT_TRUE(hasCode(r.value().diagnostics, "formats.svg.element_ignored", "never fetched"));
}

TEST(SvgImport, MalformedXml) {
    EXPECT_NE(errorOf("<svg><rect width=\"1\" height=\"1\"></svg>").find("does not match '<rect>' opened at line 1"),
              std::string::npos);
    EXPECT_NE(errorOf("<svg>\n<g>\n").find("truncated"), std::string::npos);
    EXPECT_NE(errorOf("<svg><rect width=1/></svg>").find("not quoted"), std::string::npos);
    EXPECT_NE(errorOf("<svg a=\"1\" a=\"2\"/>").find("given twice"), std::string::npos);
    EXPECT_NE(errorOf("<html/>").find("not <svg>"), std::string::npos);
    EXPECT_NE(errorOf("<svg/><svg/>").find("second root"), std::string::npos);
    EXPECT_NE(errorOf(mmDoc("<path d=\"L 1 1\"/>")).find("must start with a moveto"), std::string::npos);
    EXPECT_NE(errorOf(mmDoc("<polygon points=\"0 0 1\"/>")).find("odd count"), std::string::npos);
    EXPECT_NE(errorOf(std::string("\xFF\xFE<\0s", 5)).find("UTF-16"), std::string::npos);
}

TEST(SvgImport, HostileInputs) {
    // Billion laughs: refused at the DOCTYPE, before any entity could be expanded.
    const std::string laughs =
        "<?xml version=\"1.0\"?>\n<!DOCTYPE lolz [\n <!ENTITY lol \"lol\">\n <!ENTITY lol2 \"&lol;&lol;&lol;&lol;\">\n]>\n"
        "<svg><text>&lol2;</text></svg>";
    auto b = importSvg(laughs);
    ASSERT_FALSE(b);
    EXPECT_EQ(b.error().code, core::ErrorCode::Unsupported);
    EXPECT_NE(b.error().message.find("SVG line 2: a DOCTYPE with an internal subset"), std::string::npos);
    // Undeclared entity references are refused, not expanded or ignored.
    EXPECT_NE(errorOf("<svg><text>&xxe;</text></svg>").find("entity reference '&xxe;' is refused"), std::string::npos);
    // An external DTD identifier alone is accepted and never fetched.
    EXPECT_TRUE(importSvg("<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">"
                          "<svg width=\"10mm\" height=\"10mm\" viewBox=\"0 0 10 10\"><rect width=\"5\" height=\"5\"/></svg>"));
    // Huge numbers: out of range, or beyond the 1e8 mm coordinate limit after scaling.
    EXPECT_NE(errorOf(mmDoc("<rect width=\"1e400\" height=\"1\"/>")).find("out of range"), std::string::npos);
    EXPECT_NE(errorOf(mmDoc("<rect width=\"1e9\" height=\"1\"/>")).find("coordinate limit"), std::string::npos);
    EXPECT_NE(errorOf(mmDoc("<g transform=\"scale(1e300)\"><rect transform=\"scale(1e300)\" width=\"1\" height=\"1\"/></g>"))
                  .find("singular or out of range"),
              std::string::npos);
    EXPECT_NE(errorOf(mmDoc("<path d=\"M0 0 L1e308 0 L0 1 Z\"/>")).find("coordinate limit"), std::string::npos);
    // Deep nesting.
    std::string deep = "<svg>";
    for (int k = 0; k < 100; ++k) deep += "<g>";
    for (int k = 0; k < 100; ++k) deep += "</g>";
    deep += "</svg>";
    EXPECT_NE(errorOf(deep).find("nested deeper than 64"), std::string::npos);
    // Element and size caps.
    SvgImportOptions small;
    small.maxElements = 3;
    EXPECT_NE(errorOf(mmDoc("<g/><g/><g/>"), small).find("more than 3 elements"), std::string::npos);
    small = {};
    small.maxBytes = 10;
    EXPECT_NE(errorOf(mmDoc(""), small).find("more than the limit of 10"), std::string::npos);
    // Garbage never crashes.
    for (const char* g : {"", "<", "<svg", "<svg ", "<svg a", "<svg a=", "<svg a=\"", "<!--", "<?x", "&", "<svg>&#0;</svg>",
                          "<svg>&#x110000;</svg>", "<svg><path d=\"M\"/></svg>", "<svg><path d=\"M1 1 A\"/></svg>",
                          "<svg><path d=\"M0 0 A1 1 0 2 0 1 1\"/></svg>", "<svg viewBox=\"0 0 0 0\"/>"})
        EXPECT_FALSE(importSvg(g)) << g;
}

TEST(SvgImport, FeedsPanelShape) {
    // 600 x 400 outline with a d=40 hole centred at (100, 100) from the top-left of the drawing:
    // panel frame (y up): centre (100, 400 - 100) = (100, 300).
    const auto r = importSvg(mmDoc("<rect width=\"600\" height=\"400\"/><circle cx=\"100\" cy=\"100\" r=\"20\"/>"));
    ASSERT_TRUE(r);
    const auto s = importPanelShape(contoursOf(r.value()));
    ASSERT_TRUE(s) << s.error().message;
    EXPECT_DOUBLE_EQ(s.value().length, 600);
    EXPECT_DOUBLE_EQ(s.value().width, 400);
    ASSERT_EQ(s.value().cutoutKinds.size(), 1u);
    EXPECT_EQ(s.value().cutoutKinds[0].kind, CutoutPrimitive::Kind::Circle);
    EXPECT_NEAR(s.value().cutoutKinds[0].x, 100, 1e-9);
    EXPECT_NEAR(s.value().cutoutKinds[0].y, 300, 1e-9);
    EXPECT_NEAR(s.value().cutoutKinds[0].radius, 20, 1e-9);
}

} // namespace
} // namespace fos::formats
