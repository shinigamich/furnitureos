// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// File > Import DXF outline: the options dialog (name, material, thickness, layer, units,
/// skip unsupported cut-outs). The conversion is fos::project::buildDxfPanelItem.
#include "fos/formats/dxf_import.hpp"
#include "fos/project/dxf_panel.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace fos::app::gui {

class ImportDxfDialog : public QDialog {
    Q_OBJECT
public:
    ImportDxfDialog(const furniture::Libraries& libraries, const QString& fileName, QWidget* parent = nullptr);

    project::DxfPanelRequest request() const;
    formats::DxfImportOptions options() const;
    /// Shows a refusal under the fields (the dialog stays open with its values).
    void showRefusal(const QString& message);

private:
    QLineEdit* name_;
    QComboBox* material_;
    QDoubleSpinBox* thickness_;
    QLineEdit* layer_;
    QComboBox* units_;
    QCheckBox* skipCutouts_;
    QLabel* error_;
};

} // namespace fos::app::gui
