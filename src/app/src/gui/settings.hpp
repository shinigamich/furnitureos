// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Persistent user preferences (QSettings, organisation/application "FurnitureOS").
#include "fos/app/label_export.hpp"
#include "fos/app/update_plan.hpp"

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
    /// Automatic update check at startup ("updates/startupCheck"): asked once on first run;
    /// nothing is checked automatically before the user agreed.
    UpdateConsent updateConsent = UpdateConsent::NotAsked;
    /// Update channel ("updates/includePrereleases"): false = stable releases only.
    bool includePrereleases = false;

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

/// Installs the translators of `language` ("en" or "uk"): the application catalogue
/// (:/i18n/furnitureos_<language>) and Qt's own (qtbase_<language>), replacing the ones a
/// previous call installed, and sets the default QLocale (Ukrainian for "uk", the system locale
/// for "en"). Installing a translator sends QEvent::LanguageChange to every widget, so widgets
/// that retranslate in changeEvent() (WelcomeDialog) switch at once; most windows build their
/// texts once, which is why a language change otherwise needs a restart.
void applyLanguage(const QString& language);

/// Whether the Welcome dialog was already shown at start-up (QSettings "ui/welcomeShown").
bool welcomeShown();
void setWelcomeShown();

/// The folders the component browser scans: the stored list, or the default per-user
/// folder (created on first use) when the list is empty.
std::vector<std::filesystem::path> effectiveComponentFolders(const Preferences& preferences);

} // namespace fos::app::gui
