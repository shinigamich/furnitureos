// SPDX-License-Identifier: AGPL-3.0-only
// Label sheet export with barcode options: settings keys, files per page and the rendering
// warnings shown to the user (barcode omitted on labels too narrow for the minimum module).
#include "fos/app/label_export.hpp"
#include "test_support.hpp"

namespace fos::app::test {
namespace {

std::vector<production::Label> baseCabinetLabels(Session& s, const std::string& projectName) {
    EXPECT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    production::LabelOptions lo;
    lo.projectName = projectName;
    return production::labels(s.resolved()->model, lo).labels;
}

TEST(LabelExport, SettingsKeysRoundTrip) {
    LabelBarcodeSettings code;
    code.barcode = production::LabelBarcode::Code128;
    code.content = production::LabelBarcodeContent::ProjectAndPartId;
    EXPECT_EQ(labelBarcodeKey(code.barcode), QStringLiteral("code128"));
    EXPECT_EQ(labelBarcodeContentKey(code.content), QStringLiteral("project_and_part_id"));
    EXPECT_EQ(labelBarcodeSettings(labelBarcodeKey(code.barcode), labelBarcodeContentKey(code.content)), code);
    // Unknown / missing stored text falls back to the defaults.
    EXPECT_EQ(labelBarcodeSettings(QStringLiteral("qr"), QString()), LabelBarcodeSettings{});
    EXPECT_EQ(labelBarcodeChoices().size(), 2u);
    EXPECT_EQ(labelBarcodeContentChoices().size(), 2u);
    const auto options = labelSheetOptions(code);
    EXPECT_EQ(options.barcode, production::LabelBarcode::Code128);
    EXPECT_EQ(options.barcodeContent, production::LabelBarcodeContent::ProjectAndPartId);
    EXPECT_DOUBLE_EQ(options.labelWidth, 70.0);
}

// 70 mm labels: the barcode band is at most 70 mm wide. "<project>|<part id>" with a 33-character
// project name has L >= 33 + 1 + 6 = 40 characters -> 11 * (40 + 2) + 13 = 475 modules plus
// 2 * 10 quiet-zone modules = 495 -> module <= 70 / 495 = 0.141 mm < 0.25 mm minimum, so EVERY
// label keeps its text and reports production.label.barcode_too_narrow.
TEST(LabelExport, NarrowLabelsWarnAndNoBarcodeDoesNot) {
    Session s;
    const auto labels = baseCabinetLabels(s, "Kitchen for the Kovalenko family A");
    ASSERT_FALSE(labels.empty());
    TempDir dir;

    LabelBarcodeSettings code;
    code.barcode = production::LabelBarcode::Code128;
    code.content = production::LabelBarcodeContent::ProjectAndPartId;
    auto exported = exportLabelSheets(labels, dir.path / "labels.svg", code);
    ASSERT_TRUE(exported.ok()) << exported.error().message;
    ASSERT_EQ(exported.value().warnings.size(), labels.size());
    for (const auto& w : exported.value().warnings) {
        EXPECT_EQ(w.code, "production.label.barcode_too_narrow");
        EXPECT_FALSE(w.message.empty());
    }
    // 24 labels per A4 page (3 x 8): one file "labels.svg" or "labels-<n>.svg" per page.
    const std::size_t pages = (labels.size() + 23) / 24;
    ASSERT_EQ(exported.value().files.size(), pages);
    for (const auto& f : exported.value().files)
        EXPECT_TRUE(std::filesystem::exists(f));
    if (pages == 1) {
        EXPECT_EQ(exported.value().files[0].filename(), "labels.svg");
    }

    // Summary text: heading with the count, at most 2 messages, then "and N more".
    const QString text = labelWarningsText(exported.value().warnings, 2);
    EXPECT_TRUE(text.contains(QString::number(labels.size())));
    if (labels.size() > 2) {
        EXPECT_TRUE(text.contains(QString::number(labels.size() - 2) + QStringLiteral(" more")));
    }
    EXPECT_TRUE(labelWarningsText({}).isEmpty());

    // Without a barcode there is nothing to warn about.
    auto plain = exportLabelSheets(labels, dir.path / "plain.svg", LabelBarcodeSettings{});
    ASSERT_TRUE(plain.ok());
    EXPECT_TRUE(plain.value().warnings.empty());
    EXPECT_EQ(plain.value().files.size(), pages);
}

} // namespace
} // namespace fos::app::test
