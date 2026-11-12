// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

#include "fos/cutting/svg.hpp"

using namespace fos::cutting;
using namespace fos::cutting::test;

namespace {

std::size_t count(const std::string& haystack, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t pos = haystack.find(needle); pos != std::string::npos; pos = haystack.find(needle, pos + 1)) ++n;
    return n;
}

} // namespace

TEST(Svg, EscapesXmlSpecialCharacters) {
    EXPECT_EQ(xmlEscape("A&B <\"x\"> 'y'"), "A&amp;B &lt;&quot;x&quot;&gt; &apos;y&apos;");
    EXPECT_EQ(xmlEscape("\xD0\x91\xD1\x96\xD0\xBA"), "\xD0\x91\xD1\x96\xD0\xBA"); // UTF-8 passes through
    EXPECT_EQ(xmlEscape(std::string("a\x01" "b")), "a b");
}

TEST(Svg, ExactMillimetreViewBoxWithoutLegend) {
    Job job = makeJob(2000, 1000, 4);
    job.trim = Trim{10, 10, 10, 10};
    addPiece(job, "side", 1500, 600, 1, false);
    job.pieces.back().label = "Side <L&R>";
    addPiece(job, "door", 400, 900, 1, true);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    SvgOptions o;
    o.legend = false;
    const std::string svg = renderSheetSvg(job, r.sheets[0], o);

    EXPECT_NE(svg.find("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"), std::string::npos);
    EXPECT_NE(svg.find("width=\"2000mm\" height=\"1000mm\" viewBox=\"0 0 2000 1000\""), std::string::npos);
    // The label is escaped, never emitted raw.
    EXPECT_NE(svg.find("Side &lt;L&amp;R&gt;"), std::string::npos);
    EXPECT_EQ(svg.find("<L&R>"), std::string::npos);
    // One rect per placement, per offcut and per split-cut kerf strip.
    const SheetLayout& s = r.sheets[0];
    std::size_t reusable = 0, splits = 0;
    for (const Offcut& oc : s.offcuts) reusable += oc.reusable ? 1 : 0;
    for (const Cut& c : s.cuts) splits += c.kind == CutKind::Split ? 1 : 0;
    EXPECT_EQ(count(svg, "<rect class=\"fos-piece\""), s.placements.size());
    EXPECT_EQ(count(svg, "<rect class=\"fos-remnant\""), reusable);
    EXPECT_EQ(count(svg, "<rect class=\"fos-waste\""), s.offcuts.size() - reusable);
    EXPECT_EQ(count(svg, "<rect class=\"fos-kerf\""), splits);
    EXPECT_EQ(count(svg, "<rect class=\"fos-trim\""), 1u);
    EXPECT_EQ(count(svg, "<rect class=\"fos-usable\" x=\"10\" y=\"10\" width=\"1980\" height=\"980\""), 1u);
    // Size text with the multiplication sign.
    EXPECT_NE(svg.find("1500 \xC3\x97 600"), std::string::npos);
    // Balanced groups and a closed document.
    EXPECT_EQ(count(svg, "<g "), count(svg, "</g>"));
    EXPECT_EQ(count(svg, "<text "), count(svg, "</text>"));
    EXPECT_EQ(svg.substr(svg.size() - 7), "</svg>\n");
}

// The sheet origin is drawn bottom-left: a piece at sheet (0, 0) of size 600 x 400 on a
// 1000 x 500 sheet has its SVG rect at x = 0, y = 500 - 0 - 400 = 100.
TEST(Svg, SheetOriginIsBottomLeftAndRotationIsMarked) {
    Job job = makeJob(1000, 500, 4);
    addPiece(job, "p", 600, 400, 1, false);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    const std::string svg = renderSheetSvg(job, r.sheets[0]);
    EXPECT_NE(svg.find("<rect class=\"fos-piece\" x=\"0\" y=\"100\" width=\"600\" height=\"400\"/>"), std::string::npos);
    EXPECT_NE(svg.find("data-rotated=\"false\""), std::string::npos);
    // Legend present by default and inside an enlarged (still 1 unit = 1 mm) viewBox.
    EXPECT_NE(svg.find("Reusable remnant"), std::string::npos);
    EXPECT_NE(svg.find("Saw kerf"), std::string::npos);
    EXPECT_NE(svg.find("Edge trim"), std::string::npos);
    EXPECT_NE(svg.find("Waste"), std::string::npos);
    EXPECT_NE(svg.find("viewBox=\"0 0 1000 "), std::string::npos);
    EXPECT_EQ(svg.find("viewBox=\"0 0 1000 500\""), std::string::npos);

    // A piece that only fits turned: 400 x 900 on a 1000 x 500 sheet -> rotated marker U+21BB.
    Job turned = makeJob(1000, 500, 4);
    addPiece(turned, "door", 400, 900, 1, true);
    const CuttingResult rt = solve(turned);
    expectValid(turned, rt);
    ASSERT_TRUE(rt.sheets[0].placements[0].rotated);
    const std::string svg2 = renderSheetSvg(turned, rt.sheets[0]);
    EXPECT_NE(svg2.find("data-rotated=\"true\""), std::string::npos);
    EXPECT_NE(svg2.find(">\xE2\x86\xBB door<"), std::string::npos);
    EXPECT_NE(svg2.find("900 \xC3\x97 400"), std::string::npos); // placed size: 900 along X
}
