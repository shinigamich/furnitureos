// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Part label sheet export shared by the main window and the batch window: the barcode choice
/// (production::LabelBarcode / LabelBarcodeContent), its QSettings text keys, and the writing of
/// the A4 70 x 37 mm SVG pages with the rendering warnings (e.g. a barcode omitted because the
/// label is too narrow for `barcodeMinModule`) handed back for the user.
#include "fos/core/result.hpp"
#include "fos/production/labels.hpp"

#include <QString>

#include <filesystem>
#include <utility>
#include <vector>

namespace fos::app {

/// Barcode settings of a label export (persisted by the GUI as text keys, below).
struct LabelBarcodeSettings {
    production::LabelBarcode barcode = production::LabelBarcode::None;
    production::LabelBarcodeContent content = production::LabelBarcodeContent::PartId;
    bool operator==(const LabelBarcodeSettings&) const = default;
};

/// Stable text keys for QSettings: "none" / "code128" and "part_id" / "project_and_part_id".
QString labelBarcodeKey(production::LabelBarcode barcode);
QString labelBarcodeContentKey(production::LabelBarcodeContent content);
/// Inverse of the keys; unknown text -> the defaults (None / PartId).
LabelBarcodeSettings labelBarcodeSettings(const QString& barcodeKey, const QString& contentKey);

/// Translated combo entries in enum order.
std::vector<std::pair<QString, production::LabelBarcode>> labelBarcodeChoices();
std::vector<std::pair<QString, production::LabelBarcodeContent>> labelBarcodeContentChoices();

/// Default sheet layout (A4, 70 x 37 mm) with the translated captions and `settings`.
production::LabelSheetOptions labelSheetOptions(const LabelBarcodeSettings& settings);

/// Files written by a label export plus the rendering warnings (in label order).
struct LabelExport {
    std::vector<std::filesystem::path> files;
    core::Diagnostics warnings;
};

/// Renders `labels` (production::renderLabelSheets) and writes one SVG per page: a single page
/// to `file`, several pages as "<stem>-<n><ext>" next to it. Errors: invalid layout, write failure.
/// No labels -> no files.
core::Result<LabelExport> exportLabelSheets(const std::vector<production::Label>& labels,
                                            const std::filesystem::path& file, const LabelBarcodeSettings& settings);

/// Translated summary of the warnings for a message box: a heading with the count, then up to
/// `maxLines` engine messages (English, verbatim) and "… and N more". Empty for no warnings.
QString labelWarningsText(const core::Diagnostics& warnings, int maxLines = 10);

} // namespace fos::app
