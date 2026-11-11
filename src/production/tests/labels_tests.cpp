// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/labels.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

using namespace fos;
using namespace fos::production;

namespace {
std::size_t count(const std::string& hay, const std::string& needle) {
    std::size_t n = 0;
    for (auto pos = hay.find(needle); pos != std::string::npos; pos = hay.find(needle, pos + needle.size())) ++n;
    return n;
}
} // namespace

TEST(Labels, BasicCabinet) {
    LabelOptions o;
    o.projectName = "Kitchen";
    const auto set = labels(domain::fixtures::basicCabinet(), o);
    EXPECT_TRUE(set.diagnostics.empty());
    ASSERT_EQ(set.labels.size(), 6u);

    const auto& left = set.labels[0];
    EXPECT_EQ(left.projectName, "Kitchen");
    EXPECT_EQ(left.assemblyName, "Base cabinet 800");
    EXPECT_EQ(left.partName, "Left side");
    EXPECT_EQ(left.partId.str(), "cab1/side.left");
    EXPECT_EQ(left.partNumber, 1);
    EXPECT_DOUBLE_EQ(left.length, 720);
    EXPECT_NEAR(left.cutLength, 719.2, 1e-9);
    EXPECT_NEAR(left.cutWidth, 558, 1e-9);
    EXPECT_EQ(left.materialName, "Particleboard 18 mm");
    EXPECT_EQ(left.edgeCode, "1011"); // L1 banded, L2 raw, W1 + W2 banded
    EXPECT_DOUBLE_EQ(left.edgeThickness[0], 2.0);
    EXPECT_DOUBLE_EQ(left.edgeThickness[1], 0.0);
    EXPECT_DOUBLE_EQ(left.edgeThickness[2], 0.4);
    EXPECT_EQ(left.quantityText(), "1/1"); // holes -> its own group

    EXPECT_EQ(set.labels[1].quantityText(), "1/1"); // right side
    EXPECT_EQ(set.labels[2].partName, "Bottom");
    EXPECT_EQ(set.labels[2].edgeCode, "1000");
    EXPECT_EQ(set.labels[2].quantityText(), "1/2");
    EXPECT_EQ(set.labels[3].partName, "Top");
    EXPECT_EQ(set.labels[3].quantityText(), "2/2");
    EXPECT_EQ(set.labels[5].edgeCode, "0000");
    EXPECT_EQ(set.labels[5].materialName, "HDF 3 mm");
}

TEST(Labels, NumberingIsUniqueAndStable) {
    const auto m = production::testing::twoCabinets();
    const auto a = labels(m), b = labels(production::testing::twoCabinets());
    ASSERT_EQ(a.labels.size(), 12u);
    for (std::size_t i = 0; i < 12; ++i) {
        EXPECT_EQ(a.labels[i].partNumber, static_cast<int>(i) + 1); // unique, sequential
        EXPECT_EQ(a.labels[i].partNumber, b.labels[i].partNumber);  // same on regeneration
        EXPECT_EQ(a.labels[i].partId, b.labels[i].partId);
        EXPECT_EQ(a.labels[i].quantityText(), b.labels[i].quantityText());
    }
    // Group "bottom/top" has parts 3, 4, 9, 10 -> the top of cab2 (part 10) is 4/4.
    EXPECT_EQ(a.labels[9].partId.str(), "cab2/top");
    EXPECT_EQ(a.labels[9].quantityText(), "4/4");
    EXPECT_EQ(a.labels[6].quantityText(), "2/2"); // left side of cab2

    // Scoping to one assembly keeps the model-wide numbers; the index counts within the scope.
    const auto scoped = labels(m, {}, Scope{domain::AssemblyId("cab2")});
    ASSERT_EQ(scoped.labels.size(), 6u);
    EXPECT_EQ(scoped.labels[3].partNumber, 10);
    EXPECT_EQ(scoped.labels[3].quantityText(), "2/2");

    // Without grouping across assemblies the index restarts per cabinet.
    LabelOptions per;
    per.cutList.groupAcrossAssemblies = false;
    EXPECT_EQ(labels(m, per).labels[9].quantityText(), "2/2");
}

TEST(LabelSheet, GridFromLabelSize) {
    auto g = labelGrid({});
    ASSERT_TRUE(g.ok());
    EXPECT_EQ(g.value().columns, 3); // 210 / 70
    EXPECT_EQ(g.value().rows, 8);    // (297 - 0.5) / 37 = 8.01
    EXPECT_EQ(g.value().perPage(), 24);

    LabelSheetOptions o;
    o.labelWidth = 100;
    o.labelHeight = 50;
    o.marginLeft = 5;
    o.marginTop = 5;
    g = labelGrid(o);
    ASSERT_TRUE(g.ok());
    EXPECT_EQ(g.value().columns, 2); // floor(205 / 100)
    EXPECT_EQ(g.value().rows, 5);    // floor(292 / 50)

    // Gaps: 3 x 63.5 + 2 x 2.5 = 195.5 <= 210 - 7 = 203; a 4th column would need 261.5.
    o = {};
    o.labelWidth = 63.5;
    o.labelHeight = 38.1;
    o.marginLeft = 7;
    o.marginTop = 15;
    o.gapX = 2.5;
    g = labelGrid(o);
    ASSERT_TRUE(g.ok());
    EXPECT_EQ(g.value().columns, 3);
    EXPECT_EQ(g.value().rows, 7); // floor(282 / 38.1) = 7 (266.7 mm)
}

TEST(LabelSheet, InvalidLayoutIsAnErrorNotACrash) {
    LabelSheetOptions o;
    o.labelWidth = 300; // wider than A4
    auto r = renderLabelSheetsSvg(labels(domain::fixtures::basicCabinet()).labels, o);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_NE(r.error().message.find("300"), std::string::npos);
    EXPECT_NE(r.error().message.find("210"), std::string::npos);
    o = {};
    o.labelHeight = 0;
    EXPECT_FALSE(labelGrid(o).ok());
    o = {};
    o.gapX = -1;
    EXPECT_FALSE(labelGrid(o).ok());
    o = {};
    o.pageWidth = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(labelGrid(o).ok());
}

TEST(LabelSheet, SvgIsExactMillimetresAndEscaped) {
    auto m = domain::fixtures::basicCabinet();
    m.assemblies[0].name = "Тумба <800> & \"кухня\"";
    m.assemblies[0].parts[0].name = "Боковина ліва";
    LabelOptions lo;
    lo.projectName = "Проєкт №1";
    const auto set = labels(m, lo);
    const auto pages = renderLabelSheetsSvg(set.labels);
    ASSERT_TRUE(pages.ok());
    ASSERT_EQ(pages.value().size(), 1u);
    const std::string& svg = pages.value()[0];

    EXPECT_EQ(svg.rfind("<?xml version=\"1.0\" encoding=\"UTF-8\"?>", 0), 0u);
    EXPECT_NE(svg.find("width=\"210mm\" height=\"297mm\" viewBox=\"0 0 210 297\""), std::string::npos);
    // Cyrillic survives byte for byte; markup characters are escaped.
    EXPECT_NE(svg.find("Боковина ліва"), std::string::npos);
    EXPECT_NE(svg.find("Проєкт №1"), std::string::npos);
    EXPECT_NE(svg.find("Тумба &lt;800&gt; &amp; &quot;кухня&quot;"), std::string::npos);
    EXPECT_EQ(svg.find("<800>"), std::string::npos);
    // Content of the first label.
    EXPECT_NE(svg.find("No. 1  Боковина ліва"), std::string::npos);
    EXPECT_NE(svg.find("720 × 560 × 18"), std::string::npos);
    EXPECT_NE(svg.find("Cut 719.2 × 558"), std::string::npos);
    EXPECT_NE(svg.find(">cab1/side.left<"), std::string::npos);
    EXPECT_NE(svg.find(">1011<"), std::string::npos);
    EXPECT_NE(svg.find(">2/2<"), std::string::npos);
    // Six labels, each a translated group; default grid is 3 columns of 70 x 37 from (0, 0.5).
    EXPECT_EQ(count(svg, "<clipPath"), 6u);
    EXPECT_NE(svg.find("<g transform=\"translate(0 0.5)\">"), std::string::npos);
    EXPECT_NE(svg.find("<g transform=\"translate(140 0.5)\">"), std::string::npos); // 3rd label
    EXPECT_NE(svg.find("<g transform=\"translate(0 37.5)\">"), std::string::npos);  // 4th label
    // Crude well-formedness: tags balance.
    EXPECT_EQ(count(svg, "<g "), count(svg, "</g>"));
    EXPECT_EQ(count(svg, "<text "), count(svg, "</text>"));
    EXPECT_EQ(count(svg, "<svg "), 1u);
    EXPECT_EQ(svg.substr(svg.size() - 7), "</svg>\n");
}

TEST(LabelSheet, PaginationCustomSizeAndCaptions) {
    const auto set = labels(production::testing::twoCabinets()); // 12 labels
    LabelSheetOptions o;
    o.pageWidth = 100;
    o.pageHeight = 150;
    o.labelWidth = 50;
    o.labelHeight = 30;
    o.marginTop = 0;
    o.text.cut = "Розкрій";
    o.text.number = "№";
    o.drawBorders = false;
    // 2 columns x 5 rows = 10 per page -> 12 labels need 2 pages (10 + 2).
    const auto pages = renderLabelSheetsSvg(set.labels, o);
    ASSERT_TRUE(pages.ok());
    ASSERT_EQ(pages.value().size(), 2u);
    EXPECT_NE(pages.value()[0].find("width=\"100mm\" height=\"150mm\" viewBox=\"0 0 100 150\""), std::string::npos);
    EXPECT_EQ(count(pages.value()[0], "<clipPath"), 10u);
    EXPECT_EQ(count(pages.value()[1], "<clipPath"), 2u);
    EXPECT_NE(pages.value()[1].find("№ 11  "), std::string::npos);
    EXPECT_NE(pages.value()[0].find("Розкрій 719.2"), std::string::npos);
    EXPECT_EQ(pages.value()[0].find("Cut "), std::string::npos);
    EXPECT_EQ(pages.value()[0].find("stroke=\"#999\""), std::string::npos); // no borders
    EXPECT_NE(pages.value()[0].find("<g transform=\"translate(50 120)\">"), std::string::npos); // 10th

    const auto none = renderLabelSheetsSvg({}, o);
    ASSERT_TRUE(none.ok());
    EXPECT_TRUE(none.value().empty());
}
