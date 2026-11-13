// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Small dialogs: template size, preferences, keyboard shortcuts.
#include "fos/app/templates.hpp"
#include "settings.hpp"

#include <QDialog>

class QAction;
class QComboBox;
class QDoubleSpinBox;
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

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(const Preferences& current, QWidget* parent = nullptr);
    Preferences preferences() const;

private:
    QComboBox* language_;
    QSpinBox* autosave_;
};

/// Lists the shortcuts of the given actions (read from the actions themselves).
class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    ShortcutsDialog(const QList<QAction*>& actions, QWidget* parent = nullptr);
};

} // namespace fos::app::gui
