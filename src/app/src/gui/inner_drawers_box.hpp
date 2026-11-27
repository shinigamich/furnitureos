// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// "Inner drawers" group of the interior editor: drawer boxes behind the door of a LEAF section
/// (fos::app::InnerDrawerInput, inner_drawer_rows.hpp). Apply submits one undoable section
/// command; a refusal and the generator findings of the item's inner drawers (e.g.
/// furniture.inner_drawers.hinge_conflict) are shown below the form.
#include "fos/app/interior_rows.hpp"

#include <QGroupBox>

#include <string>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class InnerDrawersBox : public QGroupBox {
    Q_OBJECT
public:
    InnerDrawersBox(Session& session, std::string itemId, QWidget* parent = nullptr);

    /// Shows the section `row` (nullptr = nothing selected); `path` is the section path.
    void showSection(const SectionRow* row);

private:
    void updateEnabled();
    void apply();

    Session& session_;
    std::string itemId_;
    std::string path_;
    bool editable_ = false;
    QCheckBox* enabled_;
    QLineEdit* heights_;
    QComboBox* slide_;
    QComboBox* bottom_;
    QComboBox* joint_;
    QComboBox* front_;
    QComboBox* handle_;
    QPushButton* apply_;
    QLabel* hint_;
    QLabel* error_;
    QLabel* findings_;
};

} // namespace fos::app::gui
