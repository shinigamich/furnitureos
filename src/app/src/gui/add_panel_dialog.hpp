// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Edit > Add panel…: size, orientation, material, edge bands and placement of a new free panel
/// (fos::app::AddPanelRequest). A live preview line shows the size and position computed by
/// fos::app::planAddPanel (or its error message).
#include "fos/app/panel_tools.hpp"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace fos::app::gui {

class AddPanelDialog : public QDialog {
    Q_OBJECT
public:
    /// `selection`: ids of the selected items (the references of the relative placements).
    AddPanelDialog(const Session& session, std::vector<std::string> selection, QWidget* parent = nullptr);

    AddPanelRequest request() const;
    void setPlacement(PanelPlacement placement);

private:
    void updatePreview();

    const Session& session_;
    std::vector<std::string> selection_;
    QLineEdit* name_ = nullptr;
    QDoubleSpinBox* length_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QComboBox* orientation_ = nullptr;
    QComboBox* material_ = nullptr;
    QComboBox* edges_[4] = {};
    QComboBox* placement_ = nullptr;
    QDoubleSpinBox* offset_ = nullptr;
    QLabel* preview_ = nullptr;
    QWidget* okButton_ = nullptr;
};

} // namespace fos::app::gui
