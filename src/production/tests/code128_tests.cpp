// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/code128.hpp"
#include "fos/production/labels.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

#include <set>

using namespace fos;
using namespace fos::production;

namespace {
std::size_t count(const std::string& hay, const std::string& needle) {
    std::size_t k = 0;
    for (auto pos = hay.find(needle); pos != std::string::npos; pos = hay.find(needle, pos + needle.size())) ++k;
    return k;
}
std::vector<int> widthsOf(std::initializer_list<const char*> patterns) {
    std::vector<int> w;
    for (const char* p : patterns)
        for (const char* c = p; *c; ++c) w.push_back(*c - '0');
    return w;
}
} // namespace

TEST(Code128, PatternTableInvariants) {
    // ISO/IEC 15417: each symbol character is 3 bars + 3 spaces spanning 11 modules, the bar
    // modules are even and the space modules odd; all 106 patterns are distinct. Stop = 13 modules.
    std::set<std::string_view> seen;
    for (int v = 0; v <= 105; ++v) {
        const auto p = code128Pattern(v);
        ASSERT_EQ(p.size(), 6u) << v;
        int bars = 0, spaces = 0;
        for (std::size_t i = 0; i < 6; ++i) (i % 2 == 0 ? bars : spaces) += p[i] - '0';
        EXPECT_EQ(bars + spaces, 11) << v;
        EXPECT_EQ(bars % 2, 0) << v;
        EXPECT_EQ(spaces % 2, 1) << v;
        EXPECT_TRUE(seen.insert(p).second) << v;
    }
    EXPECT_EQ(code128Pattern(0), "212222");    // space ' '
    EXPECT_EQ(code128Pattern(33), "111323");   // 'A'
    EXPECT_EQ(code128Pattern(104), "211214");  // Start B
    EXPECT_EQ(code128Pattern(106), "2331112"); // Stop
    EXPECT_TRUE(code128Pattern(107).empty());
    EXPECT_TRUE(code128Pattern(-1).empty());
}

TEST(Code128, SingleCharacter) {
    // "A" = 65 - 32 = 33. Checksum = (104 + 1 * 33) mod 103 = 137 - 103 = 34.
    const auto r = encodeCode128B("A");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().values, (std::vector<int>{104, 33, 34, 106}));
    EXPECT_EQ(r.value().checksum, 34);
    EXPECT_EQ(r.value().widths, widthsOf({"211214", "111323", "131123", "2331112"}));
    EXPECT_EQ(r.value().modules(), 46); // 11 * 3 + 13
    EXPECT_EQ(r.value().widths.size(), 25u); // 3 * 6 + 7, starts and ends with a bar (odd count)
}

TEST(Code128, MultiCharacterChecksum) {
    // "PJJ123C": P=48, J=42, J=42, 1=17, 2=18, 3=19, C=35.
    // 104 + 1*48 + 2*42 + 3*42 + 4*17 + 5*18 + 6*19 + 7*35
    //   = 104 + 48 + 84 + 126 + 68 + 90 + 114 + 245 = 879; 879 mod 103 = 879 - 824 = 55.
    const auto r = encodeCode128B("PJJ123C");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().values, (std::vector<int>{104, 48, 42, 42, 17, 18, 19, 35, 55, 106}));
    EXPECT_EQ(r.value().checksum, 55);
    EXPECT_EQ(r.value().modules(), 112); // 11 * 9 + 13
    // "~~": 126 - 32 = 94 each; 104 + 94 + 2*94 = 386; 386 mod 103 = 386 - 309 = 77.
    const auto t = encodeCode128B("~~");
    ASSERT_TRUE(t.ok());
    EXPECT_EQ(t.value().checksum, 77);
    EXPECT_EQ(t.value().widths, widthsOf({"211214", "131141", "131141", "413111", "2331112"})); // 94, 94, 77
    // " " (value 0): checksum 104 mod 103 = 1 -> pattern "222122".
    EXPECT_EQ(encodeCode128B(" ").value().checksum, 1);
}

TEST(Code128, RejectsWhatSubsetBCannotEncode) {
    EXPECT_EQ(encodeCode128B("").error().code, core::ErrorCode::InvalidArgument);
    const auto tab = encodeCode128B("a\tb");
    ASSERT_FALSE(tab.ok());
    EXPECT_NE(tab.error().message.find("0x09"), std::string::npos);
    EXPECT_NE(tab.error().message.find("position 2"), std::string::npos);
    EXPECT_FALSE(encodeCode128B("\xC3\xA9").ok()); // UTF-8 e-acute
    EXPECT_FALSE(encodeCode128B("\x7F").ok());
}

TEST(LabelBarcode, DefaultOutputIsUnchanged) {
    const auto set = labels(domain::fixtures::basicCabinet());
    LabelSheetOptions o;
    o.barcodeContent = LabelBarcodeContent::ProjectAndPartId; // ignored without a barcode
    const auto plain = renderLabelSheetsSvg(set.labels);
    const auto sheets = renderLabelSheets(set.labels, o);
    ASSERT_TRUE(plain.ok());
    ASSERT_TRUE(sheets.ok());
    EXPECT_EQ(sheets.value().pages, plain.value());
    EXPECT_TRUE(sheets.value().diagnostics.empty());
    EXPECT_EQ(plain.value()[0].find("data-barcode"), std::string::npos);
    EXPECT_NE(plain.value()[0].find(">cab1/side.left<"), std::string::npos);
}

TEST(LabelBarcode, Code128BarsOnLabel) {
    const auto set = labels(domain::fixtures::basicCabinet());
    LabelSheetOptions o;
    o.barcode = LabelBarcode::Code128;
    const auto r = renderLabelSheets(set.labels, o);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().diagnostics.empty());
    const std::string& svg = r.value().pages[0];
    EXPECT_EQ(count(svg, "data-barcode=\"code128\""), set.labels.size());
    const auto at = svg.find("<g data-barcode=\"code128\" data-text=\"cab1/side.left\">");
    ASSERT_NE(at, std::string::npos);
    // The barcode replaces the part-id text line.
    EXPECT_EQ(svg.find(">cab1/side.left<"), std::string::npos);
    // "cab1/side.left" = 14 characters -> 11 * 16 + 13 = 189 modules + 2 * 10 quiet = 209.
    // Band = 70 - 2 * 2 = 66 mm -> X = 66 / 209 = 0.315789 mm. First bar (Start B "211214")
    // at 2 + 10 X = 5.157895, 2 X = 0.631579 wide; band rows 26..31.6 (scale 1).
    const std::string first = "<rect x=\"5.158\" y=\"26\" width=\"0.632\" height=\"5.6\"/>";
    EXPECT_EQ(svg.find(first, at), svg.find("<rect", at));
    // Bars: 16 six-element characters (Start, 14 data, checksum) x 3 + Stop 4 = 52.
    const auto end = svg.find("</g>", at);
    EXPECT_EQ(count(svg.substr(at, end - at), "<rect"), 52u);
    // Last bar = final 2-module bar of Stop ending at 2 + (10 + 189) X = 64.842 -> x = 64.842 - 0.632.
    EXPECT_NE(svg.substr(at, end - at).find("<rect x=\"64.211\" y=\"26\" width=\"0.632\""), std::string::npos);
    EXPECT_EQ(count(svg, "<g "), count(svg, "</g>"));
}

TEST(LabelBarcode, TooNarrowIsOmittedWithWarning) {
    LabelOptions lo;
    lo.projectName = "Kitchen";
    const auto set = labels(domain::fixtures::basicCabinet(), lo);
    LabelSheetOptions o;
    o.barcode = LabelBarcode::Code128;
    o.barcodeContent = LabelBarcodeContent::ProjectAndPartId;
    const auto r = renderLabelSheets(set.labels, o);
    ASSERT_TRUE(r.ok());
    ASSERT_FALSE(r.value().diagnostics.empty());
    const auto& d = r.value().diagnostics[0];
    EXPECT_EQ(d.severity, core::Severity::Warning);
    EXPECT_EQ(d.code, "production.label.barcode_too_narrow");
    EXPECT_EQ(d.entityId, "cab1/side.left");
    // "Kitchen|cab1/side.left" = 22 characters -> 11 * 24 + 13 + 20 = 297 modules;
    // 66 / 297 = 0.2222 mm < 0.25 mm.
    EXPECT_NE(d.message.find("297 modules"), std::string::npos) << d.message;
    EXPECT_NE(d.message.find("0.222 mm"), std::string::npos) << d.message;
    EXPECT_NE(d.message.find("0.25 mm"), std::string::npos) << d.message;
    const std::string& svg = r.value().pages[0];
    EXPECT_EQ(svg.find("data-text=\"Kitchen|cab1/side.left\""), std::string::npos);
    EXPECT_NE(svg.find(">cab1/side.left<"), std::string::npos); // text line kept
    // Each label either has a barcode or a warning, never both.
    EXPECT_EQ(count(svg, "data-barcode") + r.value().diagnostics.size(), set.labels.size());

    // A wider label fits it: 100 mm -> band 96 mm, 96 / 297 = 0.3232 mm.
    o.pageWidth = 300;
    o.labelWidth = 100;
    const auto wide = renderLabelSheets(set.labels, o);
    ASSERT_TRUE(wide.ok());
    EXPECT_TRUE(wide.value().diagnostics.empty());
    EXPECT_NE(wide.value().pages[0].find("data-text=\"Kitchen|cab1/side.left\""), std::string::npos);
}

TEST(LabelBarcode, UnencodableTextAndBadMinimum) {
    LabelOptions lo;
    lo.projectName = "\xD0\x9F"; // Cyrillic capital Pe (UTF-8)
    const auto set = labels(domain::fixtures::basicCabinet(), lo);
    LabelSheetOptions o;
    o.barcode = LabelBarcode::Code128;
    o.barcodeContent = LabelBarcodeContent::ProjectAndPartId;
    const auto r = renderLabelSheets(set.labels, o);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().diagnostics.size(), set.labels.size());
    EXPECT_EQ(r.value().diagnostics[0].code, "production.label.barcode_unencodable");
    EXPECT_EQ(labelBarcodeText(set.labels[0], LabelBarcodeContent::PartId), "cab1/side.left");

    o.barcodeMinModule = 0;
    EXPECT_EQ(renderLabelSheets(set.labels, o).error().code, core::ErrorCode::InvalidArgument);
}
