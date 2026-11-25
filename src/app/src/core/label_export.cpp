// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/label_export.hpp"

#include "fos/app/production_text.hpp"
#include "fos/production/csv.hpp"

#include <QCoreApplication>
#include <QStringList>

namespace fos::app {

namespace {
class LabelExportText {
    Q_DECLARE_TR_FUNCTIONS(LabelExportText)
};
} // namespace

QString labelBarcodeKey(production::LabelBarcode barcode) {
    return barcode == production::LabelBarcode::Code128 ? QStringLiteral("code128") : QStringLiteral("none");
}

QString labelBarcodeContentKey(production::LabelBarcodeContent content) {
    return content == production::LabelBarcodeContent::ProjectAndPartId ? QStringLiteral("project_and_part_id")
                                                                        : QStringLiteral("part_id");
}

LabelBarcodeSettings labelBarcodeSettings(const QString& barcodeKey, const QString& contentKey) {
    LabelBarcodeSettings s;
    if (barcodeKey == QLatin1String("code128"))
        s.barcode = production::LabelBarcode::Code128;
    if (contentKey == QLatin1String("project_and_part_id"))
        s.content = production::LabelBarcodeContent::ProjectAndPartId;
    return s;
}

std::vector<std::pair<QString, production::LabelBarcode>> labelBarcodeChoices() {
    return {{LabelExportText::tr("No barcode"), production::LabelBarcode::None},
            {LabelExportText::tr("Code 128"), production::LabelBarcode::Code128}};
}

std::vector<std::pair<QString, production::LabelBarcodeContent>> labelBarcodeContentChoices() {
    return {{LabelExportText::tr("Part id"), production::LabelBarcodeContent::PartId},
            {LabelExportText::tr("Project name and part id"), production::LabelBarcodeContent::ProjectAndPartId}};
}

production::LabelSheetOptions labelSheetOptions(const LabelBarcodeSettings& settings) {
    production::LabelSheetOptions options;
    options.text = ProductionText::labelText();
    options.barcode = settings.barcode;
    options.barcodeContent = settings.content;
    return options;
}

core::Result<LabelExport> exportLabelSheets(const std::vector<production::Label>& labels,
                                            const std::filesystem::path& file, const LabelBarcodeSettings& settings) {
    auto sheets = production::renderLabelSheets(labels, labelSheetOptions(settings));
    if (!sheets)
        return sheets.error();
    LabelExport out;
    out.warnings = std::move(sheets.value().diagnostics);
    const auto& pages = sheets.value().pages;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        std::filesystem::path path = file;
        if (pages.size() > 1)
            path.replace_filename(file.stem().string() + "-" + std::to_string(i + 1) + file.extension().string());
        if (auto r = production::writeTextFile(path, pages[i]); !r)
            return r.error();
        out.files.push_back(std::move(path));
    }
    return out;
}

QString labelWarningsText(const core::Diagnostics& warnings, int maxLines) {
    if (warnings.empty())
        return {};
    QStringList lines;
    lines << LabelExportText::tr("%n label warning(s):", nullptr, static_cast<int>(warnings.size()));
    const int shown = std::min(static_cast<int>(warnings.size()), std::max(maxLines, 0));
    for (int i = 0; i < shown; ++i)
        lines << QStringLiteral("• ") + QString::fromStdString(warnings[static_cast<std::size_t>(i)].message);
    if (static_cast<int>(warnings.size()) > shown)
        lines << LabelExportText::tr("… and %n more.", nullptr, static_cast<int>(warnings.size()) - shown);
    return lines.join(QLatin1Char('\n'));
}

} // namespace fos::app
