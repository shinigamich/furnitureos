// SPDX-License-Identifier: AGPL-3.0-only
// Packing workspace view-model (rows over production::packingList, assembly sheet rich text,
// exports) and the material density field of the library editor.
#include "fos/app/library_editor.hpp"
#include "fos/app/packing_rows.hpp"
#include "fos/domain/fixtures.hpp"
#include "test_support.hpp"

#include <fstream>
#include <limits>
#include <sstream>

namespace fos::app::test {
namespace {

/// basicCabinet with particleboard 650 kg/m3 and HDF `hdfDensity` (see production packing tests):
///   sides 4.71744 kg each, bottom/top 5.005728, shelf 4.826952, back 0.569936 m2 x 3 mm x 800 = 1.3678464 kg.
domain::Model weighedCabinet(double hdfDensity) {
    auto m = domain::fixtures::basicCabinet();
    m.materials[0].density = 650;
    m.materials[1].density = hdfDensity;
    return m;
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

TEST(PackingRows, PackagesBagsAndUnknownWeight) {
    auto list = projectPackingList(weighedCabinet(800), PackingInput{}); // 25 kg, padding 10 mm
    ASSERT_TRUE(list) << list.error().message;
    auto rows = PackingText::rows(list.value());
    ASSERT_EQ(rows.packages.size(), 2u);
    // P1 = back, bottom, top, shelf, side: 25.6411344 - 4.71744 = 20.9236944 kg; 816 x 736 x (3 + 4*18 + 20) = 95.
    EXPECT_EQ(rows.packages[0].package, QStringLiteral("1/2"));
    EXPECT_EQ(rows.packages[0].id, QStringLiteral("cab1-P1"));
    EXPECT_EQ(rows.packages[0].size, QStringLiteral("816 × 736 × 95 mm"));
    EXPECT_EQ(rows.packages[0].weight, QStringLiteral("20.92 kg"));
    EXPECT_FALSE(rows.packages[0].weightUnknown);
    EXPECT_TRUE(rows.packages[0].parts.startsWith(QStringLiteral("#6 "))) << rows.packages[0].parts.toStdString();
    // P2 = the second side alone: 740 x 580 x 38, 4.71744 kg.
    EXPECT_EQ(rows.packages[1].size, QStringLiteral("740 × 580 × 38 mm"));
    EXPECT_EQ(rows.packages[1].weight, QStringLiteral("4.72 kg"));
    EXPECT_TRUE(rows.packages[1].parts.startsWith(QStringLiteral("#2 ")));
    ASSERT_EQ(rows.bags.size(), 1u); // 4 shelf pins, a fitting
    EXPECT_EQ(rows.bags[0].quantity, 4);
    EXPECT_EQ(rows.bags[0].group, QStringLiteral("Fittings"));
    EXPECT_TRUE(PackingText::unknownWeightBanner(list.value()).isEmpty());

    // HDF density unknown: the back gets its own unknown-weight package; the rest stays known.
    list = projectPackingList(weighedCabinet(0), PackingInput{});
    ASSERT_TRUE(list) << list.error().message;
    rows = PackingText::rows(list.value());
    ASSERT_FALSE(rows.packages.empty());
    EXPECT_TRUE(rows.packages.back().weightUnknown);
    EXPECT_EQ(rows.packages.back().weight, QStringLiteral("unknown"));
    EXPECT_FALSE(rows.packages.front().weightUnknown);
    EXPECT_TRUE(PackingText::unknownWeightBanner(list.value()).contains(QStringLiteral("1 package")));
    EXPECT_FALSE(rows.warnings.isEmpty()); // production.packing.density_unknown
    const std::string csv = packingCsv(list.value());
    EXPECT_EQ(csv.rfind("\xEF\xBB\xBF", 0), 0u); // UTF-8 BOM
    EXPECT_NE(csv.find("unknown"), std::string::npos);

    PackingInput bad;
    bad.maxWeightKg = 0;
    EXPECT_FALSE(projectPackingList(weighedCabinet(800), bad));
    bad = {};
    bad.padding = -1;
    EXPECT_FALSE(projectPackingList(weighedCabinet(800), bad));
}

TEST(PackingRows, AssemblySheetsAndExports) {
    const auto model = weighedCabinet(800);
    const auto sheets = production::assemblySheets(model);
    ASSERT_EQ(sheets.sheets.size(), 1u);
    const QString html = PackingText::assemblyHtml(sheets);
    EXPECT_TRUE(html.contains(PackingText::assemblyOrderNote().toHtmlEscaped()));
    EXPECT_TRUE(html.contains(QStringLiteral("Step 1: Carcass"))) << html.toStdString();
    EXPECT_TRUE(html.contains(QStringLiteral("<b>4 ×</b>"))) << html.toStdString(); // shelf pins
    EXPECT_TRUE(PackingText::assemblyHtml(production::AssemblySheets{}).contains(QStringLiteral("no assemblies")));

    TempDir dir;
    auto files = exportAssemblySheets(sheets, "Kitchen", dir.path / "a.svg");
    ASSERT_TRUE(files) << files.error().message;
    ASSERT_EQ(files.value().size(), 1u); // one sheet -> the name as given
    EXPECT_EQ(files.value()[0], dir.path / "a.svg");
    EXPECT_NE(readFile(files.value()[0]).find("<svg"), std::string::npos);
    EXPECT_FALSE(exportAssemblySheets(production::AssemblySheets{}, "x", dir.path / "b.svg"));

    const auto list = projectPackingList(model, PackingInput{});
    ASSERT_TRUE(list);
    files = exportPackageLabels(list.value(), "Kitchen", dir.path / "labels.svg");
    ASSERT_TRUE(files) << files.error().message;
    ASSERT_EQ(files.value().size(), 1u); // 2 packages + 1 bag fit on one A4 page (24 labels)
    EXPECT_NE(readFile(files.value()[0]).find("Kitchen"), std::string::npos);

    // Several pages are numbered.
    files = writeSvgPages({"<svg/>", "<svg/>"}, dir.path / "p.svg");
    ASSERT_TRUE(files);
    ASSERT_EQ(files.value().size(), 2u);
    EXPECT_EQ(files.value()[1], dir.path / "p-2.svg");
}

TEST(LibraryEditor, MaterialDensityIsValidated) {
    Session s;
    s.newProject();
    domain::Material m = newMaterial(s.project());
    EXPECT_DOUBLE_EQ(m.density, 0.0); // 0 = unknown is valid
    EXPECT_TRUE(validateMaterial(s.project(), m, true).isEmpty());
    m.density = -1;
    auto problems = validateMaterial(s.project(), m, true);
    ASSERT_EQ(problems.size(), 1);
    EXPECT_TRUE(problems[0].contains(QStringLiteral("Density"))) << problems[0].toStdString();
    m.density = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(validateMaterial(s.project(), m, true).size(), 1);
    m.density = 650;
    ASSERT_TRUE(applyMaterial(s, m, true).isEmpty());
    EXPECT_DOUBLE_EQ(s.project().libraries.findMaterial(m.id)->density, 650.0);
}

} // namespace
} // namespace fos::app::test
