// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/dxf.hpp"
#include "fos/drawings/svg.hpp"
#include "test_util.hpp"

#include <cstdlib>
#include <map>
#include <sstream>
#include <utility>

using namespace fos;
using namespace fos::drawings;
using namespace fos::drawings::testing;

namespace {

/// Value of attribute `name` in the first tag of `svg` that contains `marker`.
std::string attrOf(const std::string& svg, const std::string& marker, const std::string& name) {
    const std::size_t m = svg.find(marker);
    if (m == std::string::npos) return "<no tag>";
    const std::size_t start = svg.rfind('<', m), end = svg.find('>', m);
    const std::string tag = svg.substr(start, end - start);
    const std::string key = " " + name + "=\"";
    const std::size_t a = tag.find(key);
    if (a == std::string::npos) return "<no attr>";
    const std::size_t b = tag.find('"', a + key.size());
    return tag.substr(a + key.size(), b - a - key.size());
}
double numAttr(const std::string& svg, const std::string& marker, const std::string& name) {
    return std::strtod(attrOf(svg, marker, name).c_str(), nullptr);
}

using Pairs = std::vector<std::pair<int, std::string>>;

/// Parses DXF text into (group code, value) pairs; fails the test on malformed input.
Pairs parseDxf(const std::string& dxf) {
    std::vector<std::string> lines;
    std::istringstream in(dxf);
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    EXPECT_EQ(lines.size() % 2, 0u) << "group codes and values must come in pairs";
    Pairs pairs;
    for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
        char* end = nullptr;
        const long code = std::strtol(lines[i].c_str(), &end, 10);
        EXPECT_TRUE(!lines[i].empty() && *end == '\0') << "bad group code '" << lines[i] << "' at line " << i + 1;
        EXPECT_TRUE(code >= 0 && code <= 999);
        pairs.emplace_back(static_cast<int>(code), lines[i + 1]);
    }
    return pairs;
}

struct DxfSummary {
    std::vector<std::string> sections;
    std::map<std::string, std::size_t> entities;       ///< by entity type
    std::map<std::string, std::size_t> entitiesByLayer; ///< "LINE@HIDDEN"
    std::vector<std::string> layers, headerVars;
    bool balanced = true;
};

DxfSummary summarize(const Pairs& p) {
    DxfSummary s;
    std::string section, table, entity;
    bool open = false;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto& [code, value] = p[i];
        if (code == 0 && value == "SECTION") {
            s.balanced = s.balanced && !open && i + 1 < p.size() && p[i + 1].first == 2;
            open = true;
            section = p[i + 1].second;
            s.sections.push_back(section);
        } else if (code == 0 && value == "ENDSEC") {
            s.balanced = s.balanced && open;
            open = false;
            section.clear();
        } else if (code == 0 && value == "TABLE") {
            table = p[i + 1].second;
        } else if (code == 0 && value == "ENDTAB") {
            table.clear();
        } else if (section == "TABLES" && code == 0 && value == "LAYER" && table == "LAYER") {
            s.layers.push_back(p[i + 1].second);
        } else if (section == "HEADER" && code == 9) {
            s.headerVars.push_back(value);
        } else if (section == "ENTITIES" && code == 0) {
            entity = value;
            ++s.entities[value];
        } else if (section == "ENTITIES" && code == 8) {
            ++s.entitiesByLayer[entity + "@" + value];
        }
    }
    s.balanced = s.balanced && !open;
    return s;
}

bool has(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

Line mkLine(Vec2 a, Vec2 b, LineStyle style, std::string element = {}) {
    return Line{a, b, style, defaultLineWeight(style), {}, std::move(element)};
}

} // namespace

TEST(Svg, HeaderIsInMillimetresAndCalibrationLineIsExactly100Units) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const auto r = generateAssemblyDrawing(m, domain::AssemblyId("cab1"));
    ASSERT_TRUE(r.ok());
    const std::string svg = toSvg(r.value().sheets[0]);
    // Root element: A3 landscape.
    EXPECT_EQ(attrOf(svg, "<svg ", "width"), "420mm");
    EXPECT_EQ(attrOf(svg, "<svg ", "height"), "297mm");
    std::istringstream vb(attrOf(svg, "<svg ", "viewBox"));
    double x = -1, y = -1, w = -1, h = -1;
    vb >> x >> y >> w >> h;
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
    EXPECT_EQ(w, 420); // viewBox width equals the mm width: 1 user unit = 1 mm
    EXPECT_EQ(h, 297);
    EXPECT_EQ(svg.rfind("<?xml version=\"1.0\" encoding=\"UTF-8\"?>", 0), 0u);
    // Calibration line: paper (24, 23)-(124, 23) -> SVG y = 297 - 23 = 274 (2 notes * 3.5 + 2 above y = 14).
    const std::string cal = "data-element=\"sheet/calibration.line\"";
    const double x1 = numAttr(svg, cal, "x1"), x2 = numAttr(svg, cal, "x2");
    EXPECT_EQ(x2 - x1, 100.0) << "exactly 100 user units";
    EXPECT_EQ(x1, 24.0);
    EXPECT_EQ(numAttr(svg, cal, "y1"), 274.0);
    EXPECT_EQ(numAttr(svg, cal, "y2"), 274.0);
    EXPECT_EQ(attrOf(svg, cal, "stroke-width"), "0.35");
    // Model ids are exported for interactive viewers.
    EXPECT_NE(svg.find("data-source=\"cab1/shelf.1\""), std::string::npos);
    EXPECT_NE(svg.find("data-element=\"cab1/view.front/dim.overall.width\""), std::string::npos);
    EXPECT_NE(svg.find(">800</text>"), std::string::npos);
    // Hidden lines are dashed in mm.
    EXPECT_EQ(attrOf(svg, "data-source=\"cab1/back\"", "stroke-dasharray"), "2 1");

    // Rectangle variant: 100 x 50 user units.
    AssemblyDrawingOptions opt;
    opt.sheet.calibration.kind = CalibrationKind::Rectangle10050;
    const auto r2 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), opt);
    ASSERT_TRUE(r2.ok()) << r2.error().message;
    std::string pts = attrOf(toSvg(r2.value().sheets[0]), "data-element=\"sheet/calibration.rect\"", "points");
    for (char& c : pts)
        if (c == ',') c = ' ';
    std::istringstream ps(pts);
    double px[4], py[4];
    for (int i = 0; i < 4; ++i) ps >> px[i] >> py[i];
    EXPECT_EQ(px[1] - px[0], 100.0);
    EXPECT_EQ(py[1] - py[2], 50.0); // y is flipped: the upper edge has the smaller SVG y
}

TEST(Svg, GeometryFlipTextAnchorsRotationAndStyles) {
    Sheet s;
    s.width = 200;
    s.height = 100;
    s.primitives.emplace_back(mkLine({10, 20}, {30, 40}, LineStyle::Visible, "L"));
    s.primitives.emplace_back(mkLine({0, 0}, {5, 0}, LineStyle::Center, "C"));
    Text t;
    t.position = {50, 10};
    t.height = 3.5;
    t.hAlign = HAlign::Center;
    t.rotationDeg = 90;
    t.text = "\xD0\xA8\xD0\xB0\xD1\x84\xD0\xB0 <A&B> \"x\""; // "Шафа <A&B> "x""
    t.elementId = "T";
    s.primitives.emplace_back(t);
    Text top;
    top.position = {100, 50};
    top.height = 10;
    top.hAlign = HAlign::Right;
    top.vAlign = VAlign::Top;
    top.text = "top";
    top.elementId = "TOP";
    s.primitives.emplace_back(top);
    Arc arc;
    arc.center = {50, 50};
    arc.radius = 10;
    arc.startDeg = 0;
    arc.endDeg = 90;
    arc.elementId = "A";
    s.primitives.emplace_back(arc);
    Circle c;
    c.center = {20, 30};
    c.radius = 2.5;
    c.elementId = "CIR";
    s.primitives.emplace_back(c);
    Marker arrow;
    arrow.position = {100, 20};
    arrow.angleDeg = 0;
    arrow.size = 3;
    arrow.elementId = "ARW";
    s.primitives.emplace_back(arrow);

    const std::string svg = toSvg(s);
    EXPECT_EQ(attrOf(svg, "<svg ", "viewBox"), "0 0 200 100");
    // Line: y flipped (100 - 20 = 80, 100 - 40 = 60), x unchanged, weight in mm.
    EXPECT_EQ(attrOf(svg, "\"L\"", "x1"), "10");
    EXPECT_EQ(attrOf(svg, "\"L\"", "y1"), "80");
    EXPECT_EQ(attrOf(svg, "\"L\"", "x2"), "30");
    EXPECT_EQ(attrOf(svg, "\"L\"", "y2"), "60");
    EXPECT_EQ(attrOf(svg, "\"L\"", "stroke-width"), "0.5");
    EXPECT_EQ(attrOf(svg, "\"L\"", "stroke-dasharray"), "<no attr>");
    EXPECT_EQ(attrOf(svg, "\"C\"", "stroke-dasharray"), "8 1.5 1 1.5");
    // Text: anchor, size, counter-clockwise 90 deg on paper = rotate(-90) in SVG about the flipped point.
    EXPECT_EQ(attrOf(svg, "\"T\"", "text-anchor"), "middle");
    EXPECT_EQ(attrOf(svg, "\"T\"", "font-size"), "3.5");
    EXPECT_EQ(attrOf(svg, "\"T\"", "x"), "50");
    EXPECT_EQ(attrOf(svg, "\"T\"", "y"), "90");
    EXPECT_EQ(attrOf(svg, "\"T\"", "transform"), "rotate(-90 50 90)");
    // Escaping: Cyrillic passes through as UTF-8, markup characters are escaped.
    EXPECT_NE(svg.find(">\xD0\xA8\xD0\xB0\xD1\x84\xD0\xB0 &lt;A&amp;B&gt; &quot;x&quot;</text>"), std::string::npos);
    EXPECT_EQ(svg.find("<A&B>"), std::string::npos);
    // Top-anchored text: baseline 0.7 * 10 = 7 mm below the anchor -> paper y 43 -> SVG 57.
    EXPECT_EQ(attrOf(svg, "\"TOP\"", "text-anchor"), "end");
    EXPECT_EQ(attrOf(svg, "\"TOP\"", "y"), "57");
    EXPECT_EQ(attrOf(svg, "\"TOP\"", "transform"), "<no attr>");
    // Arc 0..90 deg CCW: from (60,50) to (50,60) on paper = (60,50) to (50,40) in SVG, sweep flag 0.
    EXPECT_EQ(attrOf(svg, "\"A\"", "d"), "M 60,50 A 10 10 0 0 0 50,40");
    EXPECT_EQ(attrOf(svg, "\"CIR\"", "cy"), "70");
    EXPECT_EQ(attrOf(svg, "\"CIR\"", "r"), "2.5");
    // Arrowhead: tip (100,20), base 3 back, half width 0.5 -> filled polygon.
    EXPECT_EQ(attrOf(svg, "\"ARW\"", "points"), "100,80 97,79.5 97,80.5");
    EXPECT_EQ(attrOf(svg, "\"ARW\"", "fill"), "#000000");
}

TEST(Svg, XmlEscape) {
    EXPECT_EQ(xmlEscape("a<b>&\"c'"), "a&lt;b&gt;&amp;&quot;c&apos;");
    EXPECT_EQ(xmlEscape("\xD0\x9F\xD0\xBE\xD0\xBB\xD0\xB8\xD1\x86\xD1\x8F \xC3\x98" "5"),
              "\xD0\x9F\xD0\xBE\xD0\xBB\xD0\xB8\xD1\x86\xD1\x8F \xC3\x98" "5")
        << "UTF-8 is passed through unchanged";
    EXPECT_EQ(xmlEscape(std::string("a\x01" "b\tc")), "a b\tc") << "forbidden control characters become spaces";
    EXPECT_EQ(xmlEscape(""), "");
}

TEST(Dxf, StructureAndEntityCountsOfHandBuiltPrimitives) {
    std::vector<Primitive> prims;
    prims.emplace_back(mkLine({0, 0}, {100, 0}, LineStyle::Visible));
    prims.emplace_back(mkLine({0, 10}, {100, 10}, LineStyle::Hidden));
    Polyline closed;
    closed.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    closed.closed = true;
    prims.emplace_back(closed); // 4 LINEs
    Polyline open;
    open.points = {{0, 0}, {5, 5}, {10, 0}};
    open.style = LineStyle::Dimension;
    prims.emplace_back(open); // 2 LINEs
    Circle c;
    c.center = {346, 37};
    c.radius = 2.5;
    prims.emplace_back(c);
    Arc a;
    a.center = {1, 2};
    a.radius = 3;
    a.startDeg = 10;
    a.endDeg = 200;
    prims.emplace_back(a);
    Text t;
    t.position = {5, 6};
    t.height = 25;
    t.hAlign = HAlign::Center;
    t.rotationDeg = 90;
    t.text = "\xD0\x9F\xD0\xBE\xD0\xBB\xD0\xBA\xD0\xB0 \xC3\x98" "5"; // "Полка Ø5"
    prims.emplace_back(t);
    Marker arrow;
    prims.emplace_back(arrow); // 3 LINEs
    Marker tick;
    tick.kind = MarkerKind::Tick;
    prims.emplace_back(tick); // 1 LINE
    Marker dot;
    dot.kind = MarkerKind::Dot;
    prims.emplace_back(dot); // 1 CIRCLE

    const std::string dxf = toDxf(prims);
    const Pairs p = parseDxf(dxf);
    const DxfSummary s = summarize(p);
    EXPECT_TRUE(s.balanced);
    const std::vector<std::string> order = {"HEADER", "TABLES", "BLOCKS", "ENTITIES"};
    EXPECT_EQ(s.sections, order);
    ASSERT_FALSE(p.empty());
    EXPECT_EQ(p.back(), (std::pair<int, std::string>{0, "EOF"}));
    EXPECT_EQ(p.front(), (std::pair<int, std::string>{0, "SECTION"}));
    // 2 + 4 + 2 + 3 + 1 = 12 LINE, 1 + 1 = 2 CIRCLE, 1 ARC, 1 TEXT.
    EXPECT_EQ(s.entities.at("LINE"), 12u);
    EXPECT_EQ(s.entities.at("CIRCLE"), 2u);
    EXPECT_EQ(s.entities.at("ARC"), 1u);
    EXPECT_EQ(s.entities.at("TEXT"), 1u);
    EXPECT_EQ(s.entities.size(), 4u) << "only LINE, CIRCLE, ARC, TEXT";
    const DxfEntityCounts counted = countDxfEntities(prims);
    EXPECT_EQ(counted.lines, 12u);
    EXPECT_EQ(counted.circles, 2u);
    EXPECT_EQ(counted.arcs, 1u);
    EXPECT_EQ(counted.texts, 1u);
    // Layers: 1 visible line + 4 polyline sides on VISIBLE, 1 on HIDDEN, 2 + 3 + 1 on DIMENSION.
    EXPECT_EQ(s.entitiesByLayer.at("LINE@VISIBLE"), 5u);
    EXPECT_EQ(s.entitiesByLayer.at("LINE@HIDDEN"), 1u);
    EXPECT_EQ(s.entitiesByLayer.at("LINE@DIMENSION"), 6u);
    EXPECT_EQ(s.entitiesByLayer.at("TEXT@TEXT"), 1u);
    for (const auto& [key, n] : s.entitiesByLayer)
        EXPECT_TRUE(has(s.layers, key.substr(key.find('@') + 1))) << key << " uses an undeclared layer";
    for (const char* layer : {"0", "VISIBLE", "HIDDEN", "DIMENSION", "TEXT"}) EXPECT_TRUE(has(s.layers, layer)) << layer;

    // Header: R12, no unit variables by default (they do not exist in R12).
    EXPECT_TRUE(has(s.headerVars, "$ACADVER"));
    EXPECT_NE(dxf.find("9\n$ACADVER\n1\nAC1009\n"), std::string::npos);
    EXPECT_FALSE(has(s.headerVars, "$INSUNITS"));
    EXPECT_FALSE(has(s.headerVars, "$MEASUREMENT"));
    // Exact entity records, 1:1 in mm.
    EXPECT_NE(dxf.find("0\nCIRCLE\n8\nVISIBLE\n10\n346\n20\n37\n30\n0\n40\n2.5\n"), std::string::npos);
    EXPECT_NE(dxf.find("0\nLINE\n8\nHIDDEN\n10\n0\n20\n10\n30\n0\n11\n100\n21\n10\n31\n0\n"), std::string::npos);
    EXPECT_NE(dxf.find("0\nARC\n8\nVISIBLE\n10\n1\n20\n2\n30\n0\n40\n3\n50\n10\n51\n200\n"), std::string::npos);
    // Text: ASCII only, \U+XXXX for the rest (П U+041F о U+043E л U+043B к U+043A а U+0430, Ø U+00D8).
    EXPECT_NE(dxf.find("0\nTEXT\n8\nTEXT\n10\n5\n20\n6\n30\n0\n40\n25\n1\n"
                       "\\U+041F\\U+043E\\U+043B\\U+043A\\U+0430 \\U+00D85\n50\n90\n7\nSTANDARD\n72\n1\n73\n0\n"
                       "11\n5\n21\n6\n31\n0\n"),
              std::string::npos);
    for (const char ch : dxf) ASSERT_LT(static_cast<unsigned char>(ch), 0x80) << "DXF output must be pure ASCII";

    DxfOptions units;
    units.writeUnitVariables = true;
    const std::string withUnits = toDxf(prims, units);
    EXPECT_NE(withUnits.find("9\n$INSUNITS\n70\n4\n"), std::string::npos);
    EXPECT_NE(withUnits.find("9\n$MEASUREMENT\n70\n1\n"), std::string::npos);
    EXPECT_TRUE(summarize(parseDxf(withUnits)).balanced);
    // An empty drawing is still a well-formed file.
    const DxfSummary e = summarize(parseDxf(toDxf({})));
    EXPECT_TRUE(e.balanced);
    EXPECT_EQ(e.sections, order);
}

TEST(Dxf, EscapeText) {
    EXPECT_EQ(dxfEscapeText("Shelf 18"), "Shelf 18");
    EXPECT_EQ(dxfEscapeText("\xC3\x97"), "\\U+00D7");
    EXPECT_EQ(dxfEscapeText("a\nb"), "a?b") << "control characters";
    EXPECT_EQ(dxfEscapeText("\xF0\x9F\x98\x80"), "?") << "beyond the BMP";
    EXPECT_EQ(dxfEscapeText("\xD0"), "\\U+FFFD") << "truncated UTF-8 becomes the replacement character";
}

TEST(Dxf, FrontViewOfTheCabinetAtFullSize) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const View v = generateAssemblyView(cabinet(m), ViewKind::Front);
    ViewAnnotations ann;
    ann.dimensions = autoDimensionView(v, 0.1, DimensionStyle{});
    const std::vector<Primitive> prims = modelSpacePrimitives(v, ann, DrawingStyle{}, 10.0);
    const std::string dxf = viewToDxf(v, ann, DrawingStyle{}, 10.0);
    const DxfSummary s = summarize(parseDxf(dxf));
    EXPECT_TRUE(s.balanced);
    // Geometry: 18 view segments (4 hidden). Dimensions: 10, each 2 extension lines + 1
    // dimension line + 2 arrowheads of 3 lines = 9 LINEs and 1 TEXT -> 18 + 90 = 108 LINEs.
    EXPECT_EQ(s.entities.at("LINE"), 108u);
    EXPECT_EQ(s.entities.at("TEXT"), 10u);
    EXPECT_EQ(s.entities.count("CIRCLE"), 0u);
    EXPECT_EQ(s.entitiesByLayer.at("LINE@HIDDEN"), 4u);
    EXPECT_EQ(s.entitiesByLayer.at("LINE@VISIBLE"), 14u);
    EXPECT_EQ(s.entitiesByLayer.at("LINE@DIMENSION"), 90u);
    const DxfEntityCounts counted = countDxfEntities(prims);
    EXPECT_EQ(counted.lines, 108u);
    EXPECT_EQ(counted.texts, 10u);
    // Geometry is 1:1: the shelf edge runs from x = 18 to x = 782 at y = 351.
    EXPECT_NE(dxf.find("0\nLINE\n8\nVISIBLE\n10\n18\n20\n351\n30\n0\n11\n782\n21\n351\n31\n0\n"), std::string::npos);
    // Annotation sizes are multiplied by 10: text 25 mm high, width dimension line 161 mm below.
    EXPECT_NE(dxf.find("40\n25\n1\n800\n"), std::string::npos);
    EXPECT_NE(dxf.find("0\nLINE\n8\nDIMENSION\n10\n0\n20\n-161\n30\n0\n11\n800\n21\n-161\n31\n0\n"), std::string::npos);
    EXPECT_NE(dxf.find("9\n$LTSCALE\n40\n10\n"), std::string::npos);
    // Extents include the annotations: the outside arrows of the 18 mm chain ends reach
    // 1.6 * 25 = 40 mm past the outline -> max (840, 760).
    EXPECT_NE(dxf.find("9\n$EXTMAX\n10\n840\n20\n760\n"), std::string::npos);
}
