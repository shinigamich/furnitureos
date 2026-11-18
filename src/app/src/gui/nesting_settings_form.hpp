// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Router nesting settings of the shaped parts (all materials): tool diameter, clearance, chord
/// tolerance, sheet margins, iterations and seed, applied as commands::setNestingSettings
/// (undoable). The command checks the ranges; a refusal is shown inline with its message.
#include <QGroupBox>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class NestingSettingsForm : public QGroupBox {
    Q_OBJECT
public:
    explicit NestingSettingsForm(Session& session, QWidget* parent = nullptr);
    /// Shows the stored settings (or the defaults) and clears the error.
    void load();
    /// Same as pressing "Apply and re-nest"; false when refused (message in errorText()).
    bool apply();
    QString errorText() const;

private:
    Session& session_;
    QDoubleSpinBox* tool_;
    QDoubleSpinBox* clearance_;
    QDoubleSpinBox* chord_;
    QDoubleSpinBox* marginLeft_;
    QDoubleSpinBox* marginRight_;
    QDoubleSpinBox* marginBottom_;
    QDoubleSpinBox* marginTop_;
    QSpinBox* iterations_;
    QLineEdit* seed_;
    QLabel* error_;
};

} // namespace fos::app::gui
