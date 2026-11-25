// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Persistent user preferences (QSettings, organisation/application "FurnitureOS").
#include "fos/app/label_export.hpp"

#include <QString>
#include <QStringList>

#include <filesystem>
#include <vector>

namespace fos::app::gui {

struct Preferences {
    QString language;   ///< "" = follow the system locale, otherwise "en" or "uk".
    int autosaveMinutes = 5; ///< 0 = autosave off.
    /// Component library folders of the "Components" dock; empty = the default per-user
    /// folder (fos::app::defaultComponentFolder()).
    QStringList componentFolders;

    static Preferences load();
    void save() const;
};

/// Barcode choice of the last label export (QSettings "export/labelBarcode",
/// "export/labelBarcodeContent"; text keys of label_export.hpp).
LabelBarcodeSettings loadLabelBarcodeSettings();
void saveLabelBarcodeSettings(const LabelBarcodeSettings& settings);

/// Language actually used: the stored choice, otherwise "uk" for a Ukrainian system locale
/// and "en" for everything else.
QString effectiveLanguage(const Preferences& preferences);

/// The folders the component browser scans: the stored list, or the default per-user
/// folder (created on first use) when the list is empty.
std::vector<std::filesystem::path> effectiveComponentFolders(const Preferences& preferences);

} // namespace fos::app::gui
