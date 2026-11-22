// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Router nesting settings of the shaped parts (all materials): tool diameter, clearance, chord
/// tolerance, sheet margins, iterations and seed, applied as commands::setNestingSettings
/// (undoable). The command checks the ranges; a refusal is shown inline with its message.
#include "fos/core/result.hpp"
#include "fos/project/commands.hpp"

#include <QGroupBox>

#include <functional>

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
    /// Edits the settings of another holder (e.g. a production batch): `project` returns a
    /// project carrying the settings, `execute` applies the setNestingSettings command.
    NestingSettingsForm(std::function<project::Project()> project,
                        std::function<core::Result<void>(const project::Command&)> execute, QWidget* parent = nullptr);
    /// Shows the stored settings (or the defaults) and clears the error.
    void load();
    /// Same as pressing "Apply and re-nest"; false when refused (message in errorText()).
    bool apply();
    QString errorText() const;

private:
    std::function<project::Project()> project_;
    std::function<core::Result<void>(const project::Command&)> execute_;
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
