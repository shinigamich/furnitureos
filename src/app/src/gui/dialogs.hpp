// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Small dialogs: template size, preferences, keyboard shortcuts, label barcode options.
#include "fos/app/templates.hpp"
#include "settings.hpp"

#include <QDialog>

#include <optional>

class QAction;
class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QSpinBox;

namespace fos::app::gui {

/// Width / height / depth of a new cabinet, pre-filled with the template's own size.
class TemplateSizeDialog : public QDialog {
    Q_OBJECT
public:
    explicit TemplateSizeDialog(TemplateKind kind, QWidget* parent = nullptr);
    TemplateSize size() const;

private:
    QDoubleSpinBox* width_;
    QDoubleSpinBox* height_;
    QDoubleSpinBox* depth_;
};

/// Asks for the barcode of a label export, pre-filled from and saved to QSettings
/// (loadLabelBarcodeSettings / saveLabelBarcodeSettings). std::nullopt when cancelled.
std::optional<LabelBarcodeSettings> askLabelBarcode(QWidget* parent);

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(const Preferences& current, QWidget* parent = nullptr);
    Preferences preferences() const;

private:
    QComboBox* language_;
    QSpinBox* autosave_;
    QListWidget* componentFolders_; ///< Empty list = default per-user folder.
};

/// Lists the shortcuts of the given actions (read from the actions themselves).
class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    ShortcutsDialog(const QList<QAction*>& actions, QWidget* parent = nullptr);
};

} // namespace fos::app::gui
